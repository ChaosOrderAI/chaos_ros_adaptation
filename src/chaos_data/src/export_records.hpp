// The typed bag reader and the control label stream.
//
// The reader decodes the messages of the selected topics with the native
// serializer of their type, and the control stream turns the ControlTrace
// topic into the journal of the action labels.
//
// This is an internal module of the export library: the header is not
// installed, so the public interface of the package stays small.
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_storage/storage_options.hpp>

#include <chaos_interfaces/msg/control_trace.hpp>
#include <chaos_interfaces/msg/robot_state.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "chaos_data/export.hpp"
#include "chaos_data/ros_message.hpp"
#include "export_source.hpp"

namespace chaos::data {

struct FieldValue {
  enum class Kind { kNone, kBool, kInt, kUint, kDouble, kString, kTimestamp };
  Kind kind = Kind::kNone;
  bool boolean = false;
  std::int64_t integer = 0;
  std::uint64_t uinteger = 0;
  double number = 0.0;
  std::string text;
  std::int64_t seconds = 0;
  std::uint32_t nanoseconds = 0;
};

class DecodedMessage {
public:
  bool decode(const std::string & type, const std::uint8_t * data,
              std::size_t size);
  bool read(const std::string & path, FieldValue & out) const;
  const sensor_msgs::msg::Image * image() const { return image_.get(); }
  std::shared_ptr<const sensor_msgs::msg::Image> imagePtr() const { return image_; }
  const chaos_interfaces::msg::ControlTrace * trace() const { return trace_.get(); }

private:
  std::shared_ptr<sensor_msgs::msg::Image> image_;
  std::shared_ptr<chaos_interfaces::msg::RobotState> state_;
  std::shared_ptr<chaos_interfaces::msg::ControlTrace> trace_;
  std::shared_ptr<DynamicMessage> dynamic_;
};

struct BagRecord {
  std::string topic;
  std::string type;
  std::int64_t receive_ns = 0;
  std::int64_t sequence = 0;
  DecodedMessage message;
};

class BagReader {
public:
  BagReader(const std::filesystem::path & bag, const std::string & storage_id,
            const std::map<std::string, std::string> & expected)
  : expected_(expected)
  {
    rosbag2_storage::StorageOptions storage;
    storage.uri = bag.string();
    storage.storage_id = storage_id;
    reader_.open(storage);
    if (!reader_.set_read_order(rosbag2_storage::ReadOrder(
          rosbag2_storage::ReadOrder::ReceivedTimestamp, false))) {
      throw std::runtime_error(
        "the bag storage must support an explicit receive timestamp order");
    }
    // Only the selected topics are read from the storage, so a bag with many
    // other topics does not pay for their payloads.
    rosbag2_storage::StorageFilter filter;
    for (const auto & entry : expected_) {
      filter.topics.push_back(entry.first);
    }
    reader_.set_filter(filter);
    std::set<std::string> found;
    for (const auto & metadata : reader_.get_all_topics_and_types()) {
      const auto expected_type = expected_.find(metadata.name);
      if (expected_type == expected_.end()) {
        continue;
      }
      if (!found.insert(metadata.name).second ||
          metadata.type != expected_type->second ||
          metadata.serialization_format != "cdr") {
        throw std::runtime_error(
          "the topic type or serialization does not match: " + metadata.name);
      }
    }
    for (const auto & entry : expected_) {
      if (found.find(entry.first) == found.end()) {
        throw std::runtime_error(
          "the bag is missing the required topic: " + entry.first);
      }
    }
  }

