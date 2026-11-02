#include "chaos_runtime/humanoid/ros_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "chaos_core/clock.hpp"
#include "chaos_core/time.hpp"
#include "chaos_runtime/driver_deadline.hpp"
#include "chaos_runtime/humanoid/options.hpp"
#include "chaos_runtime/ros_adapters.hpp"

namespace chaos::runtime {

void HumanoidAdapter::validateOptions(const AssetConfig & asset) {
  validateHumanoidOptions(asset);
}

HumanoidAdapter::HumanoidAdapter(rclcpp::Node & node, const AssetConfig & asset)
    : limits_(asset.limits), frames_(asset.frames),
      clock_domain_id_(driver_deadline::localClockDomainId()),
      ros_now_ns_([&node] { return node.get_clock()->now().nanoseconds(); }),
      mono_s_(chaos::monotonicSeconds) {
  validateOptions(asset);
  publisher_ = node.create_publisher<chaos_interfaces::msg::DriverVelocityCommand>(
    asset.options.at("cmd_vel_topic").get<std::string>(), rclcpp::QoS(1));
  odom_sub_ = node.create_subscription<nav_msgs::msg::Odometry>(
    asset.options.at("odom_topic").get<std::string>(), rclcpp::SensorDataQoS(),
    [this](const nav_msgs::msg::Odometry & message) { onOdometry(message); });
}

void HumanoidAdapter::onOdometry(const nav_msgs::msg::Odometry & message) {
  const auto & p = message.pose.pose.position;
  const auto & q = message.pose.pose.orientation;
  const auto & t = message.twist.twist;
  const std::int64_t ros_now = ros_now_ns_();
  const double received_s = mono_s_();
  auto snapshot = odometrySnapshot(frames_.odom, frames_.base,
    message.header.frame_id, message.child_frame_id,
    message.header.stamp.sec, message.header.stamp.nanosec,
    {p.x, p.y, p.z}, {q.x, q.y, q.z, q.w},
    {t.linear.x, t.linear.y, t.linear.z, t.angular.x, t.angular.y, t.angular.z},
    limits_.state_timeout_s, ros_now, received_s,
    "Humanoid odometry via managed driver; motion also requires current walking telemetry");
  std::lock_guard<std::mutex> guard(cache_mutex_);
  if (!snapshot.has_value()) {
    last_received_s_ = 0.0;
    return;
  }
  if (last_source_stamp_ns_ != 0 &&
      *snapshot->source_stamp_ns <= last_source_stamp_ns_) {
    return;  // a replay cannot refresh receipt time
  }
  last_source_stamp_ns_ = *snapshot->source_stamp_ns;
  last_received_s_ = received_s - std::max(
    0.0, static_cast<double>(ros_now - *snapshot->source_stamp_ns) / 1e9);
  data_ = *snapshot;
}

AdapterSnapshot HumanoidAdapter::snapshot() {
  std::lock_guard<std::mutex> guard(cache_mutex_);
  if (last_received_s_ == 0.0 ||
      mono_s_() - last_received_s_ >= limits_.state_timeout_s) {
    AdapterSnapshot stale;
    stale.detail = "Humanoid driver telemetry unavailable or stale";
    return stale;
  }
  return data_;
}

void HumanoidAdapter::validateVelocity(const std::array<double, 6> & values) {
  for (double value : values) {
    if (!std::isfinite(value)) {
      throw chaos::Rejected("INVALID_ARGUMENT",
                            "Humanoid velocity must contain six finite components");
    }
  }
  if (values[2] != 0.0 || values[3] != 0.0 || values[4] != 0.0) {
    throw chaos::Rejected("UNSUPPORTED",
                          "Humanoid accepts linear x/y and angular z only");
  }
}

void HumanoidAdapter::publishCommand(const std::array<double, 6> & values,
  std::int64_t source_stamp_ns, std::int64_t issued_mono_ns,
  std::int64_t expires_mono_ns, bool stop) {
  chaos_interfaces::msg::DriverVelocityCommand message;
  std::int64_t sec = 0, nanosec = 0;
  chaos::rosStampFromNs(source_stamp_ns, sec, nanosec);
  message.header.stamp.sec = static_cast<std::int32_t>(sec);
  message.header.stamp.nanosec = static_cast<std::uint32_t>(nanosec);
  message.header.frame_id = frames_.base;
  message.clock_domain_id = clock_domain_id_;
  message.issued_monotonic_ns = issued_mono_ns;
  message.expires_monotonic_ns = expires_mono_ns;
  message.stop = stop;
  message.velocity.linear.x = values[0];
  message.velocity.linear.y = values[1];
  message.velocity.linear.z = values[2];
  message.velocity.angular.x = values[3];
  message.velocity.angular.y = values[4];
  message.velocity.angular.z = values[5];
  publisher_->publish(message);
}

void HumanoidAdapter::publishStop() {
  publishCommand({0.0, 0.0, 0.0, 0.0, 0.0, 0.0}, 0,
                 driver_deadline::monotonicNowNs(), 0, true);
}

void HumanoidAdapter::setVelocity(const std::array<double, 6> & values) {
  validateVelocity(values);
  const bool nonzero = std::any_of(values.begin(), values.end(),
                                   [](double value) { return value != 0.0; });
  if (nonzero) {
    throw chaos::Rejected("INVALID_ARGUMENT",
                          "Humanoid motion requires an absolute monotonic deadline");
  }
  controlledStop();
}

void HumanoidAdapter::setVelocityUntil(const std::array<double, 6> & values,
  std::int64_t source_stamp_ns, std::int64_t expires_mono_ns) {
  validateVelocity(values);
  if (source_stamp_ns <= 0) {
    throw chaos::Rejected("INVALID_ARGUMENT",
                          "Humanoid motion requires a positive ROS-range source stamp");
  }
  std::int64_t sec = 0, nanosec = 0, checked_ns = 0;
  chaos::rosStampFromNs(source_stamp_ns, sec, nanosec);
  if (!chaos::rosStampToNs(sec, nanosec, checked_ns) || checked_ns != source_stamp_ns) {
    throw chaos::Rejected("INVALID_ARGUMENT",
                          "Humanoid motion requires a positive ROS-range source stamp");
  }
  const std::int64_t issued = driver_deadline::monotonicNowNs();
  if (expires_mono_ns <= 0 || issued <= 0 || issued >= expires_mono_ns) {
    throw chaos::Rejected("STALE_COMMAND",
                          "Humanoid motion deadline has already passed");
  }
  output_owned_ = true;
  publishCommand(values, source_stamp_ns, issued, expires_mono_ns, false);
}

void HumanoidAdapter::stop() {
  if (output_owned_) {
    publishStop();
    output_owned_ = false;
  }
}

void HumanoidAdapter::controlledStop() {
  output_owned_ = true;
  stop();
}

}  // namespace chaos::runtime
