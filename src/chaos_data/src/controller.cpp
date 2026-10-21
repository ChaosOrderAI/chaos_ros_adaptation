// Clock-gated collection supervisor; native rosbag handles all sensor payloads.
#include "chaos_data/controller.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include "chaos_core/clock.hpp"
#include "chaos_core/time.hpp"

namespace chaos::data {
namespace {

nlohmann::json timeSampleToJson(const TimeSample & sample) {
  nlohmann::json result = {
    {"status", sample.status},
    {"reason", sample.reason},
    {"sample_utc_ns", sample.sample_utc_ns},
    {"sample_monotonic_ns", sample.sample_monotonic_ns},
    {"source", sample.source},
    {"offset_basis", sample.offset_basis},
  };
  auto put = [&result](const char * key, const auto & value) {
    if (value.has_value()) {
      result[key] = *value;
    } else {
      result[key] = nullptr;
    }
  };
  put("offset_s", sample.offset_s);
  put("uncertainty_s", sample.uncertainty_s);
  put("reference_utc_ns", sample.reference_utc_ns);
  put("stratum", sample.stratum);
  put("reference_id", sample.reference_id);
  put("reference_name", sample.reference_name);
  put("root_delay_s", sample.root_delay_s);
  put("root_dispersion_s", sample.root_dispersion_s);
  put("leap_status", sample.leap_status);
  return result;
}

nlohmann::json clockDecisionToJson(const ClockDecision & decision) {
  return {
    {"allowed", decision.allowed},
    {"reason", decision.reason},
    {"epoch", decision.epoch},
    {"jump_ns", decision.jump_ns},
    {"stable_samples", decision.stable_samples},
    {"time_sample", timeSampleToJson(decision.time_sample)},
  };
}

}  // namespace

CollectionController::CollectionController(
  const nlohmann::json & asset, std::unique_ptr<Session> session,
  RecorderFactory recorder_factory, std::unique_ptr<ChronyProbe> probe,
  std::unique_ptr<ClockGuard> guard, std::function<std::uintmax_t()> free_bytes)
: asset_id_(asset.at("id").get<std::string>()),
  config_(asset.at("data_collection")),
  session_(session ? std::move(session)
                   : std::make_unique<Session>(asset, config_)),
  recorder_factory_(std::move(recorder_factory)),
  free_bytes_fn_(std::move(free_bytes)) {
  if (!config_.at("enabled").get<bool>()) {
    throw std::invalid_argument("data collection must be explicitly enabled");
  }
  const nlohmann::json & settings = config_.at("time_sync");
  probe_ = probe ? std::move(probe)
                 : std::make_unique<ChronyProbe>(
                     settings.at("max_uncertainty_ms").get<double>() / 1000.0,
                     settings.at("max_reference_age_s").get<double>(),
                     settings.at("probe_timeout_s").get<double>());
  guard_ = guard ? std::move(guard)
                 : std::make_unique<ClockGuard>(
                     settings.at("stable_samples").get<int>(),
                     settings.at("jump_threshold_ms").get<double>() / 1000.0,
                     std::max(
                       2.0, 2 * settings.at("poll_interval_s").get<double>() +
                              settings.at("probe_timeout_s").get<double>()),
                     settings.at("require_sync").get<bool>());
  if (!recorder_factory_) {
    recorder_factory_ = [](const nlohmann::json & config,
                           const std::string & path,
                           const std::string & node_name) {
      return std::make_unique<NativeRecorder>(config, path, node_name);
    };
  }
  if (!free_bytes_fn_) {
    free_bytes_fn_ = [this]() { return session_->freeBytes(); };
  }
}

void CollectionController::closeSegment(const std::string & reason,
                                        bool suspect, bool failed) {
  if (!recorder_) {
    return;
  }
  std::unique_ptr<RecorderInterface> recorder = std::move(recorder_);
  std::string final_reason = reason;
  try {
    recorder->stop();
  } catch (const std::exception & caught) {
    failed = true;
    final_reason += std::string("; recorder shutdown failed: ") + caught.what();
  }
  const std::int64_t utc = chaos::systemNs();
  const std::int64_t steady = chaos::monotonicNs();
  nlohmann::json interval = nullptr;
  if (suspect) {
    interval = {{"from", last_good_},
                {"through", {{"utc_ns", utc}, {"monotonic_ns", steady}}},
                {"reason", final_reason},
                {"meaning",
                 "samples in this tail may have an untrusted receipt clock; "
                 "UTC bounds can cross a clock step, use monotonic bounds"}};
  }
  const bool clean =
    session_->endSegment(final_reason, failed, interval, utc, steady);
  if (!clean) {
    throw std::runtime_error(final_reason);
  }
}

void CollectionController::fail(const std::string & reason) {
  state_ = "failed";
  reason_ = reason;
  exit_code_ = 1;
  try {
    closeSegment(reason, true, true);
  } catch (const std::exception & error) {
    reason_ += std::string("; ") + error.what();
  }
}

nlohmann::json CollectionController::poll() {
  if (closed_ || state_ == "failed") {
    return snapshot();
  }
  try {
    if (recorder_ && !recorder_->alive()) {
      throw std::runtime_error("native recorder exited unexpectedly");
    }
    free_bytes_known_ = true;
    free_bytes_ = free_bytes_fn_();
    const double min_free_mb =
      config_.at("min_free_space_mb").get<double>();
    if (static_cast<double>(free_bytes_) < min_free_mb * 1024.0 * 1024.0) {
      throw std::runtime_error(
        "free disk space is below min_free_space_mb; recordings retained");
    }
    const std::int64_t wall_ns = chaos::systemNs();
    const std::int64_t monotonic_ns = chaos::monotonicNs();
    nlohmann::json result = update(probe_->sample(wall_ns, monotonic_ns),
                                   wall_ns, monotonic_ns);
    if (!pending_startup_.is_null()) {
      // Process creation/DDS/storage startup can take longer than one
      // polling interval. Recheck immediately after the native ACK; never
      // silently reuse the pre-start measurement as fresh proof.
      nlohmann::json evidence = pending_startup_;
      pending_startup_ = nullptr;
      const std::int64_t checked_wall = chaos::systemNs();
      const std::int64_t checked_monotonic = chaos::monotonicNs();
      result = update(probe_->sample(checked_wall, checked_monotonic),
                      checked_wall, checked_monotonic);
      evidence["checked_utc_ns"] = checked_wall;
      evidence["checked_monotonic_ns"] = checked_monotonic;
      evidence["clock_after_start"] = last_decision_;
      evidence["meaning"] =
        "recorder startup is a measurement gap; these bounds do not prove "
        "clock quality for every sample inside that gap";
      session_->recordStartupCheck(
        evidence.at("segment_index").get<int>(), evidence);
      nlohmann::json event = evidence;
      event["event"] = "startup_clock_check";
      session_->append(event);
    }
    return result;
  } catch (const std::exception & error) {
    fail(std::string("std::exception: ") + error.what());
    return snapshot();
  }
}

nlohmann::json CollectionController::update(const TimeSample & sample,
                                            std::int64_t wall_ns,
                                            std::int64_t monotonic_ns) {
  const ClockDecision decision =
    guard_->update(sample, wall_ns, monotonic_ns);
  last_decision_ = clockDecisionToJson(decision);
  ++samples_;
  const bool jumped = epoch_ >= 0 && decision.epoch != epoch_;
  epoch_ = decision.epoch;
  if (jumped || !decision.allowed) {
    const std::string close_reason =
      jumped ? "clock epoch changed"
             : "clock quality lost: " + decision.reason;
    closeSegment(close_reason, true, false);
    state_ = "waiting_for_clock";
    reason_ = decision.reason;
    if (jumped) {
      // Even permissive mode separates wall-clock epochs. Waiting for
      // another sample avoids immediately starting on the detected step.
      guard_->reset();
    }
  } else {
    if (!recorder_) {
      const std::filesystem::path path =
        session_->beginSegment(last_decision_, wall_ns, monotonic_ns);
      recorder_ = recorder_factory_(config_, path.string(),
                                    asset_id_ + "_data_recorder");
      recorder_->start();
      pending_startup_ = {
        {"segment_index", static_cast<int>(session_->manifest().at("segments").size())},
        {"approved_utc_ns", wall_ns},
        {"approved_monotonic_ns", monotonic_ns},
        {"recorder_ready_utc_ns", chaos::systemNs()},
        {"recorder_ready_monotonic_ns", chaos::monotonicNs()},
      };
    }
    state_ = "recording";
    reason_ = decision.reason;
    // Only record evidence judged synchronized as a trusted bound. With
    // require_sync=false, a null bound explicitly means no trusted sample.
    if (sample.status == "synced") {
      last_good_ = {{"utc_ns", wall_ns}, {"monotonic_ns", monotonic_ns}};
    }
  }
  nlohmann::json event = snapshot();
  event["event"] = "clock_sample";
  event["utc_ns"] = wall_ns;
  event["monotonic_ns"] = monotonic_ns;
  session_->append(event);
  return event;
}

nlohmann::json CollectionController::snapshot() const {
  return {
    {"schema_version", 1},
    {"asset_id", asset_id_},
    {"session_id", session_->sessionId()},
    {"state", state_},
    {"reason", reason_},
    {"exit_code", exit_code_},
    {"clock", last_decision_.is_null() ? nlohmann::json() : last_decision_},
    {"clock_samples", samples_},
    {"free_bytes", free_bytes_known_ ? nlohmann::json(free_bytes_) : nlohmann::json()},
    {"segment_count", static_cast<int>(session_->manifest().at("segments").size())},
    {"require_sync", config_.at("time_sync").at("require_sync")},
  };
}

void CollectionController::close() {
  if (closed_) {
    return;
  }
  try {
    closeSegment("collector shutdown", false, false);
  } catch (const std::exception & error) {
    fail(std::string("recorder shutdown failed: ") + error.what());
  }
  if (state_ != "failed") {
    state_ = "stopped";
    reason_ = "collector shutdown";
  }
  try {
    nlohmann::json event = snapshot();
    event["event"] = "session_end";
    event["utc_ns"] = chaos::systemNs();
    event["monotonic_ns"] = chaos::monotonicNs();
    session_->append(event);
    session_->finish(exit_code_ ? "failed" : "completed", reason_, snapshot());
    if (session_->manifest().at("status") == "failed") {
      exit_code_ = 1;
    }
  } catch (const std::exception & error) {
    // Metadata I/O failure leaves the original running manifest as
    // interruption evidence and must never be reported as success.
    state_ = "failed";
    reason_ = error.what();
    exit_code_ = 1;
  }
  closed_ = true;
}

}  // namespace chaos::data
