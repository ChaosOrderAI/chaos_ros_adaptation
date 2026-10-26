// Internal feedback dispatcher of the native SDK.
//
// See feedback_dispatcher.hpp for the contract and the lifetime rules.

#include "feedback_dispatcher.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace chaos_sdk
{
namespace internal
{

namespace
{

/// Unique ids of the scopes and of the operations of the process.
std::atomic<std::uint64_t> g_next_scope_id{1};
std::atomic<std::uint64_t> g_next_operation_id{1};

/// One queued feedback job.
///
/// The job owns the feedback value and the user callable. The callable is
/// held by pointer so that moving a job never copies and never destroys a
/// user capture.
struct FeedbackJob
{
  /// Scope of the Robot that owns the feedback.
  std::uint64_t scope_id = 0;
  /// Operation (action goal) the feedback belongs to.
  std::uint64_t operation_id = 0;
  /// Cancellation token of the scope.
  std::shared_ptr<ScopeControl> scope_control;
  /// Cancellation token of the operation.
  std::shared_ptr<ObservationControl> observation_control;
  /// Owned feedback value.
  std::shared_ptr<void> payload;
  /// User callable. It receives the payload pointer.
  std::unique_ptr<std::function<void(void *)>> invoke;
};

/// The scopes that currently run a callback.
///
/// The set has one slot per worker, so it never allocates: claiming a job
/// must not allocate under the scheduler lock. A slot holds the id of an
/// active scope and zero marks a free slot. The scope ids of the process
/// start at one, so zero is never a valid scope id.
using ActiveScopeSlots = std::array<std::uint64_t, kFeedbackWorkerCount>;

/// True when a scope already runs a callback on one of the workers.
bool scope_is_active(const ActiveScopeSlots & slots, std::uint64_t scope_id)
{
  if (scope_id == 0) {
    return false;
  }
  for (const std::uint64_t slot : slots) {
    if (slot == scope_id) {
      return true;
    }
  }
  return false;
}

/// Reserve the slot of a scope that starts a callback.
///
/// Returns false when no slot is free. The caller then leaves the job
/// queued: the slot count equals the worker count, so this cannot happen
/// while the invariant holds.
bool claim_active_slot(ActiveScopeSlots & slots, std::uint64_t scope_id)
{
  if (scope_id == 0) {
    return false;
  }
  for (std::uint64_t & slot : slots) {
    if (slot == 0) {
      slot = scope_id;
      return true;
    }
  }
  return false;
}

/// Free the slot of a scope whose callback finished.
void release_active_slot(ActiveScopeSlots & slots, std::uint64_t scope_id)
{
  for (std::uint64_t & slot : slots) {
    if (slot == scope_id) {
      slot = 0;
      return;
    }
  }
}

/// Number of the scopes that currently run a callback.
std::size_t active_slot_count(const ActiveScopeSlots & slots)
{
  std::size_t count = 0;
  for (const std::uint64_t slot : slots) {
    if (slot != 0) {
      ++count;
    }
  }
  return count;
}

/// True when the scope or the operation of a job was revoked.
bool job_is_revoked(const FeedbackJob & job)
{
  if (job.scope_control &&
    job.scope_control->revoked.load(std::memory_order_acquire))
  {
    return true;
  }
  if (job.observation_control &&
    job.observation_control->cancelled.load(std::memory_order_acquire))
  {
    return true;
  }
  return false;
}

/// Destroy the user callable and the feedback value of a job.
///
/// The callable and the value are moved out of the job first, so the
/// destruction of a user capture happens outside the scheduler lock and the
/// job never holds a user callable any more.
void destroy_job(std::optional<FeedbackJob> & job)
{
  if (!job.has_value()) {
    return;
  }
  std::unique_ptr<std::function<void(void *)>> invoke_holder =
    std::move(job->invoke);
  std::shared_ptr<void> payload_holder = std::move(job->payload);
  job.reset();
  invoke_holder.reset();
  payload_holder.reset();
}

/// Fixed storage of the jobs that leave the scheduler.
///
/// The buffer never allocates: the global pending budget bounds the number
/// of the jobs that can leave the queue in one pass, so the capacity is
/// exact. Taking a job out of the queue under the scheduler lock must not
/// allocate, and a user capture must never be destroyed under the lock.
class DiscardBuffer
{
public:
  /// Capacity of the buffer. It equals the global pending budget.
  static constexpr std::size_t kCapacity = kMaxPendingFeedbackJobs;

  /// True when the buffer cannot hold another job.
  bool full() const {return count_ >= kCapacity;}

  /// Move a job into the buffer.
  ///
  /// The caller must have checked that the buffer is not full. The move
  /// never allocates and never destroys a user capture.
  void put(FeedbackJob && job)
  {
    slots_[count_].emplace(std::move(job));
    ++count_;
  }

  /// Destroy the buffered jobs.
  ///
  /// This must run without the scheduler lock: destroying a capture may
  /// release a Robot or call back into the SDK.
  void clear()
  {
    for (std::size_t index = 0; index < count_; ++index) {
      destroy_job(slots_[index]);
      slots_[index].reset();
    }
    count_ = 0;
  }

  DiscardBuffer() = default;
  DiscardBuffer(const DiscardBuffer &) = delete;
  DiscardBuffer & operator=(const DiscardBuffer &) = delete;

private:
  std::array<std::optional<FeedbackJob>, kCapacity> slots_{};
  std::size_t count_ = 0;
};

}  // namespace

const char * submit_result_name(SubmitResult result)
{
  switch (result) {
    case SubmitResult::Accepted:
      return "accepted";
    case SubmitResult::Coalesced:
      return "coalesced";
    case SubmitResult::Dropped:
      return "dropped";
    case SubmitResult::Revoked:
      return "revoked";
  }
  return "unknown";
}

/// Shared state of the worker pool.
///
/// The state is owned by the dispatcher facade and by every worker, so it
/// stays valid even when the facade and every Robot disappear.
struct FeedbackDispatcher::State
{
  /// Scheduler lock. It is never held while a user callable runs.
  std::mutex mutex;
  /// Signalled when work arrives or when a scope is released.
  std::condition_variable cv;
  /// The single global queue of the pending jobs.
  std::deque<FeedbackJob> jobs;
  /// Scopes that currently run a callback. Bounded by the worker count.
  ActiveScopeSlots active_scopes{};
  /// Set when the workers must stop.
  bool stopping = false;

  std::atomic<std::size_t> worker_count{0};
  std::atomic<std::size_t> accepted{0};
  std::atomic<std::size_t> coalesced{0};
  std::atomic<std::size_t> dropped{0};
  std::atomic<std::size_t> revoked{0};
  std::atomic<std::size_t> lock_contention_drops{0};
  std::atomic<std::size_t> callback_exceptions{0};
};

FeedbackDispatcher::FeedbackDispatcher()
: state_(std::make_shared<State>())
{
  std::vector<std::thread> workers;
  workers.reserve(kFeedbackWorkerCount);

  try {
    for (std::size_t index = 0; index < kFeedbackWorkerCount; ++index) {
      // The worker owns the shared state, so it survives the facade.
      workers.emplace_back([state = state_]() {worker_loop(state);});
      state_->worker_count.fetch_add(1);
    }
  } catch (...) {
    // A partial start must not leave a worker behind: stop and join the
    // workers that already run before the failure is reported.
    {
      const std::lock_guard<std::mutex> guard(state_->mutex);
      state_->stopping = true;
    }
    state_->cv.notify_all();
    for (std::thread & worker : workers) {
      if (worker.joinable()) {
        worker.join();
      }
    }
    state_->worker_count.store(0);
    throw;
  }

  // The pool lives as long as the process: the workers are detached and keep
  // the shared state alive on their own.
  for (std::thread & worker : workers) {
    worker.detach();
  }
}

// The facade is never destroyed: instance() leaks it on purpose.
FeedbackDispatcher::~FeedbackDispatcher() = default;

FeedbackDispatcher & FeedbackDispatcher::instance()
{
  // The facade is allocated once and never freed. A user capture that is
  // released while the static destructors of the process run may call back
  // into the dispatcher, and it must find a valid dispatcher instead of a
  // destroyed one. The pointer itself is a trivially destructible static, so
  // it stays readable until the process exits.
  static FeedbackDispatcher * const facade = new FeedbackDispatcher();
  return *facade;
}

Scope FeedbackDispatcher::create_scope()
{
  const std::uint64_t id = g_next_scope_id.fetch_add(1, std::memory_order_relaxed);
  std::shared_ptr<ScopeControl> control = std::make_shared<ScopeControl>();
  control->id = id;
  return Scope(id, std::move(control));
}

Observation FeedbackDispatcher::create_observation()
{
  const std::uint64_t id = g_next_operation_id.fetch_add(1, std::memory_order_relaxed);
  std::shared_ptr<ObservationControl> control = std::make_shared<ObservationControl>();
  control->operation_id = id;
  return Observation(id, std::move(control));
}

SubmitResult FeedbackDispatcher::submit(
  const Scope & scope,
  const Observation & observation,
  std::shared_ptr<void> payload,
  std::function<void(void *)> invoke)
{
  if (!scope.valid() || !observation.control()) {
    state_->revoked.fetch_add(1, std::memory_order_relaxed);
    return SubmitResult::Revoked;
  }
  if (scope.revoked() || observation.cancelled()) {
    state_->revoked.fetch_add(1, std::memory_order_relaxed);
    return SubmitResult::Revoked;
  }

  // The user callable is wrapped before the lock is taken: neither a copy nor
  // the destruction of a user callable may happen under the scheduler lock.
  std::unique_ptr<std::function<void(void *)>> invoke_holder;
  try {
    invoke_holder = std::make_unique<std::function<void(void *)>>(std::move(invoke));
  } catch (...) {
    // The job was not admitted: the sample is dropped and counted. The
    // scheduler lock is not held and no user capture is destroyed here.
    state_->dropped.fetch_add(1, std::memory_order_relaxed);
    return SubmitResult::Dropped;
  }

  FeedbackJob job;
  job.scope_id = scope.id();
  job.operation_id = observation.operation_id();
  job.scope_control = scope.control();
  job.observation_control = observation.control();
  job.payload = std::move(payload);
  job.invoke = std::move(invoke_holder);

  // The job that a submission replaces is kept in an optional that lives
  // outside the scheduler lock: the destruction of a user capture must never
  // happen under the lock.
  std::optional<FeedbackJob> replaced;

  SubmitResult result = SubmitResult::Dropped;

  {
    // The enqueue never waits for the scheduler lock.
    std::unique_lock<std::mutex> lock(state_->mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      state_->lock_contention_drops.fetch_add(1, std::memory_order_relaxed);
      state_->dropped.fetch_add(1, std::memory_order_relaxed);
      return SubmitResult::Dropped;
    }

    if (scope.revoked() || observation.cancelled()) {
      state_->revoked.fetch_add(1, std::memory_order_relaxed);
      result = SubmitResult::Revoked;
    } else {
      auto pending = std::find_if(
        state_->jobs.begin(), state_->jobs.end(),
        [&job](const FeedbackJob & queued) {
          return queued.scope_id == job.scope_id &&
                 queued.operation_id == job.operation_id;
        });

      if (pending != state_->jobs.end()) {
        // The newest sample replaces the pending one of the same operation.
        // The old job is moved into the optional: nothing is destroyed and
        // nothing is allocated under the lock.
        replaced.emplace(std::move(*pending));
        *pending = std::move(job);
        state_->coalesced.fetch_add(1, std::memory_order_relaxed);
        result = SubmitResult::Coalesced;
      } else if (state_->jobs.size() >= kMaxPendingFeedbackJobs) {
        state_->dropped.fetch_add(1, std::memory_order_relaxed);
        result = SubmitResult::Dropped;
      } else {
        bool queued = false;
        try {
          state_->jobs.push_back(std::move(job));
          queued = true;
        } catch (...) {
          // The queue could not grow: the sample is dropped and counted. The
          // job still owns the user callable and is destroyed outside the
          // lock.
        }
        if (queued) {
          state_->accepted.fetch_add(1, std::memory_order_relaxed);
          result = SubmitResult::Accepted;
        } else {
          state_->dropped.fetch_add(1, std::memory_order_relaxed);
          result = SubmitResult::Dropped;
        }
      }
    }
  }

  // The replaced job holds the previous user callable: destroying it may
  // release a Robot, so it must not happen under the scheduler lock.
  destroy_job(replaced);

  if (result == SubmitResult::Accepted || result == SubmitResult::Coalesced) {
    state_->cv.notify_one();
  }
  return result;
}

std::size_t FeedbackDispatcher::revoke_scope(const Scope & scope)
{
  if (!scope.valid()) {
    return 0;
  }

  // The flag is raised before the lock is taken: a worker that claims a job
  // afterwards sees the revocation and discards the job instead of running
  // it. A job that was claimed before is in flight and may still run.
  scope.control()->revoked.store(true, std::memory_order_release);

  // The buffer lives outside the scheduler lock: the discarded jobs hold
  // user callables that must not be destroyed under the lock.
  DiscardBuffer discarded;

  std::size_t removed = 0;
  {
    const std::lock_guard<std::mutex> guard(state_->mutex);
    for (auto it = state_->jobs.begin(); it != state_->jobs.end(); ) {
      if (it->scope_id == scope.id()) {
        if (discarded.full()) {
          // The buffer holds one slot per pending job, so this cannot happen
          // while the budget holds. The remaining jobs stay queued.
          break;
        }
        discarded.put(std::move(*it));
        it = state_->jobs.erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
  }

  // The discarded jobs hold user callables: destroy them without the lock.
  discarded.clear();
  state_->cv.notify_all();
  return removed;
}

std::size_t FeedbackDispatcher::retire_operation(const Observation & observation)
{
  if (!observation.control()) {
    return 0;
  }

  observation.cancel();

  // The buffer lives outside the scheduler lock: the discarded jobs hold
  // user callables that must not be destroyed under the lock.
  DiscardBuffer discarded;

  std::size_t removed = 0;
  {
    const std::lock_guard<std::mutex> guard(state_->mutex);
    for (auto it = state_->jobs.begin(); it != state_->jobs.end(); ) {
      if (it->observation_control == observation.control()) {
        if (discarded.full()) {
          // The buffer holds one slot per pending job, so this cannot happen
          // while the budget holds. The remaining jobs stay queued.
          break;
        }
        discarded.put(std::move(*it));
        it = state_->jobs.erase(it);
        ++removed;
      } else {
        ++it;
      }
    }
  }

  // The discarded jobs hold user callables: destroy them without the lock.
  discarded.clear();
  state_->cv.notify_all();
  return removed;
}

void FeedbackDispatcher::worker_loop(std::shared_ptr<State> state)
{
  // The buffer of the discarded jobs lives outside the scheduler lock: the
  // discarded jobs hold user callables that must not be destroyed under it.
  DiscardBuffer discarded;

  for (;;) {
    // The claimed job lives outside the scheduler lock as well: the user
    // callable is destroyed after the lock is released.
    std::optional<FeedbackJob> claimed;

    {
      std::unique_lock<std::mutex> lock(state->mutex);
      state->cv.wait(
        lock, [&state]() {
          if (state->stopping) {
            return true;
          }
          for (const FeedbackJob & queued : state->jobs) {
            if (!scope_is_active(state->active_scopes, queued.scope_id)) {
              return true;
            }
          }
          return false;
        });

      if (state->stopping) {
        break;
      }

      // Claim the first job whose scope has no active callback. A worker
      // never parks on a per-scope mutex: one blocked callback may hold one
      // worker only.
      for (auto it = state->jobs.begin(); it != state->jobs.end(); ) {
        if (scope_is_active(state->active_scopes, it->scope_id)) {
          ++it;
          continue;
        }
        if (job_is_revoked(*it)) {
          if (discarded.full()) {
            // The buffer holds one slot per pending job, so this cannot
            // happen while the budget holds. The job stays queued.
            break;
          }
          discarded.put(std::move(*it));
          it = state->jobs.erase(it);
          continue;
        }
        const std::uint64_t scope_id = it->scope_id;
        if (!claim_active_slot(state->active_scopes, scope_id)) {
          // No free worker slot: the job stays queued.
          ++it;
          continue;
        }
        // The job is claimed without allocating: it is moved into the
        // optional that lives outside the lock, and the slot of the scope is
        // reserved without touching a container.
        claimed.emplace(std::move(*it));
        state->jobs.erase(it);
        break;
      }
    }

    // The discarded jobs hold user callables: destroy them without the lock.
    discarded.clear();

    if (!claimed.has_value()) {
      continue;
    }

    const std::uint64_t scope_id = claimed->scope_id;
    try {
      if (claimed->invoke) {
        (*claimed->invoke)(claimed->payload.get());
      }
    } catch (...) {
      // A throwing user callback never kills the worker.
      state->callback_exceptions.fetch_add(1, std::memory_order_relaxed);
    }

    // The user callable and the feedback value are destroyed without the
    // lock: a capture may release a Robot or call back into the SDK.
    destroy_job(claimed);

    {
      const std::lock_guard<std::mutex> guard(state->mutex);
      release_active_slot(state->active_scopes, scope_id);
    }
    state->cv.notify_all();
  }
}

FeedbackDispatcher::Counters FeedbackDispatcher::counters() const
{
  Counters snapshot;
  {
    const std::lock_guard<std::mutex> guard(state_->mutex);
    snapshot.pending = state_->jobs.size();
    snapshot.active = active_slot_count(state_->active_scopes);
  }
  snapshot.accepted = state_->accepted.load(std::memory_order_relaxed);
  snapshot.coalesced = state_->coalesced.load(std::memory_order_relaxed);
  snapshot.dropped = state_->dropped.load(std::memory_order_relaxed);
  snapshot.revoked = state_->revoked.load(std::memory_order_relaxed);
  snapshot.lock_contention_drops =
    state_->lock_contention_drops.load(std::memory_order_relaxed);
  snapshot.callback_exceptions =
    state_->callback_exceptions.load(std::memory_order_relaxed);
  snapshot.worker_count = state_->worker_count.load(std::memory_order_relaxed);
  return snapshot;
}

std::size_t FeedbackDispatcher::pending_count() const
{
  const std::lock_guard<std::mutex> guard(state_->mutex);
  return state_->jobs.size();
}

std::size_t FeedbackDispatcher::active_count() const
{
  const std::lock_guard<std::mutex> guard(state_->mutex);
  return active_slot_count(state_->active_scopes);
}

std::size_t FeedbackDispatcher::accepted_count() const
{
  return state_->accepted.load(std::memory_order_relaxed);
}

std::size_t FeedbackDispatcher::coalesced_count() const
{
  return state_->coalesced.load(std::memory_order_relaxed);
}

std::size_t FeedbackDispatcher::dropped_count() const
{
  return state_->dropped.load(std::memory_order_relaxed);
}

std::size_t FeedbackDispatcher::revoked_count() const
{
  return state_->revoked.load(std::memory_order_relaxed);
}

std::size_t FeedbackDispatcher::lock_contention_drops() const
{
  return state_->lock_contention_drops.load(std::memory_order_relaxed);
}

std::size_t FeedbackDispatcher::callback_exception_count() const
{
  return state_->callback_exceptions.load(std::memory_order_relaxed);
}

std::size_t FeedbackDispatcher::worker_count() const
{
  return state_->worker_count.load(std::memory_order_relaxed);
}

std::size_t FeedbackDispatcher::retained_scope_count() const
{
  // The dispatcher keeps no registry of scopes: the state of a scope lives in
  // the scope token and disappears with the last copy of it.
  return 0;
}

}  // namespace internal
}  // namespace chaos_sdk
