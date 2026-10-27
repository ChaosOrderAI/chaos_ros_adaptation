// Native sensor_monitor node: per-stream metadata health without touching
// robot control or readiness. Port of chaos_sensors/monitor.py.
//
// The monitor only observes message headers: it never converts, re-frames or
// republishes sensor data, and it never influences the runtime readiness.
// A stream with monitor=false is not subscribed at all.
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_msgs/msg/key_value.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <std_msgs/msg/string.hpp>

#include "chaos_sensors/health.hpp"
#include "chaos_sensors/ros_stamp.hpp"
#include "chaos_sensors/sensor_config.hpp"
#include "chaos_sensors/sensor_monitor_node.hpp"
#include "chaos_sensors/stream_qos.hpp"

namespace chaos::sensors {
namespace {

struct MonitoredStream {
  std::string sensor_id;
  std::string stream_id;
  StreamConfig config;
  std::shared_ptr<HealthTracker> tracker;
  // Type-erased owner of the subscription; the node keeps it alive.
  std::shared_ptr<void> subscription;
};

}  // namespace

class SensorMonitorNode : public rclcpp::Node {
public:
  explicit SensorMonitorNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
      : rclcpp::Node("sensor_monitor", options) {
    declare_parameter<std::string>("asset_json", "{}");
    std::string asset_id;
    std::string asset_namespace;
    const std::vector<SensorConfig> sensors = parseAssetSensorsJson(
      get_parameter("asset_json").as_string(), asset_id, asset_namespace);
    asset_id_ = asset_id;
    // The endpoints are derived from the asset namespace, never from a
    // per-sensor namespace: the sensor order must not change them.
    std::string name_space = asset_namespace;
    for (const auto & sensor : sensors) {
      for (const auto & entry : sensor.streams) {
        if (!entry.second.monitor) {
          // monitor=false: the stream is not subscribed at all.
          continue;
        }
        addStream(sensor.id, entry.first, entry.second);
      }
    }
    std::string prefix = name_space;
    while (!prefix.empty() && prefix.back() == '/') {
      prefix.pop_back();
    }
    diagnostics_pub_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
      prefix + "/sensors/diagnostics", 10);
    description_pub_ = create_publisher<std_msgs::msg::String>(
      prefix + "/sensors/description",
      rclcpp::QoS(1).durability(rclcpp::DurabilityPolicy::TransientLocal));
    std_msgs::msg::String description;
    description.data = publicDescription(asset_id_, sensors);
    description_pub_->publish(description);

    // The diagnostic duty is liveness, not simulation: the timer runs on
    // the steady clock, so a paused or rewound /clock cannot stop the
    // receipt health from being reported.
    timer_ = create_wall_timer(std::chrono::seconds(1),
                               [this] { publishHealth(); });
  }

private:
  template <typename MessageT>
  void addTypedStream(const std::string & sensor_id,
                      const std::string & stream_id,
                      const StreamConfig & config) {
    MonitoredStream monitored;
    monitored.sensor_id = sensor_id;
    monitored.stream_id = stream_id;
    monitored.config = config;
    monitored.tracker = std::make_shared<HealthTracker>(
      config.frame_id, config.timeout_s,
      [this] { return get_clock()->now().nanoseconds(); });
    const auto tracker = monitored.tracker;
    auto subscription = create_subscription<MessageT>(
      config.topic, streamQoS(config.qos),
      [tracker](const MessageT & message) {
        tracker->observe(message.header.stamp.sec, message.header.stamp.nanosec,
                         message.header.frame_id);
      });
    monitored.subscription = subscription;
    streams_.push_back(std::move(monitored));
  }

  void addStream(const std::string & sensor_id, const std::string & stream_id,
                 const StreamConfig & config) {
    switch (config.type) {
      case StreamType::kImage:
        addTypedStream<sensor_msgs::msg::Image>(sensor_id, stream_id, config);
        break;
      case StreamType::kCameraInfo:
        addTypedStream<sensor_msgs::msg::CameraInfo>(sensor_id, stream_id,
                                                     config);
        break;
      case StreamType::kPointCloud2:
        addTypedStream<sensor_msgs::msg::PointCloud2>(sensor_id, stream_id,
                                                       config);
        break;
      case StreamType::kLaserScan:
        addTypedStream<sensor_msgs::msg::LaserScan>(sensor_id, stream_id,
                                                    config);
        break;
      case StreamType::kImu:
        addTypedStream<sensor_msgs::msg::Imu>(sensor_id, stream_id, config);
        break;
    }
  }

  void publishHealth() {
    diagnostic_msgs::msg::DiagnosticArray message;
    stampFromNs(get_clock()->now().nanoseconds(), message.header.stamp);
    for (const auto & monitored : streams_) {
      const Health health = monitored.tracker->snapshot();
      diagnostic_msgs::msg::DiagnosticStatus status;
      status.level = static_cast<std::uint8_t>(health.level);
      status.name = asset_id_ + "/sensors/" + monitored.sensor_id + "/" +
        monitored.stream_id;
      status.message = health.message;
      status.hardware_id = asset_id_ + "/" + monitored.sensor_id;
      for (const auto & [key, value] : health.values) {
        diagnostic_msgs::msg::KeyValue pair;
        pair.key = key;
        pair.value = value;
        status.values.push_back(pair);
      }
      diagnostic_msgs::msg::KeyValue topic;
      topic.key = "topic";
      topic.value = monitored.config.topic;
      status.values.push_back(topic);
      diagnostic_msgs::msg::KeyValue type;
      type.key = "message_type";
      type.value = streamTypeName(monitored.config.type);
      status.values.push_back(type);
      message.status.push_back(status);
    }
    diagnostics_pub_->publish(message);
  }

  std::string asset_id_;
  std::vector<MonitoredStream> streams_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr
    diagnostics_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr description_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace chaos::sensors

std::shared_ptr<rclcpp::Node> chaos::sensors::createSensorMonitorNode(
  const rclcpp::NodeOptions & options) {
  return std::make_shared<chaos::sensors::SensorMonitorNode>(options);
}
