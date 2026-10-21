// Validates the introspection driven decoder against messages that were
// serialized by the local rmw implementation. Every expected value is
// compared with the value that was written into the original message, so a
// wrong alignment or field order cannot pass.
//
// The decoder is the fallback path of the data plane: the known types are
// decoded with rclcpp::Serialization<T> in the audit, and this layer is used
// for every other type of a bag.
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <geometry_msgs/msg/pose_stamped.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include "chaos_data/ros_message.hpp"

namespace {

template<typename MessageT>
std::vector<std::uint8_t> serialize(const MessageT & message)
{
  rclcpp::Serialization<MessageT> serialization;
  rclcpp::SerializedMessage serialized(1024 * 64);
  serialization.serialize_message(&message, &serialized);
  const auto & raw = serialized.get_rcl_serialized_message();
  return std::vector<std::uint8_t>(raw.buffer, raw.buffer + raw.buffer_length);
}

// The decoder keeps the payload alive, so the buffer is handed over as a
// shared object.
std::shared_ptr<const std::vector<std::uint8_t>> keep(
  const std::vector<std::uint8_t> & bytes)
{
  return std::make_shared<const std::vector<std::uint8_t>>(bytes);
}

}  // namespace

TEST(RosMessage, DecodesImageHeaderAndScalars) {
  sensor_msgs::msg::Image image;
  image.header.stamp.sec = 1234;
  image.header.stamp.nanosec = 5678;
  image.header.frame_id = "camera";
  image.height = 2;
  image.width = 3;
  image.encoding = "mono8";
  image.is_bigendian = 1;
  image.step = 3;
  image.data = std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6};

  const auto bytes = serialize(image);
  chaos::data::DynamicMessage message("sensor_msgs/msg/Image", keep(bytes));
  ASSERT_TRUE(message.valid()) << message.decodeError();

  std::int64_t seconds = 0;
  std::uint32_t nanoseconds = 0;
  ASSERT_TRUE(message.headerStamp(seconds, nanoseconds));
  EXPECT_EQ(seconds, 1234);
  EXPECT_EQ(nanoseconds, 5678U);

  std::string frame;
  ASSERT_TRUE(message.headerFrame(frame));
  EXPECT_EQ(frame, "camera");

  chaos::data::DynamicValue value;
  ASSERT_TRUE(message.read("height", value));
  double number = 0.0;
  ASSERT_TRUE(value.asNumber(number));
  EXPECT_DOUBLE_EQ(number, 2.0);

  ASSERT_TRUE(message.read("width", value));
  ASSERT_TRUE(value.asNumber(number));
  EXPECT_DOUBLE_EQ(number, 3.0);

  ASSERT_TRUE(message.read("step", value));
  ASSERT_TRUE(value.asNumber(number));
  EXPECT_DOUBLE_EQ(number, 3.0);

  // A boolean field of a ROS 2 message is an uint8_t in the C++ struct, so
  // it arrives as an unsigned integer and asBoolean() converts it.
  ASSERT_TRUE(message.read("is_bigendian", value));
  ASSERT_EQ(value.kind, chaos::data::DynamicValue::Kind::kUint64);
  EXPECT_EQ(value.uinteger, 1U);
  EXPECT_TRUE(value.asBoolean());

  ASSERT_TRUE(message.read("encoding", value));
  ASSERT_EQ(value.kind, chaos::data::DynamicValue::Kind::kString);
  EXPECT_EQ(value.text, "mono8");
}

TEST(RosMessage, IndexesIntoSequencesWithoutExpandingThem) {
  sensor_msgs::msg::Image image;
  image.header.frame_id = "camera";
  image.data = std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6};

  const auto bytes = serialize(image);
  chaos::data::DynamicMessage message("sensor_msgs/msg/Image", keep(bytes));
  ASSERT_TRUE(message.valid()) << message.decodeError();

  chaos::data::DynamicValue value;
  // A container is never expanded into a value: the payload keeps its
  // native bytes.
  EXPECT_FALSE(message.read("data", value));

  // One element of the sequence is addressable by its index.
  ASSERT_TRUE(message.read("data.0", value));
  ASSERT_EQ(value.kind, chaos::data::DynamicValue::Kind::kUint64);
  EXPECT_EQ(value.uinteger, 1U);
  ASSERT_TRUE(message.read("data.5", value));
  EXPECT_EQ(value.uinteger, 6U);

  // The bound is the real size of the container, so an index outside it is
  // refused instead of reading foreign memory.
  EXPECT_FALSE(message.read("data.6", value));
  EXPECT_FALSE(message.read("data.4294967295", value));
  // A component that is not a number is not an index.
  EXPECT_FALSE(message.read("data.first", value));
}

