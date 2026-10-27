// ROS-backed adapters: Lingao (Twist/Odometry), Unitree Go2
// (DriverVelocityCommand/Odometry boundary to the supervised driver process),
// and the legacy DJI read-only status reader. Ports of chaos_runtime/adapters/
// {lingao,go2,dji_legacy}.py.
//
// Each adapter owns a small mutex for its telemetry cache; subscription
// callbacks run on executor threads while the coordinator reads snapshot()
// from its control path. Device writes (publish) are non-blocking.
#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <chaos_interfaces/msg/driver_velocity_command.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <std_msgs/msg/string.hpp>

#include "chaos_runtime/adapter.hpp"

namespace chaos::runtime {

bool isAbsoluteTopic(const std::string & topic);
// Validates and returns the topic option or throws AssetConfigError.
std::string requireTopicOption(const nlohmann::json & options,
                               const std::string & name,
                               const std::string & plugin);

// Pure conversion, unit-testable without DDS. Returns nullopt when the
// odometry must be rejected (bad frames, non-finite values, bad quaternion,
// missing/stale/future source stamp). `ros_now_ns` is the receiving ROS
// clock; `mono_s` the receiving steady clock; both injected for tests.
std::optional<AdapterSnapshot> odometrySnapshot(
  const std::string & odom_frame, const std::string & base_frame,
  const std::string & header_frame, const std::string & child_frame,
  std::int64_t stamp_sec, std::int64_t stamp_nanosec,
  const std::array<double, 3> & position,
  const std::array<double, 4> & quaternion_xyzw,
  const std::array<double, 6> & twist, double state_timeout_s,
  std::int64_t ros_now_ns, double mono_s, const std::string & detail);

class LingaoAdapter : public Adapter {
public:
  static void validateOptions(const AssetConfig & asset);

  LingaoAdapter(rclcpp::Node & node, const AssetConfig & asset);

  AdapterSnapshot snapshot() override;
  void validateVelocity(const std::array<double, 6> & values) override;
  void setVelocity(const std::array<double, 6> & values) override;
  void stop() override;
  void controlledStop() override;

private:
  void onOdometry(const nav_msgs::msg::Odometry & message);

  control::Limits limits_;
  AssetFrames frames_;
  std::function<std::int64_t()> ros_now_ns_;
  std::function<double()> mono_s_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr publisher_;
  // The subscription is a member: rclcpp::Node keeps only weak references,
  // so a temporary would be destroyed and the endpoint would disappear.
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  std::mutex cache_mutex_;
  double last_received_s_ = 0.0;
  AdapterSnapshot data_;
  bool output_owned_ = false;
};

class Go2Adapter : public Adapter {
public:
  static void validateOptions(const AssetConfig & asset);

  Go2Adapter(rclcpp::Node & node, const AssetConfig & asset);

  AdapterSnapshot snapshot() override;
  void validateVelocity(const std::array<double, 6> & values) override;
  void setVelocity(const std::array<double, 6> & values) override;
  // The transport path: the command carries the original source stamp and
  // the immutable absolute CLOCK_MONOTONIC deadline D of the admitted
  // motion. D is published unchanged.
  void setVelocityUntil(const std::array<double, 6> & values,
                        std::int64_t source_stamp_ns,
                        std::int64_t expires_mono_ns) override;
  void stop() override;
  void controlledStop() override;

private:
  void onOdometry(const nav_msgs::msg::Odometry & message);
  // Publishes one internal driver command. The issue time is an actual
  // CLOCK_MONOTONIC reading taken by the caller; the deadline is the
  // unchanged D of the caller.
  void publishCommand(const std::array<double, 6> & values,
                      std::int64_t source_stamp_ns,
                      std::int64_t issued_mono_ns,
                      std::int64_t expires_mono_ns, bool stop);
  // The explicit stop barrier: six exact zeros, a current issue time, no
  // deadline. It never travels through the motion validity rules.
  void publishStop();

  control::Limits limits_;
  AssetFrames frames_;
  std::string base_frame_;
  // The clock-domain identity of this process, read once at construction and
  // cached. It is the identity of the boot instance and of the Linux time
  // namespace that the published deadline belongs to. An unreadable identity
  // throws in the constructor: the adapter never publishes a command with an
  // empty or guessed domain.
  std::string clock_domain_id_;
  std::function<std::int64_t()> ros_now_ns_;
  std::function<double()> mono_s_;
  rclcpp::Publisher<chaos_interfaces::msg::DriverVelocityCommand>::SharedPtr
    publisher_;
  // The subscription is a member: rclcpp::Node keeps only weak references.
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  std::mutex cache_mutex_;
  double last_received_s_ = 0.0;
  AdapterSnapshot data_;
  bool output_owned_ = false;
};

class DjiLegacyAdapter : public Adapter {
public:
  static void validateOptions(const AssetConfig & asset);

  DjiLegacyAdapter(rclcpp::Node & node, const AssetConfig & asset);

  AdapterSnapshot snapshot() override;

private:
  void onStatus(const std_msgs::msg::String & message);

  control::Limits limits_;
  // The subscription is a member: rclcpp::Node keeps only weak references.
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr status_sub_;
  std::function<double()> mono_s_;
  std::mutex cache_mutex_;
  double last_received_s_ = 0.0;
  bool flight_fresh_ = false;
};

// Creates an adapter instance after running the shared preflight
// (capabilities, options). Throws AssetConfigError for unknown plugins.
std::unique_ptr<Adapter> createAdapter(const std::string & plugin,
                                       rclcpp::Node & node,
                                       const AssetConfig & asset);

}  // namespace chaos::runtime
