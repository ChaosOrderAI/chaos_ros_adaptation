#include "chaos_runtime/coordinator.hpp"

#include <chrono>
#include <cmath>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

#include "chaos_core/clock.hpp"
#include "chaos_core/time.hpp"
#include "chaos_runtime/driver_deadline.hpp"
#include "chaos_runtime/ota.hpp"

namespace chaos::runtime {

namespace {
constexpr double kTickWait = 0.05;  // cv wait slice; not a deadline
constexpr double kStatePublishInterval = 0.1;
constexpr double kAcceptanceHandshakeTimeout = 2.0;

// One bounded retry budget for the contended window. A worker holding the
// registry lock delays the next admission by at most one scheduling
// quantum, so a few milliseconds of yields cover the transient; the
// observation gtest demonstrates the same pattern (admitRetrying).
// `full` is retried inside the same budget: a short burst can momentarily
// fill a bounded queue, while a stalled sink outlasts the budget and is
// dropped as before, still counted in the producer counters. State and log
// stay un-retried: they are periodic, the next sample repairs the loss.
template <typename Attempt>
Admission admitWithRetry(Attempt && attempt,
                         std::chrono::milliseconds budget =
                           std::chrono::milliseconds(20)) {
  const auto deadline = std::chrono::steady_clock::now() + budget;
  Admission outcome = attempt();
  while ((outcome == Admission::contended || outcome == Admission::full) &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::yield();
    outcome = attempt();
  }
  return outcome;
}

// The capability name is a literal: the caller never allocates a string
// for it, so the trace path and the log path build their text from a
// borrowed view.
const char * capabilityName(OperationKind kind) {
  switch (kind) {
    case OperationKind::kNavigatePose:
      return "navigation.pose";
    case OperationKind::kTakeoff:
      return "flight.takeoff";
    case OperationKind::kLand:
      return "flight.land";
    case OperationKind::kOta:
      return "system.ota";
  }
  return "UNKNOWN";
}

bool validRequestId(std::string_view value) {
  if (value.empty() || value.size() > 128) {
    return false;
  }
  for (char ch : value) {
    const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' ||
                    ch == ':' || ch == '-';
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool validTraceCode(std::string_view value) {
  if (value.size() > 64 || value.empty() || value.front() < 'A' ||
      value.front() > 'Z') {
    return false;
  }
  for (char ch : value) {
    const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                    ch == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}
}  // namespace

ControlCoordinator::ControlCoordinator(
    AssetConfig asset, std::unique_ptr<Adapter> adapter, Clocks clocks,
    std::shared_ptr<ObservationEndpoint> observations,
    std::unique_ptr<OtaExecutor> ota_executor)
    : asset_(std::move(asset)),
      adapter_(std::move(adapter)),
      ota_(ota_executor ? std::move(ota_executor)
                       : std::make_unique<SimulatedOtaExecutor>()),
      clocks_(std::move(clocks)),
      observations_(std::move(observations)),
      limits_(asset_.limits),
      guard_(asset_.limits,
             clocks_.steady_s ? clocks_.steady_s : chaos::monotonicSeconds),
      capabilities_(asset_.capabilities.begin(), asset_.capabilities.end()) {
  if (!observations_) {
    // A coordinator without an endpoint has nowhere to report a result, a
    // state or a control trace. The observation channels are part of the
    // contract, so this is a startup failure and never a silently disabled
    // channel.
    throw std::invalid_argument(
      "ControlCoordinator requires an observation endpoint");
  }
  if (!clocks_.steady_s) {
    clocks_.steady_s = chaos::monotonicSeconds;
  }
  if (!clocks_.mono_ns) {
    clocks_.mono_ns = chaos::monotonicNs;
  }
  if (!clocks_.wall_ns) {
    clocks_.wall_ns = chaos::systemNs;
  }
  if (!clocks_.ros_ns) {
    clocks_.ros_ns = chaos::systemNs;
  }
  if (!clocks_.driver_mono_ns) {
    // The driver deadline clock is Linux CLOCK_MONOTONIC: the same named
    // clock as the one the adapter and the supervised driver use. Its epoch
    // is never inferred from the steady clock and never from a ROS clock.
    clocks_.driver_mono_ns = driver_deadline::monotonicNowNs;
  }
  last_tick_s_ = clocks_.steady_s();
  last_state_s_ = 0.0;
}

ControlCoordinator::~ControlCoordinator() { close(); }

void ControlCoordinator::rejected(const std::string & code,
                                  const std::string & message) {
  throw chaos::Rejected(code, message);
}

void ControlCoordinator::log(std::string_view level,
                             std::initializer_list<std::string_view> parts)
    const noexcept {
  // Observation only. This is the single construction boundary of the log
  // channel: the join of the parts and the admission are inside one
  // protected block, so a failing allocation or a failing channel drops the
  // line and never changes a control outcome. A log failure is never logged
  // again, so the channel can never recurse into itself.
  try {
    std::string text;
    for (const std::string_view part : parts) {
      text.append(part);
    }
    observations_->log(level, text);
  } catch (...) {
    observations_->recordConstructionFailure(ObservationChannel::log);
  }
}

void ControlCoordinator::log(std::string_view level,
                             std::string_view text) const noexcept {
  // One already formed text: the endpoint copies the payload on its own
  // side and its admission is noexcept, so this path owns no temporary and
  // needs no catch of its own.
  observations_->log(level, text);
}

void ControlCoordinator::publishResult(bool success, std::string_view code,
                                       std::string_view message,
                                       std::string_view request_id) {
  // The observation construction and the clock read are inside the
  // isolation block: a broken clock or a broken channel drops the result and
  // never changes the outcome of the command itself. The endpoint copies
  // the payload and delivers it on its own worker, so this call never
  // blocks on a sink.
  //
  // The parameters are views: the caller allocates nothing before this
  // block, and every copy into the owned payload happens inside it.
  try {
    CommandResultView result;
    result.stamp_ns = clocks_.ros_ns();
    result.request_id.assign(request_id);
    result.success = success;
    result.code.assign(code);
    result.message.assign(message);
    admitWithRetry([&] { return observations_->result(result); });
  } catch (...) {
    // Observation is not control: the command outcome is enforced by the
    // guard, by the deadline and by the watchdog, not by this channel. The
    // loss is counted, so it stays visible in the producer counters.
    observations_->recordConstructionFailure(ObservationChannel::results);
  }
}

void ControlCoordinator::requireCapability(const std::string & capability) {
  if (capabilities_.count(capability) == 0) {
    rejected("UNSUPPORTED", "Capability " + capability + " is not enabled");
  }
  if (closing_) {
    rejected("NOT_READY", "Runtime is shutting down");
  }
}

void ControlCoordinator::requireCapability(OperationKind capability) {
  requireCapability(capabilityName(capability));
}

void ControlCoordinator::requireReadyLocked() {
  if (stop_pending_.has_value()) {
    rejected("STOP_FAILED", "Stop output is pending; motion is inhibited");
  }
  if (!fault_.empty()) {
    rejected("DRIVER_ERROR", fault_);
  }
  const AdapterSnapshot snapshot = adapter_->snapshot();
  if (!snapshot.connected || !snapshot.ready) {
    rejected("NOT_READY", snapshot.detail);
  }
}

void ControlCoordinator::expireLocked() {
  if (guard_.expire() || (driving_ && guard_.remaining() <= 0)) {
    halt("LEASE_EXPIRED", "Control lease expired");
  }
}

void ControlCoordinator::halt(const std::string & code,
                              const std::string & message, bool explicit_stop,
                              const std::string & request_id) {
  if (operation_ && !operation_->interrupted.has_value()) {
    operation_->interrupted = std::make_pair(code, message);
  }
  if (!stop_pending_.has_value()) {
    emitTrace("motion.stop", "invalidated", code, request_id);
    stop_pending_ = StopPending{explicit_stop, 0, 0, request_id};
  } else if (explicit_stop) {
    stop_pending_->explicit_stop = true;
    stop_pending_->request_id = request_id;
    emitTrace("motion.stop", "invalidated", code, request_id);
  }
  retryStop(explicit_stop);
}

bool ControlCoordinator::retryStopAfterHalt(const std::string & code,
                                            const std::string & message) {
  if (operation_ && !operation_->interrupted.has_value()) {
    operation_->interrupted = std::make_pair(code, message);
  }
  if (!stop_pending_.has_value()) {
    emitTrace("motion.stop", "invalidated", code);
    stop_pending_ = StopPending{false, 0, 0, ""};
  }
  return retryStop(true);
}

bool ControlCoordinator::retryStop(bool force) {
  if (!stop_pending_.has_value()) {
    return true;
  }
  const std::int64_t now = clocks_.mono_ns();
  if (!force && now < stop_pending_->next_attempt_ns) {
    return false;
  }
  const std::string request_id = stop_pending_->request_id;
  try {
    if (stop_pending_->explicit_stop) {
      adapter_->controlledStop();
    } else {
      adapter_->stop();
    }
  } catch (const std::exception & error) {
    fault_ = std::string("Adapter stop failed: ") + error.what();
    // Keep retry state independent of the lease/command that caused it.
    // The backoff counter saturates at 5 but retries continue until success
    // or shutdown: a latched stop must never give up on its own.
    stop_pending_->failures = std::min(stop_pending_->failures + 1, 5);
    stop_pending_->next_attempt_ns = now + static_cast<std::int64_t>(
      std::min(1.0, 0.1 * std::pow(2.0, stop_pending_->failures - 1)) * 1e9);
    if (stop_pending_->failures == 1) {
      log("error", fault_);
    }
    return false;
  }
  driving_ = false;
  guard_.clearVelocity();
  stop_pending_.reset();
  emitTrace("motion.stop", "issued", "STOP_OUTPUT_ISSUED", request_id);
  // Preserve the fault latch: a recovered stop must not resume motion.
  return true;
}

// Caller must hold mutex_.
void ControlCoordinator::emitTrace(
  std::string_view command, std::string_view stage,
  std::string_view code, std::string_view request_id,
  const std::optional<std::array<double, 6>> & velocity,
  const std::optional<std::int64_t> & driver_deadline_ns) {
  // Best-effort evidence, isolated from control decisions and driver
  // outcomes. Sequence assignment and admission share one dedicated lock:
  // emitTrace runs on several threads (velocity callbacks, the action
  // worker, the watchdog), and a plain ++ on the sequence is a data race
  // whose duplicate and reversed sequences are exactly what a downstream
  // consumer reconciles against. With the lock, the recorded order matches
  // the assignment order and each event is admitted exactly once; the
  // bounded retry keeps a missing event a rare, accounted loss instead of
  // a routine gap. No authorization values or error text are copied.
  //
  // The whole event construction, including every clock read and every
  // field copy, lives inside the best-effort block: a broken clock must
  // never turn a control decision into an exception, and it must never
  // suppress the stop output of a failing command. The block reports its
  // own failures through the endpoint, so this function never throws and
  // an accepted command can never become a DRIVER_ERROR here.
  std::lock_guard<std::mutex> trace_order(trace_order_mutex_);
  const std::uint64_t sequence = ++trace_sequence_;
  try {
    TraceEvent event;
    event.sequence = sequence;
    event.stamp_ns = clocks_.ros_ns();
    event.monotonic_ns = clocks_.mono_ns();
    event.frame = asset_.frames.base;
    event.asset_id = asset_.id;
    event.request_id.assign(validRequestId(request_id) ? request_id
                                                       : std::string_view());
    event.command.assign(command);
    event.stage.assign(stage);
    event.code.assign(validTraceCode(code) ? code
                                           : std::string_view("UNKNOWN"));
    if (velocity.has_value() && command == "motion.velocity" &&
        stage == "issued") {
      TraceVelocity sample;
      sample.valid = true;
      sample.values = *velocity;
      // Sample the guard after the event timestamp: any sampling delay
      // shortens the exported interval, rather than extending its lease.
      double remaining_s = guard_.leaseActive()
                             ? std::max(0.0, std::min(guard_.velocityExpires(),
                                                      guard_.leaseExpires()) -
                                               clocks_.steady_s())
                             : 0.0;
      if (driver_deadline_ns.has_value()) {
        // The immutable driver deadline D bounds the interval the driver
        // really honours. The guard committed its own, later, expiry, so
        // the guard bound alone overstates the remaining time whenever the
        // origin age or the validation delay consumed part of the original
        // TTL. The driver clock is sampled once, after the trace stamps,
        // and the cap can only shorten the exported interval: no TTL is
        // added and the actual D is untouched.
        const std::int64_t driver_now_ns = clocks_.driver_mono_ns();
        if (driver_now_ns <= 0) {
          // An unusable driver clock drops this observation. The loss is
          // counted with its sequence and the accepted control is
          // untouched: this is a numeric branch, not an exception.
          observations_->recordConstructionFailure(ObservationChannel::trace,
                                                    sequence);
          return;
        }
        const std::int64_t remaining_ns = *driver_deadline_ns - driver_now_ns;
        const double remaining_deadline_s =
          remaining_ns > 0 ? static_cast<double>(remaining_ns) / 1e9 : 0.0;
        remaining_s =
          std::max(0.0, std::min(remaining_s, remaining_deadline_s));
      }
      sample.valid_for_s = remaining_s;
      event.velocity = sample;
    }
    // The endpoint copies the event and delivers it on its own worker: the
    // delivery never re-stamps and never renumbers the event. The trace
    // channel keeps its single-shot admission: its consumers reconcile
    // The endpoint copies the event and delivers it on its own worker: the
    // delivery never re-stamps and never renumbers the event. The bounded
    // retry runs under the order lock, so a contended registry lock turns
    // into a short yield instead of a consumer-visible sequence gap.
    admitWithRetry([&] { return observations_->trace(event); });
  } catch (...) {
    // The event could not even be built. The loss is counted through the
    // endpoint and the assigned sequence stays visible as a gap.
    observations_->recordConstructionFailure(ObservationChannel::trace,
                                              sequence);
  }
}

LeaseResponseView ControlCoordinator::lease(const LeaseRequestView & request) {
  // Capture before taking the control lock so lock contention consumes TTL.
  // The capture is guarded: an injected clock that throws must be reported
  // as a rejection, never escape the executor callback.
  std::optional<control::CommandDeadline> deadline;
  try {
    deadline = captureDeadline(request.expires_at);
  } catch (const std::exception & error) {
    LeaseResponseView response;
    response.success = false;
    response.code = "INVALID_ARGUMENT";
    response.message = error.what();
    return response;
  }
  LeaseResponseView response;
  std::lock_guard<std::mutex> guard(mutex_);
  try {
    deadline->require(clocks_.wall_ns(), clocks_.mono_ns());
    expireLocked();
    if (request.operation == "acquire") {
      if (capabilities_.size() == 1 && capabilities_.count("state")) {
        rejected("UNSUPPORTED", "This asset is read-only");
      }
      requireReadyLocked();
    }
    deadline->require(clocks_.wall_ns(), clocks_.mono_ns());
    const auto lease = guard_.lease(request.operation, request.owner,
                                    request.token, request.duration_s);
    response.token = lease.first;
    response.remaining_s = lease.second;
    if (request.operation == "release") {
      if (!retryStopAfterHalt("CONTROL_RELEASED", "Control lease released")) {
        response.success = false;
        response.code = "STOP_FAILED";
        response.message =
          "Control lease released; stop output failed and will be retried";
        return response;
      }
    }
    response.success = true;
    response.code = "OK";
    response.message = "Control lease updated";
    return response;
  } catch (const chaos::Rejected & error) {
    response.success = false;
    response.code = error.code();
    response.message = error.message();
    return response;
  } catch (const std::exception & error) {
    fault_ = error.what();
    halt("DRIVER_ERROR", fault_);
    response.success = false;
    response.code = "DRIVER_ERROR";
    response.message = fault_;
    return response;
  }
}

StopResponseView ControlCoordinator::stop(const StopRequestView & request) {
  std::optional<control::CommandDeadline> deadline;
  try {
    deadline = captureDeadline(request.expires_at);
  } catch (const std::exception & error) {
    StopResponseView response;
    response.success = false;
    response.code = "INVALID_ARGUMENT";
    response.message = error.what();
    return response;
  }
  StopResponseView response;
  std::lock_guard<std::mutex> guard(mutex_);
  try {
    deadline->require(clocks_.wall_ns(), clocks_.mono_ns());
    expireLocked();
    requireCapability("motion.stop");
    guard_.require(request.lease_token);
    deadline->require(clocks_.wall_ns(), clocks_.mono_ns());
    // Revoke the current lease BEFORE issuing halt output. An accepted stop
    // ends the control session: a command replayed with the old token must
    // be rejected even if stop output later fails. Only a fully validated
    // stop reaches this point, so an invalid token or an expired deadline
    // never revokes a valid lease.
    //
    // The revocation is unconditional and never re-validates the lease: a
    // lease that expires between the token check above and this point must
    // not turn an accepted stop into NO_CONTROL and skip the halt output.
    // The revocation also disarms any armed velocity sample, so a replay
    // cannot restore nonzero output.
    guard_.revoke();
    // Same semantics as Python _halt('STOPPED', ..., explicit=True): an
    // explicit stop also interrupts the active action so its execution
    // loop observes the STOPPED interruption and never moves again.
    halt("STOPPED", "Stopped by command", true, request.request_id);
    if (stop_pending_.has_value()) {
      rejected("STOP_FAILED", fault_);
    }
    response.success = true;
    response.code = "OK";
    response.message = "Controlled stop output issued";
    return response;
  } catch (const chaos::Rejected & error) {
    emitTrace("motion.stop", "rejected", error.code(), request.request_id);
    response.success = false;
    response.code = error.code();
    response.message = error.message();
    return response;
  } catch (const std::exception & error) {
    fault_ = error.what();
    halt("DRIVER_ERROR", fault_);
    emitTrace("motion.stop", "rejected", "DRIVER_ERROR", request.request_id);
    response.success = false;
    response.code = "DRIVER_ERROR";
    response.message = fault_;
    return response;
  }
}

void ControlCoordinator::velocity(const VelocityRequestView & request) {
  // The deadline clock samples are taken BEFORE the control lock, so the
  // time spent waiting for the lock and in the validation consumes the
  // original interval instead of extending it. The driver monotonic clock
  // is read first and the ROS clock second: the source age of this command
  // is measured against the ROS sample captured here and is never refreshed
  // afterwards.
  //
  // A failing clock must never escape the ROS callback, so the failure is
  // captured here and reported through the normal control path below. No
  // state is mutated and no sink is called outside the lock.
  std::int64_t origin_anchor = 0;
  std::int64_t captured_ros_now = 0;
  std::exception_ptr capture_failure;
  try {
    origin_anchor = clocks_.driver_mono_ns();
    captured_ros_now = clocks_.ros_ns();
  } catch (const std::exception &) {
    // The production clocks raise standard exceptions. The failure is kept
    // and rethrown under the lock, where the normal fault and halt path
    // reports it.
    capture_failure = std::current_exception();
  }

  std::lock_guard<std::mutex> guard(mutex_);
  // The guard commits its velocity sample inside velocityNs(). Everything
  // after that point is post-admission: a failure there has to stop the
  // motion, because the guard timeout was already extended and an old
  // physical output would otherwise borrow it.
  bool admitted = false;
  try {
    expireLocked();
    requireCapability("motion.velocity");
    requireReadyLocked();
    if (operation_) {
      rejected("BUSY", "An action is active; stop or cancel it first");
    }
    adapter_->validateVelocity(request.velocity);
    // Keep the original integer sec/nsec stamp: converting to double seconds
    // would round away adjacent-1ns ordering. A stamp outside the checked
    // ROS range is passed as zero and rejected as STALE_COMMAND below.
    std::int64_t stamp_ns = 0;
    chaos::rosStampToNs(request.stamp_sec, request.stamp_nanosec, stamp_ns);
    if (capture_failure) {
      // The clock sample failed before the lock was taken. It is a driver
      // failure and it is reported through the existing fault and halt
      // path, so the motion is stopped instead of being armed blind.
      // The control token is checked before the failure is reported: a
      // wrong token stays NO_CONTROL even when the clock is unavailable.
      guard_.require(request.lease_token);
      std::rethrow_exception(capture_failure);
    }
    // The original TTL bound. The deadline is anchored at the sample taken
    // before the lock, so the lock wait and the validation consume the
    // interval instead of extending it.
    const double ttl = request.valid_for_s;
    const bool ttl_usable = std::isfinite(ttl) && ttl > 0.0 &&
                            ttl <= limits_.command_timeout_s;
    std::optional<std::int64_t> origin_deadline;
    if (ttl_usable) {
      origin_deadline = driver_deadline::sourceDeadlineNs(
        origin_anchor, stamp_ns, captured_ros_now, ttl);
      if (!origin_deadline.has_value()) {
        // The source interval is already consumed while the TTL itself is
        // usable: the command is stale. It is rejected before the guard
        // sample is committed, so the watermark and the armed velocity
        // sample stay untouched.
        // The control token is checked before the rejection: a wrong token
        // stays NO_CONTROL even when the source interval is unusable.
        guard_.require(request.lease_token);
        rejected("STALE_COMMAND",
                 "Command is expired or timestamp is unusable");
      }
    }
    // An invalid TTL is not a deadline problem: it keeps the guard's own
    // error code (INVALID_ARGUMENT, OUT_OF_RANGE) and never commits a
    // sample. The guard validates the control token itself, so a wrong
    // token stays NO_CONTROL on this path as well.
    guard_.velocityNs(request.lease_token, stamp_ns, captured_ros_now,
                      request.valid_for_s, request.frame_id, asset_.frames.base,
                      request.velocity);
    admitted = true;
    // The lease bound. The anchor is read BEFORE the steady reading, so a
    // preemption between the two can only move the observed reading later
    // and shorten the granted interval.
    const std::int64_t bridge_anchor = clocks_.driver_mono_ns();
    const double observed_steady = clocks_.steady_s();
    const double lease_expires = guard_.leaseExpires();
    const double velocity_expires = guard_.velocityExpires();
    // Both bounds are checked on their own: std::min() would hide a NaN
    // commit behind its finite sibling.
    if (!std::isfinite(lease_expires) || lease_expires <= 0.0) {
      throw std::runtime_error("Control lease expiry is unusable");
    }
    if (!std::isfinite(velocity_expires) || velocity_expires <= 0.0) {
      throw std::runtime_error("Velocity expiry is unusable");
    }
    const std::optional<std::int64_t> bridge_deadline =
      driver_deadline::boundedSteadyDeadlineNs(
        bridge_anchor, std::min(lease_expires, velocity_expires),
        observed_steady, limits_.command_timeout_s);
    if (!bridge_deadline.has_value()) {
      throw std::runtime_error("No usable motion deadline");
    }
    // The deadline is the earlier of two independent bounds: the original
    // TTL bound and the remaining lease bound. The origin bound is what
    // keeps the validation delay from being absorbed by the guard's own,
    // later steady reading.
    //
    // The guard accepted the command, so the TTL is usable and the origin
    // deadline above was computed: it is present here.
    const std::int64_t deadline = std::min(*origin_deadline, *bridge_deadline);
    // The final check: the deadline must still be in the future when the
    // command reaches the transport. An elapsed deadline never produces a
    // nonzero output.
    const std::int64_t dispatch_now = clocks_.driver_mono_ns();
    if (dispatch_now <= 0 || dispatch_now >= deadline) {
      throw std::runtime_error("Motion deadline elapsed before dispatch");
    }
    // The command carries the original source stamp and the immutable
    // absolute deadline D. The adapter never re-anchors or extends it.
    adapter_->setVelocityUntil(request.velocity, stamp_ns, deadline);
    driving_ = true;
    emitTrace("motion.velocity", "issued", "RUNTIME_ISSUED", request.request_id,
              request.velocity, deadline);
    publishResult(true, "ACCEPTED",
                  "Velocity accepted until command expiry", request.request_id);
  } catch (const chaos::Rejected & error) {
    if (!admitted) {
      emitTrace("motion.velocity", "rejected", error.code(), request.request_id);
      publishResult(false, error.code(), error.message(), request.request_id);
      return;
    }
    // The guard already admitted and armed the sample: a rejection after
    // that point must stop the motion instead of leaving the old physical
    // output to the just extended guard timeout.
    fault_ = error.what();
    halt("DRIVER_ERROR", fault_);
    emitTrace("motion.velocity", "rejected", "DRIVER_ERROR", request.request_id);
    publishResult(false, "DRIVER_ERROR", fault_, request.request_id);
  } catch (const std::exception & error) {
    fault_ = error.what();
    halt("DRIVER_ERROR", fault_);
    emitTrace("motion.velocity", "rejected", "DRIVER_ERROR", request.request_id);
    publishResult(false, "DRIVER_ERROR", fault_, request.request_id);
  }
}

ControlCoordinator::GoalDecision ControlCoordinator::goal(
  OperationKind kind, const GoalRequestView & request, const void * goal_token,
  std::function<void(const OperationResult &)> terminal) {
  // Capture before the control lock, inside a guard: an injected clock that
  // throws must be reported as a rejection, never escape the executor.
  std::optional<control::CommandDeadline> deadline;
  try {
    deadline = captureDeadline(request.expires_at);
  } catch (const std::exception & error) {
    log("warning", {"Rejected ", capabilityName(kind), ": ", error.what()});
    return GoalDecision::kReject;
  }
  std::lock_guard<std::mutex> guard(mutex_);
  try {
    deadline->require(clocks_.wall_ns(), clocks_.mono_ns());
    expireLocked();
    requireCapability(kind);
    guard_.require(request.lease_token);
    requireReadyLocked();
    if (operation_) {
      rejected("BUSY", "Another operation is active");
    }
    Operation reservation;
    reservation.id = ++next_operation_id_;
    reservation.kind = kind;
    reservation.lease_token = request.lease_token;
    if (kind == OperationKind::kNavigatePose) {
      control::validate_pose(request.target_frame, asset_.frames.map,
                             request.position, request.orientation,
                             asset_.kind, limits_.max_altitude_m);
      reservation.target = {request.position[0], request.position[1],
                            request.position[2],
                            2.0 * std::atan2(request.orientation[2],
                                             request.orientation[3])};
    } else if (kind == OperationKind::kTakeoff) {
      const double altitude =
        control::finite_positive(request.altitude_m, "altitude_m");
      if (altitude > limits_.max_altitude_m) {
        rejected("OUT_OF_RANGE", "Takeoff altitude exceeds max_altitude_m");
      }
      reservation.target = {altitude, 0.0, 0.0, 0.0};
    } else if (kind == OperationKind::kOta) {
      // Reject a bad package identity before the motion halt, so an invalid
      // update request does not stop a command that was already running.
      reservation.ota = OtaRequest{
        request.artifact_id, request.version, request.sha256,
        request.size_bytes};
      ota_->validate(reservation.ota);
    } else {
      if (!std::isfinite(request.altitude_m) || request.altitude_m != 0) {
        rejected("INVALID_ARGUMENT", "Landing altitude_m must be zero");
      }
      reservation.target = {0.0, 0.0, 0.0, 0.0};
    }
    halt("PREEMPTED", "Velocity replaced by action");
    if (!fault_.empty()) {
      rejected("DRIVER_ERROR", fault_);
    }
    deadline->require(clocks_.wall_ns(), clocks_.mono_ns());
    reservation.start_s = clocks_.steady_s();
    reservation.goal_token = goal_token;
    reservation.terminal = std::move(terminal);
    reservation.deadline = deadline;
    operation_ = std::make_shared<Operation>(reservation);
    return GoalDecision::kAccept;
  } catch (const chaos::Rejected & error) {
    log("warning",
        {"Rejected ", capabilityName(kind), ": ", error.code(), ": ",
         error.message()});
    return GoalDecision::kReject;
  } catch (const std::exception & error) {
    log("error",
        {"Failed to validate ", capabilityName(kind), ": ", error.what()});
    return GoalDecision::kReject;
  }
}

void ControlCoordinator::accepted(const void * goal_token,
                                  const void * handle_token) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (operation_ && operation_->goal_token == goal_token) {
    operation_->handle_token = handle_token;
  }
  // Execution always proceeds so an expired reservation gets a typed
  // aborted result instead of a silent goal.
}

ControlCoordinator::CancelDecision ControlCoordinator::cancel(
  const void * goal_token, const void * handle_token) {
  std::lock_guard<std::mutex> guard(mutex_);
  // The executor advances goal status after this callback returns. Halting
  // here prevents a final movement before runOperation sees the cancel.
  // (The facade rejects cancels for inactive handles before calling here.)
  if (!operation_ || operation_->goal_token != goal_token) {
    return CancelDecision::kReject;
  }
  if (operation_->handle_token && operation_->handle_token != handle_token) {
    return CancelDecision::kReject;
  }
  if (operation_->completed) {
    // The operation reached its terminal state. The reservation is still
    // held so that no second goal can start, but the terminal result is
    // already on its way to the client: a cancel must not turn a finished
    // operation into a cancellation.
    return CancelDecision::kReject;
  }
  halt("CANCELED", "Action canceled by client");
  return CancelDecision::kAccept;
}

// Caller must hold mutex_.
void ControlCoordinator::finishOperation(Operation & operation,
                                         GoalContext & ctx,
                                         OperationResult & result,
                                         std::string & terminal) {
  if (operation_.get() == &operation) {
    if (!retryStopAfterHalt("FINISHED", "Operation finished")) {
      terminal = "aborted";
      result.success = false;
      result.code = "STOP_FAILED";
      result.message =
        "Operation ended but stop output failed and will be retried";
    }
    operation_.reset();
  }
  // The terminal transition itself is delivered by the caller after the
  // control lock is released: an action server that blocks or throws must
  // never stall the watchdog or the stop retry loop.
  (void)ctx;
}

// Caller must hold mutex_.
bool ControlCoordinator::harvestOtaAfterCancel(const Operation & operation,
                                                OperationResult & result,
                                                std::string & terminal) {
  if (operation.kind != OperationKind::kOta) {
    return false;
  }
  // The executor contract says that a cancel takes effect on the next
  // poll, so the poll after the cancel is what reveals the real outcome.
  OtaStatus status;
  try {
    ota_->cancel();
    status = ota_->poll();
  } catch (const std::exception &) {
    // The executor failed while it was harvested. The interrupt stays the
    // reported outcome and nothing is known about the package: the client is
    // told that no commit was seen.
    return false;
  }
  if (!status.done || !status.applied) {
    // Nothing was committed. The caller keeps the interrupt code and the
    // `applied` flag stays false: the client is told that no bytes were
    // written.
    return false;
  }
  // The executor committed the package. Its own status is the truth about
  // the operation, whatever the interrupt was: reporting "canceled" here
  // would tell the client that nothing was written while the device already
  // runs the new version.
  result = {status.success, status.code, status.message, true};
  terminal = status.success ? "succeeded" : "aborted";
  return true;
}

void ControlCoordinator::deliverTerminal(GoalContext & ctx,
                                         const std::string & terminal,
                                         const OperationResult & result) {
  try {
    if (!ctx.isActive()) {
      return;
    }
    if (terminal == "succeeded") {
      ctx.succeed(result);
    } else if (terminal == "canceled") {
      ctx.canceled(result);
    } else {
      ctx.abort(result);
    }
  } catch (...) {
    // Terminal transitions are delivery, not control; the stop latch and
    // reservation cleanup above already hold.
    log("error", "Goal terminal transition failed");
  }
}

OperationResult ControlCoordinator::runOperation(
  OperationKind kind, const void * goal_token, const void * handle_token,
  GoalContext & ctx) {
  OperationResult result;
  std::string terminal = "aborted";
  std::shared_ptr<Operation> mine;
  bool ota_started = false;
  bool ota_harvested = false;
  // Caller must hold mutex_.
  const auto harvest = [&] {
    ota_harvested = true;
    return harvestOtaAfterCancel(*mine, result, terminal);
  };
  // The operation is counted so that close() can wait for it: the adapter
  // must not be closed while a worker is still inside it.
  struct OperationGuard {
    std::atomic<int> & counter;
    explicit OperationGuard(std::atomic<int> & value) : counter(value) {
      counter.fetch_add(1);
    }
    ~OperationGuard() { counter.fetch_sub(1); }
  } guard(active_operations_);
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (operation_ && operation_->kind == kind &&
        operation_->goal_token == goal_token) {
      mine = operation_;
      mine->handle_token = handle_token;
    }
  }
  if (!mine) {
    // The client must always receive a typed terminal result; returning
    // without one would leave it waiting forever.
    const OperationResult lost{false, "INTERNAL_ERROR",
                               "Action reservation was lost"};
    deliverTerminal(ctx, "aborted", lost);
    return lost;
  }
  try {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (!mine->interrupted.has_value()) {
        mine->deadline->require(clocks_.wall_ns(), clocks_.mono_ns());
        guard_.require(mine->lease_token);
        requireCapability(kind);
        requireReadyLocked();
        mine->deadline->require(clocks_.wall_ns(), clocks_.mono_ns());
        // An update is not a motion label. ControlTrace only records motion
        // commands, so the training export never sees system.ota.
        if (mine->kind == OperationKind::kOta) {
          ota_started = true;
          ota_->start(mine->ota);
        } else {
          emitTrace(capabilityName(kind), "invalidated", "OPERATION_STARTING");
          adapter_->startOperation(kind, mine->target);
        }
      }
    }
    while (true) {
      bool has_feedback = false;
      bool ota_feedback = false;
      bool finished_now = false;
      double progress = 0.0;
      std::string phase;
      AdapterSnapshot snapshot;
      {
        std::lock_guard<std::mutex> guard(mutex_);
        if (closing_) {
          // The executor may have committed the package before the runtime
          // stopped. The harvest tells the client whether bytes were written.
          if (harvest()) {
            break;
          }
          result = {false, "SHUTDOWN", "Runtime stopped", false};
          break;
        }
        if (ctx.isCancelRequested()) {
          halt("CANCELED", "Action canceled by client");
          // A cancel that arrives after the executor committed the package
          // does not unwrite it. The executor's own status wins over the
          // cancellation, otherwise the client would be told that nothing was
          // written while the device already runs the new version.
          if (harvest()) {
            break;
          }
          terminal = "canceled";
          result = {false, "CANCELED", "Action canceled by client", false};
          break;
        }
        if (mine->interrupted.has_value()) {
          const auto & [code, message] = *mine->interrupted;
          // Cancellation is accepted before ROS exposes CANCELING. Wait one
          // iteration for the accepted transition.
          if (code != "CANCELED") {
            if (harvest()) {
              break;
            }
            rejected(code, message);
          }
        } else {
          guard_.require(mine->lease_token);
          requireReadyLocked();
          if (clocks_.steady_s() - mine->start_s > limits_.action_timeout_s) {
            // The timeout must reach the executor: an update that rolls back
            // in cancel() never learns that it has to roll back.
            if (harvest()) {
              break;
            }
            rejected("TIMEOUT", "Action exceeded action_timeout_s");
          }
          if (mine->kind == OperationKind::kOta) {
            const OtaStatus poll = ota_->poll();
            progress = poll.progress;
            phase = poll.phase;
            has_feedback = true;
            ota_feedback = true;
            if (poll.done) {
              terminal = poll.success ? "succeeded" : "aborted";
              result = {poll.success, poll.code, poll.message, poll.applied};
              // The reservation is kept until the terminal result is on its
              // way to the client: a cancel that arrives while the final
              // feedback is published must be refused, and a second goal must
              // not start before the first one is reported. The motion branch
              // finalizes under the same lock for the same reason.
              mine->completed = true;
              finished_now = true;
            }
          } else {
            const PollResult poll = adapter_->pollOperation();
            snapshot = adapter_->snapshot();
            progress = poll.progress;
            has_feedback = true;
            if (poll.done) {
              terminal = poll.success ? "succeeded" : "aborted";
              result = {poll.success, poll.code, poll.message};
              // Finalize under the same lock as pollOperation so a new
              // cancellation cannot slip between completion and the terminal
              // state transition. The transition itself is delivered after
              // the lock is released.
              finishOperation(*mine, ctx, result, terminal);
              break;
            }
          }
        }
      }
      if (has_feedback) {
        try {
          if (ota_feedback) {
            ctx.publishOtaFeedback(phase, progress);
          } else {
            ctx.publishFeedback(progress, snapshot);
          }
        } catch (...) {
          // Feedback is observability; a failing channel must not abort
          // the device operation.
        }
      }
      if (finished_now) {
        break;
      }
      std::unique_lock<std::mutex> wait(mutex_);
      cv_.wait_for(wait, std::chrono::duration<double>(kTickWait));
    }
  } catch (const chaos::Rejected & error) {
    result = {false, error.code(), error.message(), false};
  } catch (const std::exception & error) {
    result = {false, "DRIVER_ERROR", error.what(), false};
  } catch (...) {
    result = {false, "DRIVER_ERROR", "unknown operation failure", false};
  }
  // Also finish failures raised outside the polling loop (shutdown, expiry,
  // interrupts). Identity and active-state checks make repeated finalization
  // harmless.
  {
    std::lock_guard<std::mutex> guard(mutex_);
    // A failure raised by a lease, readiness or executor check never passed
    // through an interrupt branch: the executor still runs, and it may have
    // committed the package already.
    if (ota_started && !ota_harvested && !mine->completed) {
      harvest();
    }
    finishOperation(*mine, ctx, result, terminal);
  }
  deliverTerminal(ctx, terminal, result);
  return result;
}

void ControlCoordinator::tick() {
  // A dropped reservation must still produce a typed terminal result, and
  // the delivery must not happen while the control lock is held.
  std::function<void(const OperationResult &)> expired_terminal;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (closing_) {
      return;
    }
    const double now = clocks_.steady_s();
    try {
      expireLocked();
      if (operation_ && !operation_->handle_token &&
          (operation_->interrupted.has_value() ||
           now - operation_->start_s > kAcceptanceHandshakeTimeout)) {
        // The accepted handle may never be delivered (client disappeared);
        // expire the reservation instead of waiting forever.
        halt("TIMEOUT", "Action acceptance handshake expired");
        expired_terminal = operation_->terminal;
        operation_.reset();
      }
      if (driving_ && guard_.velocityTimedOut()) {
        halt("COMMAND_EXPIRED", "Velocity command expired");
      }
      retryStop(false);
      // The control health of the adapter: this snapshot decides the
      // telemetry-loss halt. The state observation reads its own snapshot
      // and never feeds this decision.
      const AdapterSnapshot snapshot = adapter_->snapshot();
      if ((driving_ || operation_) &&
          (!snapshot.connected || !snapshot.ready)) {
        halt("NOT_READY", "Adapter telemetry lost or no longer ready");
      }
      adapter_->update(now - last_tick_s_);
      if (now - last_state_s_ >= kStatePublishInterval) {
        publishStateLocked();
        last_state_s_ = now;
      }
    } catch (const std::exception & error) {
      fault_ = error.what();
      halt("DRIVER_ERROR", fault_);
      log("error", {"Adapter error: ", fault_});
    }
    last_tick_s_ = now;
  }
  if (expired_terminal) {
    try {
      expired_terminal(OperationResult{false, "TIMEOUT",
                                       "Action acceptance handshake expired"});
    } catch (...) {
      log("error", "Goal terminal delivery failed");
    }
  }
  cv_.notify_all();
}

