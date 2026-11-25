// Native mock_sensor node: synthetic sensor messages for integration tests.
// The images, intrinsics, point cloud and
// ranges are deliberately synthetic and are not calibration data.
//
// Sampling follows the ROS clock, so pausing /clock pauses synthetic
// acquisition. The separate monitor uses a steady timer to keep reporting
// stale streams while simulation time is stopped.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <sensor_msgs/msg/laser_scan.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include "chaos_sensors/mock_sensor_node.hpp"
#include "chaos_sensors/ros_stamp.hpp"
#include "chaos_sensors/sensor_config.hpp"
#include "chaos_sensors/stream_qos.hpp"

namespace chaos::sensors {
namespace {

constexpr std::int32_t kImageHeight = 48;
constexpr std::int32_t kImageWidth = 64;

sensor_msgs::msg::Image makeImage(const std::string & frame_id) {
  sensor_msgs::msg::Image message;
  message.header.frame_id = frame_id;
  message.height = kImageHeight;
  message.width = kImageWidth;
  message.encoding = "rgb8";
  message.is_bigendian = 0;
  message.step = static_cast<std::uint32_t>(kImageWidth * 3);
  message.data.resize(static_cast<std::size_t>(kImageHeight) * kImageWidth * 3);
  std::size_t index = 0;
  for (std::int32_t y = 0; y < kImageHeight; ++y) {
    for (std::int32_t x = 0; x < kImageWidth; ++x) {
      message.data[index++] = static_cast<std::uint8_t>(x * 4);
      message.data[index++] = static_cast<std::uint8_t>(y * 5);
      message.data[index++] = 96;
    }
  }
  return message;
}

sensor_msgs::msg::CameraInfo makeCameraInfo(const std::string & frame_id) {
  sensor_msgs::msg::CameraInfo message;
  message.header.frame_id = frame_id;
  message.height = kImageHeight;
  message.width = kImageWidth;
  message.distortion_model = "plumb_bob";
  message.d.assign(5, 0.0);
  message.k = {40.0, 0.0, 31.5, 0.0, 40.0, 23.5, 0.0, 0.0, 1.0};
  message.r = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  message.p = {40.0, 0.0, 31.5, 0.0, 0.0, 40.0, 23.5, 0.0, 0.0, 0.0, 1.0, 0.0};
  return message;
}

sensor_msgs::msg::PointCloud2 makePointCloud2(const std::string & frame_id) {
  sensor_msgs::msg::PointCloud2 message;
  message.header.frame_id = frame_id;
  const char * names[3] = {"x", "y", "z"};
  for (std::uint32_t index = 0; index < 3; ++index) {
    sensor_msgs::msg::PointField field;
    field.name = names[index];
    field.offset = index * 4;
    field.datatype = sensor_msgs::msg::PointField::FLOAT32;
    field.count = 1;
    message.fields.push_back(field);
  }
  constexpr std::uint32_t kPoints = 8;
  message.height = 1;
  message.width = kPoints;
  message.is_bigendian = false;
  message.point_step = 12;
  message.row_step = message.point_step * message.width;
  message.is_dense = true;
  message.data.resize(static_cast<std::size_t>(kPoints) * 12);
  std::size_t offset = 0;
  for (std::uint32_t index = 0; index < kPoints; ++index) {
    const double angle = index * M_PI / 4.0;
    const float values[3] = {
      static_cast<float>(2.0 * std::cos(angle)),
      static_cast<float>(2.0 * std::sin(angle)),
      0.0F};
    std::memcpy(message.data.data() + offset, values, sizeof(values));
    offset += sizeof(values);
  }
  return message;
}

sensor_msgs::msg::LaserScan makeLaserScan(const std::string & frame_id,
                                          double rate_hz) {
  sensor_msgs::msg::LaserScan message;
  message.header.frame_id = frame_id;
  message.angle_min = -M_PI;
  message.angle_max = M_PI;
  message.angle_increment = M_PI / 18.0;
  message.time_increment = 0.0;  // one synthetic instantaneous sample
  message.scan_time = 1.0 / rate_hz;
  message.range_min = 0.1;
  message.range_max = 10.0;
  message.ranges.assign(37, 2.0);
  message.intensities.assign(37, 1.0);
  return message;
}

sensor_msgs::msg::Imu makeImu(const std::string & frame_id) {
  sensor_msgs::msg::Imu message;
  message.header.frame_id = frame_id;
  message.orientation.w = 1.0;
  message.linear_acceleration.z = 9.80665;
  message.orientation_covariance = {0.001, 0.0, 0.0, 0.0, 0.001, 0.0,
                                    0.0, 0.0, 0.001};
  message.angular_velocity_covariance = {0.0001, 0.0, 0.0, 0.0, 0.0001, 0.0,
                                         0.0, 0.0, 0.0001};
  message.linear_acceleration_covariance = {0.001, 0.0, 0.0, 0.0, 0.001, 0.0,
                                            0.0, 0.0, 0.001};
  return message;
}

}  // namespace

class MockSensorNode : public rclcpp::Node {
public:
  explicit MockSensorNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
      : rclcpp::Node("mock_sensor", options) {
    declare_parameter<std::string>("sensor_json", "{}");
    const SensorConfig sensor =
      parseSensorJson(get_parameter("sensor_json").as_string());
    for (const auto & entry : sensor.streams) {
      addStream(entry.second);
    }
    RCLCPP_INFO(get_logger(), "mock_sensor: %zu synthetic streams ready",
                sensor.streams.size());
  }

private:
  // The message payload is allocated once and reused on every publication;
  // the timer callback owns the publisher and the payload by value. Only the
  // payload buffers are preallocated; the DDS transport and the message
  // serialization may still allocate.
  template <typename MessageT>
  void addTypedStream(const StreamConfig & stream, MessageT message) {
    auto publisher = create_publisher<MessageT>(
      stream.topic, streamQoS(stream.qos));
    auto payload = std::make_shared<MessageT>(std::move(message));
    // The sampling cadence follows the node clock, exactly like the
    // pre-migration mock: a paused or rewound /clock stops the synthetic
    // samples, because a sample is a simulated acquisition and its stamp
    // would otherwise repeat. The monitor and the runtime watchdog use the
    // steady clock instead, because their duty is liveness, not simulation.
    timers_.push_back(create_timer(
      std::chrono::duration<double>(1.0 / stream.rate_hz),
      [this, payload = std::move(payload), publisher = std::move(publisher)] {
        stampFromNs(get_clock()->now().nanoseconds(), payload->header.stamp);
        publisher->publish(*payload);
      }));
  }

  void addStream(const StreamConfig & stream) {
    switch (stream.type) {
      case StreamType::kImage:
        addTypedStream(stream, makeImage(stream.frame_id));
        break;
      case StreamType::kCameraInfo:
        addTypedStream(stream, makeCameraInfo(stream.frame_id));
        break;
      case StreamType::kPointCloud2:
        addTypedStream(stream, makePointCloud2(stream.frame_id));
        break;
      case StreamType::kLaserScan:
        addTypedStream(stream, makeLaserScan(stream.frame_id, stream.rate_hz));
        break;
      case StreamType::kImu:
        addTypedStream(stream, makeImu(stream.frame_id));
        break;
    }
  }

  // Owners of the stream timers: the node keeps every stream alive.
  std::vector<rclcpp::TimerBase::SharedPtr> timers_;
};

}  // namespace chaos::sensors

std::shared_ptr<rclcpp::Node> chaos::sensors::createMockSensorNode(
  const rclcpp::NodeOptions & options) {
  return std::make_shared<chaos::sensors::MockSensorNode>(options);
}
