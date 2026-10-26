// Focused unit tests of the internal feedback dispatcher.
//
// The dispatcher is standard-library only, so this test links the dispatcher
// directly and needs no ROS runtime.
//
// Every test releases its barriers on the way out, also when an assertion
// fails, and it never leaves a worker blocked: the objects that a job owns
// are heap allocated and stay valid while the job is queued.
//
// The dispatcher drops a submission when the scheduler lock is contended, so
// the setup steps that only need an admitted job retry it. The tests that
// check a semantic outcome of a submission (the capacity, the revocation and
// the coalescing) submit directly while the scheduler is quiescent.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "feedback_dispatcher.hpp"

using namespace std::chrono_literals;

namespace
{

using chaos_sdk::internal::FeedbackDispatcher;
using chaos_sdk::internal::kFeedbackWorkerCount;
using chaos_sdk::internal::kMaxPendingFeedbackJobs;
using chaos_sdk::internal::Observation;
using chaos_sdk::internal::Scope;
using chaos_sdk::internal::SubmitResult;

/// One shot latch that another thread can release.
class Latch
{
public:
  void release()
  {
    {
      const std::lock_guard<std::mutex> guard(mutex_);
      released_ = true;
    }
    cv_.notify_all();
  }

  bool wait(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this]() {return released_;});
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool released_ = false;
};

/// Counts how often a callback started.
class StartCounter
{
public:
  void mark()
  {
    {
      const std::lock_guard<std::mutex> guard(mutex_);
      ++count_;
    }
    cv_.notify_all();
  }

  /// True when the callback started within the timeout.
  bool wait(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this]() {return count_ > 0;});
  }

  std::size_t count() const
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    return count_;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::size_t count_ = 0;
};

/// Releases a barrier when the test ends, also after a failed assertion.
class BarrierGuard
{
public:
  explicit BarrierGuard(std::shared_ptr<Latch> latch)
  : latch_(std::move(latch)) {}

  ~BarrierGuard()
  {
    if (latch_) {
      latch_->release();
    }
  }

  BarrierGuard(const BarrierGuard &) = delete;
  BarrierGuard & operator=(const BarrierGuard &) = delete;

private:
  std::shared_ptr<Latch> latch_;
};

/// Calls back into the SDK when it is destroyed.
class ReentrantCleanup
{
public:
  explicit ReentrantCleanup(std::function<void()> on_destroy)
  : on_destroy_(std::move(on_destroy)) {}

  ~ReentrantCleanup()
  {
    if (on_destroy_) {
      on_destroy_();
    }
  }

  ReentrantCleanup(const ReentrantCleanup &) = delete;
  ReentrantCleanup & operator=(const ReentrantCleanup &) = delete;

private:
  std::function<void()> on_destroy_;
};

/// Owned feedback value of a job.
std::shared_ptr<void> make_payload(int value)
{
  return std::make_shared<int>(value);
}

/// Waits until a condition holds, without a fixed sleep.
template<typename Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

/// Submits a job and retries it while the scheduler lock is contended.
///
/// The dispatcher drops a submission when the scheduler lock is busy, so a
/// setup step that needs an admitted job retries it. The retry uses the same
/// scope and the same observation, so it never changes the meaning of the
/// test: a dropped submission was never queued, so the retry admits the same
/// job. Only a dropped submission is retried, and the retry stops at the
/// deadline; every other outcome is returned to the caller unchanged.
SubmitResult submit_with_retry(
  FeedbackDispatcher & dispatcher,
  const Scope & scope,
  const Observation & observation,
  std::shared_ptr<void> payload,
  std::function<void(void *)> invoke,
  std::chrono::milliseconds timeout = 5s)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    // The payload and the callable are copied on every attempt, so a retry
    // never consumes the ownership of the caller.
    const SubmitResult result =
      dispatcher.submit(scope, observation, payload, invoke);
    if (result != SubmitResult::Dropped) {
      return result;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return result;
    }
    std::this_thread::sleep_for(1ms);
  }
}

