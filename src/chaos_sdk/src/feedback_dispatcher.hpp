// Internal feedback dispatcher of the native SDK.
//
// The dispatcher owns the execution of the user feedback callbacks. A ROS
// feedback callback copies its value into a job and submits it; a fixed
// process-wide worker pool executes the job later. A slow or blocking user
// callback therefore never runs on the ROS executor of the SDK: the executor
// only performs a value copy and an admission check.
//
// This header is internal to the SDK implementation: it lives in src/, it is
// not installed and it is not part of the public SDK API. It depends on the
// C++ standard library only.
//
// Ownership and lifetime:
// * The worker pool is a process-lifetime resource. It is created exactly
//   once and shared by every Robot of the process. Robot::close() never
//   stops it, and no worker is ever created per Robot, per goal or per
//   message.
// * The dispatcher facade is never destroyed. The singleton is a pointer that
//   is allocated once and kept for the whole process, so a user capture that
//   is released while the static destructors run still finds a valid
//   dispatcher when it calls back into the SDK.
// * A job owns the feedback value and the user callable. Both are moved out
//   of the scheduler before they are destroyed, so a capture that releases a
//   Robot or calls back into the SDK never runs under the scheduler lock.
// * A job holds a scope token and an observation token. Both are small
//   cancellation flags: they carry no Robot, no Impl, no node, no action
//   client and no goal handle.
// * A user callable is never force-cancelled. A blocked callback keeps its
//   worker and its captures until it returns. A callback that captures a
//   Robot strongly keeps that Robot alive for the same time, so callers
//   should prefer weak or shared ownership in their captures.
// * The dispatcher owns no user capture. The lifetime of the objects that a
//   capture refers to stays the responsibility of the caller: the dispatcher
//   only guarantees that the capture is destroyed outside its own locks.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

namespace chaos_sdk
{
namespace internal
{

/// Maximum number of queued feedback jobs of the whole process.
///
/// The budget covers every Robot of the process: there is one global queue,
/// so the per-Robot mailboxes cannot grow past it.
constexpr std::size_t kMaxPendingFeedbackJobs = 64;

/// Number of feedback worker threads of the whole process.
constexpr std::size_t kFeedbackWorkerCount = 2;

/// Outcome of a feedback submission.
enum class SubmitResult
{
  /// The feedback was queued as a new job.
  Accepted,
  /// The newest pending job of the same (scope, operation) was replaced.
  Coalesced,
  /// The feedback was rejected: the global budget was exhausted or the
  /// scheduler lock was contended. The feedback is not queued anywhere.
  Dropped,
  /// The scope or the observation was already revoked, so the feedback has
  /// no valid target left.
  Revoked,
};

/// Short stable name of a submission outcome.
const char * submit_result_name(SubmitResult result);

/// Shared cancellation state of one Robot scope.
///
/// The state is a plain flag: it holds no SDK, ROS or Robot object.
struct ScopeControl
{
  /// Unique id of the scope.
  std::uint64_t id = 0;
  /// Set when the scope is closed. A queued job of a revoked scope is
  /// discarded instead of being executed.
  std::atomic<bool> revoked{false};
};

/// Unique lightweight scope of one Robot.
///
/// The scope is an id plus a small shared flag. Copies of a scope share the
/// same state, and the state disappears with the last copy: the dispatcher
/// keeps no registry of scopes.
class Scope
{
public:
  Scope() = default;

  /// Unique id of the scope, zero for an empty scope.
  std::uint64_t id() const {return id_;}

  /// True when the scope refers to a real Robot.
  bool valid() const {return control_ != nullptr;}

  /// True when the scope was revoked.
  bool revoked() const
  {
    return control_ && control_->revoked.load(std::memory_order_acquire);
  }

  /// Shared cancellation state of the scope.
  const std::shared_ptr<ScopeControl> & control() const {return control_;}

private:
  friend class FeedbackDispatcher;

  Scope(std::uint64_t id, std::shared_ptr<ScopeControl> control)
  : id_(id), control_(std::move(control)) {}

  std::uint64_t id_ = 0;
  std::shared_ptr<ScopeControl> control_;
};

/// Shared cancellation state of one operation (an action goal).
struct ObservationControl
{
  /// Unique id of the operation inside its scope.
  std::uint64_t operation_id = 0;
  /// Set when the operation is retired. A queued job of a retired operation
  /// is discarded instead of being executed.
  std::atomic<bool> cancelled{false};
};

/// Lightweight cancellable observation of one operation.
///
/// The observation is the operation id plus a small shared flag. It is the
/// token the SDK retires when an action goal reaches its terminal state,
/// times out or is closed.
class Observation
{
public:
  Observation() = default;

  /// Unique id of the observed operation.
  std::uint64_t operation_id() const {return operation_id_;}

  /// True when the operation was retired.
  bool cancelled() const
  {
    return control_ && control_->cancelled.load(std::memory_order_acquire);
  }

  /// Retire the operation. This only raises the flag; use
  /// FeedbackDispatcher::retire_operation() to drop the queued work as well.
  void cancel() const
  {
    if (control_) {
      control_->cancelled.store(true, std::memory_order_release);
    }
  }

  /// Shared cancellation state of the observation.
  const std::shared_ptr<ObservationControl> & control() const {return control_;}

private:
  friend class FeedbackDispatcher;

  Observation(std::uint64_t operation_id, std::shared_ptr<ObservationControl> control)
  : operation_id_(operation_id), control_(std::move(control)) {}

