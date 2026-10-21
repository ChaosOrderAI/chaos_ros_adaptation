// The typed bag reader and the control label stream.
//
// The reader decodes the messages of the selected topics with the native
// serializer of their type, and the control pass turns the ControlTrace
// topic into the journal of the action labels.
//
// The two passes are separate because they need different things: the reader
// walks the bag once per pass, and the control journal is written first and
// read back later with a cursor, so the memory of the export does not grow
// with the number of control events of the episode.
#include "export_records.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_storage/storage_options.hpp>

#include <chaos_interfaces/msg/control_trace.hpp>
#include <chaos_interfaces/msg/robot_state.hpp>

#include "chaos_data/file_io.hpp"
#include "chaos_data/ros_message.hpp"

namespace chaos::data {

constexpr std::int64_t kNanoseconds = 1000000000LL;
constexpr const char * kTraceType = "chaos_interfaces/msg/ControlTrace";

bool DecodedMessage::decode(const std::string & type, const std::uint8_t * data,
                            std::size_t size)
{
  // The view is reused for every record of the bag, so the decoding of the
  // previous record has to be dropped. A stale image or a stale robot state
  // would otherwise answer for the current record.
  image_.reset();
  state_.reset();
  trace_.reset();
  dynamic_.reset();
  std::string error;
  if (type == "sensor_msgs/msg/Image") {
    auto message = std::make_shared<sensor_msgs::msg::Image>();
    if (!deserializeTyped(data, size, *message, error)) {
      throw std::runtime_error("cannot decode the image message: " + error);
    }
    image_ = message;
    return true;
  }
  if (type == "chaos_interfaces/msg/RobotState") {
    auto message = std::make_shared<chaos_interfaces::msg::RobotState>();
    if (!deserializeTyped(data, size, *message, error)) {
      throw std::runtime_error("cannot decode the robot state message: " + error);
    }
    state_ = message;
    return true;
  }
  if (type == kTraceType) {
    auto message = std::make_shared<chaos_interfaces::msg::ControlTrace>();
    if (!deserializeTyped(data, size, *message, error)) {
      throw std::runtime_error("cannot decode the control trace message: " + error);
    }
    trace_ = message;
    return true;
  }
  auto payload = std::make_shared<std::vector<std::uint8_t>>(data, data + size);
  auto message = std::make_shared<DynamicMessage>(type, payload);
  if (!message->valid()) {
    throw std::runtime_error(
      "cannot decode the message of type " + type + ": " + message->decodeError());
  }
  dynamic_ = message;
  return true;
}

bool DecodedMessage::read(const std::string & path, FieldValue & out) const
{
  if (image_ != nullptr) {
    if (path == "header.stamp") {
      out.kind = FieldValue::Kind::kTimestamp;
      out.seconds = image_->header.stamp.sec;
      out.nanoseconds = image_->header.stamp.nanosec;
      return true;
    }
    if (path == "header.frame_id") {
      out.kind = FieldValue::Kind::kString;
      out.text = image_->header.frame_id;
      return true;
    }
    if (path == "height") {
      out.kind = FieldValue::Kind::kUint;
      out.uinteger = image_->height;
      return true;
    }
    if (path == "width") {
      out.kind = FieldValue::Kind::kUint;
      out.uinteger = image_->width;
      return true;
    }
    if (path == "encoding") {
      out.kind = FieldValue::Kind::kString;
      out.text = image_->encoding;
      return true;
    }
    if (path == "is_bigendian") {
      out.kind = FieldValue::Kind::kUint;
      out.uinteger = image_->is_bigendian;
      return true;
    }
    if (path == "step") {
      out.kind = FieldValue::Kind::kUint;
      out.uinteger = image_->step;
      return true;
    }
    return false;
  }
  if (state_ != nullptr) {
    if (path == "pose.header.stamp") {
      out.kind = FieldValue::Kind::kTimestamp;
      out.seconds = state_->pose.header.stamp.sec;
      out.nanoseconds = state_->pose.header.stamp.nanosec;
      return true;
    }
    if (path == "pose.header.frame_id") {
      out.kind = FieldValue::Kind::kString;
      out.text = state_->pose.header.frame_id;
      return true;
    }
    if (path == "pose_valid") {
      out.kind = FieldValue::Kind::kBool;
      out.boolean = state_->pose_valid;
      return true;
    }
    if (path == "connected") {
      out.kind = FieldValue::Kind::kBool;
      out.boolean = state_->connected;
      return true;
    }
    if (path == "pose.pose.position.x") {
      out.kind = FieldValue::Kind::kDouble;
      out.number = state_->pose.pose.position.x;
      return true;
    }
    if (path == "pose.pose.position.y") {
      out.kind = FieldValue::Kind::kDouble;
      out.number = state_->pose.pose.position.y;
      return true;
    }
    if (path == "pose.pose.position.z") {
      out.kind = FieldValue::Kind::kDouble;
      out.number = state_->pose.pose.position.z;
      return true;
    }
    if (path == "pose.pose.orientation.x") {
      out.kind = FieldValue::Kind::kDouble;
      out.number = state_->pose.pose.orientation.x;
      return true;
    }
    if (path == "pose.pose.orientation.y") {
      out.kind = FieldValue::Kind::kDouble;
      out.number = state_->pose.pose.orientation.y;
      return true;
    }
    if (path == "pose.pose.orientation.z") {
      out.kind = FieldValue::Kind::kDouble;
      out.number = state_->pose.pose.orientation.z;
      return true;
    }
    if (path == "pose.pose.orientation.w") {
      out.kind = FieldValue::Kind::kDouble;
      out.number = state_->pose.pose.orientation.w;
      return true;
    }
    return false;
  }
  if (dynamic_ == nullptr) {
    return false;
  }
  DynamicValue value;
  if (!dynamic_->read(path, value)) {
    return false;
  }
  switch (value.kind) {
    case DynamicValue::Kind::kBool:
      out.kind = FieldValue::Kind::kBool;
      out.boolean = value.boolean;
      return true;
    case DynamicValue::Kind::kInt64:
      out.kind = FieldValue::Kind::kInt;
      out.integer = value.integer;
      return true;
    case DynamicValue::Kind::kUint64:
      out.kind = FieldValue::Kind::kUint;
      out.uinteger = value.uinteger;
      return true;
    case DynamicValue::Kind::kDouble:
      out.kind = FieldValue::Kind::kDouble;
      out.number = value.number;
      return true;
    case DynamicValue::Kind::kString:
      out.kind = FieldValue::Kind::kString;
      out.text = value.text;
      return true;
    case DynamicValue::Kind::kTimestamp:
      out.kind = FieldValue::Kind::kTimestamp;
      out.seconds = value.seconds;
      out.nanoseconds = value.nanoseconds;
      return true;
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// The control label stream
// ---------------------------------------------------------------------------

namespace {

// One control event as the line of the journal.
//
// The line is the whole state of an event, so the cursor of the second pass
// can rebuild the event without reading the bag again.
nlohmann::ordered_json eventToJson(const ControlEvent & event)
{
  nlohmann::ordered_json value;
  value["event_index"] = event.event_index;
  value["sequence"] = event.sequence;
  value["monotonic_ns"] = event.monotonic_ns;
  value["source_ns"] = event.source_ns;
  value["receive_ns"] = event.receive_ns;
  // The difference is computed with a wide integer, so a pair of extreme
  // times cannot overflow.
  const __int128 difference =
    static_cast<__int128>(event.receive_ns) - static_cast<__int128>(event.source_ns);
  if (difference > static_cast<__int128>(INT64_MAX) ||
      difference < static_cast<__int128>(INT64_MIN)) {
    throw std::runtime_error(
      "the difference between the receipt and the source time is out of range");
  }
  value["receive_minus_source_ns"] = static_cast<std::int64_t>(difference);
  value["stage"] = event.stage;
  value["command"] = event.command;
  value["request_id"] = event.request_id;
  value["code"] = event.code;
  value["velocity_valid"] = event.velocity_valid;
  value["valid_until_ns"] = event.valid_until_ns;
  if (event.has_values) {
    value["values"] = event.values;
  } else {
    value["values"] = nullptr;
  }
  return value;
}

}  // namespace

// Reads the ControlTrace topic of one episode and writes the journal.
//
// Only an issued velocity command becomes an action label. A rejected
// command is an audit event, and an invalidated event ends the action at its
// own source time.
//
// The result of the control pass is the number of the written events and the
// source time of the last one. The last event time is kept here because the
// journal is later read with a cursor, which only knows the events it has
// already read.
ControlJournalSummary readControlEvents(
  const SourceEpisode & source, const ActionSpec & action,
  const std::filesystem::path & journal_path)
{
  std::map<std::string, std::string> expected = {
    {action.topic, kTraceType}};
  BagReader reader(source.bag, source.storage_id, expected);
  ControlJournalSummary summary;
  std::uint64_t previous_sequence = 0;
  std::int64_t previous_source = -1;
  std::uint64_t previous_monotonic = 0;
  bool has_previous_sequence = false;
  bool has_previous_source = false;
  bool has_previous_monotonic = false;

  ExclusiveFile journal(journal_path);
  BagRecord record;
  while (reader.next(record)) {
    if (record.topic != action.topic) {
      continue;
    }
    const chaos_interfaces::msg::ControlTrace & trace = *record.message.trace();
    const std::int64_t stamp =
      static_cast<std::int64_t>(trace.header.stamp.sec) * kNanoseconds +
      static_cast<std::int64_t>(trace.header.stamp.nanosec);
    if (trace.header.stamp.sec < 0 || trace.header.stamp.nanosec >= 1000000000U ||
        stamp == 0) {
      throw std::runtime_error(
        "a ControlTrace requires a valid nonzero header timestamp");
    }
    if (has_previous_source && stamp < previous_source) {
      throw std::runtime_error(
        "the ControlTrace source timestamps are reversed; the labels cannot be "
        "reordered");
    }
    previous_source = stamp;
    has_previous_source = true;
    if (stamp < source.episode->start_utc_ns ||
        stamp > source.episode->end_utc_ns) {
      continue;
    }
    if (trace.sequence == 0) {
      throw std::runtime_error(
        "the ControlTrace sequence must be an integer in [1, 18446744073709551615]");
    }
    if (trace.monotonic_ns == 0) {
      throw std::runtime_error(
        "the ControlTrace monotonic_ns must be an integer in [1, "
        "18446744073709551615]");
    }
    if (has_previous_sequence) {
      if (previous_sequence == UINT64_MAX) {
        throw std::runtime_error(
          "the ControlTrace sequence overflows in the episode");
      }
      if (trace.sequence != previous_sequence + 1) {
        throw std::runtime_error(
          "the ControlTrace sequence has a gap, a duplicate or a reversal in "
          "the episode");
      }
    }
    previous_sequence = trace.sequence;
    has_previous_sequence = true;
    if (has_previous_monotonic && trace.monotonic_ns < previous_monotonic) {
      throw std::runtime_error(
        "the ControlTrace monotonic time is reversed in the episode");
    }
    previous_monotonic = trace.monotonic_ns;
    has_previous_monotonic = true;
    if (trace.asset_id != source.manifest["asset"].value("id", std::string())) {
      throw std::runtime_error(
        "the ControlTrace asset_id does not match the recording asset");
    }
    if (trace.header.frame_id != action.frame) {
      throw std::runtime_error(
        "the ControlTrace frame does not match action.frame");
    }
    if (trace.stage != "issued" && trace.stage != "rejected" &&
        trace.stage != "invalidated") {
      throw std::runtime_error("unknown ControlTrace stage: " + trace.stage);
    }
    if (trace.command != "motion.velocity" && trace.command != "motion.stop" &&
        trace.command != "navigation.pose" && trace.command != "flight.takeoff" &&
        trace.command != "flight.land") {
      throw std::runtime_error("unknown ControlTrace command: " + trace.command);
    }
    const double ttl = trace.valid_for_s;
    if (!std::isfinite(ttl) || ttl < 0.0 || ttl > 86400.0) {
      throw std::runtime_error(
        "the ControlTrace valid_for_s must be finite and within [0, 86400]");
    }
    const bool active = trace.stage == "issued" &&
      trace.command == "motion.velocity" && trace.velocity_valid;
    if (!active && (trace.velocity_valid || ttl != 0.0)) {
      throw std::runtime_error(
        "a non velocity ControlTrace event must not carry a valid velocity or a "
        "time to live");
    }
    ControlEvent event;
    event.event_index = summary.count;
    event.sequence = trace.sequence;
    event.monotonic_ns = trace.monotonic_ns;
    event.source_ns = stamp;
    event.receive_ns = record.receive_ns;
    event.stage = trace.stage;
    event.command = trace.command;
    event.request_id = trace.request_id;
    event.code = trace.code;
    event.velocity_valid = trace.velocity_valid;
    event.valid_until_ns = stamp +
      static_cast<std::int64_t>(std::floor(ttl * static_cast<double>(kNanoseconds)));
    if (active) {
      for (const auto & field : action.fields) {
        double value = 0.0;
        if (field == "velocity.linear.x") {
          value = trace.velocity.linear.x;
        } else if (field == "velocity.linear.y") {
          value = trace.velocity.linear.y;
        } else if (field == "velocity.linear.z") {
          value = trace.velocity.linear.z;
        } else if (field == "velocity.angular.x") {
          value = trace.velocity.angular.x;
        } else if (field == "velocity.angular.y") {
          value = trace.velocity.angular.y;
        } else if (field == "velocity.angular.z") {
          value = trace.velocity.angular.z;
        } else {
          throw std::runtime_error(
            "the action fields must select ControlTrace velocity components");
        }
        if (!std::isfinite(value)) {
          throw std::runtime_error(
            "the ControlTrace velocity is not a finite numeric scalar");
        }
        event.values.push_back(value);
      }
      event.has_values = true;
    }
    const std::string text = eventToJson(event).dump() + "\n";
    journal.write(text);
    summary.last_event_ns = event.source_ns;
    ++summary.count;
  }
  journal.close();
  if (summary.count == 0) {
    throw std::runtime_error(
      "the episode contains no ControlTrace events; request and result logs "
      "are not action labels");
  }
  return summary;
}

}  // namespace chaos::data
