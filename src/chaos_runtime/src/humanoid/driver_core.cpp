#include "chaos_runtime/humanoid/driver_core.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "chaos_core/clock.hpp"
#include "chaos_core/time.hpp"
#include "chaos_runtime/driver_deadline.hpp"

namespace chaos::humanoid {
namespace {

template <std::size_t N>
bool allFinite(const std::array<double, N> & values) {
  return std::all_of(values.begin(), values.end(),
                     [](double value) { return std::isfinite(value); });
}

std::int64_t stampNs(const Stamp & stamp) {
  std::int64_t result = 0;
  if (!chaos::rosStampToNs(stamp.sec, stamp.nanosec, result)) {
    throw std::invalid_argument("invalid source timestamp");
  }
  return result;
}

}  // namespace

HumanoidDriverCore::HumanoidDriverCore(
  DriverConfig config, std::shared_ptr<HumanoidBackend> backend,
  std::function<double()> steady_clock,
  std::function<std::int64_t()> ros_clock,
  std::function<std::int64_t()> monotonic_clock)
    : config_(std::move(config)), backend_(std::move(backend)),
      steady_clock_(steady_clock ? std::move(steady_clock) : chaos::monotonicSeconds),
      ros_clock_(ros_clock ? std::move(ros_clock) : chaos::systemNs),
      monotonic_clock_(monotonic_clock ? std::move(monotonic_clock)
        : runtime::driver_deadline::monotonicNowNs) {
  if (config_.source_base_frame.empty()) config_.source_base_frame = config_.base_frame;
  if (config_.source_odom_frame.empty()) config_.source_odom_frame = config_.odom_frame;
}

void HumanoidDriverCore::setBackend(std::shared_ptr<HumanoidBackend> backend) {
  std::lock_guard<std::mutex> guard(cache_mutex_);
  backend_ = std::move(backend);
}

HumanoidDriverCore::ClockReading HumanoidDriverCore::readClocks() const {
  ClockReading reading;
  try {
    reading.steady_s = steady_clock_();
    reading.steady_valid = std::isfinite(reading.steady_s);
  } catch (const std::exception &) {
    reading.steady_valid = false;
  }
  try {
    reading.monotonic_ns = monotonic_clock_();
    reading.monotonic_valid = reading.monotonic_ns > 0;
  } catch (const std::exception &) {
    reading.monotonic_valid = false;
  }
  return reading;
}

void HumanoidDriverCore::stopLocked() {
  command_.reset();
  stop_pending_ = true;
  ++stop_generation_;
}

bool HumanoidDriverCore::odometryFreshLocked(double now_s) const {
  return odometry_.has_value() && now_s - odometry_->received_s >= 0.0 &&
    now_s - odometry_->received_s < config_.limits.state_timeout_s;
}

bool HumanoidDriverCore::motionReadyLocked(double now_s) const {
  return odometryFreshLocked(now_s) && has_mode_ && motion_enabled_ &&
    now_s - mode_received_s_ >= 0.0 &&
    now_s - mode_received_s_ < config_.limits.state_timeout_s;
}

bool HumanoidDriverCore::acceptOdometry(const Odometry & message) {
  double now_s = 0.0;
  std::int64_t ros_now_ns = 0;
  try {
    now_s = steady_clock_();
    ros_now_ns = ros_clock_();
    if (!std::isfinite(now_s) || ros_now_ns < 0) {
      throw std::invalid_argument("steady clock is invalid");
    }
    if (message.frame_id != config_.source_odom_frame ||
        message.child_frame_id != config_.source_base_frame) {
      throw std::invalid_argument("odometry frame mismatch");
    }
    if (!allFinite(message.position) || !allFinite(message.quaternion_xyzw) ||
        !allFinite(message.twist)) {
      throw std::invalid_argument("odometry contains a non-finite number");
    }
    const double norm = std::hypot(message.quaternion_xyzw[0],
      std::hypot(message.quaternion_xyzw[1],
        std::hypot(message.quaternion_xyzw[2], message.quaternion_xyzw[3])));
    if (std::abs(norm - 1.0) > 0.01) {
      throw std::invalid_argument("invalid orientation quaternion");
    }
    const std::int64_t source_ns = stampNs(message.stamp);
    const std::int64_t ahead_ns = source_ns - ros_now_ns;
    const std::int64_t age_ns = ros_now_ns > source_ns ? ros_now_ns - source_ns : 0;
    if (source_ns <= 0 || ahead_ns > 100000000LL ||
        age_ns >= runtime::driver_deadline::floorDurationNs(
          config_.limits.state_timeout_s).value_or(0)) {
      throw std::invalid_argument("odometry source timestamp is absent, future or stale");
    }
    Sample sample;
    sample.source_stamp_ns = source_ns;
    sample.position = message.position;
    sample.quaternion_xyzw = {
      message.quaternion_xyzw[0] / norm, message.quaternion_xyzw[1] / norm,
      message.quaternion_xyzw[2] / norm, message.quaternion_xyzw[3] / norm};
    sample.twist = message.twist;
    const double received_s = now_s - static_cast<double>(age_ns) / 1e9;
    std::lock_guard<std::mutex> guard(cache_mutex_);
    if (closed_) {
      return false;
    }
    if (last_source_stamp_ns_ != 0 && source_ns <= last_source_stamp_ns_) {
      detail_ = "Repeated or backward odometry source timestamp";
      return false;
    }
    last_source_stamp_ns_ = source_ns;
    odometry_ = CachedOdometry{sample, received_s};
    ++sequence_;
    return true;
  } catch (const std::exception & error) {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    odometry_.reset();
    detail_ = std::string("Invalid humanoid odometry: ") + error.what();
    stopLocked();
    return false;
  }
}

bool HumanoidDriverCore::acceptMotionMode(bool motion_enabled) {
  double now_s = 0.0;
  try {
    now_s = steady_clock_();
    if (!std::isfinite(now_s)) {
      throw std::invalid_argument("steady clock is invalid");
    }
  } catch (const std::exception & error) {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    has_mode_ = false;
    motion_enabled_ = false;
    detail_ = std::string("Motion mode clock unavailable: ") + error.what();
    stopLocked();
    return false;
  }
  std::lock_guard<std::mutex> guard(cache_mutex_);
  if (closed_) {
    return false;
  }
  has_mode_ = true;
  motion_enabled_ = motion_enabled;
  mode_received_s_ = now_s;
  if (!motion_enabled_) {
    detail_ = "Humanoid mode does not permit locomotion";
    stopLocked();
  }
  return true;
}

bool HumanoidDriverCore::acceptCommand(const VelocityCommand & message) {
  const ClockReading reading = readClocks();
  if (!reading.monotonic_valid) {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    detail_ = "Command monotonic clock unavailable";
    stopLocked();
    return false;
  }
  try {
    if (config_.clock_domain_id.empty() ||
        message.clock_domain_id != config_.clock_domain_id) {
      throw std::invalid_argument("Velocity command clock domain does not match the driver domain");
    }
    if (config_.base_frame.empty() || message.frame_id != config_.base_frame) {
      throw std::invalid_argument("Velocity command must use frame " + config_.base_frame);
    }
    if (message.stop) {
      for (double value : message.values) {
        if (value != 0.0) {
          throw std::invalid_argument("Stop command must carry six exact zeros");
        }
      }
      if (message.issued_monotonic_ns <= 0 ||
          message.issued_monotonic_ns > reading.monotonic_ns) {
        throw std::invalid_argument("Stop command issue time is missing or in the future");
      }
      std::lock_guard<std::mutex> guard(cache_mutex_);
      last_command_issued_mono_ns_ = std::max(
        last_command_issued_mono_ns_, message.issued_monotonic_ns);
      stopLocked();
      return true;
    }
    if (!reading.steady_valid) {
      throw std::invalid_argument("Command steady clock unavailable");
    }
    if (stampNs(message.stamp) <= 0) {
      throw std::invalid_argument("Velocity command timestamp is missing");
    }
    if (!allFinite(message.values)) {
      throw std::invalid_argument("velocity command must contain 6 finite values");
    }
    const double vx = message.values[0], vy = message.values[1];
    const double wz = message.values[5];
    if (message.values[2] != 0.0 || message.values[3] != 0.0 ||
        message.values[4] != 0.0) {
      throw std::invalid_argument("Humanoid accepts only body x/y translation and yaw rotation");
    }
    if (std::hypot(vx, vy) > config_.limits.linear_mps ||
        std::abs(wz) > config_.limits.angular_rps) {
      throw std::invalid_argument("Velocity command exceeds configured speed limits");
    }
    if (message.issued_monotonic_ns <= 0 || message.expires_monotonic_ns <= 0) {
      throw std::invalid_argument("Velocity command issue time or deadline is missing");
    }
    if (message.issued_monotonic_ns > reading.monotonic_ns) {
      throw std::invalid_argument("Velocity command issue time is in the future");
    }
    if (reading.monotonic_ns >= message.expires_monotonic_ns) {
      throw std::invalid_argument("Velocity command deadline has already passed");
    }
    const auto cap_ns = runtime::driver_deadline::floorDurationNs(
      config_.limits.command_timeout_s);
    if (!cap_ns.has_value()) {
      throw std::invalid_argument("Configured command cap is not a usable interval");
    }
    if (message.expires_monotonic_ns - message.issued_monotonic_ns > *cap_ns) {
      throw std::invalid_argument("Velocity command exceeds the driver command cap");
    }
    std::lock_guard<std::mutex> guard(cache_mutex_);
    if (message.issued_monotonic_ns <= last_command_issued_mono_ns_) {
      detail_ = "Repeated or backward velocity command issue time";
      stopLocked();
      return false;
    }
    if (closed_ || !fault_.empty() || stop_pending_ ||
        !motionReadyLocked(reading.steady_s)) {
      detail_ = !fault_.empty() ? fault_ :
        "Motion requires fresh odometry, current motion-enabled mode and a successful zero output";
      stopLocked();
      return false;
    }
    command_ = Velocity{{vx, vy, wz}, message.issued_monotonic_ns,
                        message.expires_monotonic_ns};
    last_command_issued_mono_ns_ = message.issued_monotonic_ns;
    return true;
  } catch (const std::exception & error) {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    detail_ = error.what();
    stopLocked();
    return false;
  }
}

bool HumanoidDriverCore::expireLocked(const ClockReading & reading) {
  const bool ready = reading.steady_valid && motionReadyLocked(reading.steady_s);
  const bool expired = command_.has_value() && (!reading.monotonic_valid ||
    reading.monotonic_ns >= command_->expires_monotonic_ns);
  if (!reading.steady_valid || !reading.monotonic_valid || !ready || expired ||
      closed_ || !fault_.empty()) {
    if (command_.has_value()) {
      stopLocked();
    }
  }
  if (!reading.steady_valid || !reading.monotonic_valid) {
    detail_ = "Driver clock unavailable";
  } else if (!ready) {
    detail_ = "Waiting for fresh odometry and current motion-enabled mode";
  } else if (expired) {
    detail_ = "Velocity command expired";
  }
  return ready;
}

bool HumanoidDriverCore::write(const std::array<double, 3> & values,
                               std::uint64_t generation) {
  std::shared_ptr<HumanoidBackend> backend;
  {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    backend = backend_;
    output_owned_ = true;
  }
  const auto fail = [this](std::string error) {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    output_error_ = std::move(error);
    if (initialized_) {
      fault_ = output_error_;
    }
    detail_ = output_error_;
    stopLocked();
    return false;
  };
  try {
    if (!backend) {
      throw std::runtime_error("humanoid backend is unavailable");
    }
    const int result = backend->move(values[0], values[1], values[2]);
    if (result != 0) {
      throw std::runtime_error("move returned " + std::to_string(result));
    }
  } catch (const std::exception & error) {
    return fail(std::string("Humanoid velocity output failed: ") + error.what());
  } catch (...) {
    return fail("Humanoid velocity output failed with an unknown error");
  }
  std::lock_guard<std::mutex> guard(cache_mutex_);
  const bool zero = isZero(values);
  moving_ = !zero;
  if (zero && generation == stop_generation_) {
    stop_pending_ = false;
    initialized_ = true;
  }
  output_error_.clear();
  return true;
}

std::optional<Sample> HumanoidDriverCore::step() {
  std::lock_guard<std::mutex> output_guard(output_mutex_);
  return stepUnlocked();
}

std::optional<Sample> HumanoidDriverCore::stepUnlocked() {
  const ClockReading reading = readClocks();
  std::optional<std::array<double, 3>> values;
  std::int64_t expires_ns = 0, issued_ns = 0;
  std::uint64_t generation = 0;
  {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    const bool ready = expireLocked(reading);
    if (!initialized_ && !ready && !closed_) {
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
  const std::uint64_t selected_generation = generation;
  bool zero_attempted = false;
  if (values.has_value()) {
    const bool nonzero = !isZero(*values);
    bool armed = true;
    if (nonzero) {
      const ClockReading final_reading = readClocks();
      std::lock_guard<std::mutex> guard(cache_mutex_);
      const bool ready = final_reading.steady_valid &&
        motionReadyLocked(final_reading.steady_s);
      armed = final_reading.steady_valid && final_reading.monotonic_valid && ready &&
        final_reading.monotonic_ns < expires_ns && selected_generation == stop_generation_;
      if (!armed) {
        if (!final_reading.steady_valid || !final_reading.monotonic_valid || !ready ||
            (command_.has_value() && command_->issued_monotonic_ns == issued_ns)) {
          stopLocked();
        }
        generation = stop_generation_;
      }
    }
    zero_attempted = !nonzero || !armed;
    write(armed ? *values : kZero, generation);
    if (nonzero && armed) {
      bool zero = false;
      const ClockReading after = readClocks();
      {
        std::lock_guard<std::mutex> guard(cache_mutex_);
        if (!after.steady_valid || !after.monotonic_valid) {
          stopLocked();
          zero = true;
        } else {
          const bool ready = expireLocked(after);
          zero = !ready || stop_pending_ || after.monotonic_ns >= expires_ns ||
            selected_generation != stop_generation_;
        }
        generation = stop_generation_;
      }
      if (zero) {
        zero_attempted = true;
        write(kZero, generation);
      }
    }
  }
  const ClockReading final_reading = readClocks();
  bool final_zero = false;
  std::optional<Sample> result;
  {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    const bool motion_ready = expireLocked(final_reading);
    if (!final_reading.steady_valid || !final_reading.monotonic_valid) {
      stopLocked();
      final_zero = true;
    } else if (!zero_attempted && moving_ &&
      (stop_pending_ || selected_generation != stop_generation_ ||
       (expires_ns > 0 && final_reading.monotonic_ns >= expires_ns))) {
      final_zero = true;
    }
    generation = stop_generation_;
    if (!final_zero && motion_ready && initialized_ &&
        !stop_pending_ && fault_.empty() && !closed_ &&
        sequence_ != published_sequence_) {
      published_sequence_ = sequence_;
      result = odometry_->sample;
    }
  }
  if (final_zero) {
    write(kZero, generation);
  }
  return result;
}

void HumanoidDriverCore::requestStop(bool closing) {
  std::lock_guard<std::mutex> guard(cache_mutex_);
  closed_ = closed_ || closing;
  stopLocked();
}

void HumanoidDriverCore::close() noexcept {
  std::lock_guard<std::mutex> output_guard(output_mutex_);
  {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    if (shutdown_complete_) {
      return;
    }
    shutdown_complete_ = true;
    closed_ = true;
    stopLocked();
  }
  try {
    bool owned = false;
    {
      std::lock_guard<std::mutex> guard(cache_mutex_);
      owned = output_owned_;
    }
    if (owned) {
      for (int attempt = 0; attempt < 3; ++attempt) {
        stepUnlocked();
        if (!status().stop_pending) {
          break;
        }
      }
    }
    std::shared_ptr<HumanoidBackend> backend;
    {
      std::lock_guard<std::mutex> guard(cache_mutex_);
      backend = backend_;
    }
    if (backend) {
      backend->stop();
    }
  } catch (const std::exception & error) {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    detail_ = std::string("Humanoid shutdown failed: ") + error.what();
  } catch (...) {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    detail_ = "Humanoid shutdown failed with an unknown error";
  }
}

DriverStatus HumanoidDriverCore::status() const {
  std::lock_guard<std::mutex> guard(cache_mutex_);
  return DriverStatus{detail_, fault_, output_error_, moving_, initialized_,
    stop_pending_, closed_, output_owned_, command_.has_value(),
    odometry_.has_value(), has_mode_, motion_enabled_,
    last_command_issued_mono_ns_};
}

std::optional<Sample> HumanoidDriverCore::sample() const {
  std::lock_guard<std::mutex> guard(cache_mutex_);
  return odometry_.has_value() ? std::optional<Sample>(odometry_->sample)
                               : std::nullopt;
}

}  // namespace chaos::humanoid