/// Occupies both workers of the process with a blocked callback.
///
/// The barriers are released when the object dies, so a failed assertion
/// never leaves a worker blocked. The constructor never fails the test: it
/// reports whether both workers parked in a blocked callback, and the test
/// asserts it after the object is constructed.
class BusyPool
{
public:
  explicit BusyPool(FeedbackDispatcher & dispatcher)
  : release_a_(std::make_shared<Latch>()),
    release_b_(std::make_shared<Latch>()),
    started_a_(std::make_shared<StartCounter>()),
    started_b_(std::make_shared<StartCounter>())
  {
    const Scope scope_a = dispatcher.create_scope();
    const Scope scope_b = dispatcher.create_scope();
    const Observation observation_a = dispatcher.create_observation();
    const Observation observation_b = dispatcher.create_observation();

    const SubmitResult first = submit_with_retry(
      dispatcher, scope_a, observation_a, make_payload(1),
      [started = started_a_, release = release_a_](void *) {
        started->mark();
        release->wait(30s);
      });
    const SubmitResult second = submit_with_retry(
      dispatcher, scope_b, observation_b, make_payload(2),
      [started = started_b_, release = release_b_](void *) {
        started->mark();
        release->wait(30s);
      });

    // Both workers must park in the blocked callbacks, otherwise the pool is
    // not busy and the test would measure something else. The waits always
    // run, also when a submission failed, so the pool ends in a known state.
    const bool started_a = started_a_->wait(5s);
    const bool started_b = started_b_->wait(5s);

    ok_ = first == SubmitResult::Accepted &&
          second == SubmitResult::Accepted &&
          started_a &&
          started_b;
  }

  ~BusyPool()
  {
    release();
  }

  BusyPool(const BusyPool &) = delete;
  BusyPool & operator=(const BusyPool &) = delete;

  /// True when both workers are parked in a blocked callback.
  bool ok() const {return ok_;}

  void release()
  {
    release_a_->release();
    release_b_->release();
  }

private:
  std::shared_ptr<Latch> release_a_;
  std::shared_ptr<Latch> release_b_;
  std::shared_ptr<StartCounter> started_a_;
  std::shared_ptr<StartCounter> started_b_;
  bool ok_ = false;
};

}  // namespace

// A: one callback per Robot at a time, two Robots at the same time.
TEST(FeedbackDispatcherTest, RunsOneCallbackPerScopeAndBothWorkersInParallel)
{
  FeedbackDispatcher & dispatcher = FeedbackDispatcher::instance();
  const Scope scope_a = dispatcher.create_scope();
  const Scope scope_b = dispatcher.create_scope();

  auto release_a = std::make_shared<Latch>();
  auto release_b = std::make_shared<Latch>();
  BarrierGuard guard_a(release_a);
  BarrierGuard guard_b(release_b);

  auto started_a = std::make_shared<StartCounter>();
  auto started_a_second = std::make_shared<StartCounter>();
  auto started_b = std::make_shared<StartCounter>();
  auto running_in_scope_a = std::make_shared<std::atomic<int>>(0);
  auto overlapped = std::make_shared<std::atomic<bool>>(false);

  const SubmitResult first = submit_with_retry(
    dispatcher, scope_a, dispatcher.create_observation(), make_payload(1),
    [started = started_a, release = release_a,
      running = running_in_scope_a, overlapped = overlapped](void *) {
      if (running->fetch_add(1) + 1 > 1) {
        overlapped->store(true);
      }
      started->mark();
      release->wait(30s);
      running->fetch_sub(1);
    });
  ASSERT_EQ(SubmitResult::Accepted, first);
  ASSERT_TRUE(started_a->wait(5s));

  // A second job of the same Robot must wait for the first one.
  const SubmitResult second = submit_with_retry(
    dispatcher, scope_a, dispatcher.create_observation(), make_payload(2),
    [started = started_a_second,
      running = running_in_scope_a, overlapped = overlapped](void *) {
      if (running->fetch_add(1) + 1 > 1) {
        overlapped->store(true);
      }
      started->mark();
      running->fetch_sub(1);
    });
  ASSERT_EQ(SubmitResult::Accepted, second);
  EXPECT_FALSE(started_a_second->wait(300ms));

  // A job of another Robot runs on the second worker while the first one is
  // still blocked.
  const SubmitResult third = submit_with_retry(
    dispatcher, scope_b, dispatcher.create_observation(), make_payload(3),
    [started = started_b, release = release_b](void *) {
      started->mark();
      release->wait(30s);
    });
  ASSERT_EQ(SubmitResult::Accepted, third);
  EXPECT_TRUE(started_b->wait(5s));

  // Releasing the first callback lets the queued job of the same Robot run.
  release_a->release();
  release_b->release();
  EXPECT_TRUE(started_a_second->wait(5s));
  EXPECT_FALSE(overlapped->load());

  // Every callback finished, so the next test starts with an empty queue.
  ASSERT_TRUE(
    wait_until(
      [&dispatcher]() {
        return dispatcher.active_count() == 0 && dispatcher.pending_count() == 0;
      },
      5s));
}