  // The next record of one of the selected topics, in receive time order.
  bool next(BagRecord & out)
  {
    while (reader_.has_next()) {
      auto raw = reader_.read_next();
      if (!raw) {
        return false;
      }
      if (expected_.find(raw->topic_name) == expected_.end()) {
        continue;
      }
      if (raw->serialized_data == nullptr ||
          raw->serialized_data->buffer_length > kMaxMessageBytes) {
        throw std::runtime_error(
          "the selected ROS message exceeds the 64 MiB export limit");
      }
      out.topic = raw->topic_name;
      out.type = expected_.at(out.topic);
      out.receive_ns = static_cast<std::int64_t>(raw->recv_timestamp);
      out.sequence = ++sequence_;
      if (previous_receive_ns_ >= 0 && out.receive_ns < previous_receive_ns_) {
        throw std::runtime_error(
          "the bag receive timestamps are reversed despite the explicit read "
          "order");
      }
      previous_receive_ns_ = out.receive_ns;
      out.message.decode(out.type, raw->serialized_data->buffer,
                         raw->serialized_data->buffer_length);
      return true;
    }
    return false;
  }

private:
  rosbag2_cpp::Reader reader_;
  std::map<std::string, std::string> expected_;
  std::int64_t sequence_ = 0;
  std::int64_t previous_receive_ns_ = -1;
};

struct ControlEvent {
  std::int64_t event_index = 0;
  // The sequence and the monotonic time of the trace are unsigned 64 bit
  // values, so they are kept unsigned and never wrap into a negative number.
  std::uint64_t sequence = 0;
  std::uint64_t monotonic_ns = 0;
  std::int64_t source_ns = 0;
  std::int64_t receive_ns = 0;
  std::string stage;
  std::string command;
  std::string request_id;
  std::string code;
  bool velocity_valid = false;
  std::int64_t valid_until_ns = 0;
  bool has_values = false;
  std::vector<double> values;
};

struct ControlJournalSummary {
  std::int64_t count = 0;
  std::int64_t last_event_ns = 0;
};

class ControlJournalCursor {
public:
  explicit ControlJournalCursor(const std::filesystem::path & path)
  : stream_(path)
  {
    if (!stream_) {
      throw std::runtime_error("cannot read the control journal");
    }
  }

  // Applies the events whose source time is at or before the grid.
  //
  // An invalidated event ends the action at its own source time, an issued
  // event becomes the action, and a rejected event is only an audit event:
  // it never becomes a command and never ends the action.
  void apply(std::int64_t grid, const ControlEvent * & active)
  {
    while (true) {
      if (!pending_.has_value()) {
        pending_ = readNext();
        if (!pending_.has_value()) {
          return;
        }
      }
      if (pending_->source_ns > grid) {
        return;
      }
      const ControlEvent & event = *pending_;
      if (event.stage == "invalidated") {
        active_event_.reset();
        active = nullptr;
      } else if (event.stage == "issued") {
        // The active event is kept by the cursor, so the pointer of the
        // caller stays valid until the next event replaces it.
        active_event_ = event;
        active = event.has_values ? &*active_event_ : nullptr;
      }
      pending_.reset();
    }
  }

private:
  std::optional<ControlEvent> readNext()
  {
    std::string line;
    while (std::getline(stream_, line)) {
      if (line.empty()) {
        continue;
      }
      nlohmann::ordered_json value;
      try {
        value = nlohmann::ordered_json::parse(line);
      } catch (const std::exception & error) {
        throw std::runtime_error(
          std::string("cannot parse the control journal: ") + error.what());
      }
      ControlEvent event;
      event.event_index = value.at("event_index").get<std::int64_t>();
      event.sequence = value.at("sequence").get<std::uint64_t>();
      event.monotonic_ns = value.at("monotonic_ns").get<std::uint64_t>();
      event.source_ns = value.at("source_ns").get<std::int64_t>();
      event.receive_ns = value.at("receive_ns").get<std::int64_t>();
      event.stage = value.at("stage").get<std::string>();
      event.command = value.at("command").get<std::string>();
      event.request_id = value.at("request_id").get<std::string>();
      event.code = value.at("code").get<std::string>();
      event.velocity_valid = value.at("velocity_valid").get<bool>();
      event.valid_until_ns = value.at("valid_until_ns").get<std::int64_t>();
      const nlohmann::ordered_json & values = value.at("values");
      if (!values.is_null()) {
        event.has_values = true;
        for (const auto & item : values) {
          event.values.push_back(item.get<double>());
        }
      }
      return event;
    }
    // The end of the journal is the only reason to stop. A read error has to
    // be reported, because a journal that was read only half way would end
    // the action silently and label the rest of the episode with a command
    // that was never stopped.
    if (stream_.bad() || (stream_.fail() && !stream_.eof())) {
      throw std::runtime_error("cannot read the control journal: an I/O error occurred");
    }
    return std::nullopt;
  }

  std::ifstream stream_;
  std::optional<ControlEvent> pending_;
  std::optional<ControlEvent> active_event_;
};

// Reads the ControlTrace topic of one episode and writes the journal.
//
// Only an issued velocity command becomes an action label. A rejected
// command is an audit event, and an invalidated event ends the action at its
// own source time.
ControlJournalSummary readControlEvents(
  const SourceEpisode & source, const ActionSpec & action,
  const std::filesystem::path & journal_path);

}  // namespace chaos::data