void ControlCoordinator::publishStateLocked() {
  // The snapshot exists only to build the observation. The control health
  // of the same adapter is decided separately by the tick, which keeps its
  // own snapshot and its own fault path: a state that cannot be published
  // never changes a control decision.
  //
  // Everything that belongs to the observation, including the snapshot read
  // and every string copy, is inside one protected block. A failing clock, a
  // failing snapshot or a failing channel drops the state and is counted as
  // a construction failure of the state channel.
  try {
    const AdapterSnapshot snapshot = adapter_->snapshot();
    StateView state;
    state.stamp_ns = clocks_.ros_ns();
    state.asset_id = asset_.id;
    state.model = asset_.model;
    state.connected = snapshot.connected;
    state.ready = snapshot.ready && fault_.empty();
    state.pose_valid = snapshot.pose_valid && snapshot.connected;
    if (!fault_.empty()) {
      state.operational_state = "fault";
    } else if (!snapshot.connected) {
      state.operational_state = "offline";
    } else if (!snapshot.ready) {
      state.operational_state = "read_only";
    } else if (operation_) {
      state.operational_state = "executing";
    } else if (driving_) {
      state.operational_state = "velocity";
    } else {
      state.operational_state = "idle";
    }
    state.snapshot = snapshot;
    state.detail = fault_.empty() ? snapshot.detail : fault_;
    // The endpoint copies the state and coalesces the pending slot: the
    // admission never blocks and never runs a sink on this thread.
    observations_->state(state);
  } catch (...) {
    // State publication is observability; it must never break the watchdog
    // and it must never turn into a control fault.
    observations_->recordConstructionFailure(ObservationChannel::state);
  }
}

