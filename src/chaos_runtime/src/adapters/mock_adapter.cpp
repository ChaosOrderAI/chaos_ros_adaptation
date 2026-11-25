#include "chaos_runtime/adapters/mock_adapter.hpp"

#include <algorithm>
#include <cmath>

#include "chaos_runtime/asset_config.hpp"
#include "chaos_runtime/asset_validation.hpp"

namespace chaos::runtime {

std::set<std::string> MockAdapter::capabilitiesFor(const std::string & kind) {
  std::set<std::string> all = pluginSpec("mock").supported_capabilities;
  if (kind == "ugv" || kind == "quadruped" || kind == "humanoid") {
    all.erase("flight.takeoff");
    all.erase("flight.land");
  }
  return all;
}

std::string MockAdapter::validateOptions(const AssetConfig & asset) {
  for (const auto & key : asset.options.items()) {
    if (!key.key().empty() && pluginSpec("mock").option_names.count(key.key()) == 0) {
      throw AssetConfigError("Unsupported mock options: " + key.key());
    }
  }
  auto kind_option = asset.options.find("kind");
  const std::string kind = kind_option != asset.options.end()
                             ? kind_option->get<std::string>()
                             : asset.kind;
  if (kind != "ugv" && kind != "uav" && kind != "quadruped" && kind != "humanoid") {
    throw AssetConfigError("mock kind must be ugv, uav, quadruped or humanoid");
  }
  if (asset.kind != kind) {
    throw AssetConfigError("mock options.kind must agree with the asset kind");
  }
  return kind;
}

MockAdapter::MockAdapter(AssetConfig asset)
    : kind_(validateOptions(asset)),
      limits_(asset.limits),
      map_frame_(asset.frames.map),
      base_frame_(asset.frames.base) {}

AdapterSnapshot MockAdapter::snapshot() {
  AdapterSnapshot snapshot;
  snapshot.connected = true;
  snapshot.ready = true;
  snapshot.pose_valid = true;
  snapshot.position = position_;
  snapshot.quaternion = {0.0, 0.0, std::sin(yaw_ / 2), std::cos(yaw_ / 2)};
  snapshot.velocity = velocity_;
  // Humanoid battery telemetry is not part of the SDK contract: preserve
  // AdapterSnapshot's NaN (rendered as null) instead of inventing a value.
  if (kind_ != "humanoid") {
    snapshot.battery = 1.0;
  }
  snapshot.detail = "Synthetic mock telemetry; no hardware connected";
  snapshot.pose_frame = map_frame_;
  snapshot.twist_frame = base_frame_;
  return snapshot;
}

void MockAdapter::validateVelocity(const std::array<double, 6> & values) {
  if (std::abs(values[3]) > 1e-9 || std::abs(values[4]) > 1e-9) {
    throw chaos::Rejected("UNSUPPORTED",
                          "Mock velocity supports yaw angular velocity only");
  }
  if (kind_ == "ugv" &&
      (std::abs(values[1]) > 1e-9 || std::abs(values[2]) > 1e-9)) {
    throw chaos::Rejected("UNSUPPORTED",
                          "Ground velocity supports linear x and angular z only");
  }
  if ((kind_ == "quadruped" || kind_ == "humanoid") &&
      std::abs(values[2]) > 1e-9) {
    throw chaos::Rejected(
      "UNSUPPORTED",
      "Planar velocity supports linear x/y and angular z only");
  }
}

void MockAdapter::setVelocity(const std::array<double, 6> & values) {
  validateVelocity(values);
  velocity_ = values;
}

void MockAdapter::stop() {
  velocity_ = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  target_.reset();
  finished_ = false;
}

void MockAdapter::startOperation(OperationKind kind,
                                 const std::array<double, 4> & target) {
  velocity_ = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  if (kind == OperationKind::kNavigatePose) {
    target_ = target;
  } else if (kind == OperationKind::kTakeoff) {
    if (kind_ != "uav") {
      throw chaos::Rejected("UNSUPPORTED",
                            "Only mock UAV supports takeoff");
    }
    if (position_[2] > 0.1) {
      throw chaos::Rejected("INVALID_STATE",
                            "Mock UAV is already airborne");
    }
    target_ = std::array<double, 4>{position_[0], position_[1], target[0],
                                    yaw_};
  } else if (kind == OperationKind::kLand) {
    if (kind_ != "uav") {
      throw chaos::Rejected("UNSUPPORTED", "Only mock UAV supports landing");
    }
    target_ = std::array<double, 4>{position_[0], position_[1], 0.0, yaw_};
  } else {
    throw chaos::Rejected("UNSUPPORTED", "unknown operation");
  }
  initial_distance_ = distance();
  progress_ = 0.0;
  finished_ = false;
}

double MockAdapter::distance() const {
  if (!target_) {
    return 0.0;
  }
  const double dx = (*target_)[0] - position_[0];
  const double dy = (*target_)[1] - position_[1];
  const double dz = (*target_)[2] - position_[2];
  const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
  double angle = std::remainder((*target_)[3] - yaw_, 2 * M_PI);
  angle = std::abs(angle);
  return distance + angle;
}

void MockAdapter::update(double dt) {
  dt = std::max(0.0, std::min(dt, 0.2));
  if (target_.has_value()) {
    const auto old_position = position_;
    const double old_yaw = yaw_;
    const double dx = (*target_)[0] - position_[0];
    const double dy = (*target_)[1] - position_[1];
    const double dz = (*target_)[2] - position_[2];
    const double distance = std::hypot(dx, dy, dz);
    const double step = std::min(distance, limits_.linear_mps * dt);
    if (distance > 1e-9) {
      position_[0] += dx / distance * step;
      position_[1] += dy / distance * step;
      position_[2] += dz / distance * step;
    }
    const double angle =
      std::remainder((*target_)[3] - yaw_, 2 * M_PI);
    yaw_ += std::max(-limits_.angular_rps * dt,
                     std::min(limits_.angular_rps * dt, angle));
    if (dt > 0) {
      const double vx = (position_[0] - old_position[0]) / dt;
      const double vy = (position_[1] - old_position[1]) / dt;
      const double vz = (position_[2] - old_position[2]) / dt;
      velocity_ = {vx * std::cos(yaw_) + vy * std::sin(yaw_),
                   -vx * std::sin(yaw_) + vy * std::cos(yaw_), vz, 0.0, 0.0,
                   (yaw_ - old_yaw) / dt};
    }
    const double remaining = this->distance();
    progress_ = std::max(
      0.0, std::min(1.0, 1.0 - remaining / std::max(initial_distance_, 1e-9)));
    if (remaining < 1e-5) {
      position_ = {(*target_)[0], (*target_)[1], (*target_)[2]};
      yaw_ = (*target_)[3];
      target_.reset();
      finished_ = true;
      progress_ = 1.0;
      velocity_ = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    }
    return;
  }
  const double vx = velocity_[0], vy = velocity_[1], vz = velocity_[2];
  const double wz = velocity_[5];
  position_[0] += (vx * std::cos(yaw_) - vy * std::sin(yaw_)) * dt;
  position_[1] += (vx * std::sin(yaw_) + vy * std::cos(yaw_)) * dt;
  const double previous_z = position_[2];
  position_[2] = std::max(
    0.0, std::min(limits_.max_altitude_m, previous_z + vz * dt));
  if (dt > 0 &&
      (position_[2] == 0.0 || position_[2] == limits_.max_altitude_m)) {
    velocity_[2] = (position_[2] - previous_z) / dt;
  }
  yaw_ = std::remainder(yaw_ + wz * dt, 2 * M_PI);
}


PollResult MockAdapter::pollOperation() {
  PollResult result;
  result.done = finished_;
  result.success = finished_;
  result.code = "OK";
  result.message = "Mock target reached";
  result.progress = progress_;
  return result;
}

}  // namespace chaos::runtime
