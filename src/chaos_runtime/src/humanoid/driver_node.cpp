#include "chaos_runtime/humanoid/driver_node.hpp"

#include <chrono>
#include <utility>

#include "chaos_runtime/asset_validation.hpp"
#include "chaos_runtime/driver_deadline.hpp"
#include "chaos_runtime/humanoid/process_guard.hpp"
#include "chaos_runtime/ros_stamp.hpp"

namespace chaos::humanoid {

nav_msgs::msg::Odometry fillOdometry(const Sample & sample,
  const std::string & odom_frame, const std::string & base_frame) {
  nav_msgs::msg::Odometry message;
  runtime::stampFromNs(sample.source_stamp_ns, message.header.stamp);
  message.header.frame_id = odom_frame;
  message.child_frame_id = base_frame;
  message.pose.pose.position.x = sample.position[0];
  message.pose.pose.position.y = sample.position[1];
  message.pose.pose.position.z = sample.position[2];
  message.pose.pose.orientation.x = sample.quaternion_xyzw[0];
  message.pose.pose.orientation.y = sample.quaternion_xyzw[1];
  message.pose.pose.orientation.z = sample.quaternion_xyzw[2];
  message.pose.pose.orientation.w = sample.quaternion_xyzw[3];
  message.twist.twist.linear.x = sample.twist[0];
  message.twist.twist.linear.y = sample.twist[1];
  message.twist.twist.linear.z = sample.twist[2];
  message.twist.twist.angular.x = sample.twist[3];
  message.twist.twist.angular.y = sample.twist[4];
  message.twist.twist.angular.z = sample.twist[5];
  return message;
}

HumanoidDriverNode::HumanoidDriverNode(
  BackendFactory backend_factory, const rclcpp::NodeOptions & options)
    : rclcpp::Node("humanoid_driver", options) {
  declare_parameter<std::string>("asset_json", "{}");
  asset_ = runtime::parseAssetJson(get_parameter("asset_json").as_string());
  runtime::validateAssetConfig(asset_);
  if (asset_.plugin != "humanoid") {
    throw std::runtime_error("humanoid_driver requires a humanoid asset");
  }
  const std::int64_t context_domain =
    get_node_base_interface()->get_context()->get_domain_id();
  if (context_domain != asset_.ros_domain_id) {
    throw std::runtime_error("Humanoid asset ros_domain_id does not match the active ROS context");
  }

  DriverConfig config;
  config.base_frame = asset_.frames.base;
  config.odom_frame = asset_.frames.odom;
  config.source_base_frame = asset_.options.at("source_base_frame").get<std::string>();
  config.source_odom_frame = asset_.options.at("source_odom_frame").get<std::string>();
  config.clock_domain_id = runtime::driver_deadline::localClockDomainId();
  config.limits = asset_.limits;
  core_ = std::make_unique<HumanoidDriverCore>(
    config, nullptr, nullptr,
    [this] { return get_clock()->now().nanoseconds(); },
    runtime::driver_deadline::monotonicNowNs);

  odom_pub_ = create_publisher<nav_msgs::msg::Odometry>(
    asset_.options.at("odom_topic").get<std::string>(), rclcpp::QoS(1));
  cmd_sub_ = create_subscription<chaos_interfaces::msg::DriverVelocityCommand>(
    asset_.options.at("cmd_vel_topic").get<std::string>(), rclcpp::QoS(1),
    [this](const chaos_interfaces::msg::DriverVelocityCommand & message) {
      onCommand(message);
    });

  BackendConfig backend_config;
  backend_config.dds_domain_id = static_cast<int>(
    asset_.options.at("dds_domain_id").get<std::int64_t>());
  backend_config.network_interface =
    asset_.options.at("network_interface").get<std::string>();
  backend_config.state_topic = asset_.options.at("state_topic").get<std::string>();
  backend_config.robot_state_topic =
    asset_.options.at("robot_state_topic").get<std::string>();
  if (!backend_factory) {
    backend_factory = backendFactory(asset_.options.at("backend").get<std::string>());
  }
  auto backend = backend_factory(
    backend_config,
    [this](const Odometry & message) { core_->acceptOdometry(message); },
    [this](bool enabled) { core_->acceptMotionMode(enabled); });
  if (!backend) {
    throw std::runtime_error("humanoid backend factory returned no backend");
  }
  core_->setBackend(std::move(backend));
  publishVendorSession(backend_config.dds_domain_id, backend_config.network_interface);
  // MoveCommand carries no vendor deadline. A steady thread, not the ROS
  // executor, owns the zero so a stalled callback cannot keep the last speed.
  watchdog_ = std::thread([this] { watchdogLoop(); });
  RCLCPP_INFO(get_logger(),
    "%s: humanoid driver started; waiting for odometry and motion-enabled mode",
    asset_.id.c_str());
}

HumanoidDriverNode::~HumanoidDriverNode() { close(); }

void HumanoidDriverNode::onCommand(
  const chaos_interfaces::msg::DriverVelocityCommand & message) {
  VelocityCommand command;
  command.stamp = Stamp{message.header.stamp.sec,
                        static_cast<std::int64_t>(message.header.stamp.nanosec)};
  command.frame_id = message.header.frame_id;
  command.values = {message.velocity.linear.x, message.velocity.linear.y,
    message.velocity.linear.z, message.velocity.angular.x,
    message.velocity.angular.y, message.velocity.angular.z};
  command.clock_domain_id = message.clock_domain_id;
  command.issued_monotonic_ns = message.issued_monotonic_ns;
  command.expires_monotonic_ns = message.expires_monotonic_ns;
  command.stop = message.stop;
  if (!core_->acceptCommand(command)) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "%s",
                         core_->status().detail.c_str());
  }
}

void HumanoidDriverNode::watchdogLoop() {
  constexpr auto period = std::chrono::milliseconds(10);
  while (true) {
    {
      std::unique_lock<std::mutex> lock(watchdog_mutex_);
      if (stop_watchdog_) {
        return;
      }
    }
    tick();
    std::unique_lock<std::mutex> lock(watchdog_mutex_);
    watchdog_cv_.wait_for(lock, period, [this] { return stop_watchdog_; });
    if (stop_watchdog_) {
      return;
    }
  }
}

void HumanoidDriverNode::tick() {
  const auto sample = core_->step();
  const auto status = core_->status();
  if (!status.fault.empty() && status.fault != logged_fault_) {
    logged_fault_ = status.fault;
    RCLCPP_ERROR(get_logger(), "%s; motion disabled until driver restart",
                 logged_fault_.c_str());
  } else if (!status.output_error.empty() && !status.initialized) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                         "%s; retrying initial zero output",
                         status.output_error.c_str());
  }
  if (sample.has_value()) {
    odom_pub_->publish(fillOdometry(*sample, core_->odomFrame(), core_->baseFrame()));
  }
}

void HumanoidDriverNode::close() {
  if (closing_) {
    return;
  }
  closing_ = true;
  {
    std::lock_guard<std::mutex> guard(watchdog_mutex_);
    stop_watchdog_ = true;
  }
  watchdog_cv_.notify_all();
  if (watchdog_.joinable()) {
    watchdog_.join();
  }
  core_->close();
  const auto status = core_->status();
  if (status.stop_pending && status.output_owned) {
    RCLCPP_ERROR(get_logger(), "Shutdown zero output failed after three attempts");
  } else {
    // The parent supervisor sends a zero only when this hand-off is missing.
    publishVendorSessionRelease();
  }
}

}  // namespace chaos::humanoid
