#include "chaos_skill/server_ports.hpp"

#include <utility>

namespace chaos_skill
{

namespace
{

constexpr const char * kCloudStream = "cloud";
constexpr const char * kImuStream = "imu";

std::int64_t stamp_ns(const builtin_interfaces::msg::Time & stamp)
{
  return static_cast<std::int64_t>(stamp.sec) * 1000000000LL +
    static_cast<std::int64_t>(stamp.nanosec);
}

}  // namespace

ServerPorts::ServerPorts(rclcpp::Node * node, ServerPortsOptions options)
: node_(node), options_(std::move(options))
{
  if (!node_) {
    throw std::invalid_argument("ServerPorts requires a node");
  }

  publisher_ = node_->create_publisher<chaos_interfaces::msg::Announcement>(
    options_.announcement_topic, rclcpp::QoS(10).reliable());

  cloud_.stream = kCloudStream;
  imu_.stream = kImuStream;

  if (!options_.cloud_topic.empty()) {
    cloud_sub_ = node_->create_subscription<sensor_msgs::msg::PointCloud2>(
      options_.cloud_topic, rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::PointCloud2::ConstSharedPtr message) {on_cloud(message);});
  }
  if (!options_.imu_topic.empty()) {
    imu_sub_ = node_->create_subscription<sensor_msgs::msg::Imu>(
      options_.imu_topic, rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::Imu::ConstSharedPtr message) {on_imu(message);});
  }
}

bool ServerPorts::announce(
  const std::string & topic,
  const std::string & payload_json,
  const SkillOrigin & origin,
  std::string & code,
  std::string & message)
{
  if (!publisher_) {
    code = "skill.no_communication";
    message = "this deployment has no communication channel";
    return false;
  }

  chaos_interfaces::msg::Announcement announcement;
  announcement.request_id = origin.request_id;
  announcement.source = origin.source;
  announcement.topic = topic;
  announcement.payload_json = payload_json;
  announcement.stamp = node_->now();
  publisher_->publish(announcement);

  code.clear();
  message.clear();
  return true;
}

AuxReading ServerPorts::read_aux(const std::string & stream) const
{
  const std::lock_guard<std::mutex> guard(mutex_);
  if (stream == kCloudStream) {
    return cloud_;
  }
  if (stream == kImuStream) {
    return imu_;
  }
  return AuxReading{};
}

std::vector<std::string> ServerPorts::aux_streams() const
{
  std::vector<std::string> streams;
  const std::lock_guard<std::mutex> guard(mutex_);
  if (cloud_sub_) {
    streams.push_back(cloud_.stream);
  }
  if (imu_sub_) {
    streams.push_back(imu_.stream);
  }
  return streams;
}

void ServerPorts::on_cloud(const sensor_msgs::msg::PointCloud2::ConstSharedPtr & message)
{
  if (!message) {
    return;
  }
  const std::lock_guard<std::mutex> guard(mutex_);
  cloud_.stream = kCloudStream;
  cloud_.frame_id = message->header.frame_id;
  cloud_.stamp_ns = stamp_ns(message->header.stamp);
  cloud_.valid = true;
}

void ServerPorts::on_imu(const sensor_msgs::msg::Imu::ConstSharedPtr & message)
{
  if (!message) {
    return;
  }
  const std::lock_guard<std::mutex> guard(mutex_);
  imu_.stream = kImuStream;
  imu_.frame_id = message->header.frame_id;
  imu_.stamp_ns = stamp_ns(message->header.stamp);
  imu_.valid = true;
}

}  // namespace chaos_skill
