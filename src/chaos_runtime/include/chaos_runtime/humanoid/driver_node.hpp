// Independent ROS process facade for HumanoidDriverCore. SDK writes run on a
// steady-clock thread, not telemetry callbacks; a paused ROS clock cannot
// stop expiry, and a stalled executor cannot postpone a zero.
#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <chaos_interfaces/msg/driver_velocity_command.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>

#include "chaos_runtime/asset_config.hpp"
#include "chaos_runtime/humanoid/backend.hpp"
#include "chaos_runtime/humanoid/driver_core.hpp"

namespace chaos::humanoid {

nav_msgs::msg::Odometry fillOdometry(const Sample & sample,
  const std::string & odom_frame, const std::string & base_frame);

class HumanoidDriverNode : public rclcpp::Node {
public:
  explicit HumanoidDriverNode(
    BackendFactory backend_factory = {},
    const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  ~HumanoidDriverNode() override;

  void close();
  DriverStatus driverStatus() const { return core_->status(); }

private:
  void onCommand(const chaos_interfaces::msg::DriverVelocityCommand & message);
  void tick();
  void watchdogLoop();

  runtime::AssetConfig asset_;
  std::unique_ptr<HumanoidDriverCore> core_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;
  rclcpp::Subscription<chaos_interfaces::msg::DriverVelocityCommand>::SharedPtr cmd_sub_;
  std::mutex watchdog_mutex_;
  std::condition_variable watchdog_cv_;
  std::thread watchdog_;
  bool stop_watchdog_ = false;
  bool closing_ = false;
  std::string logged_fault_;
};

}  // namespace chaos::humanoid
