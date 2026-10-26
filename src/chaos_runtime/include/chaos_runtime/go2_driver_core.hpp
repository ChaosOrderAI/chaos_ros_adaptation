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
// Deadline contract: the driver enforces an immutable absolute
// CLOCK_MONOTONIC deadline that the transport delivered together with the
// command. The driver never recomputes, restamps or extends that deadline
// and never ages the command a second time against its own ROS clock. The
// deadline is only comparable when the clock-domain identity of the command
// matches the configured local domain, because the issue time and the
// deadline are readings of the boot instance and the time namespace that the
// identity names.
//
// Clock-failure contract: a clock that is observed to be unavailable, or a
// telemetry sample that is observed to be stale, invalidates every cached
// motion, whichever issue identity it carries, and the driver writes a zero.
// A clock failure is not latched: fresh telemetry and a successful zero
// admit motion again.
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

// A velocity command as it arrives from the transport.
//
// The three deadline fields are the cross-process contract:
//
//   * clock_domain_id names the boot instance and the Linux time namespace
//     that issued_monotonic_ns and expires_monotonic_ns belong to. Two
//     participants may compare their monotonic readings only when both
//     domains are equal and nonempty.
//   * issued_monotonic_ns is the CLOCK_MONOTONIC reading taken when the
//     command was issued. It is the replay barrier of the transport.
//   * expires_monotonic_ns is the immutable absolute CLOCK_MONOTONIC
//     deadline of the command. The driver never recomputes, restamps or
//     extends it.
//
// stop marks an explicit stop request. A stop carries six exact zeros, a
// current issue time and no expiry, and its source stamp may be zero. A
// command with stop == false is ordinary motion and is admitted through the
// regular deadline rules, including the case that all of its values are zero.
struct VelocityCommand {
  Stamp stamp{};
  std::string frame_id;
  std::array<double, 6> values{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  std::string clock_domain_id;
  std::int64_t issued_monotonic_ns = 0;
  std::int64_t expires_monotonic_ns = 0;
  bool stop = false;
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
  // Clock-domain identity of this driver. It is compared with the identity
  // that the transport puts on every command. An empty identity admits no
  // motion at all: the driver never discovers the local identity itself.
  std::string clock_domain_id;
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
  // Highest issue time (CLOCK_MONOTONIC nanoseconds) that the driver has
  // accepted. This is the strict replay barrier: an ordinary motion command
  // must carry a strictly larger issue time, and a stop never lowers it.
  // 0 means that no command has been accepted yet.
  std::int64_t last_command_issued_mono_ns = 0;
  std::optional<SourceIdentity> source_identity;
};

class Go2DriverCore {
public:
  // The clocks are injected so that the core stays free of any system
  // discovery:
  //   * clock is the seconds clock and owns the telemetry age,
  //   * ros_clock is the ROS clock and owns the published sample stamps,
  //   * mono_clock is the named transport clock (CLOCK_MONOTONIC) and owns
  //     every command deadline. It defaults to the shared helper reading.
  Go2DriverCore(Go2AssetConfig config,
                std::function<double()> clock = nullptr,
                std::function<std::int64_t()> ros_clock = nullptr,
                std::function<std::int64_t()> mono_clock = nullptr);

  bool acceptState(const SportState & message);
  bool acceptCommand(const VelocityCommand & message);

