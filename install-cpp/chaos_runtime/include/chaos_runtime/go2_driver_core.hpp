// Go2 transport safety and telemetry conversion, independent of ROS and the
// vendor SDK. Port of chaos_runtime/go2_driver_core.py (the ROS-free part;
// fill_odometry stays with the ROS wrapper in a later batch).
//
// Threading contract, mirroring the Python core: DDS callbacks cache state
// through acceptState; one control thread owns all SDK writes through step.
// step never holds the cache lock across `move`, so a blocked or slow SDK
// write cannot starve telemetry callbacks. A failed SDK write latches a
// fault until restart; a successful subsequent zero write does not silently
// restore readiness or replay an old command.
//
// External readers use status()/sample(), which return locked copies; no
// reference or mutable handle to the internal state escapes.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

#include "chaos_runtime/control.hpp"

namespace chaos::go2 {

struct Stamp {
  std::int64_t sec = 0;
  std::int64_t nanosec = 0;
};

// Unitree SportModeState equivalent. The vendor message exposes either a
// uint32 tick or a stamp depending on firmware; both are carried explicitly.
struct SportState {
  bool has_tick = false;
  std::uint32_t tick = 0;
  bool has_stamp = false;
  Stamp stamp{};
  std::array<double, 3> position{0.0, 0.0, 0.0};
  std::array<double, 3> velocity{0.0, 0.0, 0.0};
  double yaw_speed = 0.0;
  // IMU quaternion in wxyz order, as delivered by the vendor message.
  std::array<double, 4> quaternion{1.0, 0.0, 0.0, 0.0};
  std::array<double, 3> gyroscope{0.0, 0.0, 0.0};
  std::int32_t error_code = 0;
};

struct VelocityCommand {
  Stamp stamp{};
  std::string frame_id;
  std::array<double, 6> values{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
};

struct SportSample {
  double received = 0.0;
  std::int64_t ros_stamp_ns = 0;
  std::array<double, 3> position{0.0, 0.0, 0.0};
  // xyzw order, normalized.
  std::array<double, 4> quaternion{0.0, 0.0, 0.0, 1.0};
  std::array<double, 3> linear{0.0, 0.0, 0.0};
  std::array<double, 3> angular{0.0, 0.0, 0.0};
};

struct Go2AssetConfig {
  std::string base_frame;
  std::string odom_frame;
  // odom (rotated into the body frame) or body (used as delivered).
  std::string velocity_frame = "odom";
  control::Limits limits;
};

// Source counter identity used for replay/backward detection.
struct SourceIdentity {
  enum class Kind { kNone, kTick, kStamp } kind = Kind::kNone;
  std::int64_t value = 0;
  friend bool operator==(const SourceIdentity & a, const SourceIdentity & b) {
    return a.kind == b.kind && a.value == b.value;
  }
};

// Throws std::invalid_argument unless the stamp is inside the ROS int32
// second range (overflow-safe, chaos::rosStampToNs).
std::int64_t stampNs(const Stamp & stamp);

// Apply the inverse of the normalized xyzw body-to-odom rotation.
std::array<double, 3> worldToBody(const std::array<double, 3> & vector,
                                  const std::array<double, 4> & xyzw);

// Bounded no-reply Move path: returns 0 on local output success, nonzero or
// an exception on failure. The return code confirms only local output, never
// physical robot response.
using Move = std::function<int(double, double, double)>;

// Locked copy of the externally visible driver state. status() and sample()
// are the only external observation points and both take the cache lock.
struct Status {
  std::string detail;
  std::string fault;
  std::string output_error;
  bool moving = false;
  bool initialized = false;
  bool stop_pending = false;
  bool closed = false;
  bool has_command = false;
  std::int64_t last_command_stamp_ns = 0;
  std::optional<SourceIdentity> source_identity;
};

class Go2DriverCore {
public:
  Go2DriverCore(Go2AssetConfig config,
                std::function<double()> clock = nullptr,
                std::function<std::int64_t()> ros_clock = nullptr);

  bool acceptState(const SportState & message);
  bool acceptCommand(const VelocityCommand & message);

