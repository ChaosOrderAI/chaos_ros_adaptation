// Bounded, read only source timestamp audits for ROS messages and rosbag2.
//
// The difference between the bag reception time and a message timestamp
// includes clock domain differences, driver buffering, transport and
// scheduling. It is not a measurement of the device clock offset and not a
// proof of physical synchronisation. Original messages and timestamps are
// never changed, paired or interpolated.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace chaos::data {

// Streaming audit with bounded report memory.
class AuditTracker {
public:
  AuditTracker(int max_topics = 256, int max_frames_per_topic = 8,
               double max_difference_s = 0.1);

  // One observed message with a decoded source stamp.
  struct Observation {
    bool decoded = false;
    bool has_source = false;
    std::int64_t source_ns = 0;
    // "valid", "missing", "invalid", "zero" or "multi_stamp_excluded".
    std::string stamp_kind = "missing";
    bool has_frame = false;
    std::string frame;
  };

  void observe(const std::string & topic, const std::string & type_name,
               std::int64_t receipt_ns, const Observation & observation,
               int epoch = 0);
  void observeDecodeError(const std::string & topic,
                          const std::string & type_name, std::int64_t receipt_ns,
                          int epoch = 0);
  // A topic that is beyond the topic limit is counted, but it does not get
  // its own statistics and no source timestamp is claimed for it.
  void noteTopicLimitExceeded();

  nlohmann::json snapshot() const;

  std::int64_t messages() const { return messages_; }
  std::int64_t topicLimitDroppedMessages() const { return topic_limit_dropped_; }
  std::int64_t maxDifferenceNs() const { return max_difference_ns_; }

private:
  struct TopicStats {
    std::string type_name;
    std::int64_t messages = 0;
    std::int64_t valid_source_stamps = 0;
    std::int64_t missing_source_stamps = 0;
    std::int64_t invalid_source_stamps = 0;
    std::int64_t zero_source_stamps = 0;
    std::int64_t excluded_multi_stamp_messages = 0;
    std::int64_t decode_errors = 0;
    std::int64_t invalid_receipt_stamps = 0;
    std::int64_t source_reversals = 0;
    std::int64_t source_duplicates = 0;
    std::int64_t receipt_reversals = 0;
    std::int64_t receipt_duplicates = 0;
    std::int64_t epoch_changes = 0;
    std::int64_t type_changes = 0;
    std::int64_t first_source_ns = -1;
    std::int64_t last_source_ns = -1;
    std::int64_t first_receipt_ns = -1;
    std::int64_t last_receipt_ns = -1;
    std::int64_t difference_samples = 0;
    std::int64_t difference_min_ns = 0;
    std::int64_t difference_max_ns = 0;
    std::int64_t difference_abs_exceeded = 0;
    std::int64_t source_ahead_of_receipt = 0;
    std::map<std::string, std::int64_t> frames;
    std::int64_t frames_not_retained = 0;
    // A wide accumulator: the difference of two clocks over many epochs can
    // exceed the int64 range, and an overflow would be undefined behaviour.
    __int128 difference_sum = 0;
    std::int64_t last_source_in_epoch = -1;
    std::int64_t last_receipt_in_epoch = -1;
    int epoch = -1;
  };

  TopicStats * find(const std::string & topic, const std::string & type_name,
                    int epoch, bool & dropped);
  static bool receipt(TopicStats & stats, std::int64_t receipt_ns);

  int max_topics_;
  int max_frames_per_topic_;
  std::int64_t max_difference_ns_;
  std::int64_t messages_ = 0;
  std::int64_t topic_limit_dropped_ = 0;
  std::map<std::string, TopicStats> topics_;
};

struct AuditOptions {
  std::string storage_id;
  int max_topics = 256;
  int max_frames_per_topic = 8;
  double max_difference_s = 0.1;
  long long max_messages = -1;
  std::string index_path;
};

// Reads an existing MCAP/SQLite bag without publishing or modifying it.
// The report keeps the original field names and semantics.
nlohmann::json auditBag(const std::string & bag_path,
                        const AuditOptions & options = AuditOptions());

}  // namespace chaos::data
