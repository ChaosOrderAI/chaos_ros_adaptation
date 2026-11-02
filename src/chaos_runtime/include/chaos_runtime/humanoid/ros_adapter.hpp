// ROS adapter for the independent humanoid watchdog transport.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <chaos_interfaces/msg/driver_velocity_command.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>

#include "chaos_runtime/adapter.hpp"

namespace chaos::runtime {

class HumanoidAdapter : public Adapter {
public:
  static void validateOptions(const AssetConfig & asset);
  HumanoidAdapter(rclcpp::Node & node, const AssetConfig & asset);

  AdapterSnapshot snapshot() override;
  void validateVelocity(const std::array<double, 6> & values) override;
  void setVelocity(const std::array<double, 6> & values) override;
  void setVelocityUntil(const std::array<double, 6> & values,
    std::int64_t source_stamp_ns, std::int64_t expires_mono_ns) override;
  void stop() override;
  void controlledStop() override;

private:
  void onOdometry(const nav_msgs::msg::Odometry & message);
  void publishCommand(const std::array<double, 6> & values,
    std::int64_t source_stamp_ns, std::int64_t issued_mono_ns,
    std::int64_t expires_mono_ns, bool stop);
  void publishStop();

  control::Limits limits_;
  AssetFrames frames_;
  std::string clock_domain_id_;
  std::function<std::int64_t()> ros_now_ns_;
  std::function<double()> mono_s_;
  rclcpp::Publisher<chaos_interfaces::msg::DriverVelocityCommand>::SharedPtr publisher_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  std::mutex cache_mutex_;
  double last_received_s_ = 0.0;
  std::int64_t last_source_stamp_ns_ = 0;
  AdapterSnapshot data_;
  bool output_owned_ = false;
};

}  // namespace chaos::runtime
