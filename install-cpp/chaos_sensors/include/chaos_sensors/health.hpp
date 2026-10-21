// Metadata-only freshness checks, with injectable clocks and no ROS
// dependency. Port of chaos_sensors/health.py.
//
// These checks cannot detect a driver that assigns a new timestamp to cached
// hardware data. Drivers must preserve the measurement timestamp for that
// guarantee.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace chaos::sensors {

struct Health {
  int level = 0;
  std::string message;
  std::map<std::string, std::string> values;
};

// Track receipt and source freshness independently for a single stream.
// observe() is the hot path: fixed-size state, no allocation on valid input.
class HealthTracker {
public:
  HealthTracker(std::string frame_id, double timeout_s,
                std::function<std::int64_t()> ros_now_ns,
                std::function<double()> monotonic = nullptr);

  void observe(std::int64_t sec, std::int64_t nanosec,
               const std::string & frame_id);
  Health snapshot() const;

  const std::string & frameId() const { return frame_id_; }
  double timeoutSeconds() const { return timeout_s_; }
  std::int64_t receivedCount() const { return received_count_; }
  std::int64_t invalidStampCount() const { return invalid_stamp_count_; }
  std::int64_t nonforwardStampCount() const { return nonforward_stamp_count_; }
  std::int64_t frameMismatchCount() const { return frame_mismatch_count_; }

private:
  std::string frame_id_;
  double timeout_s_;
  std::function<std::int64_t()> ros_now_ns_;
  std::function<double()> monotonic_;
  bool has_receipt_ = false;
  double receipt_ = 0.0;
  bool has_stamp_ = false;
  std::int64_t stamp_ns_ = 0;
  bool has_previous_stamp_ = false;
  std::int64_t previous_stamp_ns_ = 0;
  std::string source_issue_;
  std::string received_frame_;
  std::int64_t received_count_ = 0;
  std::int64_t invalid_stamp_count_ = 0;
  std::int64_t nonforward_stamp_count_ = 0;
  std::int64_t frame_mismatch_count_ = 0;
};

}  // namespace chaos::sensors