TEST(RosMessage, DecodesNestedStructures) {
  geometry_msgs::msg::PoseStamped pose;
  pose.header.stamp.sec = 42;
  pose.header.stamp.nanosec = 24;
  pose.header.frame_id = "map";
  pose.pose.position.x = 1.5;
  pose.pose.position.y = -2.25;
  pose.pose.position.z = 3.0;
  pose.pose.orientation.x = 0.0;
  pose.pose.orientation.y = 0.0;
  pose.pose.orientation.z = 0.0;
  pose.pose.orientation.w = 1.0;

  const auto bytes = serialize(pose);
  chaos::data::DynamicMessage message("geometry_msgs/msg/PoseStamped", keep(bytes));
  ASSERT_TRUE(message.valid()) << message.decodeError();

  chaos::data::DynamicValue value;
  double number = 0.0;
  ASSERT_TRUE(message.read("pose.position.x", value));
  ASSERT_TRUE(value.asNumber(number));
  EXPECT_DOUBLE_EQ(number, 1.5);
  ASSERT_TRUE(message.read("pose.position.y", value));
  ASSERT_TRUE(value.asNumber(number));
  EXPECT_DOUBLE_EQ(number, -2.25);
  ASSERT_TRUE(message.read("pose.orientation.w", value));
  ASSERT_TRUE(value.asNumber(number));
  EXPECT_DOUBLE_EQ(number, 1.0);

  std::int64_t seconds = 0;
  std::uint32_t nanoseconds = 0;
  ASSERT_TRUE(message.read("header.stamp", value));
  ASSERT_TRUE(value.asTimestamp(seconds, nanoseconds));
  EXPECT_EQ(seconds, 42);
  EXPECT_EQ(nanoseconds, 24U);
}

TEST(RosMessage, DecodesSequencesOfMessages) {
  sensor_msgs::msg::PointCloud2 cloud;
  cloud.header.frame_id = "lidar";
  cloud.height = 1;
  cloud.width = 2;
  cloud.fields.resize(2);
  cloud.fields[0].name = "x";
  cloud.fields[0].count = 1;
  cloud.fields[0].datatype = 7;
  cloud.fields[0].offset = 0;
  cloud.fields[1].name = "y";
  cloud.fields[1].count = 1;
  cloud.fields[1].datatype = 7;
  cloud.fields[1].offset = 4;
  cloud.is_bigendian = 1;
  cloud.point_step = 4;
  cloud.row_step = 8;
  cloud.data = std::vector<std::uint8_t>{9, 8, 7, 6};
  cloud.is_dense = true;

  const auto bytes = serialize(cloud);
  chaos::data::DynamicMessage message("sensor_msgs/msg/PointCloud2", keep(bytes));
  ASSERT_TRUE(message.valid()) << message.decodeError();

  chaos::data::DynamicValue value;
  // The element of a sequence of messages is a message, so a field inside it
  // is addressed through the index.
  ASSERT_TRUE(message.read("fields.0.name", value));
  ASSERT_EQ(value.kind, chaos::data::DynamicValue::Kind::kString);
  EXPECT_EQ(value.text, "x");
  ASSERT_TRUE(message.read("fields.1.name", value));
  EXPECT_EQ(value.text, "y");
  EXPECT_FALSE(message.read("fields.2.name", value));

  double number = 0.0;
  ASSERT_TRUE(message.read("point_step", value));
  ASSERT_TRUE(value.asNumber(number));
  EXPECT_DOUBLE_EQ(number, 4.0);
  ASSERT_TRUE(message.read("is_dense", value));
  EXPECT_TRUE(value.asBoolean());
  ASSERT_TRUE(message.read("data.0", value));
  EXPECT_EQ(value.uinteger, 9U);
  ASSERT_TRUE(message.read("data.3", value));
  EXPECT_EQ(value.uinteger, 6U);
  EXPECT_FALSE(message.read("data.4", value));

  std::string frame;
  ASSERT_TRUE(message.headerFrame(frame));
  EXPECT_EQ(frame, "lidar");
}

TEST(RosMessage, UnknownTypeIsReported) {
  const std::vector<std::uint8_t> bytes(16, 0);
  chaos::data::DynamicMessage message("nonexistent_msgs/msg/Nothing", keep(bytes));
  EXPECT_FALSE(message.valid());
  EXPECT_FALSE(message.decodeError().empty());
  EXPECT_FALSE(chaos::data::DynamicMessage::typeSupported(
    "nonexistent_msgs/msg/Nothing"));
  EXPECT_TRUE(chaos::data::DynamicMessage::typeSupported(
    "sensor_msgs/msg/Image"));
}

TEST(RosMessage, CorruptPayloadIsReported) {
  // A payload that is far too short for the message cannot be decoded: the
  // deserializer has to refuse it instead of reading beyond the buffer.
  const std::vector<std::uint8_t> truncated(8, 0);
  chaos::data::DynamicMessage message("sensor_msgs/msg/Image", keep(truncated));
  EXPECT_FALSE(message.valid());
  EXPECT_FALSE(message.decodeError().empty());
}

TEST(RosMessage, MissingPathFails) {
  sensor_msgs::msg::Image image;
  image.header.frame_id = "c";
  const auto bytes = serialize(image);
  chaos::data::DynamicMessage message("sensor_msgs/msg/Image", keep(bytes));
  ASSERT_TRUE(message.valid());
  chaos::data::DynamicValue value;
  EXPECT_FALSE(message.read("not_a_member", value));
  EXPECT_FALSE(message.read("header.not_a_member", value));
  EXPECT_FALSE(message.read("", value));
}