void ControlCoordinator::close() {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (closing_) {
      // Already marked: still wait for the in-flight operation below.
    } else {
      closing_ = true;
      halt("SHUTDOWN", "Runtime shutting down");
      if (stop_pending_.has_value()) {
        retryStop(true);
      }
    }
  }
  cv_.notify_all();
  // Phase 1 of the two-phase shutdown: wait until no operation is in flight.
  // The operation loop observes closing_ within one 50 ms slice and leaves
  // the adapter in a consistent state. The adapter itself is closed later,
  // in finalize(), after the action worker has been joined: closing it here
  // would race with a worker that is still inside the adapter.
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::milliseconds(2000);
  while (active_operations_.load() > 0 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

void ControlCoordinator::finalize() {
  // Phase 2 of the two-phase shutdown: the action worker has been joined,
  // so no other thread can touch the adapter any more.
  // Adapters must not block in close (no vendor SDK joins); the go2 driver
  // process owns blocking SDK teardown on its own watchdog.
  try {
    adapter_->close();
  } catch (const std::exception & error) {
    log("error", {"Adapter close failed: ", error.what()});
  }
  // The update executor owns resources of its own: a file, a socket or a
  // vendor SDK. They are released here, next to the adapter, and never
  // while an operation may still be inside the executor.
  try {
    ota_->close();
  } catch (const std::exception & error) {
    log("error", {"Update executor close failed: ", error.what()});
  }
}

}  // namespace chaos::runtime
