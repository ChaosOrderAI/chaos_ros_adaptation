// Native rosbag2 source timestamp audit.
#include "chaos_data/audit.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_storage/storage_options.hpp>

#include <chaos_interfaces/msg/control_trace.hpp>
#include <chaos_interfaces/msg/robot_state.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>

#include "chaos_data/ros_message.hpp"
#include "chaos_data/file_io.hpp"

namespace chaos::data {
namespace {

constexpr std::int64_t kNanoseconds = 1000000000LL;

// A source stamp that is valid for the ROS range.
bool stampFromSeconds(std::int64_t seconds, std::uint32_t nanoseconds,
                      AuditTracker::Observation & observation)
{
  if (seconds < 0 || seconds > std::numeric_limits<std::int32_t>::max() ||
      nanoseconds >= static_cast<std::uint32_t>(kNanoseconds)) {
    observation.stamp_kind = "invalid";
    return false;
  }
  const std::int64_t value = seconds * kNanoseconds + nanoseconds;
  if (value == 0) {
    observation.stamp_kind = "zero";
    return false;
  }
  observation.has_source = true;
  observation.source_ns = value;
  observation.stamp_kind = "valid";
  return true;
}

// The known message types are decoded with the native ROS serializer: the
// payload keeps its own byte vector and no element is expanded. Only the
// header fields that the audit needs are read.
bool typedSourceStamp(const std::string & type_name, const std::uint8_t * data,
                      std::size_t size, AuditTracker::Observation & observation)
{
  std::string error;
  if (type_name == "sensor_msgs/msg/Image") {
    sensor_msgs::msg::Image message;
    if (!deserializeTyped(data, size, message, error)) {
      return false;
    }
    observation.has_frame = true;
    observation.frame = message.header.frame_id;
    return stampFromSeconds(message.header.stamp.sec,
                            message.header.stamp.nanosec, observation);
  }
  if (type_name == "sensor_msgs/msg/PointCloud2") {
    sensor_msgs::msg::PointCloud2 message;
    if (!deserializeTyped(data, size, message, error)) {
      return false;
    }
    observation.has_frame = true;
    observation.frame = message.header.frame_id;
    return stampFromSeconds(message.header.stamp.sec,
                            message.header.stamp.nanosec, observation);
  }
  if (type_name == "chaos_interfaces/msg/ControlTrace") {
    chaos_interfaces::msg::ControlTrace message;
    if (!deserializeTyped(data, size, message, error)) {
      return false;
    }
    observation.has_frame = true;
    observation.frame = message.header.frame_id;
    return stampFromSeconds(message.header.stamp.sec,
                            message.header.stamp.nanosec, observation);
  }
  if (type_name == "chaos_interfaces/msg/RobotState") {
    chaos_interfaces::msg::RobotState message;
    if (!deserializeTyped(data, size, message, error)) {
      return false;
    }
    // RobotState carries a top level stamp and no frame.
    return stampFromSeconds(message.stamp.sec, message.stamp.nanosec,
                            observation);
  }
  return false;
}

// The source stamp of one message, with the original categories.
AuditTracker::Observation sourceStamp(const DynamicMessage & message,
                                       const std::string & type_name)
{
  AuditTracker::Observation observation;
  if (type_name == "tf2_msgs/msg/TFMessage") {
    // A TF message carries several stamps; none of them may be picked.
    observation.stamp_kind = "multi_stamp_excluded";
    return observation;
  }
  if (message.hasHeader()) {
    std::string frame;
    if (message.headerFrame(frame)) {
      observation.has_frame = true;
      observation.frame = frame;
    }
    std::int64_t seconds = 0;
    std::uint32_t nanoseconds = 0;
    if (!message.headerStamp(seconds, nanoseconds)) {
      observation.stamp_kind = "invalid";
      return observation;
    }
    if (seconds < 0 || seconds > std::numeric_limits<std::int32_t>::max() ||
        nanoseconds >= static_cast<std::uint32_t>(kNanoseconds)) {
      observation.stamp_kind = "invalid";
      return observation;
    }
    const std::int64_t value = seconds * kNanoseconds + nanoseconds;
    if (value == 0) {
      observation.stamp_kind = "zero";
      return observation;
    }
    observation.has_source = true;
    observation.source_ns = value;
    observation.stamp_kind = "valid";
    return observation;
  }
  // A message without a header may still carry a top level stamp.
  DynamicValue value;
  if (message.read("stamp", value)) {
    if (value.kind != DynamicValue::Kind::kTimestamp) {
      observation.stamp_kind = "invalid";
      return observation;
    }
    if (value.seconds < 0 ||
        value.seconds > std::numeric_limits<std::int32_t>::max() ||
        value.nanoseconds >= static_cast<std::uint32_t>(kNanoseconds)) {
      observation.stamp_kind = "invalid";
      return observation;
    }
    const std::int64_t stamp = value.seconds * kNanoseconds + value.nanoseconds;
    if (stamp == 0) {
      observation.stamp_kind = "zero";
      return observation;
    }
    observation.has_source = true;
    observation.source_ns = stamp;
    observation.stamp_kind = "valid";
    return observation;
  }
  observation.stamp_kind = "missing";
  return observation;
}

std::string jsonText(const std::string & value)
{
  return value;
}

}  // namespace

AuditTracker::AuditTracker(int max_topics, int max_frames_per_topic,
                           double max_difference_s)
{
  if (max_topics < 1 || max_topics > 4096) {
    throw std::invalid_argument("max_topics must be an integer in [1, 4096]");
  }
  if (max_frames_per_topic < 1 || max_frames_per_topic > 256) {
    throw std::invalid_argument(
      "max_frames_per_topic must be an integer in [1, 256]");
  }
  if (!std::isfinite(max_difference_s) || max_difference_s <= 0 ||
      max_difference_s > 1000000.0) {
    throw std::invalid_argument("max_difference_s must be finite and positive");
  }
  max_topics_ = max_topics;
  max_frames_per_topic_ = max_frames_per_topic;
  const double nanoseconds = max_difference_s * static_cast<double>(kNanoseconds);
  if (!std::isfinite(nanoseconds) || nanoseconds > 9.0e18) {
    throw std::invalid_argument("max_difference_s is out of range");
  }
  max_difference_ns_ = static_cast<std::int64_t>(std::llround(nanoseconds));
}

void AuditTracker::noteTopicLimitExceeded()
{
  // The message is counted, but the topic gets no statistics: the original
  // behaviour is that a topic beyond the limit is only counted and dropped.
  ++messages_;
  ++topic_limit_dropped_;
}

AuditTracker::TopicStats * AuditTracker::find(const std::string & topic,
                                               const std::string & type_name,
                                               int epoch, bool & dropped)
{
  dropped = false;
  ++messages_;
  auto found = topics_.find(topic);
  if (found == topics_.end()) {
    if (static_cast<int>(topics_.size()) >= max_topics_) {
      ++topic_limit_dropped_;
      dropped = true;
      return nullptr;
    }
    found = topics_.emplace(topic, TopicStats()).first;
    found->second.type_name = type_name;
  }
  TopicStats & stats = found->second;
  ++stats.messages;
  if (stats.type_name != type_name) {
    ++stats.type_changes;
  }
  if (stats.epoch != epoch) {
    if (stats.epoch >= 0) {
      ++stats.epoch_changes;
    }
    stats.epoch = epoch;
    stats.last_source_in_epoch = -1;
    stats.last_receipt_in_epoch = -1;
  }
  return &stats;
}

bool AuditTracker::receipt(TopicStats & stats, std::int64_t receipt_ns)
{
  if (receipt_ns < 0) {
    ++stats.invalid_receipt_stamps;
    return false;
  }
  if (stats.last_receipt_in_epoch >= 0) {
    if (receipt_ns < stats.last_receipt_in_epoch) {
      ++stats.receipt_reversals;
    } else if (receipt_ns == stats.last_receipt_in_epoch) {
      ++stats.receipt_duplicates;
    }
  }
  stats.last_receipt_in_epoch = receipt_ns;
  if (stats.first_receipt_ns < 0) {
    stats.first_receipt_ns = receipt_ns;
  }
  stats.last_receipt_ns = receipt_ns;
  return true;
}

void AuditTracker::observe(const std::string & topic,
                           const std::string & type_name,
                           std::int64_t receipt_ns,
                           const Observation & observation, int epoch)
{
  bool dropped = false;
  TopicStats * stats = find(topic, type_name, epoch, dropped);
  if (stats == nullptr) {
    return;
  }
  const bool receipt_valid = receipt(*stats, receipt_ns);
  if (observation.has_frame) {
    const std::string & frame = observation.frame;
    if (frame.size() <= 256 &&
        (stats->frames.count(frame) > 0 ||
         static_cast<int>(stats->frames.size()) < max_frames_per_topic_)) {
      ++stats->frames[frame];
    } else {
      ++stats->frames_not_retained;
    }
  }
  if (observation.stamp_kind == "missing") {
    ++stats->missing_source_stamps;
    return;
  }
  if (observation.stamp_kind == "invalid") {
    ++stats->invalid_source_stamps;
    return;
  }
  if (observation.stamp_kind == "zero") {
    ++stats->zero_source_stamps;
    return;
  }
  if (observation.stamp_kind == "multi_stamp_excluded") {
    ++stats->excluded_multi_stamp_messages;
    return;
  }
  ++stats->valid_source_stamps;
  const std::int64_t stamp = observation.source_ns;
  if (stats->last_source_in_epoch >= 0) {
    if (stamp < stats->last_source_in_epoch) {
      ++stats->source_reversals;
    } else if (stamp == stats->last_source_in_epoch) {
      ++stats->source_duplicates;
    }
  }
  stats->last_source_in_epoch = stamp;
  if (stats->first_source_ns < 0) {
    stats->first_source_ns = stamp;
  }
  stats->last_source_ns = stamp;
  if (!receipt_valid) {
    return;
  }
  const std::int64_t delta = receipt_ns - stamp;
  ++stats->difference_samples;
  stats->difference_sum += delta;
  if (stats->difference_samples == 1) {
    stats->difference_min_ns = delta;
    stats->difference_max_ns = delta;
  } else {
    stats->difference_min_ns = std::min(stats->difference_min_ns, delta);
    stats->difference_max_ns = std::max(stats->difference_max_ns, delta);
  }
  if (std::llabs(delta) > max_difference_ns_) {
    ++stats->difference_abs_exceeded;
  }
  if (delta < 0) {
    ++stats->source_ahead_of_receipt;
  }
}

void AuditTracker::observeDecodeError(const std::string & topic,
                                      const std::string & type_name,
                                      std::int64_t receipt_ns, int epoch)
{
  bool dropped = false;
  TopicStats * stats = find(topic, type_name, epoch, dropped);
  if (stats == nullptr) {
    return;
  }
  ++stats->decode_errors;
  receipt(*stats, receipt_ns);
}

nlohmann::json AuditTracker::snapshot() const
{
  nlohmann::json topics = nlohmann::json::object();
  for (const auto & entry : topics_) {
    const TopicStats & stats = entry.second;
    nlohmann::json values = {
      {"type_name", stats.type_name},
      {"messages", stats.messages},
      {"valid_source_stamps", stats.valid_source_stamps},
      {"missing_source_stamps", stats.missing_source_stamps},
      {"invalid_source_stamps", stats.invalid_source_stamps},
      {"zero_source_stamps", stats.zero_source_stamps},
      {"excluded_multi_stamp_messages", stats.excluded_multi_stamp_messages},
      {"decode_errors", stats.decode_errors},
      {"invalid_receipt_stamps", stats.invalid_receipt_stamps},
      {"source_reversals", stats.source_reversals},
      {"source_duplicates", stats.source_duplicates},
      {"receipt_reversals", stats.receipt_reversals},
      {"receipt_duplicates", stats.receipt_duplicates},
      {"epoch_changes", stats.epoch_changes},
      {"type_changes", stats.type_changes},
      {"first_source_ns", stats.first_source_ns < 0 ? nlohmann::json()
                                                    : nlohmann::json(stats.first_source_ns)},
      {"last_source_ns", stats.last_source_ns < 0 ? nlohmann::json()
                                                   : nlohmann::json(stats.last_source_ns)},
      {"first_receipt_ns", stats.first_receipt_ns < 0 ? nlohmann::json()
                                                      : nlohmann::json(stats.first_receipt_ns)},
      {"last_receipt_ns", stats.last_receipt_ns < 0 ? nlohmann::json()
                                                     : nlohmann::json(stats.last_receipt_ns)},
      {"difference_samples", stats.difference_samples},
      {"difference_min_ns", stats.difference_samples > 0
                              ? nlohmann::json(stats.difference_min_ns)
                              : nlohmann::json()},
      {"difference_max_ns", stats.difference_samples > 0
                              ? nlohmann::json(stats.difference_max_ns)
                              : nlohmann::json()},
      {"difference_abs_exceeded", stats.difference_abs_exceeded},
      {"source_ahead_of_receipt", stats.source_ahead_of_receipt},
      {"frames", nlohmann::json::object()},
      {"frames_not_retained", stats.frames_not_retained},
    };
    for (const auto & frame : stats.frames) {
      values["frames"][frame.first] = frame.second;
    }
    values["difference_mean_ns"] =
      stats.difference_samples > 0
        ? nlohmann::json(static_cast<double>(stats.difference_sum) /
                         static_cast<double>(stats.difference_samples))
        : nlohmann::json();
    topics[entry.first] = values;
  }
  return {
    {"schema_version", 1},
    {"timestamp_difference", "receipt_minus_source; not device clock offset"},
    {"source_clock_domain", "unverified; source and recorder clocks may differ"},
    {"epoch_policy", "caller supplied; no correction or cross-epoch pairing"},
    {"max_difference_ns", max_difference_ns_},
    {"messages", messages_},
    {"topic_limit_dropped_messages", topic_limit_dropped_},
    {"topics", topics},
  };
}

nlohmann::json auditBag(const std::string & bag_path, const AuditOptions & options)
{
  if (!options.storage_id.empty() && options.storage_id != "mcap" &&
      options.storage_id != "sqlite3") {
    throw std::invalid_argument("storage_id must be mcap, sqlite3, or empty");
  }
  if (options.max_messages == 0 || options.max_messages < -1) {
    throw std::invalid_argument("max_messages must be a positive integer");
  }
  std::error_code error;
  // The symlinks are resolved, so a link into the bag cannot slip through.
  const std::filesystem::path resolved = canonicalPath(bag_path);
  if (!std::filesystem::exists(resolved, error)) {
    throw std::runtime_error("bag not found: " + bag_path);
  }
  const std::filesystem::path index_resolved =
    options.index_path.empty() ? std::filesystem::path()
                               : canonicalPath(options.index_path);
  if (!options.index_path.empty() && pathIsInside(resolved, index_resolved)) {
    // The index is a side product of the audit: writing it into the bag
    // would pollute the source data that is being audited.
    throw std::invalid_argument("index must be outside the source bag");
  }

  AuditTracker tracker(options.max_topics, options.max_frames_per_topic,
                       options.max_difference_s);

  rosbag2_cpp::Reader reader;
  rosbag2_storage::StorageOptions storage;
  storage.uri = resolved.string();
  storage.storage_id = options.storage_id;
  reader.open(storage);

  // The index is a new file: an existing file is never overwritten. The
  // handle owns the file, so a failure in the middle of the bag leaves no
  // descriptor behind.
  std::unique_ptr<ExclusiveFile> index_file;
  if (!options.index_path.empty()) {
    try {
      index_file = std::make_unique<ExclusiveFile>(index_resolved);
    } catch (const std::exception &) {
      throw std::runtime_error("cannot create the index file: " +
                               options.index_path);
    }
  }

  std::map<std::string, std::string> type_by_topic;
  std::map<std::string, std::string> format_by_topic;
  for (const auto & metadata : reader.get_all_topics_and_types()) {
    if (static_cast<int>(type_by_topic.size()) >= options.max_topics) {
      break;
    }
    type_by_topic[metadata.name] = metadata.type;
    format_by_topic[metadata.name] = metadata.serialization_format;
  }

  // A storage plugin may sort by receive time, hiding original arrival
  // reversals. Request file order and report whether it was supported.
  bool file_order = false;
  try {
    const rosbag2_storage::ReadOrder order(
      rosbag2_storage::ReadOrder::File, false);
    file_order = reader.set_read_order(order);
  } catch (const std::exception &) {
    file_order = false;
  }

  std::int64_t count = 0;
  while (reader.has_next()) {
    if (options.max_messages > 0 && count >= options.max_messages) {
      break;
    }
    auto message = reader.read_next();
    if (!message) {
      break;
    }
    ++count;
    const std::string & topic = message->topic_name;
    const std::int64_t receipt_ns = message->recv_timestamp;
    const auto type_entry = type_by_topic.find(topic);
    AuditTracker::Observation observation;
    bool decoded = false;
    bool excluded = false;
    if (type_entry == type_by_topic.end()) {
      // The topic is beyond the topic limit: it is counted and dropped, no
      // statistics are created and no source timestamp is claimed.
      tracker.noteTopicLimitExceeded();
      observation.stamp_kind = "topic_limit_excluded";
      excluded = true;
    } else {
      const std::string & type_name = type_entry->second;
      if (format_by_topic[topic] == "cdr" && message->serialized_data != nullptr) {
        // The known types use the native typed decoder; the other types use
        // the introspection based decoder.
        const bool typed = typedSourceStamp(
          type_name, message->serialized_data->buffer,
          message->serialized_data->buffer_length, observation);
        if (!typed) {
          auto payload = std::make_shared<std::vector<std::uint8_t>>(
            message->serialized_data->buffer,
            message->serialized_data->buffer + message->serialized_data->buffer_length);
          const DynamicMessage decoded_message(type_name, payload);
          if (decoded_message.valid()) {
            observation = sourceStamp(decoded_message, type_name);
            decoded = true;
          }
        } else {
          decoded = true;
        }
      }
      if (!decoded) {
        // Unknown or corrupt payloads stay visible as a decode error; a
        // source timestamp is never invented.
        tracker.observeDecodeError(topic, type_name, receipt_ns);
        observation.stamp_kind = "decode_error";
      } else {
        tracker.observe(topic, type_name, receipt_ns, observation);
      }
    }
    if (index_file != nullptr) {
      nlohmann::json line = {
        {"sequence", count},
        {"topic", topic},
        {"type", excluded ? nlohmann::json()
                          : nlohmann::json(type_by_topic.at(topic))},
        {"receive_ns", receipt_ns},
        {"source_ns", observation.has_source ? nlohmann::json(observation.source_ns)
                                             : nlohmann::json()},
        {"stamp_kind", observation.stamp_kind},
        {"frame", (observation.has_frame && observation.frame.size() <= 256)
                    ? nlohmann::json(observation.frame)
                    : nlohmann::json()},
        {"clock_epoch", nlohmann::json()},
      };
      const std::string text = line.dump() + "\n";
      index_file->write(text);
    }
  }
  if (index_file != nullptr) {
    // The explicit close reports a write error that only appears when the
    // buffer is flushed.
    index_file->close();
  }

  nlohmann::json report = tracker.snapshot();
  report["bag_path"] = resolved.string();
  report["storage_id"] = reader.get_metadata().storage_identifier;
  report["reader_order"] = file_order ? "file" : "storage_default";
  report["complete"] = !reader.has_next();
  report["clock_epoch_evidence"] =
    "unavailable from bag receive timestamps alone; consult session manifest and time_sync.jsonl";
  report["modified_messages"] = 0;
  report["index_path"] =
    options.index_path.empty() ? nlohmann::json() : nlohmann::json(index_resolved.string());
  return report;
}

}  // namespace chaos::data