  // Run one watchdog iteration and return at most one fresh odom sample.
  // Never holds the cache lock across `move`. The callable is taken as a
  // forwarding reference (like the Python duck-typed bound method) so
  // stateful observers see every call; stateless callables can use Move.
  template <typename MoveFn>
  std::optional<SportSample> step(MoveFn && move) {
    // The clock readings are taken outside the cache lock: a clock failure
    // must never keep the lock while the driver decides what to do about it.
    const ClockReading reading = readClocks();
    std::optional<std::array<double, 3>> values;
    // The deadline, the issue identity and the stop generation are selected
    // together with the values. A newer command never extends the interval
    // that was selected here.
    std::int64_t expires_ns = 0;
    std::int64_t issued_ns = 0;
    std::uint64_t generation = 0;
    {
      std::lock_guard<std::mutex> guard(lock_);
      const bool fresh = expireLocked(reading);
      if (!initialized_ && !fresh && !closed_) {
        return std::nullopt;
      }
      if (stop_pending_) {
        values = kZero;
      } else if (command_.has_value()) {
        values = command_->values;
        expires_ns = command_->expires_monotonic_ns;
        issued_ns = command_->issued_monotonic_ns;
      }
      generation = stop_generation_;
    }
    // The stop generation of the selection. It never changes afterwards, so
    // that a later check can still tell whether the selected sample was
    // invalidated by a stop in the meantime.
    const std::uint64_t selected_generation = generation;
    bool zero_attempted = false;
    if (values.has_value()) {
      const bool nonzero = !isZero(*values);
      bool armed = true;
      if (nonzero) {
        // Immediately before the nonzero SDK invocation, re-read the clocks
        // and enforce the deadline and the stop generation that were
        // selected with these values. A newer cached command with a longer
        // lifetime must never excuse an already expired selection, and a
        // known clock failure or a stale sample must never permit a
        // nonzero write.
        const ClockReading final_reading = readClocks();
        std::lock_guard<std::mutex> guard(lock_);
        const bool clock_failed = !final_reading.monotonic_valid ||
                                  !final_reading.seconds_valid;
        const bool fresh = final_reading.seconds_valid &&
                           stateFreshLocked(final_reading.seconds);
        armed = !clock_failed && fresh &&
                final_reading.monotonic_ns < expires_ns &&
                selected_generation == stop_generation_;
        if (!armed) {
          if (clock_failed || !fresh ||
              (command_.has_value() &&
               command_->issued_monotonic_ns == issued_ns)) {
            // A clock failure or a stale sample invalidates every cached
            // motion, whichever issue identity it carries. Otherwise only
            // the selected sample is disarmed and a newer command with a
            // longer lifetime may stay armed.
            stopLocked();
          }
          generation = stop_generation_;
        }
      }
      zero_attempted = !nonzero || !armed;
      write(move, armed ? *values : kZero, generation);
      if (nonzero && armed) {
        // The SDK call took time, so the selected sample is checked again:
        // a newer cached command must never excuse an already expired
        // selection, and a clock failure must never leave a replacement
        // armed.
        bool zero = false;
        const ClockReading after = readClocks();
        {
          std::lock_guard<std::mutex> guard(lock_);
          if (!after.monotonic_valid || !after.seconds_valid) {
            // A clock failure invalidates every cached motion, whichever
            // issue identity it carries.
            stopLocked();
            zero = true;
          } else {
            const bool fresh = expireLocked(after);
            zero = !fresh || stop_pending_ ||
                   after.monotonic_ns >= expires_ns ||
                   selected_generation != stop_generation_;
          }
          generation = stop_generation_;
        }
        if (zero) {
          zero_attempted = true;
          write(move, kZero, generation);
        }
      }
    }
    const ClockReading final_reading = readClocks();
    bool final_zero = false;
    std::optional<SportSample> result;
    {
      std::lock_guard<std::mutex> guard(lock_);
      const bool fresh = expireLocked(final_reading);
      if (!final_reading.monotonic_valid || !final_reading.seconds_valid) {
        stopLocked();
        final_zero = true;
      } else if (!zero_attempted && moving_ &&
                 (stop_pending_ || selected_generation != stop_generation_ ||
                  (expires_ns > 0 && final_reading.monotonic_ns >= expires_ns))) {
        // The final telemetry observation also consumes time. Do not leave
        // a known-invalid output active until the next watchdog iteration.
        final_zero = true;
      }
      generation = stop_generation_;
      if (!final_zero && fresh && fault_.empty() && !stop_pending_ && !closed_ &&
          sequence_ != published_sequence_) {
        published_sequence_ = sequence_;
        result = sample_;
      }
    }
    if (final_zero) {
      write(move, kZero, generation);
    }
    return result;
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
    snapshot.last_command_issued_mono_ns = last_command_issued_mono_ns_;
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
  // The cached velocity of one admitted command. The deadline is immutable:
  // it is the absolute CLOCK_MONOTONIC value that the transport delivered.
  struct Velocity {
    std::array<double, 3> values{0.0, 0.0, 0.0};
    std::int64_t expires_monotonic_ns = 0;
    // The issue identity of the sample that was selected with the values.
    std::int64_t issued_monotonic_ns = 0;
  };

  // One reading of both clocks. An unavailable clock is reported as invalid
  // instead of being replaced by a guess.
  struct ClockReading {
    double seconds = 0.0;
    bool seconds_valid = false;
    std::int64_t monotonic_ns = 0;
    bool monotonic_valid = false;
  };

  static constexpr std::array<double, 3> kZero{0.0, 0.0, 0.0};

  static bool isZero(const std::array<double, 3> & values) {
    return values[0] == 0.0 && values[1] == 0.0 && values[2] == 0.0;
  }

  ClockReading readClocks() const;
  void stopLocked();
  bool stateFreshLocked(double now) const;
  std::optional<SourceIdentity> identityOf(const SportState & message) const;
  bool expireLocked(const ClockReading & reading);
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
  std::function<std::int64_t()> mono_clock_;
  mutable std::mutex lock_;
  std::optional<SportSample> sample_;
  std::optional<Velocity> command_;
  // Highest accepted issue time; see Status::last_command_issued_mono_ns.
  std::int64_t last_command_issued_mono_ns_ = 0;
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
