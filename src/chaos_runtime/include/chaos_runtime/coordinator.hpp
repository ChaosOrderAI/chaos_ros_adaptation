// ROS-free control coordinator: leases, ingress deadlines, velocity guard,
// stop arbitration with bounded backoff and fault latch, action reservation
// and execution, and control trace emission.
//
// Replaces the former Python RobotRuntime. Protocol compatibility is checked
// by the regression suite. Mutable control state lives under one mutex;
// action polling waits release it via a condition variable. No ROS types
// appear here, so the control contract is unit-testable without DDS.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>

#include "chaos_runtime/adapter.hpp"
#include "chaos_runtime/asset_config.hpp"
#include "chaos_runtime/control.hpp"
#include "chaos_runtime/trace.hpp"

namespace chaos::runtime {

struct Clocks {
  // Guard/watchdog steady clock (seconds), used by ControlGuard and for
  // action duration accounting.
  std::function<double()> steady_s;
  // Steady clock in nanoseconds (stop backoff, reservation timestamps).
  std::function<std::int64_t()> mono_ns;
  // System (Unix) clock in nanoseconds; only for ingress deadlines.
  std::function<std::int64_t()> wall_ns;
  // ROS clock in nanoseconds; source-stamp ages and message stamps. Never
  // used for timeout decisions.
  std::function<std::int64_t()> ros_ns;
};

// Typed request/response views keep ROS messages out of the coordinator.
struct LeaseRequestView {
  std::string operation;
  std::string owner;
  std::string token;
  double duration_s = 5.0;
  control::RosTime expires_at{};
};

struct LeaseResponseView {
  bool success = false;
  std::string code = "OK";
  std::string message;
  std::string token;
  double remaining_s = 0.0;
};

struct StopRequestView {
  std::string lease_token;
  std::string request_id;
  control::RosTime expires_at{};
};

struct StopResponseView {
  bool success = false;
  std::string code = "OK";
  std::string message;
};

struct VelocityRequestView {
  std::int64_t stamp_sec = 0;
  std::int64_t stamp_nanosec = 0;
  std::string frame_id;
  std::string lease_token;
  std::string request_id;
  double valid_for_s = 0.0;
  std::array<double, 6> velocity{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
};

struct CommandResultView {
  std::int64_t stamp_ns = 0;
  std::string request_id;
  bool success = false;
  std::string code;
  std::string message;
};

// Action goals share one wire deadline; targets are typed per capability.
struct GoalRequestView {
  std::string lease_token;
  control::RosTime expires_at{};
  // navigation.pose
  std::string target_frame;
  std::array<double, 3> position{0.0, 0.0, 0.0};
  std::array<double, 4> orientation{0.0, 0.0, 0.0, 1.0};
  // flight.takeoff / flight.land
  double altitude_m = 0.0;
};

struct OperationResult {
  bool success = false;
  std::string code = "INTERNAL_ERROR";
  std::string message;
};

// Implemented by the ROS facade for one accepted goal. The coordinator
// drives this synchronously from its execution loop; terminal transitions
// happen only after the stop outcome is known and carry the typed result.
class GoalContext {
public:
  virtual ~GoalContext() = default;
  virtual bool isCancelRequested() = 0;
  virtual bool isActive() = 0;
  virtual void publishFeedback(double progress,
                               const AdapterSnapshot & snapshot) = 0;
  virtual void succeed(const OperationResult & result) = 0;
  virtual void canceled(const OperationResult & result) = 0;
  virtual void abort(const OperationResult & result) = 0;
};

// State view computed by the coordinator; the facade renders RobotState.
struct StateView {
  std::int64_t stamp_ns = 0;
  std::string asset_id;
  std::string model;
  bool connected = false;
  bool ready = false;
  bool pose_valid = false;
  std::string operational_state;
  AdapterSnapshot snapshot;
  std::string detail;
};

class ControlCoordinator {
public:
  // `token` on goal/cancel identifies the goal request instance; the facade
  // uses the address of the rclcpp action Goal shared_ptr, which lives for
  // the whole goal lifetime.
  ControlCoordinator(AssetConfig asset, std::unique_ptr<Adapter> adapter,
                     Clocks clocks, TraceSink & traces);

  // Non-copyable; the mutex and cv own the synchronization.
  ControlCoordinator(const ControlCoordinator &) = delete;
  ControlCoordinator & operator=(const ControlCoordinator &) = delete;
  ~ControlCoordinator();

  // Deadline capture happens at method entry, before the control lock, so
  // lock contention consumes TTL exactly as in the Python runtime.
  LeaseResponseView lease(const LeaseRequestView & request);
  StopResponseView stop(const StopRequestView & request);
  void velocity(const VelocityRequestView & request);

  enum class GoalDecision { kAccept, kReject };
  // `terminal` delivers a typed terminal result to the client. It is called
  // when a reservation is dropped without execution (for example the
  // acceptance handshake expired), so the client never waits forever for a
  // result that cannot arrive. It may be empty; then no such result is
  // produced.
  GoalDecision goal(OperationKind kind, const GoalRequestView & request,
                    const void * goal_token,
                    std::function<void(const OperationResult &)> terminal = {});
  // Binds the accepted goal handle token to the reservation. The handle
  // token must outlive the operation; the facade passes the address of a
  // stable per-goal object.
  void accepted(const void * goal_token, const void * handle_token);
  // Returns kAccept/kReject for a cancel request, mirroring the Python
  // identity checks (reservation token, bound handle token).
  enum class CancelDecision { kAccept, kReject };
  CancelDecision cancel(const void * goal_token, const void * handle_token);
  // Blocking execution loop for an accepted goal. Runs on the action
  // server's dedicated callback-group thread. Control progress does not
  // depend on it: the steady watchdog timer (own callback group) keeps
  // expiring leases, retrying latched stops, and halting on telemetry loss
  // while runOperation is parked in cv waits that never hold the lock.
  OperationResult runOperation(OperationKind kind, const void * goal_token,
                               const void * handle_token, GoalContext & ctx);

