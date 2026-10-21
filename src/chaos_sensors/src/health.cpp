// Port of chaos_sensors/health.py.
#include "chaos_sensors/health.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

#include "chaos_core/clock.hpp"
#include "chaos_core/format.hpp"
#include "chaos_core/time.hpp"

namespace chaos::sensors {

namespace {
std::string fixed6(double value) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.6f", value);
  return buffer;
}
}  // namespace

HealthTracker::HealthTracker(std::string frame_id, double timeout_s,
                             std::function<std::int64_t()> ros_now_ns,
                             std::function<double()> monotonic)
    : frame_id_(std::move(frame_id)),
      timeout_s_(timeout_s),
      ros_now_ns_(std::move(ros_now_ns)),
      monotonic_(monotonic ? std::move(monotonic) : chaos::monotonicSeconds) {
  if (frame_id_.empty()) {
    throw std::invalid_argument("frame_id must not be empty");
  }
  if (!std::isfinite(timeout_s_) || timeout_s_ < 0.001 || timeout_s_ > 300.0) {
    throw std::invalid_argument(
      "timeout_s must be finite and between 0.001 and 300 seconds");
  }
}

void HealthTracker::observe(std::int64_t sec, std::int64_t nanosec,
                            const std::string & frame_id) {
  receipt_ = monotonic_();
  has_receipt_ = true;
  received_frame_ = frame_id;
  ++received_count_;
  if (frame_id != frame_id_) {
    ++frame_mismatch_count_;
  }
  source_issue_.clear();
  std::int64_t stamp_ns = 0;
  if (!chaos::rosStampToNs(sec, nanosec, stamp_ns)) {
    has_stamp_ = false;
    source_issue_ = "invalid source timestamp";
    ++invalid_stamp_count_;
    return;
  }
  stamp_ns_ = stamp_ns;
  has_stamp_ = true;
  if (stamp_ns_ == 0) {
    source_issue_ = "zero source timestamp";
    ++invalid_stamp_count_;
    return;
  }
  if (has_previous_stamp_ && stamp_ns_ <= previous_stamp_ns_) {
    source_issue_ = "source timestamp did not advance";
    ++nonforward_stamp_count_;
  }
  // A restarted sensor/ROS clock can recover on its next advancing sample.
  previous_stamp_ns_ = stamp_ns_;
  has_previous_stamp_ = true;
}

Health HealthTracker::snapshot() const {
  Health health;
  health.values = {
    {"expected_frame", frame_id_},
    {"received_frame", received_frame_},
    {"timeout_s", pythonFloatStr(timeout_s_)},
    {"received_count", std::to_string(received_count_)},
    {"invalid_stamp_count", std::to_string(invalid_stamp_count_)},
    {"nonforward_stamp_count", std::to_string(nonforward_stamp_count_)},
    {"frame_mismatch_count", std::to_string(frame_mismatch_count_)},
  };
  if (!has_receipt_) {
    health.values["receipt"] = "no data";
    health.values["source"] = "no data";
    health.values["frame"] = "no data";
    health.level = 3;
    health.message = "no messages received";
    return health;
  }
  std::string issues;
  int level = 0;
  auto append_issue = [&issues](const std::string & issue) {
    if (!issues.empty()) {
      issues += "; ";
    }
    issues += issue;
  };
  const double age = std::max(0.0, monotonic_() - receipt_);
  health.values["receipt_age_s"] = fixed6(age);
  health.values["receipt"] = "ok";
  if (age > timeout_s_) {
    health.values["receipt"] = "timeout";
    append_issue("receipt timeout");
    level = 3;
  }
  health.values["frame"] = "ok";
  if (received_frame_ != frame_id_) {
    health.values["frame"] = "mismatch";
    append_issue("frame mismatch");
    level = std::max(level, 2);
  }
  std::string source_issues;
  auto append_source = [&source_issues](const std::string & issue) {
    if (!source_issues.empty()) {
      source_issues += "; ";
    }
    source_issues += issue;
  };
  if (!source_issue_.empty()) {
    append_source(source_issue_);
  }
  if (has_stamp_ && stamp_ns_ > 0) {
    const double source_age_s =
      static_cast<double>(ros_now_ns_() - stamp_ns_) /
      static_cast<double>(1'000'000'000LL);
    health.values["source_age_s"] = fixed6(source_age_s);
    if (source_age_s > timeout_s_) {
      append_source("source timestamp expired");
    } else if (source_age_s < -0.1) {
      append_source("source timestamp is in the future");
    }
  }
  health.values["source"] = source_issues.empty() ? "ok" : source_issues;
  if (!source_issues.empty()) {
    append_issue(source_issues);
    level = std::max(level, 2);
  }
  health.level = level;
  health.message = issues.empty() ? "receiving fresh data" : issues;
  return health;
}

}  // namespace chaos::sensors