  // Run one watchdog iteration and return at most one fresh odom sample.
  // Never holds the cache lock across `move`. The callable is taken as a
  // forwarding reference (like the Python duck-typed bound method) so
  // stateful observers see every call; stateless callables can use Move.
  template <typename MoveFn>
  std::optional<SportSample> step(MoveFn && move) {
    std::optional<std::array<double, 3>> values;
    std::uint64_t generation = 0;
    {
      std::lock_guard<std::mutex> guard(lock_);
      const bool fresh = expireLocked(clock_(), ros_clock_());
      if (!initialized_ && !fresh && !closed_) {
        return std::nullopt;
      }
      if (stop_pending_) {
        values = kZero;
      } else if (command_.has_value()) {
        values = command_->values;
      }
      generation = stop_generation_;
    }
    if (values.has_value()) {
      write(move, *values, generation);
      if (!isZero(*values)) {
        // A source expiry, bad DDS sample, or failed write during I/O must
        // immediately replace a nonzero output with a zero attempt.
        bool stop = false;
        {
          std::lock_guard<std::mutex> guard(lock_);
          expireLocked(clock_(), ros_clock_());
          stop = stop_pending_;
          generation = stop_generation_;
        }
        if (stop) {
          write(move, kZero, generation);
        }
      }
    }
    {
      std::lock_guard<std::mutex> guard(lock_);
      const bool fresh = expireLocked(clock_(), ros_clock_());
      if (!fresh || !fault_.empty() || stop_pending_ || closed_ ||
          sequence_ == published_sequence_) {
        return std::nullopt;
      }
      published_sequence_ = sequence_;
      return sample_;
    }
  }

  void requestStop(bool closing = false);

  // Locked snapshots for external readers (telemetry, tests, the ROS
  // wrapper). Never hand out references to the guarded members.
  Status status() const {
    std::lock_guard<std::mutex> guard(lock_);
    Status snapshot;
    snapshot.detail = detail_;
    snapshot.fault = fault_;
    snapshot.output_error = output_error_;
    snapshot.moving = moving_;
    snapshot.initialized = initialized_;
    snapshot.stop_pending = stop_pending_;
    snapshot.closed = closed_;
    snapshot.has_command = command_.has_value();
    snapshot.last_command_stamp_ns = last_command_stamp_ns_;
    snapshot.source_identity = source_identity_;
    return snapshot;
  }

  std::optional<SportSample> sample() const {
    std::lock_guard<std::mutex> guard(lock_);
    return sample_;
  }

  const std::string & baseFrame() const { return config_.base_frame; }
  const std::string & odomFrame() const { return config_.odom_frame; }

private:
  struct Velocity {
    std::array<double, 3> values{0.0, 0.0, 0.0};
    double monotonic_deadline = 0.0;
    std::int64_t source_deadline_ns = 0;
    std::int64_t received_ros_ns = 0;
  };

  static constexpr std::array<double, 3> kZero{0.0, 0.0, 0.0};

  static bool isZero(const std::array<double, 3> & values) {
    return values[0] == 0.0 && values[1] == 0.0 && values[2] == 0.0;
  }

  void stopLocked();
  bool stateFreshLocked(double now) const;
  std::optional<SourceIdentity> identityOf(const SportState & message) const;
  bool expireLocked(double now, std::int64_t ros_ns);
  template <typename MoveFn>
  bool write(MoveFn & move, const std::array<double, 3> & values,
             std::uint64_t generation) {
    try {
      const int code = move(values[0], values[1], values[2]);
      if (code != 0) {
        throw std::runtime_error("Move returned " + std::to_string(code));
      }
    } catch (const std::exception & error) {
      std::lock_guard<std::mutex> guard(lock_);
      output_error_ =
        std::string("Go2 velocity output failed: ") + error.what();
      // DDS discovery of the sport API writer may lag telemetry. Initial
      // zero failures keep the driver unready and retry; failures after
      // initialization are permanently latched.
      if (initialized_) {
        fault_ = output_error_;
      }
      detail_ = output_error_;
      stopLocked();
      return false;
    }
    std::lock_guard<std::mutex> guard(lock_);
    moving_ = !isZero(values);
    if (isZero(values) && generation == stop_generation_) {
      stop_pending_ = false;
      initialized_ = true;
    }
    output_error_.clear();
    return true;
  }

  Go2AssetConfig config_;
  std::function<double()> clock_;
  std::function<std::int64_t()> ros_clock_;
  mutable std::mutex lock_;
  std::optional<SportSample> sample_;
  std::optional<Velocity> command_;
  std::int64_t last_command_stamp_ns_ = 0;
  std::optional<SourceIdentity> source_identity_;
  std::uint64_t sequence_ = 0;
  std::uint64_t published_sequence_ = 0;
  bool stop_pending_ = true;
  std::uint64_t stop_generation_ = 0;
  bool moving_ = false;
  bool initialized_ = false;
  std::string output_error_;
  std::string fault_;
  bool closed_ = false;
  std::string detail_ =
    "Waiting for fresh sport telemetry and initial zero output";
};

}  // namespace chaos::go2