// B: bounded queue, coalescing, overflow, prompt submissions.
TEST(FeedbackDispatcherTest, BoundsPendingJobsCoalescesAndDropsOverflow)
{
  FeedbackDispatcher & dispatcher = FeedbackDispatcher::instance();
  BusyPool pool(dispatcher);
  ASSERT_TRUE(pool.ok());
  EXPECT_EQ(0u, dispatcher.pending_count());

  const Scope scope = dispatcher.create_scope();
  const Observation observation = dispatcher.create_observation();

  const std::size_t dropped_before = dispatcher.dropped_count();
  const std::size_t contention_before = dispatcher.lock_contention_drops();
  const std::size_t pending_before = dispatcher.pending_count();

  // The scheduler is quiescent here: both workers are parked in a blocked
  // callback, so no submission is dropped by lock contention and the
  // capacity of the queue is the only limit of the loop below.
  //
  // The same Robot and the same operation coalesce: the newest sample wins.
  EXPECT_EQ(
    SubmitResult::Accepted,
    dispatcher.submit(scope, observation, make_payload(1), [](void *) {}));
  EXPECT_EQ(
    SubmitResult::Coalesced,
    dispatcher.submit(scope, observation, make_payload(2), [](void *) {}));
  EXPECT_EQ(pending_before + 1, dispatcher.pending_count());

  // Fill the global budget while both workers are blocked.
  const auto start = std::chrono::steady_clock::now();
  std::size_t accepted = 0;
  std::size_t coalesced = 0;
  std::size_t dropped = 0;
  for (int index = 0; index < 200; ++index) {
    const SubmitResult result = dispatcher.submit(
      scope, dispatcher.create_observation(), make_payload(index), [](void *) {});
    switch (result) {
      case SubmitResult::Accepted:
        ++accepted;
        break;
      case SubmitResult::Coalesced:
        ++coalesced;
        break;
      case SubmitResult::Dropped:
        ++dropped;
        break;
      case SubmitResult::Revoked:
        break;
    }
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;

  // The submissions never wait for queue capacity.
  EXPECT_LT(elapsed, 2s);
  EXPECT_EQ(0u, coalesced);
  EXPECT_GT(dropped, 0u);
  EXPECT_GT(dispatcher.dropped_count() - dropped_before, 0u);

  // The pending storage stays inside the global budget.
  EXPECT_LE(dispatcher.pending_count(), kMaxPendingFeedbackJobs);
  EXPECT_EQ(kMaxPendingFeedbackJobs, dispatcher.pending_count());
  EXPECT_EQ(kMaxPendingFeedbackJobs - pending_before - 1, accepted);
  EXPECT_EQ(200 - accepted, dropped);

  // No submission of this test was dropped by lock contention.
  EXPECT_EQ(contention_before, dispatcher.lock_contention_drops());

  // The test leaves no queued work behind for the next test.
  EXPECT_EQ(kMaxPendingFeedbackJobs, dispatcher.revoke_scope(scope));
  EXPECT_EQ(0u, dispatcher.pending_count());
}

// C: revoked scopes and retired operations discard the queued work.
TEST(FeedbackDispatcherTest, RevocationAndRetirementDiscardQueuedWork)
{
  FeedbackDispatcher & dispatcher = FeedbackDispatcher::instance();
  BusyPool pool(dispatcher);
  ASSERT_TRUE(pool.ok());

  auto invoked = std::make_shared<std::atomic<bool>>(false);
  auto invoked_retired = std::make_shared<std::atomic<bool>>(false);

  const Scope scope = dispatcher.create_scope();
  const Observation observation = dispatcher.create_observation();
  ASSERT_EQ(
    SubmitResult::Accepted,
    dispatcher.submit(
      scope, observation, make_payload(1),
      [invoked = invoked](void *) {invoked->store(true);}));
  ASSERT_EQ(1u, dispatcher.pending_count());

  // Closing the Robot drops its queued feedback.
  EXPECT_EQ(1u, dispatcher.revoke_scope(scope));
  EXPECT_EQ(0u, dispatcher.pending_count());
  EXPECT_EQ(
    SubmitResult::Revoked,
    dispatcher.submit(
      scope, dispatcher.create_observation(), make_payload(2), [](void *) {}));

  // Retiring one operation drops only its queued feedback.
  const Scope scope_two = dispatcher.create_scope();
  const Observation observation_two = dispatcher.create_observation();
  ASSERT_EQ(
    SubmitResult::Accepted,
    dispatcher.submit(
      scope_two, observation_two, make_payload(3),
      [invoked = invoked_retired](void *) {invoked->store(true);}));
  EXPECT_EQ(1u, dispatcher.retire_operation(observation_two));
  EXPECT_EQ(0u, dispatcher.pending_count());

  // Let the workers finish the callbacks they already claimed.
  pool.release();
  ASSERT_TRUE(
    wait_until(
      [&dispatcher]() {
        return dispatcher.active_count() == 0 && dispatcher.pending_count() == 0;
      },
      5s));

  // Neither the revoked nor the retired job was invoked.
  EXPECT_FALSE(invoked->load());
  EXPECT_FALSE(invoked_retired->load());
}

// D: the scope lifecycle does not grow the worker pool.
TEST(FeedbackDispatcherTest, RepeatedScopeLifecycleKeepsTheWorkerPoolStable)
{
  FeedbackDispatcher & dispatcher = FeedbackDispatcher::instance();
  const std::size_t workers_before = dispatcher.worker_count();
  ASSERT_EQ(kFeedbackWorkerCount, workers_before);

  for (int index = 0; index < 2000; ++index) {
    const Scope scope = dispatcher.create_scope();
    dispatcher.revoke_scope(scope);
  }

  EXPECT_EQ(workers_before, dispatcher.worker_count());
  EXPECT_EQ(kFeedbackWorkerCount, dispatcher.worker_count());
  // The dispatcher keeps no registry of completed scopes.
  EXPECT_EQ(0u, dispatcher.retained_scope_count());
}

// E: a throwing callback does not kill its worker.
TEST(FeedbackDispatcherTest, ThrowingCallbackDoesNotKillTheWorker)
{
  FeedbackDispatcher & dispatcher = FeedbackDispatcher::instance();
  const Scope scope = dispatcher.create_scope();
  const std::size_t exceptions_before = dispatcher.callback_exception_count();

  auto thrown = std::make_shared<std::promise<void>>();
  auto thrown_future = thrown->get_future();
  const SubmitResult result = submit_with_retry(
    dispatcher, scope, dispatcher.create_observation(), make_payload(1),
    [thrown = thrown](void *) {
      thrown->set_value();
      throw std::runtime_error("user callback failed");
    });
  ASSERT_EQ(SubmitResult::Accepted, result);
  ASSERT_EQ(std::future_status::ready, thrown_future.wait_for(5s));

  // The same worker keeps serving later work.
  auto afterwards = std::make_shared<std::promise<void>>();
  auto afterwards_future = afterwards->get_future();
  const SubmitResult next = submit_with_retry(
    dispatcher, scope, dispatcher.create_observation(), make_payload(2),
    [afterwards = afterwards](void *) {afterwards->set_value();});
  ASSERT_EQ(SubmitResult::Accepted, next);
  ASSERT_EQ(std::future_status::ready, afterwards_future.wait_for(5s));
  // Same-scope serialization ensures the earlier exception was accounted
  // before this callback started; its own promise precedes the throw.
  EXPECT_GT(dispatcher.callback_exception_count(), exceptions_before);
}

// F: the cleanup of a discarded callable may re-enter the SDK.
TEST(FeedbackDispatcherTest, DiscardedCallableCleanupCanReenterTheDispatcher)
{
  FeedbackDispatcher & dispatcher = FeedbackDispatcher::instance();
  // Both workers are busy, so the queued job cannot be claimed before the
  // submission that replaces it.
  BusyPool pool(dispatcher);
  ASSERT_TRUE(pool.ok());

  const Scope scope = dispatcher.create_scope();
  const Observation observation = dispatcher.create_observation();

  auto destroyed = std::make_shared<std::promise<void>>();
  auto destroyed_future = destroyed->get_future();

  // The queued callable owns a guard that calls back into the dispatcher
  // when the job is discarded. The callback must run without the scheduler
  // lock, otherwise it deadlocks.
  auto guard = std::make_shared<ReentrantCleanup>(
    [&dispatcher, scope, destroyed]() {
      dispatcher.revoke_scope(scope);
      destroyed->set_value();
    });
  std::weak_ptr<ReentrantCleanup> weak_guard = guard;

  const SubmitResult accepted = submit_with_retry(
    dispatcher, scope, observation, make_payload(1), [guard](void *) {(void)guard;});
  ASSERT_EQ(SubmitResult::Accepted, accepted);

  // The queued job owns the guard now: the test must not keep it alive.
  guard.reset();
  EXPECT_FALSE(weak_guard.expired());

  // The second submission of the same operation replaces the queued job, so
  // the discarded callable is destroyed here.
  const SubmitResult coalesced = submit_with_retry(
    dispatcher, scope, observation, make_payload(2), [](void *) {});
  ASSERT_EQ(SubmitResult::Coalesced, coalesced);

  EXPECT_EQ(std::future_status::ready, destroyed_future.wait_for(5s));
  EXPECT_TRUE(weak_guard.expired());
  EXPECT_EQ(0u, dispatcher.pending_count());
}

// G: retiring an operation frees the pending budget at once, and the
// destruction of the discarded callable may re-enter the dispatcher.
TEST(FeedbackDispatcherTest, RetirementFreesTheBudgetAndCleanupMayReenter)
{
  FeedbackDispatcher & dispatcher = FeedbackDispatcher::instance();
  // Both workers are blocked, so the queue is the only place where the job
  // can live while the operation is retired.
  BusyPool pool(dispatcher);
  ASSERT_TRUE(pool.ok());

  const Scope scope = dispatcher.create_scope();
  const Observation observation = dispatcher.create_observation();

  auto reentered = std::make_shared<std::promise<void>>();
  auto reentered_future = reentered->get_future();

  // The queued callable owns a guard that calls back into the dispatcher when
  // the job is discarded. The callback must run without the scheduler lock,
  // otherwise it deadlocks.
  auto guard = std::make_shared<ReentrantCleanup>(
    [&dispatcher, scope, reentered]() {
      dispatcher.revoke_scope(scope);
      reentered->set_value();
    });
  std::weak_ptr<ReentrantCleanup> weak_guard = guard;

  const SubmitResult accepted = submit_with_retry(
    dispatcher, scope, observation, make_payload(1), [guard](void *) {(void)guard;});
  ASSERT_EQ(SubmitResult::Accepted, accepted);
  guard.reset();
  EXPECT_FALSE(weak_guard.expired());
  ASSERT_EQ(1u, dispatcher.pending_count());

  // Retiring the operation discards the queued job at once, even though both
  // workers are blocked.
  EXPECT_EQ(1u, dispatcher.retire_operation(observation));
  EXPECT_EQ(0u, dispatcher.pending_count());

  // The destruction of the user capture re-entered the dispatcher while the
  // retirement was running.
  EXPECT_EQ(std::future_status::ready, reentered_future.wait_for(5s));
  EXPECT_TRUE(weak_guard.expired());

  // The freed budget is usable right away.
  const Scope other_scope = dispatcher.create_scope();
  const Observation other_observation = dispatcher.create_observation();
  ASSERT_EQ(
    SubmitResult::Accepted,
    dispatcher.submit(
      other_scope, other_observation, make_payload(2), [](void *) {}));
  EXPECT_EQ(1u, dispatcher.pending_count());
  EXPECT_EQ(1u, dispatcher.revoke_scope(other_scope));
  EXPECT_EQ(0u, dispatcher.pending_count());
}
