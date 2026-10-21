// Port of chaos_runtime/node.py (RobotRuntime). The Python file is the
// behavior oracle; keep ordering and failure semantics in sync with it.
#include "chaos_runtime/coordinator.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

#include "chaos_core/clock.hpp"
#include "chaos_core/time.hpp"

namespace chaos::runtime {

namespace {
constexpr double kTickWait = 0.05;  // cv wait slice; not a deadline
constexpr double kStatePublishInterval = 0.1;
constexpr double kAcceptanceHandshakeTimeout = 2.0;

std::string capabilityName(OperationKind kind) {
  switch (kind) {
    case OperationKind::kNavigatePose:
      return "navigation.pose";
    case OperationKind::kTakeoff:
      return "flight.takeoff";
    case OperationKind::kLand:
      return "flight.land";
  }
  return "UNKNOWN";
}

bool validRequestId(const std::string & value) {
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

bool validTraceCode(const std::string & value) {
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

ControlCoordinator::ControlCoordinator(AssetConfig asset,
                                       std::unique_ptr<Adapter> adapter,
                                       Clocks clocks, TraceSink & traces)
    : asset_(std::move(asset)),
      adapter_(std::move(adapter)),
      clocks_(std::move(clocks)),
      traces_(traces),
      limits_(asset_.limits),
      guard_(asset_.limits,
             clocks_.steady_s ? clocks_.steady_s : chaos::monotonicSeconds),
      capabilities_(asset_.capabilities.begin(), asset_.capabilities.end()) {
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
  last_tick_s_ = clocks_.steady_s();
  last_state_s_ = 0.0;
}

ControlCoordinator::~ControlCoordinator() { close(); }

void ControlCoordinator::rejected(const std::string & code,
                                  const std::string & message) {
  throw chaos::Rejected(code, message);
}

void ControlCoordinator::log(const std::string & level,
                             const std::string & text) const {
  if (sinks_.log) {
    try {
      sinks_.log(level, text);
    } catch (...) {
    }
  }
}

void ControlCoordinator::freezeSinks(Sinks sinks) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (sinks_frozen_) {
    throw std::logic_error("coordinator sinks are frozen");
  }
  sinks_ = std::move(sinks);
  sinks_frozen_ = true;
}

void ControlCoordinator::publishResult(const CommandResultView & result) {
  if (!sinks_.results) {
    return;
  }
  try {
    sinks_.results(result);
  } catch (...) {
    // A failed result channel must not change control decisions; the
    // command outcome is still enforced by the guard and watchdog.
    log("warning", "Command result publication failed; result dropped");
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
  const std::string & command, const std::string & stage,
  const std::string & code, const std::string & request_id,
  const std::optional<std::array<double, 6>> & velocity) {
  // Best-effort evidence, isolated from control decisions and driver
  // outcomes. The sequence advances before construction/publication so a
  // missing event is visible to an offline consumer. No authorization
  // values or error text are copied.
  TraceEvent event;
  event.sequence = ++trace_sequence_;
  event.stamp_ns = clocks_.ros_ns();
  event.monotonic_ns = clocks_.mono_ns();
  event.frame = asset_.frames.base;
  event.asset_id = asset_.id;
  event.request_id = validRequestId(request_id) ? request_id : std::string();
  event.command = command;
  event.stage = stage;
  event.code = validTraceCode(code) ? code : "UNKNOWN";
  if (velocity.has_value() && command == "motion.velocity" &&
      stage == "issued") {
    TraceVelocity sample;
    sample.valid = true;
    sample.values = *velocity;
    // Sample the guard after the event timestamp: any sampling delay
    // shortens the exported interval, rather than extending its lease.
    sample.valid_for_s = guard_.leaseActive()
                           ? std::max(0.0, std::min(guard_.velocityExpires(),
                                                    guard_.leaseExpires()) -
                                             clocks_.steady_s())
                           : 0.0;
    event.velocity = sample;
  }
  try {
    traces_.publish(event);
  } catch (...) {
    // A failed observation channel must not turn an accepted command into a
    // rejected one, or suppress watchdog/stop output.
    log("warning", "Control trace publication failed; event dropped");
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
  std::lock_guard<std::mutex> guard(mutex_);
  try {
    expireLocked();
    requireCapability("motion.velocity");
    requireReadyLocked();
    if (operation_) {
      rejected("BUSY", "An action is active; stop or cancel it first");
    }
    adapter_->validateVelocity(request.velocity);
    std::int64_t stamp_ns = 0;
    double stamp_s = 0.0;
    if (chaos::rosStampToNs(request.stamp_sec, request.stamp_nanosec,
                            stamp_ns)) {
      stamp_s = stamp_ns / 1e9;
    }
    guard_.velocity(request.lease_token, stamp_s, clocks_.ros_ns() / 1e9,
                    request.valid_for_s, request.frame_id, asset_.frames.base,
                    request.velocity);
    adapter_->setVelocity(request.velocity);
    driving_ = true;
    emitTrace("motion.velocity", "issued", "RUNTIME_ISSUED", request.request_id,
              request.velocity);
    {
      CommandResultView result;
      result.stamp_ns = clocks_.ros_ns();
      result.request_id = request.request_id;
      result.success = true;
      result.code = "ACCEPTED";
      result.message = "Velocity accepted until command expiry";
      publishResult(result);
    }
  } catch (const chaos::Rejected & error) {
    emitTrace("motion.velocity", "rejected", error.code(), request.request_id);
    {
      CommandResultView result;
      result.stamp_ns = clocks_.ros_ns();
      result.request_id = request.request_id;
      result.success = false;
      result.code = error.code();
      result.message = error.message();
      publishResult(result);
    }
  } catch (const std::exception & error) {
    fault_ = error.what();
    halt("DRIVER_ERROR", fault_);
    emitTrace("motion.velocity", "rejected", "DRIVER_ERROR",
              request.request_id);
    {
      CommandResultView result;
      result.stamp_ns = clocks_.ros_ns();
      result.request_id = request.request_id;
      result.success = false;
      result.code = "DRIVER_ERROR";
      result.message = fault_;
      publishResult(result);
    }
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
    log("warning", std::string("Rejected ") + capabilityName(kind) + ": " +
                    error.what());
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
    log("warning", "Rejected " + capabilityName(kind) + ": " + error.code() +
                    ": " + error.message());
    return GoalDecision::kReject;
  } catch (const std::exception & error) {
    log("error", "Failed to validate " + capabilityName(kind) +
                  ": " + std::string(error.what()));
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
        emitTrace(capabilityName(kind), "invalidated", "OPERATION_STARTING");
        adapter_->startOperation(kind, mine->target);
      }
    }
    while (true) {
      bool has_feedback = false;
      double progress = 0.0;
      AdapterSnapshot snapshot;
      {
        std::lock_guard<std::mutex> guard(mutex_);
        if (closing_) {
          result = {false, "SHUTDOWN", "Runtime stopped"};
          break;
        }
        if (ctx.isCancelRequested()) {
          halt("CANCELED", "Action canceled by client");
          terminal = "canceled";
          result = {false, "CANCELED", "Action canceled by client"};
          break;
        }
        if (mine->interrupted.has_value()) {
          const auto & [code, message] = *mine->interrupted;
          // Cancellation is accepted before ROS exposes CANCELING. Wait one
          // iteration for the accepted transition.
          if (code != "CANCELED") {
            rejected(code, message);
          }
        } else {
          guard_.require(mine->lease_token);
          requireReadyLocked();
          if (clocks_.steady_s() - mine->start_s > limits_.action_timeout_s) {
            rejected("TIMEOUT", "Action exceeded action_timeout_s");
          }
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
      if (has_feedback) {
        try {
          ctx.publishFeedback(progress, snapshot);
        } catch (...) {
          // Feedback is observability; a failing channel must not abort
          // the device operation.
        }
      }
      std::unique_lock<std::mutex> wait(mutex_);
      cv_.wait_for(wait, std::chrono::duration<double>(kTickWait));
    }
  } catch (const chaos::Rejected & error) {
    result = {false, error.code(), error.message()};
  } catch (const std::exception & error) {
    result = {false, "DRIVER_ERROR", error.what()};
  } catch (...) {
    result = {false, "DRIVER_ERROR", "unknown operation failure"};
  }
  // Also finish failures raised outside the polling loop (shutdown, expiry,
  // interrupts). Identity and active-state checks make repeated finalization
  // harmless.
  {
    std::lock_guard<std::mutex> guard(mutex_);
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
      log("error", "Adapter error: " + fault_);
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
  if (!sinks_.state) {
    return;
  }
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
  try {
    if (sinks_.state) {
      sinks_.state(state);
    }
  } catch (...) {
    // State publication is observability; it must never break the watchdog.
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
    log("error", "Adapter close failed: " + std::string(error.what()));
  }
}

}  // namespace chaos::runtime