  std::uint64_t operation_id_ = 0;
  std::shared_ptr<ObservationControl> control_;
};

/// Process-wide dispatcher of the user feedback callbacks.
///
/// The dispatcher is the only owner of the feedback worker pool. It is a
/// process-lifetime resource: the facade is created once and is never
/// destroyed, and its two workers are detached and keep the shared state
/// alive on their own, so the facade and every Robot may disappear without
/// stopping them.
///
/// The facade is intentionally leaked. A user capture may be released while
/// the static destructors of the process run, and such a capture may call
/// back into the dispatcher; a destroyed facade would turn that call into
/// undefined behaviour.
class FeedbackDispatcher
{
public:
  /// Snapshot of the internal counters. Every value is bounded by the
  /// documented limits, except the lifetime counters.
  struct Counters
  {
    /// Jobs that are queued and not claimed by a worker yet.
    std::size_t pending = 0;
    /// Jobs that a worker claimed and that are running right now.
    std::size_t active = 0;
    /// Submissions that queued a new job.
    std::size_t accepted = 0;
    /// Submissions that replaced a pending job of the same operation.
    std::size_t coalesced = 0;
    /// Submissions that were rejected because of the budget or of lock
    /// contention.
    std::size_t dropped = 0;
    /// Submissions that were refused because the scope or the observation
    /// was already revoked.
    std::size_t revoked = 0;
    /// Submissions that were dropped because the scheduler lock was busy.
    std::size_t lock_contention_drops = 0;
    /// User callbacks that threw. A throwing callback never kills a worker.
    std::size_t callback_exceptions = 0;
    /// Live feedback workers of the process.
    std::size_t worker_count = 0;
  };

  FeedbackDispatcher(const FeedbackDispatcher &) = delete;
  FeedbackDispatcher & operator=(const FeedbackDispatcher &) = delete;

  /// The dispatcher of the process.
  ///
  /// The first call creates the two workers. A failure while the workers are
  /// started cleans up the already started ones, so a retry never
  /// accumulates workers.
  ///
  /// The facade is never destroyed: it is allocated once and kept for the
  /// whole process. A user capture that is released while the static
  /// destructors run may call back into the dispatcher, and it must find a
  /// valid dispatcher instead of a destroyed one.
  static FeedbackDispatcher & instance();

  /// Create a new unique scope for a Robot.
  Scope create_scope();

  /// Create a new observation for one operation of a scope.
  Observation create_observation();

  /// Submit one feedback sample.
  ///
  /// The submission copies nothing and waits for nothing: it either queues
  /// the job, replaces the pending job of the same operation, or refuses the
  /// sample. The scheduler lock is taken without blocking, so a contended
  /// submission is dropped and counted instead of delaying the caller.
  ///
  /// ``payload`` is the owned feedback value and ``invoke`` is the user
  /// callable. The callable receives the payload pointer. Both belong to the
  /// job as soon as the job is queued, and both are destroyed outside the
  /// scheduler lock.
  SubmitResult submit(
    const Scope & scope,
    const Observation & observation,
    std::shared_ptr<void> payload,
    std::function<void(void *)> invoke);

  /// Revoke a scope and discard its queued work.
  ///
  /// A job that a worker already claimed is in flight and may still run.
  /// Returns the number of discarded jobs.
  std::size_t revoke_scope(const Scope & scope);

  /// Retire one operation and discard its queued work.
  ///
  /// A job that a worker already claimed is in flight and may still run.
  /// Returns the number of discarded jobs.
  std::size_t retire_operation(const Observation & observation);

  /// Snapshot of the counters.
  Counters counters() const;

  /// Jobs that are queued and not claimed by a worker yet.
  std::size_t pending_count() const;

  /// Jobs that a worker claimed and that are running right now.
  std::size_t active_count() const;

  /// Submissions that queued a new job.
  std::size_t accepted_count() const;

  /// Submissions that replaced a pending job of the same operation.
  std::size_t coalesced_count() const;

  /// Submissions that were rejected because of the budget or of lock
  /// contention.
  std::size_t dropped_count() const;

  /// Submissions that were refused because the scope or the observation was
  /// already revoked.
  std::size_t revoked_count() const;

  /// Submissions that were dropped because the scheduler lock was busy.
  std::size_t lock_contention_drops() const;

  /// User callbacks that threw.
  std::size_t callback_exception_count() const;

  /// Live feedback workers of the process.
  std::size_t worker_count() const;

  /// Number of scopes the dispatcher keeps in a registry.
  ///
  /// The dispatcher keeps none: the state of a scope lives in the scope
  /// token, so a created and revoked scope leaves nothing behind. The value
  /// is always zero and exists to make the invariant observable.
  std::size_t retained_scope_count() const;

  /// Maximum number of queued jobs of the process.
  static constexpr std::size_t max_pending_jobs() {return kMaxPendingFeedbackJobs;}

  /// Number of feedback workers of the process.
  static constexpr std::size_t worker_threads() {return kFeedbackWorkerCount;}

private:
  FeedbackDispatcher();

  /// The facade is never destroyed: instance() allocates it once and leaks
  /// it on purpose, so the destructor exists only to complete the type.
  ~FeedbackDispatcher();

  struct State;

  /// Entry point of a worker. The worker owns its own shared state, so the
  /// dispatcher facade may disappear while the worker is still running.
  static void worker_loop(std::shared_ptr<State> state);

  std::shared_ptr<State> state_;
};

}  // namespace internal
}  // namespace chaos_sdk
