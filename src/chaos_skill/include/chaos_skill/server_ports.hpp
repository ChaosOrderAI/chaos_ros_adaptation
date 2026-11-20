#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include "chaos_interfaces/msg/announcement.hpp"
#include "chaos_skill/capability.hpp"

namespace chaos_skill
{

/// The streams and the channel one deployment exposes to the skill layer.
struct ServerPortsOptions
{
  /// Topic the announcements are published on.
  std::string announcement_topic = "communication/announcement";
  /// Point cloud stream the "observe" skill may read; empty disables it.
  std::string cloud_topic;
  /// IMU stream the "observe" skill may read; empty disables it.
  std::string imu_topic;
};

/// The base capabilities of one deployment, as the skill server sees them.
///
/// The communication channel is a plain ROS topic: the inter-machine transport
/// subscribes to it and forwards the announcements. Nothing in this class
/// knows which transport that is, so replacing the transport touches neither
/// the skill layer nor the base capabilities below it.
///
/// The auxiliary sensor streams are read-only probes: the skill layer reports
/// the newest sample of each configured stream, never the payload itself.
class ServerPorts : public CapabilityPorts
{
public:
  /// The node that owns the publishers and subscriptions of the ports. The
  /// node outlives the ports: both belong to the same skill server.
  explicit ServerPorts(rclcpp::Node * node, ServerPortsOptions options = {});
  ~ServerPorts() override = default;

  bool announce(
    const std::string & topic,
    const std::string & payload_json,
    const SkillOrigin & origin,
    std::string & code,
    std::string & message) override;

  AuxReading read_aux(const std::string & stream) const override;

  std::vector<std::string> aux_streams() const override;

private:
  void on_cloud(const sensor_msgs::msg::PointCloud2::ConstSharedPtr & message);
  void on_imu(const sensor_msgs::msg::Imu::ConstSharedPtr & message);

  rclcpp::Node * node_;
  ServerPortsOptions options_;
  rclcpp::Publisher<chaos_interfaces::msg::Announcement>::SharedPtr publisher_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr imu_sub_;
  mutable std::mutex mutex_;
  AuxReading cloud_;
  AuxReading imu_;
};

}  // namespace chaos_skill