  // 20 Hz watchdog: expiry, acceptance-handshake timeout, velocity timeout,
  // stop retry, telemetry-loss halt, adapter update, throttled state sink.
  void tick();

  // Two-phase shutdown.
  //
  // Phase 1 (close): refuse new goals, interrupt the running operation and
  // wait until no operation is in flight. The adapter is still open, so the
  // operation can finish its stop output.
  //
  // Phase 2 (finalize): close the adapter. It must be called only after the
  // action worker has been joined, so the adapter is never touched
  // concurrently.
  void close();
  void finalize();

  bool closing() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return closing_;
  }
  // Locked value snapshot; never exposes the internal reference.
  std::string fault() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return fault_;
  }

  // Sinks are injected once, before the node starts spinning, and frozen.
  struct Sinks {
    std::function<void(const CommandResultView &)> results;
    std::function<void(const StateView &)> state;
    std::function<void(const std::string & level, const std::string & text)>
      log;
  };
  // Throws std::logic_error when called a second time.
  void freezeSinks(Sinks sinks);

private:
  struct Operation {
    std::uint64_t id = 0;
    OperationKind kind;
    std::array<double, 4> target{0.0, 0.0, 0.0, 0.0};
    std::optional<std::pair<std::string, std::string>> interrupted;
    double start_s = 0.0;
    std::string lease_token;
    const void * goal_token = nullptr;
    const void * handle_token = nullptr;
    std::optional<control::CommandDeadline> deadline;
    // Delivers a typed terminal result when the reservation is dropped
    // without execution. Never invoked while the control lock is held.
    std::function<void(const OperationResult &)> terminal;
  };

  struct StopPending {
    bool explicit_stop = false;
    int failures = 0;
    std::int64_t next_attempt_ns = 0;
    std::string request_id;
  };

  [[noreturn]] void rejected(const std::string & code,
                             const std::string & message);
  void requireCapability(OperationKind capability);
  void requireCapability(const std::string & capability);
  void requireReadyLocked();
  void halt(const std::string & code, const std::string & message,
            bool explicit_stop = false, const std::string & request_id = "");
  bool retryStop(bool force);
  // halt() + immediate first retry within one lock scope (lease release and
  // operation finish use this).
  bool retryStopAfterHalt(const std::string & code, const std::string & message);
  void expireLocked();
  // Reads the monotonic clock first and the wall clock second, matching the
  // Python capture order: preemption between the two reads may shorten the
  // allowed interval but must never extend it. Separate statements are
  // required because C++ does not order function-argument evaluation.
  control::CommandDeadline captureDeadline(const control::RosTime & stamp) {
    const std::int64_t monotonic_now_ns = clocks_.mono_ns();
    const std::int64_t wall_now_ns = clocks_.wall_ns();
    return control::CommandDeadline::capture(stamp, wall_now_ns,
                                             monotonic_now_ns);
  }
  // Publishes a command result; observation-channel failures are isolated
  // and never change control outcomes.
  void publishResult(const CommandResultView & result);
  // Caller must hold mutex_. Never blocks on the trace sink beyond a normal
  // publish call; failures are isolated from control.
  void emitTrace(const std::string & command, const std::string & stage,
                 const std::string & code,
                 const std::string & request_id = "",
                 const std::optional<std::array<double, 6>> & velocity = std::nullopt);
  // Finalizes an operation: requests stop output, waits for its outcome,
  // then performs the terminal transition. May rewrite `result` and
  // `terminal` when stop output fails. The state cleanup runs under the
  // control lock; the terminal transition itself is delivered by the caller
  // after the lock is released, so a slow or failing action server can never
  // block the watchdog.
  void finishOperation(Operation & operation, GoalContext & ctx,
                       OperationResult & result, std::string & terminal);
  // Delivers the terminal transition to the action server. Runs outside the
  // control lock; failures are isolated from control decisions.
  void deliverTerminal(GoalContext & ctx, const std::string & terminal,
                        const OperationResult & result);
  void publishStateLocked();
  void log(const std::string & level, const std::string & text) const;

  AssetConfig asset_;
  std::unique_ptr<Adapter> adapter_;
  Clocks clocks_;
  TraceSink & traces_;
  control::Limits limits_;
  control::ControlGuard guard_;
  std::set<std::string> capabilities_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool closing_ = false;
  // Number of operations currently inside the coordinator. close() waits
  // for this to reach zero before the adapter is closed.
  std::atomic<int> active_operations_{0};
  bool driving_ = false;
  std::optional<StopPending> stop_pending_;
  std::string fault_;
  std::shared_ptr<Operation> operation_;
  bool sinks_frozen_ = false;
  Sinks sinks_;
  std::uint64_t trace_sequence_ = 0;
  std::uint64_t next_operation_id_ = 0;
  double last_tick_s_ = 0.0;
  double last_state_s_ = 0.0;
};

}  // namespace chaos::runtime
