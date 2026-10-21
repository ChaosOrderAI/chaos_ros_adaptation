// Native audit tests against a real rosbag2 bag.
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosbag2_cpp/writer.hpp>
#include <rosbag2_storage/storage_options.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "chaos_data/audit.hpp"

namespace {

class TemporaryDirectory {
public:
  TemporaryDirectory()
  {
    const auto base = std::filesystem::temp_directory_path();
    for (int attempt = 0; attempt < 64; ++attempt) {
      const auto candidate =
        base / ("chaos_data_audit_" + std::to_string(::getpid()) + "_" +
                std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(candidate, error)) {
        path_ = candidate;
        return;
      }
    }
    throw std::runtime_error("cannot create a temporary directory");
  }
  ~TemporaryDirectory()
  {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  const std::filesystem::path & path() const { return path_; }

private:
  std::filesystem::path path_;
};

sensor_msgs::msg::Image makeImage(std::int32_t sec, std::uint32_t nanosec,
                                  const std::string & frame)
{
  sensor_msgs::msg::Image image;
  image.header.stamp.sec = sec;
  image.header.stamp.nanosec = nanosec;
  image.header.frame_id = frame;
  image.height = 1;
  image.width = 2;
  image.encoding = "mono8";
  image.is_bigendian = 0;
  image.step = 2;
  image.data = std::vector<std::uint8_t>{1, 2};
  return image;
}

// Writes a bag with three valid stamps, one zero stamp and one message of
// an unknown type.
std::filesystem::path writeBag(const std::filesystem::path & directory)
{
  rosbag2_cpp::Writer writer;
  rosbag2_storage::StorageOptions storage;
  storage.uri = (directory / "bag").string();
  storage.storage_id = "sqlite3";
  writer.open(storage);

  rosbag2_storage::TopicMetadata topic;
  topic.name = "/test/image";
  topic.type = "sensor_msgs/msg/Image";
  topic.serialization_format = "cdr";
  writer.create_topic(topic);

  const std::vector<sensor_msgs::msg::Image> images = {
    makeImage(100, 5, "camera"),
    makeImage(101, 6, "camera"),
    makeImage(0, 0, "camera"),   // a zero stamp is not a valid source time
    makeImage(102, 7, "other"),  // a second frame id
  };
  std::int64_t receipt = 200000000000LL;
  for (const auto & image : images) {
    rclcpp::Serialization<sensor_msgs::msg::Image> serialization;
    rclcpp::SerializedMessage serialized(1024);
    serialization.serialize_message(&image, &serialized);
    const auto & raw = serialized.get_rcl_serialized_message();
    rosbag2_storage::SerializedBagMessage message;
    message.serialized_data = std::make_shared<rcutils_uint8_array_t>();
    message.serialized_data->buffer_length = raw.buffer_length;
    message.serialized_data->buffer = const_cast<uint8_t *>(raw.buffer);
    message.topic_name = topic.name;
    message.recv_timestamp = receipt;
    message.send_timestamp = receipt;
    writer.write(std::make_shared<rosbag2_storage::SerializedBagMessage>(message));
    receipt += 1000000;
  }
  writer.close();
  return directory / "bag";
}

}  // namespace

TEST(Audit, ReportsSourceStampStatisticsOfARealBag)
{
  TemporaryDirectory directory;
  const auto bag = writeBag(directory.path());

  chaos::data::AuditOptions options;
  options.index_path = (directory.path() / "index.jsonl").string();
  const nlohmann::json report = chaos::data::auditBag(bag.string(), options);

  // The original report fields are preserved.
  EXPECT_EQ(report.at("schema_version").get<int>(), 1);
  EXPECT_EQ(report.at("timestamp_difference").get<std::string>(),
            "receipt_minus_source; not device clock offset");
  EXPECT_EQ(report.at("source_clock_domain").get<std::string>(),
            "unverified; source and recorder clocks may differ");
  EXPECT_EQ(report.at("epoch_policy").get<std::string>(),
            "caller supplied; no correction or cross-epoch pairing");
  EXPECT_EQ(report.at("max_difference_ns").get<std::int64_t>(), 100000000);
  EXPECT_EQ(report.at("modified_messages").get<int>(), 0);
  EXPECT_TRUE(report.at("complete").get<bool>());
  EXPECT_EQ(report.at("storage_id").get<std::string>(), "sqlite3");
  EXPECT_FALSE(report.at("clock_epoch_evidence").get<std::string>().empty());

  const nlohmann::json & topic = report.at("topics").at("/test/image");
  EXPECT_EQ(topic.at("messages").get<std::int64_t>(), 4);
  EXPECT_EQ(topic.at("valid_source_stamps").get<std::int64_t>(), 3);
  EXPECT_EQ(topic.at("zero_source_stamps").get<std::int64_t>(), 1);
  EXPECT_EQ(topic.at("decode_errors").get<std::int64_t>(), 0);
  EXPECT_EQ(topic.at("source_reversals").get<std::int64_t>(), 0);
  EXPECT_EQ(topic.at("difference_samples").get<std::int64_t>(), 3);
  EXPECT_EQ(topic.at("first_source_ns").get<std::int64_t>(), 100000000005LL);
  EXPECT_EQ(topic.at("last_source_ns").get<std::int64_t>(), 102000000007LL);
  EXPECT_EQ(topic.at("frames").at("camera").get<std::int64_t>(), 3);
  EXPECT_EQ(topic.at("frames").at("other").get<std::int64_t>(), 1);
  EXPECT_EQ(report.at("messages").get<std::int64_t>(), 4);

  // The index is a new file outside the bag.
  std::ifstream index(options.index_path);
  ASSERT_TRUE(index.good());
  std::string line;
  int lines = 0;
  while (std::getline(index, line)) {
    const nlohmann::json entry = nlohmann::json::parse(line);
    EXPECT_TRUE(entry.contains("sequence"));
    EXPECT_TRUE(entry.contains("stamp_kind"));
    ++lines;
  }
  EXPECT_EQ(lines, 4);
}

TEST(Audit, ReportsDecodeErrorsForUnknownTypes)
{
  TemporaryDirectory directory;
  rosbag2_cpp::Writer writer;
  rosbag2_storage::StorageOptions storage;
  storage.uri = (directory.path() / "bag").string();
  storage.storage_id = "sqlite3";
  writer.open(storage);

  rosbag2_storage::TopicMetadata topic;
  topic.name = "/test/unknown";
  topic.type = "no_such_msgs/msg/Unknown";
  topic.serialization_format = "cdr";
  writer.create_topic(topic);

  const std::vector<std::uint8_t> payload{1, 2, 3, 4};
  rosbag2_storage::SerializedBagMessage message;
  message.serialized_data = std::make_shared<rcutils_uint8_array_t>();
  message.serialized_data->buffer_length = payload.size();
  message.serialized_data->buffer = const_cast<std::uint8_t *>(payload.data());
  message.topic_name = topic.name;
  message.recv_timestamp = 1000;
  message.send_timestamp = 1000;
  writer.write(std::make_shared<rosbag2_storage::SerializedBagMessage>(message));
  writer.close();

  const nlohmann::json report =
    chaos::data::auditBag((directory.path() / "bag").string());
  const nlohmann::json & stats = report.at("topics").at("/test/unknown");
  EXPECT_EQ(stats.at("decode_errors").get<std::int64_t>(), 1);
  EXPECT_EQ(stats.at("valid_source_stamps").get<std::int64_t>(), 0);
  // An unknown type must not invent a source timestamp.
  EXPECT_EQ(stats.at("first_source_ns").is_null(), true);
}

TEST(Audit, RejectsAnIndexInsideTheBag)
{
  TemporaryDirectory directory;
  const auto bag = writeBag(directory.path());
  chaos::data::AuditOptions options;
  options.index_path = (bag / "index.jsonl").string();
  EXPECT_THROW(chaos::data::auditBag(bag.string(), options), std::exception);
}

TEST(Audit, RejectsInvalidBounds)
{
  TemporaryDirectory directory;
  chaos::data::AuditOptions options;
  options.max_topics = 0;
  EXPECT_THROW(chaos::data::auditBag((directory.path() / "bag").string(), options),
               std::exception);
  chaos::data::AuditOptions other;
  other.max_difference_s = 0.0;
  EXPECT_THROW(chaos::data::auditBag((directory.path() / "bag").string(), other),
               std::exception);
}
