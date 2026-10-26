// Port of chaos_runtime/go2_driver_core.py (ROS-free core). step() and
// write() live in the header as templates so stateful Move observers are
// never copied.
//
// The deadline rules of this file are the transport contract:
//   * a command carries an immutable absolute CLOCK_MONOTONIC deadline and
//     the issue time it was created with,
//   * the deadline is comparable only inside one clock domain,
//   * the driver never restamps, recomputes or extends a deadline and never
//     ages a command a second time against its own ROS clock.
#include "chaos_runtime/go2_driver_core.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "chaos_core/clock.hpp"
#include "chaos_core/time.hpp"
#include "chaos_runtime/driver_deadline.hpp"

namespace chaos::go2 {

namespace {
bool allFinite(const std::array<double, 3> & values) {
  for (double value : values) {
    if (!std::isfinite(value)) {
      return false;
    }
  }
  return true;
}
}  // namespace

std::int64_t stampNs(const Stamp & stamp) {
  std::int64_t ns = 0;
  if (!chaos::rosStampToNs(stamp.sec, stamp.nanosec, ns)) {
    throw std::invalid_argument("Invalid timestamp");
  }
  return ns;
}

std::array<double, 3> worldToBody(const std::array<double, 3> & vector,
                                  const std::array<double, 4> & q) {
  const double x = q[0], y = q[1], z = q[2], w = q[3];
  const double vx = vector[0], vy = vector[1], vz = vector[2];
  return {
    (1 - 2 * (y * y + z * z)) * vx + 2 * (x * y + z * w) * vy +
      2 * (x * z - y * w) * vz,
    2 * (x * y - z * w) * vx + (1 - 2 * (x * x + z * z)) * vy +
      2 * (y * z + x * w) * vz,
    2 * (x * z + y * w) * vx + 2 * (y * z - x * w) * vy +
      (1 - 2 * (x * x + y * y)) * vz,
  };
}

Go2DriverCore::Go2DriverCore(Go2AssetConfig config,
                             std::function<double()> clock,
                             std::function<std::int64_t()> ros_clock,
                             std::function<std::int64_t()> mono_clock)
    : config_(std::move(config)),
      clock_(clock ? std::move(clock) : chaos::monotonicSeconds),
      ros_clock_(ros_clock ? std::move(ros_clock) : chaos::systemNs),
      mono_clock_(mono_clock ? std::move(mono_clock)
                             : chaos::runtime::driver_deadline::monotonicNowNs) {
  if (config_.velocity_frame != "odom" && config_.velocity_frame != "body") {
    throw std::invalid_argument("velocity_frame must be odom or body");
  }
}

Go2DriverCore::ClockReading Go2DriverCore::readClocks() const {
  ClockReading reading;
  try {
    reading.seconds = clock_();
    // A nonfinite seconds reading is an unavailable clock, never a valid
    // one: the telemetry age cannot be computed from it.
    reading.seconds_valid = std::isfinite(reading.seconds);
  } catch (const std::exception &) {
    reading.seconds_valid = false;
  }
  try {
    reading.monotonic_ns = mono_clock_();
    reading.monotonic_valid = reading.monotonic_ns > 0;
  } catch (const std::exception &) {
    reading.monotonic_valid = false;
  }
  return reading;
}

void Go2DriverCore::stopLocked() {
  command_.reset();
  stop_pending_ = true;
  ++stop_generation_;
}

bool Go2DriverCore::stateFreshLocked(double now) const {
  return sample_.has_value() && now - sample_->received >= 0 &&
         now - sample_->received < config_.limits.state_timeout_s;
}

std::optional<SourceIdentity> Go2DriverCore::identityOf(
    const SportState & message) const {
  if (message.has_tick) {
    return SourceIdentity{SourceIdentity::Kind::kTick, message.tick};
  }
  if (message.has_stamp) {
    return SourceIdentity{SourceIdentity::Kind::kStamp,
                          stampNs(message.stamp)};
  }
  return std::nullopt;
}

bool Go2DriverCore::acceptState(const SportState & message) {
  double received = 0.0;
  std::int64_t ros_ns = 0;
  try {
    received = clock_();
    ros_ns = ros_clock_();
  } catch (const std::exception & error) {
    std::lock_guard<std::mutex> guard(lock_);
    sample_.reset();
    detail_ = std::string("Sport telemetry clock unavailable: ") + error.what();
    stopLocked();
    return false;
  }
  try {
    const std::optional<SourceIdentity> identity = identityOf(message);
    if (!allFinite(message.position)) {
      throw std::invalid_argument(
        "position must contain 3 finite values");
    }
    if (!allFinite(message.velocity)) {
      throw std::invalid_argument("velocity must contain 3 finite values");
    }
    if (!allFinite(message.gyroscope)) {
      throw std::invalid_argument("gyroscope must contain 3 finite values");
    }
    for (double component : message.quaternion) {
      if (!std::isfinite(component)) {
        throw std::invalid_argument(
          "quaternion must contain 4 finite values");
      }
    }
    // The vendor message carries wxyz; stored as normalized xyzw.
    const double norm = std::hypot(
      message.quaternion[0],
      std::hypot(message.quaternion[1],
                 std::hypot(message.quaternion[2], message.quaternion[3])));
    if (std::abs(norm - 1.0) > 0.01) {
      throw std::invalid_argument("Invalid orientation quaternion");
    }
    const std::array<double, 4> quaternion{message.quaternion[1] / norm,
                                           message.quaternion[2] / norm,
                                           message.quaternion[3] / norm,
                                           message.quaternion[0] / norm};
    if (!std::isfinite(message.yaw_speed)) {
      throw std::invalid_argument("Invalid yaw speed");
    }
    if (message.error_code != 0) {
      throw std::invalid_argument("Go2 sport error_code=" +
                                  std::to_string(message.error_code));
    }
    if (ros_ns <= 0) {
      throw std::invalid_argument("ROS clock has not started");
    }
    std::array<double, 3> linear = message.velocity;
    if (config_.velocity_frame == "odom") {
      // Unitree's official read_motion_state.cpp identifies velocity as
      // odometry-frame data. ROS Odometry twist uses child_frame.
      // https://github.com/unitreerobotics/unitree_ros2/blob/master/example/src/src/read_motion_state.cpp
      linear = worldToBody(linear, quaternion);
      if (!allFinite(linear)) {
        throw std::invalid_argument(
          "body velocity must contain 3 finite values");
      }
    }
    const SportSample sample{received, ros_ns, message.position, quaternion,
                             linear, message.gyroscope};
    std::lock_guard<std::mutex> guard(lock_);
    if (closed_) {
      return false;
    }
    const std::optional<SourceIdentity> previous = source_identity_;
    if (identity.has_value() && previous.has_value()) {
      if (identity->kind != previous->kind) {
        detail_ = "Sport telemetry clock changed; restart driver";
        sample_.reset();
        stopLocked();
        return false;
      }
      bool newer;
      if (identity->kind == SourceIdentity::Kind::kTick) {
        // Wrap-aware uint32 ordering.
        const std::uint32_t delta =
          static_cast<std::uint32_t>(identity->value) -
          static_cast<std::uint32_t>(previous->value);
        newer = delta > 0 && delta < (std::uint32_t{1} << 31);
      } else {
        newer = identity->value > previous->value;
      }
      if (!newer) {
        // Repeated/backward source counters cannot keep a dead publisher
        // alive, even if DDS keeps delivering callbacks.
        return false;
      }
    }
    source_identity_ = identity;
    sample_ = sample;
    ++sequence_;
    return true;
  } catch (const std::exception & error) {
    std::lock_guard<std::mutex> guard(lock_);
    sample_.reset();
    detail_ = std::string("Invalid sport telemetry: ") + error.what();
    stopLocked();
    return false;
  }
}

bool Go2DriverCore::acceptCommand(const VelocityCommand & message) {
  // The transport clock is read before anything else: without it no deadline
  // can be checked and no motion may be admitted.
  const ClockReading reading = readClocks();
  if (!reading.monotonic_valid || !reading.seconds_valid) {
    std::lock_guard<std::mutex> guard(lock_);
    detail_ = "Command clock unavailable";
    stopLocked();
    return false;
  }
  const std::int64_t mono_now = reading.monotonic_ns;
  try {
    // The clock domain names the boot instance and the time namespace of the
    // issue time and of the deadline. An empty or different domain is a
    // different clock: its numbers cannot be compared with ours. An empty
    // command domain is already covered by the comparison, because an empty
    // local domain is refused above.
    if (config_.clock_domain_id.empty() ||
        message.clock_domain_id != config_.clock_domain_id) {
      throw std::invalid_argument(
        "Velocity command clock domain does not match the driver domain");
    }
    if (config_.base_frame.empty() || message.frame_id != config_.base_frame) {
      throw std::invalid_argument("Velocity command must use frame " +
                                  config_.base_frame);
    }
    if (message.stop) {
      // An explicit stop: six exact zeros and a current issue time. The
      // source stamp and the expiry are not needed for a stop.
      for (double value : message.values) {
        if (value != 0.0) {
          throw std::invalid_argument(
            "Stop command must carry six exact zeros");
        }
      }
      if (message.issued_monotonic_ns <= 0 ||
          message.issued_monotonic_ns > mono_now) {
        throw std::invalid_argument(
          "Stop command issue time is missing or in the future");
      }
      std::lock_guard<std::mutex> guard(lock_);
      // The barrier only ever rises: a duplicate or older stop is safe and
      // idempotent, and it never lowers the barrier.
      last_command_issued_mono_ns_ =
        std::max(last_command_issued_mono_ns_, message.issued_monotonic_ns);
      stopLocked();
      return true;
    }

    // Ordinary motion. Every input is checked before the command and the
    // watermark are committed.
    const std::int64_t source_ns = stampNs(message.stamp);
    if (source_ns <= 0) {
      throw std::invalid_argument(
        "Velocity command timestamp is missing");
    }
    const std::array<double, 6> & twist = message.values;
    for (double value : twist) {
      if (!std::isfinite(value)) {
        throw std::invalid_argument(
          "velocity command must contain 6 finite values");
      }
    }
    const double vx = twist[0], vy = twist[1], vz = twist[2];
    const double wx = twist[3], wy = twist[4], wz = twist[5];
    if (vz != 0.0 || wx != 0.0 || wy != 0.0) {
      throw std::invalid_argument(
        "Go2 accepts only body x/y translation and yaw rotation");
    }
    if (std::hypot(vx, vy) > config_.limits.linear_mps ||
        std::abs(wz) > config_.limits.angular_rps) {
      throw std::invalid_argument(
        "Velocity command exceeds configured speed limits");
    }
    if (message.issued_monotonic_ns <= 0) {
      throw std::invalid_argument(
        "Velocity command issue time is missing");
    }
    if (message.expires_monotonic_ns <= 0) {
      throw std::invalid_argument(
        "Velocity command deadline is missing");
    }
    if (message.issued_monotonic_ns > mono_now) {
      throw std::invalid_argument(
        "Velocity command issue time is in the future");
    }
    if (mono_now >= message.expires_monotonic_ns) {
      throw std::invalid_argument(
        "Velocity command deadline has already passed");
    }
    const std::optional<std::int64_t> cap_ns =
      chaos::runtime::driver_deadline::floorDurationNs(
        config_.limits.command_timeout_s);
    if (!cap_ns.has_value()) {
      throw std::invalid_argument(
        "Configured command cap is not a usable interval");
    }
    // Both operands are positive and the deadline is larger than the issue
    // time, so the difference cannot overflow.
    if (message.expires_monotonic_ns - message.issued_monotonic_ns > *cap_ns) {
      throw std::invalid_argument(
        "Velocity command exceeds the driver command cap");
    }

    const std::array<double, 3> values{vx, vy, wz};
    std::lock_guard<std::mutex> guard(lock_);
    if (message.issued_monotonic_ns <= last_command_issued_mono_ns_) {
      // The replay barrier is the issue time of the transport. The original
      // source stamp is never compared with it.
      detail_ = "Repeated or backward velocity command issue time";
      return false;
    }
    if (closed_ || !fault_.empty() || stop_pending_ ||
        !stateFreshLocked(clock_())) {
      detail_ = !fault_.empty()
                  ? fault_
                  : "Motion requires fresh telemetry and a successful zero output";
      stopLocked();
      return false;
    }
    Velocity velocity;
    velocity.values = values;
    velocity.expires_monotonic_ns = message.expires_monotonic_ns;
    velocity.issued_monotonic_ns = message.issued_monotonic_ns;
    command_ = velocity;
    last_command_issued_mono_ns_ = message.issued_monotonic_ns;
    return true;
  } catch (const std::exception & error) {
    std::lock_guard<std::mutex> guard(lock_);
    detail_ = error.what();
    stopLocked();
    return false;
  }
}

bool Go2DriverCore::expireLocked(const ClockReading & reading) {
  const bool fresh =
    reading.seconds_valid && stateFreshLocked(reading.seconds);
  // The expiry depends on the monotonic deadline only. The ROS clock of the
  // driver never ages the command a second time.
  const bool expired =
    command_.has_value() &&
    (!reading.monotonic_valid ||
     reading.monotonic_ns >= command_->expires_monotonic_ns);
  // An unavailable clock or a stale sample invalidates every cached motion,
  // whichever issue identity it carries.
  if ((!fresh || expired || closed_ || !fault_.empty()) &&
      command_.has_value()) {
    stopLocked();
  }
  if (!fresh) {
    detail_ = "Waiting for fresh sport telemetry";
  } else if (expired) {
    detail_ = "Velocity command expired";
  }
  return fresh;
}

void Go2DriverCore::requestStop(bool closing) {
  std::lock_guard<std::mutex> guard(lock_);
  closed_ = closed_ || closing;
  stopLocked();
}

}  // namespace chaos::go2
