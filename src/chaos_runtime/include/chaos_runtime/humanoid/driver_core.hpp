// ROS-free, vendor-neutral humanoid transport safety. The backend is injected
// and every backend write occurs outside the telemetry cache mutex.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "chaos_runtime/control.hpp"
#include "chaos_runtime/humanoid/backend.hpp"

namespace chaos::humanoid {

struct VelocityCommand {
  Stamp stamp{};
  std::string frame_id;
  std::array<double, 6> values{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  std::string clock_domain_id;
  std::int64_t issued_monotonic_ns = 0;
  std::int64_t expires_monotonic_ns = 0;
  bool stop = false;
};

struct Sample {
  std::int64_t source_stamp_ns = 0;
  std::array<double, 3> position{0.0, 0.0, 0.0};
  std::array<double, 4> quaternion_xyzw{0.0, 0.0, 0.0, 1.0};
  std::array<double, 6> twist{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
};

struct DriverConfig {
  std::string base_frame;
  std::string odom_frame;
  // Vendor frame names may differ from namespaced output frames. An empty
  // source name means the corresponding output name for direct core users.
  std::string source_base_frame;
  std::string source_odom_frame;
  std::string clock_domain_id;
  control::Limits limits;
};

struct DriverStatus {
  std::string detail;
  std::string fault;
  std::string output_error;
  bool moving = false;
  bool initialized = false;
  bool stop_pending = true;
  bool closed = false;
  bool output_owned = false;
  bool has_command = false;
  bool has_odometry = false;
  bool has_mode = false;
  bool motion_enabled = false;
  std::int64_t last_command_issued_mono_ns = 0;
};

// The subset the 10 ms watchdog logs. Both strings are empty in normal
// operation, so reading it every tick does not allocate.
struct DriverFaults {
  std::string fault;
  std::string output_error;
  bool initialized = false;
};

class HumanoidDriverCore {
public:
  explicit HumanoidDriverCore(
    DriverConfig config, std::shared_ptr<HumanoidBackend> backend = nullptr,
    std::function<double()> steady_clock = nullptr,
    std::function<std::int64_t()> ros_clock = nullptr,
    std::function<std::int64_t()> monotonic_clock = nullptr);
  ~HumanoidDriverCore() { close(); }

  void setBackend(std::shared_ptr<HumanoidBackend> backend);
  bool acceptOdometry(const Odometry & message);
  bool acceptMotionMode(bool motion_enabled);
  bool acceptCommand(const VelocityCommand & message);

  // One independent-watchdog iteration. At most one newly accepted odometry
  // sample is returned; its original source stamp is never replaced.
  std::optional<Sample> step();
  void requestStop(bool closing = false);
  // Retry zero at most three times when output was owned, then close
  // telemetry. Idempotent and non-throwing for destruction paths.
  void close() noexcept;

  DriverStatus status() const;
  DriverFaults faults() const;
  std::optional<Sample> sample() const;
  const std::string & baseFrame() const { return config_.base_frame; }
  const std::string & odomFrame() const { return config_.odom_frame; }

private:
  struct CachedOdometry {
    Sample sample;
    double received_s = 0.0;
  };
  struct Velocity {
    std::array<double, 3> values{0.0, 0.0, 0.0};
    std::int64_t issued_monotonic_ns = 0;
    std::int64_t expires_monotonic_ns = 0;
  };
  struct ClockReading {
    double steady_s = 0.0;
    bool steady_valid = false;
    std::int64_t monotonic_ns = 0;
    bool monotonic_valid = false;
  };

  static constexpr std::array<double, 3> kZero{0.0, 0.0, 0.0};
  static bool isZero(const std::array<double, 3> & values) {
    return values[0] == 0.0 && values[1] == 0.0 && values[2] == 0.0;
  }
  ClockReading readClocks() const;
  void stopLocked();
  bool odometryFreshLocked(double now_s) const;
  bool motionReadyLocked(double now_s) const;
  bool expireLocked(const ClockReading & reading);
  // Caller holds output_mutex_.
  std::optional<Sample> stepUnlocked();
  bool write(const std::array<double, 3> & values, std::uint64_t generation);
  // Reject paths: record the reason and stop. They reuse detail_'s storage
  // and never throw for control flow, so a stream of invalid telemetry costs
  // no exception unwinding.
  bool rejectOdometry(const char * reason);
  bool rejectCommand(const char * reason);

  DriverConfig config_;
  std::shared_ptr<HumanoidBackend> backend_;
  std::function<double()> steady_clock_;
  std::function<std::int64_t()> ros_clock_;
  std::function<std::int64_t()> monotonic_clock_;
  mutable std::mutex cache_mutex_;
  // Covers a whole step() and close(), including backend stop. Telemetry
  // callbacks never take it. Acquire it before cache_mutex_.
  std::mutex output_mutex_;
  std::optional<CachedOdometry> odometry_;
  std::optional<Velocity> command_;
  std::int64_t last_source_stamp_ns_ = 0;
  std::int64_t last_command_issued_mono_ns_ = 0;
  double mode_received_s_ = 0.0;
  bool has_mode_ = false;
  bool motion_enabled_ = false;
  std::uint64_t sequence_ = 0;
  std::uint64_t published_sequence_ = 0;
  bool stop_pending_ = true;
  std::uint64_t stop_generation_ = 0;
  bool moving_ = false;
  bool initialized_ = false;
  bool output_owned_ = false;
  bool closed_ = false;
  bool shutdown_complete_ = false;
  std::string output_error_;
  std::string fault_;
  std::string detail_ = "Waiting for fresh odometry, motion mode and initial zero output";
};

}  // namespace chaos::humanoid
