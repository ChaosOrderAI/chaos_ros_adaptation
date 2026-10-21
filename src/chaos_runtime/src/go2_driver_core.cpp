// Port of chaos_runtime/go2_driver_core.py (ROS-free core). step() and
// write() live in the header as templates so stateful Move observers are
// never copied.
#include "chaos_runtime/go2_driver_core.hpp"

#include <cmath>
#include <stdexcept>

#include "chaos_core/clock.hpp"
#include "chaos_core/time.hpp"

namespace chaos::go2 {

namespace {
constexpr std::int64_t kNs = 1'000'000'000;
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
                             std::function<std::int64_t()> ros_clock)
    : config_(std::move(config)),
      clock_(clock ? std::move(clock) : chaos::monotonicSeconds),
      ros_clock_(ros_clock ? std::move(ros_clock) : chaos::systemNs) {
  if (config_.velocity_frame != "odom" && config_.velocity_frame != "body") {
    throw std::invalid_argument("velocity_frame must be odom or body");
  }
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
  const double received = clock_();
  const std::int64_t ros_ns = ros_clock_();
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
  const double received = clock_();
  const std::int64_t ros_ns = ros_clock_();
  try {
    const std::int64_t source_ns = stampNs(message.stamp);
    const std::int64_t ttl_ns = static_cast<std::int64_t>(
      config_.limits.command_timeout_s * kNs);
    const std::int64_t age_ns = ros_ns - source_ns;
    if (source_ns <= 0 || age_ns < 0 || age_ns >= ttl_ns) {
      throw std::invalid_argument(
        "Velocity command timestamp is missing, stale or in the future");
    }
    if (message.frame_id != config_.base_frame) {
      throw std::invalid_argument("Velocity command must use frame " +
                                  config_.base_frame);
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
    const std::array<double, 3> values{vx, vy, wz};
    // Source age consumes the receiving watchdog budget; later ROS clock
    // corrections can never extend this monotonic deadline.
    Velocity command;
    command.values = values;
    command.monotonic_deadline =
      received + static_cast<double>(ttl_ns - age_ns) / kNs;
    command.source_deadline_ns = source_ns + ttl_ns;
    command.received_ros_ns = ros_ns;
    std::lock_guard<std::mutex> guard(lock_);
    if (isZero(values)) {
      // A stop is also a source-time barrier: delayed commands sent before
      // it must not restart motion after the zero is issued.
      last_command_stamp_ns_ = std::max(last_command_stamp_ns_, source_ns);
      stopLocked();
      return true;
    }
    if (source_ns <= last_command_stamp_ns_) {
      detail_ = "Repeated or backward velocity command timestamp";
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
    command_ = command;
    last_command_stamp_ns_ = source_ns;
    return true;
  } catch (const std::exception & error) {
    std::lock_guard<std::mutex> guard(lock_);
    detail_ = error.what();
    stopLocked();
    return false;
  }
}

bool Go2DriverCore::expireLocked(double now, std::int64_t ros_ns) {
  const bool fresh = stateFreshLocked(now);
  const std::optional<Velocity> command = command_;
  const bool expired =
    command.has_value() &&
    (now >= command->monotonic_deadline ||
     ros_ns >= command->source_deadline_ns ||
     ros_ns < command->received_ros_ns);
  if ((!fresh || expired || closed_ || !fault_.empty()) &&
      command.has_value()) {
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
