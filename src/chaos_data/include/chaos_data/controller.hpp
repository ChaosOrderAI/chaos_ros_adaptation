// Clock-gated collection supervisor; native rosbag handles all sensor payloads.
// No ROS dependency:
// publishing and process lifetime are the collector node's only ROS concerns.
//
// Rotate subscriptions after clock loss so latched messages are reacquired.
// Polling can detect a clock fault only after it happened. A segment closed
// for bad clock quality is explicitly marked with the potentially affected
// tail; neither a successful NTP sample nor this gate proves hardware
// timestamps.
#pragma once

#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

#include "chaos_data/recorder_supervisor.hpp"
#include "chaos_data/session.hpp"
#include "chaos_data/timing.hpp"

namespace chaos::data {

class CollectionController {
public:
  using RecorderFactory = std::function<std::unique_ptr<RecorderInterface>(
    const nlohmann::json & config, const std::string & path,
    const std::string & node_name)>;

  // Injected dependencies keep every branch testable without processes or
  // subprocess probes; production passes none.
  CollectionController(
    const nlohmann::json & asset,
    std::unique_ptr<Session> session = nullptr,
    RecorderFactory recorder_factory = {},
    std::unique_ptr<ChronyProbe> probe = nullptr,
    std::unique_ptr<ClockGuard> guard = nullptr,
    std::function<std::uintmax_t()> free_bytes = {});

  const std::string & assetId() const { return asset_id_; }
  Session & session() { return *session_; }
  const Session & session() const { return *session_; }
  const std::string & state() const { return state_; }
  const std::string & reason() const { return reason_; }
  int exitCode() const { return exit_code_; }

  // One polling cycle: recorder liveness, disk gate, chrony probe, admission.
  nlohmann::json poll();

  // Applies one measured sample; explicit clocks make epoch behavior testable.
  nlohmann::json update(const TimeSample & sample, std::int64_t wall_ns,
                        std::int64_t monotonic_ns);

  nlohmann::json snapshot() const;
  void fail(const std::string & reason);
  void close();

private:
  void closeSegment(const std::string & reason, bool suspect = false,
                    bool failed = false);

  std::string asset_id_;
  nlohmann::json config_;
  std::unique_ptr<Session> session_;
  std::unique_ptr<ChronyProbe> probe_;
  std::unique_ptr<ClockGuard> guard_;
  RecorderFactory recorder_factory_;
  std::function<std::uintmax_t()> free_bytes_fn_;
  std::unique_ptr<RecorderInterface> recorder_;
  nlohmann::json last_decision_;
  nlohmann::json last_good_;
  nlohmann::json pending_startup_;
  int epoch_ = -1;
  int samples_ = 0;
  bool closed_ = false;
  bool free_bytes_known_ = false;
  std::uintmax_t free_bytes_ = 0;
  std::string state_ = "waiting_for_clock";
  std::string reason_ = "waiting for clock quality samples";
  int exit_code_ = 0;
};

}  // namespace chaos::data
