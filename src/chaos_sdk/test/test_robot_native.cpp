// Native SDK contract tests: real mock ROS services and a real action server.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_interfaces/action/navigate_pose.hpp"
#include "chaos_interfaces/msg/velocity_command.hpp"
#include "chaos_interfaces/srv/control_lease.hpp"
#include "chaos_interfaces/srv/get_description.hpp"
#include "chaos_interfaces/srv/stop.hpp"

#include "chaos_sdk/action_slots.hpp"
#include "chaos_sdk/robot.hpp"

// The internal dispatcher of the SDK. The header is not installed and not
// part of the public API, but the test observes the process wide dispatcher
// through it: the counters are the only way to see what the SDK really
// queued. The include is relative to this file, so no include path of the
// package has to change.
#include "../src/feedback_dispatcher.hpp"

using namespace std::chrono_literals;

namespace
{

using NavigatePose = chaos_interfaces::action::NavigatePose;

void ensure_init()
{
  static std::once_flag flag;
  std::call_once(flag, []() {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
  });
}

/// Compact description of a received target pose.
///
/// The mock server echoes the target, so a test can verify that the client
/// really sent the coordinates and the frame it was given.
std::string describe_target(const geometry_msgs::msg::PoseStamped & target)
{
  std::ostringstream out;
  out << "frame=" << target.header.frame_id
      << " x=" << target.pose.position.x
      << " y=" << target.pose.position.y
      << " z=" << target.pose.position.z
      << " qx=" << target.pose.orientation.x
      << " qy=" << target.pose.orientation.y
      << " qz=" << target.pose.orientation.z
      << " qw=" << target.pose.orientation.w;
  return out.str();
}

/// A mock service call whose answer the test releases explicitly.
///
/// The service callback parks on the call until the test filled in the answer
/// and released it. That gives the test full control over the order in which
/// the client sees the answers, without any sleep based timing.
struct DeferredCall
{
  /// The lease operation ("acquire", "renew", "release") or "stop".
  std::string operation;
  /// The token that the request carried.
  std::string request_token;
  /// The owner that the request carried.
  std::string request_owner;
  /// The answers that the test hands to the parked callback.
  chaos_interfaces::srv::ControlLease::Response lease_answer;
  chaos_interfaces::srv::Stop::Response stop_answer;
  /// Copies the answer into the response object of the parked callback.
  std::function<void ()> apply;
  /// Fulfilled when the test released the call.
  std::promise<void> release;
  std::shared_future<void> released;
  /// True once the test released the call.
  std::atomic<bool> answered{false};
};

/// The answer of a successful lease request that hands out ``token``.
chaos_interfaces::srv::ControlLease::Response lease_answer_ok(const std::string & token)
{
  chaos_interfaces::srv::ControlLease::Response answer;
  answer.success = true;
  answer.code = "OK";
  answer.token = token;
  answer.remaining_s = 5.0;
  return answer;
}

/// The answer of an accepted stop.
chaos_interfaces::srv::Stop::Response stop_answer_ok()
{
  chaos_interfaces::srv::Stop::Response answer;
  answer.success = true;
  answer.code = "OK";
  answer.message = "Controlled stop output issued";
  return answer;
}

/// The answer of a rejected stop.
chaos_interfaces::srv::Stop::Response stop_answer_rejected(const std::string & code)
{
  chaos_interfaces::srv::Stop::Response answer;
  answer.success = false;
  answer.code = code;
  answer.message = "the lease token does not match";
  return answer;
}

// --- The controlled goal of the mock action server -------------------------
//
// The controlled goal is opt-in: only the tests that install it see the gate.
// The default behaviour of the mock server stays exactly as it was.

/// Owned state of one controlled goal of the mock action server.
///
/// The mock server accepts the goal, keeps the handle of the accepted goal
/// and holds its terminal result on a gate. The test publishes the feedback
/// itself and releases the result when it wants the goal to finish.
///
/// The state is heap owned: a queued feedback callback may capture it, so no
/// callback ever refers to the stack of the test.
class ControlledGoal
{
public:
  using GoalHandle = rclcpp_action::ServerGoalHandle<NavigatePose>;

  /// The mock server accepted the goal: keep the handle of the goal.
  void accept(std::shared_ptr<GoalHandle> goal_handle)
  {
    {
      const std::lock_guard<std::mutex> guard(mutex_);
      handle_ = std::move(goal_handle);
      handle_known_ = true;
    }
    changed_.notify_all();
  }

  /// Wait until the server accepted the goal and published its handle.
  bool wait_for_handle(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout, [this]() {return handle_known_;});
  }

  /// The owned handle of the accepted goal, or nullptr.
  std::shared_ptr<GoalHandle> handle() const
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    return handle_;
  }

  /// Publish one feedback sample of the controlled goal.
  ///
  /// The publication may be lost while the feedback publisher of the server
  /// is not matched with the subscription of the client yet, so the test
  /// publishes again until it observes the sample.
  void publish(float progress)
  {
    auto goal_handle = handle();
    if (!goal_handle) {
      return;
    }
    auto feedback = std::make_shared<NavigatePose::Feedback>();
    feedback->progress = progress;
    goal_handle->publish_feedback(feedback);
  }

  /// Release the terminal result of the goal. The call is idempotent.
  void release_result()
  {
    {
      const std::lock_guard<std::mutex> guard(mutex_);
      if (result_released_) {
        return;
      }
      result_released_ = true;
    }
    changed_.notify_all();
  }

  /// Wait until the test released the terminal result.
  ///
  /// The wait is bounded: a test that dies before the release must not block
  /// the executor of the mock forever.
  bool await_result(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout, [this]() {return result_released_;});
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::shared_ptr<GoalHandle> handle_;
  bool handle_known_ = false;
  bool result_released_ = false;
};

/// Owned probe of a user feedback callback.
///
/// The probe counts the entries and the completions of the callback and
/// records that it threw. It is heap owned: the callback may run on a worker
/// of the dispatcher after the stack of the test is gone.
class FeedbackProbe
{
public:
  /// Signal the entry of the callback.
  void enter()
  {
    {
      const std::lock_guard<std::mutex> guard(mutex_);
      ++entries_;
    }
    changed_.notify_all();
  }

  /// Signal that the callback is about to throw.
  void mark_throw()
  {
    {
      const std::lock_guard<std::mutex> guard(mutex_);
      threw_ = true;
    }
    changed_.notify_all();
  }

  /// Signal that the callback returned.
  void leave()
  {
    {
      const std::lock_guard<std::mutex> guard(mutex_);
      ++completions_;
    }
    changed_.notify_all();
  }

  /// Wait until the callback entered at least ``count`` times.
  bool wait_for_entries(std::size_t count, std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout, [this, count]() {return entries_ >= count;});
  }

  /// Wait until the callback threw.
  bool wait_for_throw(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout, [this]() {return threw_;});
  }

  /// Number of the entries of the callback so far.
  std::size_t entry_count() const
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    return entries_;
  }

  /// Number of the callbacks that returned so far.
  std::size_t completion_count() const
  {
    const std::lock_guard<std::mutex> guard(mutex_);
    return completions_;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::size_t entries_ = 0;
  std::size_t completions_ = 0;
  bool threw_ = false;
};

/// Owned gate that a user feedback callback blocks on.
///
/// The gate is heap owned: the queued callback captures it and the test
/// releases it in the cleanup, so a blocked worker never outlives the test.
class UserGate
{
public:
  /// Release the gate: the blocked callback returns.
  void release()
  {
    {
      const std::lock_guard<std::mutex> guard(mutex_);
      released_ = true;
    }
    changed_.notify_all();
  }

  /// Wait until the gate is released. The wait is bounded.
  bool wait(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return changed_.wait_for(lock, timeout, [this]() {return released_;});
  }

private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool released_ = false;
};

/// Owned one-shot signal.
///
/// The signal lets a test observe a real event instead of guessing a timing:
/// the callback raises it when it returned. It is heap owned: the callback
/// may run on a worker of the dispatcher after the stack of the test is gone.
class Signal
{
public:
  Signal() : future_(promise_.get_future().share()) {}

  /// Raise the signal. The call is idempotent.
  void raise()
  {
    if (!raised_.exchange(true)) {
      promise_.set_value();
    }
  }

  /// True when the signal was raised.
  bool raised() const {return raised_.load();}

  /// The future of the signal.
  std::shared_future<void> future() const {return future_;}

private:
  std::promise<void> promise_;
  std::shared_future<void> future_;
  std::atomic<bool> raised_{false};
};

/// Owner of the close thread, of the task mutex and of the feedback gate.
///
/// The destructor releases the feedback gate and unlocks the task mutex
/// before it joins the close thread. A failed assertion must never leave the
/// close thread blocked on the mutex of the task, and a worker of the
/// dispatcher must never stay blocked on the gate: the releases always
/// happen before the join.
class CloseOrderGuard
{
public:
  CloseOrderGuard(std::unique_lock<std::mutex> * task_lock, std::shared_ptr<UserGate> gate)
  : task_lock_(task_lock), gate_(std::move(gate)), done_(std::make_shared<Signal>())
  {
  }

  CloseOrderGuard(const CloseOrderGuard &) = delete;
  CloseOrderGuard & operator=(const CloseOrderGuard &) = delete;

  ~CloseOrderGuard()
  {
    release();
    join();
  }

  /// Release the feedback gate and unlock the task mutex.
  ///
  /// The gate comes first: a blocked callback keeps the worker of its scope
  /// busy. Releasing it ensures fixture cleanup also drains the dispatcher.
  /// The call is idempotent.
  void release()
  {
    if (gate_) {
      gate_->release();
      gate_.reset();
    }
    if (task_lock_ != nullptr && task_lock_->owns_lock()) {
      task_lock_->unlock();
    }
  }

  /// Start the close thread.
  ///
  /// The thread raises the done signal when it returned, so the test can
  /// wait for it with a bounded timeout.
  void start(std::function<void ()> body)
  {
    auto done = done_;
    auto failed = failed_;
    closer_ = std::thread([body, done, failed]() {
        try {
          body();
        } catch (...) {
          failed->store(true);
        }
        done->raise();
      });
  }

  bool failed() const {return failed_->load();}

  /// Wait for the return of the close thread. The wait is bounded.
  bool wait(std::chrono::milliseconds timeout)
  {
    return done_->future().wait_for(timeout) == std::future_status::ready;
  }

  /// Join the close thread.
  ///
  /// The gate is released and the mutex is unlocked before, so the thread
  /// cannot stay blocked on a lock of the SDK.
  void join()
  {
    if (closer_.joinable()) {
      closer_.join();
    }
  }

private:
  std::unique_lock<std::mutex> * task_lock_;
  std::shared_ptr<UserGate> gate_;
  std::shared_ptr<Signal> done_;
  std::shared_ptr<std::atomic<bool>> failed_ = std::make_shared<std::atomic<bool>>(false);
  std::thread closer_;
};

/// The outcome that a feedback callback really observed.
struct StopOutcome
{
  /// True when the stop answered and reported success.
  bool success = false;
  /// True when the stop ran into the local response timeout.
  bool timed_out = false;
  /// The message of an unexpected error.
  std::string error;
};

/// Owned promise of the outcome of a feedback callback.
///
/// The callback settles the promise exactly once, so the test awaits the real
/// outcome of the callback instead of guessing a timing.
class OutcomeSlot
{
public:
  OutcomeSlot() : future_(promise_.get_future().share()) {}

  /// Settle the promise with the outcome of the callback.
  void record(const StopOutcome & outcome)
  {
    if (!recorded_.exchange(true)) {
      promise_.set_value(outcome);
    }
  }

  /// The future of the recorded outcome.
  std::shared_future<StopOutcome> future() const {return future_;}

private:
  std::promise<StopOutcome> promise_;
  std::shared_future<StopOutcome> future_;
  std::atomic<bool> recorded_{false};
};

/// The observed result of a goal whose feedback callback called stop().
struct StopFromFeedbackResult
{
  /// The task of the goal, or nullptr when the goal was not accepted.
  std::shared_ptr<chaos_sdk::Robot::NavigationTask> task;
  /// The outcome that the callback really observed.
  StopOutcome outcome;
  /// True when the callback really ran and settled its promise.
  bool callback_ran = false;
};

/// The cell that the capture guard and the test share.
///
/// The cell holds the future of the cancel request and the observations of
/// the guard. It is heap owned: the guard may run inside the cleanup of the
/// SDK, long after the stack of the test is gone.
class CancelCell
{
public:
  /// What the guard observed while it was destroyed.
  struct Observation
  {
    /// True when the cell held a valid future when the guard was destroyed.
    bool future_valid = false;
    /// True when that future was already settled at that moment.
    bool future_ready = false;
    /// True when the guard called the close of the client.
    bool close_called = false;
    /// True when that close returned without an error.
    bool close_succeeded = false;
  };

  /// The future of the cancel request. The test stores it before the timeout
  /// of the request elapses.
  chaos_sdk::Robot::NavigationTask::CancelFuture cancel_future;
  /// The client of the test. The guard closes it through this weak
  /// reference: a strong reference would keep the client alive.
  std::weak_ptr<chaos_sdk::Robot> robot;
  /// What the guard observed.
  Observation observed;
  /// The mutex of the cell.
  std::mutex mutex;
};

/// Guard that a user capture owns.
///
/// The guard records how it was destroyed: it observes the future of the
/// request that released it and it closes the client through a weak
/// reference. Its destructor must never block and must never throw: it may
/// run inside the cleanup of the SDK, while another thread holds a lock of
/// the SDK.
class CaptureGuard
{
public:
  CaptureGuard(std::shared_ptr<CancelCell> cell, std::shared_ptr<Signal> returned)
  : cell_(std::move(cell)), returned_(std::move(returned))
  {
  }

  CaptureGuard(const CaptureGuard &) = delete;
  CaptureGuard & operator=(const CaptureGuard &) = delete;

  ~CaptureGuard() noexcept
  {
    chaos_sdk::Robot::NavigationTask::CancelFuture future;
    std::weak_ptr<chaos_sdk::Robot> robot;
    {
      const std::lock_guard<std::mutex> guard(cell_->mutex);
      future = cell_->cancel_future;
      robot = cell_->robot;
      cell_->observed.future_valid = future.valid();
    }
    // The destructor must never wait for the future: only a non blocking
    // observation is allowed here, a wait could hang the cleanup.
    const bool ready = future.valid() &&
      future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    {
      const std::lock_guard<std::mutex> guard(cell_->mutex);
      cell_->observed.future_ready = ready;
    }
    // The close is re-entered from here: it runs inside the cleanup of the
    // SDK. The mutex of the cell is not held while it runs.
    bool closed = false;
    bool called = false;
    if (auto client = robot.lock()) {
      called = true;
      try {
        client->close();
        closed = true;
      } catch (...) {
        closed = false;
      }
    }
    {
      const std::lock_guard<std::mutex> guard(cell_->mutex);
      cell_->observed.close_called = called;
      cell_->observed.close_succeeded = closed;
    }
    returned_->raise();
  }

private:
  std::shared_ptr<CancelCell> cell_;
  std::shared_ptr<Signal> returned_;
};

/// Releases the server side of a controlled goal when the test ends.
///
/// The mock server holds the terminal result of a controlled goal until the
/// test releases it. A test that dies before the release must not block the
/// executor of the mock forever.
class ScopedGoalRelease
{
public:
  explicit ScopedGoalRelease(std::shared_ptr<ControlledGoal> controlled)
  : controlled_(controlled)
  {
  }

  ScopedGoalRelease(const ScopedGoalRelease &) = delete;
  ScopedGoalRelease & operator=(const ScopedGoalRelease &) = delete;

  ~ScopedGoalRelease()
  {
    controlled_->release_result();
  }

private:
  std::shared_ptr<ControlledGoal> controlled_;
};

// --- The observations of the internal dispatcher ----------------------------

/// The internal dispatcher of the process.
///
/// The dispatcher is a process wide resource: two workers, one global queue
/// with a budget of 64 pending jobs. The test reads its counters through the
/// internal header instead of creating a second instance.
chaos_sdk::internal::FeedbackDispatcher & dispatcher()
{
  return chaos_sdk::internal::FeedbackDispatcher::instance();
}

/// Wait until the dispatcher counted a new callback exception.
bool wait_for_callback_exception(std::size_t baseline, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (dispatcher().callback_exception_count() > baseline) {
      return true;
    }
    std::this_thread::sleep_for(2ms);
  }
  return dispatcher().callback_exception_count() > baseline;
}

/// Wait until the queue of the dispatcher holds at most ``count`` jobs.
bool wait_for_pending_count(std::size_t count, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (dispatcher().pending_count() <= count) {
      return true;
    }
    std::this_thread::sleep_for(2ms);
  }
  return dispatcher().pending_count() <= count;
}

/// Wait until the dispatcher holds no queued job and no running callback.
///
/// The wait is the ordered completion of the dispatcher: a worker releases the
/// slot of its scope only after the callback returned and after the job was
/// destroyed, so an idle dispatcher cannot run a stale callback any more.
bool wait_for_dispatcher_idle(
  std::size_t baseline_pending,
  std::size_t baseline_active,
  std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto counters = dispatcher().counters();
    if (counters.pending <= baseline_pending && counters.active <= baseline_active) {
      return true;
    }
    std::this_thread::sleep_for(2ms);
  }
  const auto counters = dispatcher().counters();
  return counters.pending <= baseline_pending && counters.active <= baseline_active;
}

/// Publish a feedback sample until the observation shows up.
///
/// The transport may lose the first samples while the feedback publisher of
/// the server is not matched with the subscription of the client yet, and the
/// dispatcher may refuse a submission (a contended lock or an exhausted
/// budget). The test therefore publishes again until it observes the event.
/// The number of attempts is bounded. The loop publishes at least once, so a
/// sample is always on the way.
///
/// The interval between two attempts is a parameter: a test that waits for a
/// sample of a goal whose callback blocks on a gate uses a wider interval, so
/// a sample that is only delayed does not produce a second callback.
bool publish_feedback_until(
  const std::shared_ptr<ControlledGoal> & controlled,
  float progress,
  const std::function<bool ()> & observed,
  std::chrono::milliseconds timeout,
  std::chrono::milliseconds interval = 10ms)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  do {
    controlled->publish(progress);
    if (observed()) {
      return true;
    }
    std::this_thread::sleep_for(interval);
  } while (std::chrono::steady_clock::now() < deadline);
  return observed();
}

/// Publish a sample on two controlled goals until the observation shows up.
///
/// The mock hands the controlled goals to the accepted goals in the order in
/// which the requests arrive, so a test that observes several goals at the
/// same time does not know which controlled goal belongs to which client. It
/// therefore publishes on both of them until the observation shows up.
bool publish_feedback_until(
  const std::shared_ptr<ControlledGoal> & first,
  const std::shared_ptr<ControlledGoal> & second,
  float progress,
  const std::function<bool ()> & observed,
  std::chrono::milliseconds timeout,
  std::chrono::milliseconds interval = 10ms)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  do {
    first->publish(progress);
    second->publish(progress);
    if (observed()) {
      return true;
    }
    std::this_thread::sleep_for(interval);
  } while (std::chrono::steady_clock::now() < deadline);
  return observed();
}

/// Wait until the dispatcher claimed and runs at least ``count`` jobs.
///
/// A claimed job runs on a worker of the process. The wait is bounded.
bool wait_for_active_count(std::size_t count, std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (dispatcher().active_count() >= count) {
      return true;
    }
    std::this_thread::sleep_for(2ms);
  }
  return dispatcher().active_count() >= count;
}

/// Wait until the client holds at most ``count`` ROS action clients.
///
/// A retired slot releases its client, so this number drops again when a goal
/// timed out or was closed. The wait is bounded.
bool wait_for_action_client_count(
  const std::shared_ptr<chaos_sdk::Robot> & robot,
  std::size_t count,
  std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (robot->action_client_count() <= count) {
      return true;
    }
    std::this_thread::sleep_for(2ms);
  }
  return robot->action_client_count() <= count;
}

struct MockState
{
  std::atomic<bool> lease_success{true};
  std::atomic<bool> lease_stop_failed{false};
  std::atomic<bool> description_silent{false};
  std::atomic<bool> stop_silent{false};
  std::atomic<bool> lease_silent{false};
  std::atomic<int> action_mode{0};  // 0 succeed, 1 reject, 2 hang until canceled
  std::atomic<int> silent_goals{0};  // number of goals that never answer at once
  std::atomic<bool> slow_cancel{false};
  std::atomic<bool> publish_feedback{false};
  /// The goal finishes on its own even though a cancel request is pending.
  std::atomic<bool> succeed_despite_cancel{false};
  std::atomic<int> lease_calls{0};
  /// Park the service callbacks instead of answering them immediately. The
  /// test then decides when and in which order the client sees the answers.
  std::atomic<bool> defer_responses{false};
  /// The calls that the mock server parked.
  std::mutex calls_mutex;
  std::condition_variable calls_changed;
  std::vector<std::shared_ptr<DeferredCall>> calls;
  bool closing = false;  // protected by calls_mutex
  /// The controlled goals of the mock action server, in the order in which
  /// they were installed. Every accepted goal takes the first entry of the
  /// queue, so a test may observe several goals at the same time. The queue
  /// is empty when the mock answers a goal on its own. Protected by
  /// calls_mutex.
  std::vector<std::shared_ptr<ControlledGoal>> controlled_queue;

  /// Park the current service callback until the test releases it.
  void park(const std::shared_ptr<DeferredCall> & call)
  {
    call->released = call->release.get_future().share();
    {
      const std::lock_guard<std::mutex> guard(calls_mutex);
      if (closing) {
        return;
      }
      calls.push_back(call);
    }
    calls_changed.notify_all();
    // The answer is copied into the response object of the parked callback.
    call->released.wait();
    call->apply();
  }
};

class SdkTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    ensure_init();
    state_ = std::make_shared<MockState>();
    server_node_ = std::make_shared<rclcpp::Node>("mock_asset");
    client_node_ = std::make_shared<rclcpp::Node>("sdk_client");

    // The services share one reentrant group: a parked callback must not stop
    // the other services from answering, so the test can control the order in
    // which the client sees the answers.
    service_group_ = server_node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);

    description_server_ = server_node_->create_service<chaos_interfaces::srv::GetDescription>(
      "/robots/test/description",
      [state = state_](
        const std::shared_ptr<chaos_interfaces::srv::GetDescription::Request>,
        std::shared_ptr<chaos_interfaces::srv::GetDescription::Response> response) {
        if (state->description_silent.load()) {
          std::this_thread::sleep_for(2s);
          return;
        }
        if (state->defer_responses.load()) {
          // The test decides when the client sees the answer. The deferred
          // description has no effect on the cached state of the client: a
          // late answer never reaches it.
          auto call = std::make_shared<DeferredCall>();
          call->operation = "describe";
          call->apply = [response]() {
            response->asset_id = "test";
            response->model = "mock";
            response->base_frame = "test/base";
            response->command_timeout_s = 0.2;
            response->capabilities = {"motion.velocity"};
          };
          state->park(call);
          return;
        }
        response->asset_id = "test";
        response->model = "mock";
        response->base_frame = "test/base";
        response->command_timeout_s = 0.2;
        response->capabilities = {"motion.velocity"};
      },
      rclcpp::ServicesQoS(),
      service_group_);

    lease_server_ = server_node_->create_service<chaos_interfaces::srv::ControlLease>(
      "/robots/test/control/lease",
      [state = state_](
        const std::shared_ptr<chaos_interfaces::srv::ControlLease::Request> request,
        std::shared_ptr<chaos_interfaces::srv::ControlLease::Response> response) {
        state->lease_calls.fetch_add(1);
        if (state->lease_silent.load()) {
          std::this_thread::sleep_for(1s);
          return;
        }
        if (state->defer_responses.load()) {
          // The test decides when the client sees the answer.
          auto call = std::make_shared<DeferredCall>();
          call->operation = request->operation;
          call->request_token = request->token;
          call->request_owner = request->owner;
          call->apply = [pending = call.get(), response]() {*response = pending->lease_answer;};
          state->park(call);
          return;
        }
        if (request->operation == "release" && state->lease_stop_failed.load()) {
          response->success = false;
          response->code = "STOP_FAILED";
          response->message = "stop failed";
          response->token = "";
          return;
        }
        if (state->lease_success.load()) {
          response->success = true;
          response->code = "OK";
          response->token = "token-1";
          response->remaining_s = request->duration_s;
        } else {
          response->success = false;
          response->code = "BUSY";
          response->message = "busy";
        }
      },
      rclcpp::ServicesQoS(),
      service_group_);

    stop_server_ = server_node_->create_service<chaos_interfaces::srv::Stop>(
      "/robots/test/motion/stop",
      [state = state_](
        const std::shared_ptr<chaos_interfaces::srv::Stop::Request> request,
        std::shared_ptr<chaos_interfaces::srv::Stop::Response> response) {
        if (state->stop_silent.load()) {
          std::this_thread::sleep_for(2s);
          return;
        }
        if (state->defer_responses.load()) {
          // The test decides when the client sees the answer.
          auto call = std::make_shared<DeferredCall>();
          call->operation = "stop";
          call->request_token = request->lease_token;
          call->apply = [pending = call.get(), response]() {*response = pending->stop_answer;};
          state->park(call);
          return;
        }
        response->success = true;
        response->code = "OK";
      },
      rclcpp::ServicesQoS(),
      service_group_);

    action_group_ = server_node_->create_callback_group(rclcpp::CallbackGroupType::Reentrant);
    action_server_ = rclcpp_action::create_server<NavigatePose>(
      server_node_, "/robots/test/navigation/to_pose",
      [state = state_](
        const rclcpp_action::GoalUUID &,
        std::shared_ptr<const NavigatePose::Goal>) {
        if (state->silent_goals.load() > 0) {
          // Simulate a server that does not answer the goal request.
          state->silent_goals.fetch_sub(1);
          std::this_thread::sleep_for(1s);
        }
        if (state->action_mode.load() == 1) {
          return rclcpp_action::GoalResponse::REJECT;
        }
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [state = state_](const std::shared_ptr<rclcpp_action::ServerGoalHandle<NavigatePose>>) {
        // rclcpp_action calls the cancel callback unconditionally; an empty
        // callback would terminate the process.
        if (state->slow_cancel.load()) {
          // The server answers the cancel request far too late.
          std::this_thread::sleep_for(1s);
        }
        return rclcpp_action::CancelResponse::ACCEPT;
      },
      [state = state_](std::shared_ptr<rclcpp_action::ServerGoalHandle<NavigatePose>> handle) {
        // A controlled goal: the mock keeps the handle of the accepted goal
        // and holds its terminal result until the test releases it. The
        // default behaviour of the mock is unchanged.
        std::shared_ptr<ControlledGoal> controlled;
        {
          const std::lock_guard<std::mutex> guard(state->calls_mutex);
          // Every accepted goal takes the next controlled goal of the queue.
          // The test therefore observes the goals in the order in which it
          // installed them.
          if (!state->controlled_queue.empty()) {
            controlled = state->controlled_queue.front();
            state->controlled_queue.erase(state->controlled_queue.begin());
          }
        }
        if (controlled) {
          controlled->accept(handle);
          // The wait is bounded: a test that never releases the gate must not
          // block the executor of the mock forever.
          controlled->await_result(30s);
          auto controlled_result = std::make_shared<NavigatePose::Result>();
          controlled_result->success = true;
          controlled_result->code = "OK";
          controlled_result->message = describe_target(handle->get_goal()->target);
          if (handle->is_canceling()) {
            handle->canceled(controlled_result);
          } else {
            handle->succeed(controlled_result);
          }
          return;
        }
        if (state->action_mode.load() == 2) {
          while (rclcpp::ok() && !handle->is_canceling()) {
            std::this_thread::sleep_for(20ms);
          }
        }
        if (state->succeed_despite_cancel.load()) {
          // The goal finishes on its own while the cancel request is still
          // unanswered.
          std::this_thread::sleep_for(100ms);
        }
        if (state->publish_feedback.load()) {
          auto feedback = std::make_shared<NavigatePose::Feedback>();
          feedback->progress = 0.5f;
          handle->publish_feedback(feedback);
        }
        auto result = std::make_shared<NavigatePose::Result>();
        result->success = true;
        result->code = "OK";
        result->message = describe_target(handle->get_goal()->target);
        if (handle->is_canceling()) {
          handle->canceled(result);
        } else {
          handle->succeed(result);
        }
      },
      rcl_action_server_get_default_options(),
      action_group_);

    // Several executor threads: a parked service callback must not block the
    // other services or the callbacks of the client.
    executor_ = std::make_shared<rclcpp::executors::MultiThreadedExecutor>(
      rclcpp::ExecutorOptions(), 4);
    executor_->add_node(server_node_);
    executor_->add_node(client_node_);
    spin_ = std::thread([this]() {executor_->spin();});

    robot_ = std::make_shared<chaos_sdk::Robot>(client_node_, "test");
  }

  void TearDown() override
  {
    // A failed ASSERT must not leave a blocked worker, a held goal or a
    // parked callback blocking the shutdown: every gate is released before
    // the executor is joined. park() also refuses callbacks that race with
    // this cleanup.
    //
    // The fixture owns every gate and every controlled goal of the test: the
    // singular ones are part of those lists, so a test that creates more of
    // them never leaves one behind.
    for (const auto & gate : user_gates_) {
      gate->release();
    }
    for (const auto & controlled : controlled_goals_) {
      controlled->release_result();
    }
    {
      const std::lock_guard<std::mutex> guard(state_->calls_mutex);
      state_->closing = true;
      for (const auto & call : state_->calls) {
        if (!call->answered.exchange(true)) {
          call->release.set_value();
        }
      }
    }
    if (robot_) {
      robot_->close();
    }
    executor_->cancel();
    if (spin_.joinable()) {
      spin_.join();
    }
    // A blocked worker would starve the feedback of the next test: the
    // dispatcher of the process must be idle again when the test is over.
    EXPECT_TRUE(wait_for_dispatcher_idle(0, 0, 5s));
  }

  /// Wait until the mock server parked at least ``count`` calls.
  bool wait_for_parked_calls(std::size_t count)
  {
    std::unique_lock<std::mutex> lock(state_->calls_mutex);
    return state_->calls_changed.wait_for(
      lock, 5s, [count, state = state_]() {return state->calls.size() >= count;});
  }

  /// The parked call for the given operation, or nullptr when there is none.
  std::shared_ptr<DeferredCall> parked_call(const std::string & operation)
  {
    const std::lock_guard<std::mutex> guard(state_->calls_mutex);
    for (const auto & call : state_->calls) {
      if (call->operation == operation && !call->answered.load()) {
        return call;
      }
    }
    return nullptr;
  }

  /// Hand the lease answer to the parked call and let the mock answer.
  void answer_lease(
    const std::shared_ptr<DeferredCall> & call,
    const chaos_interfaces::srv::ControlLease::Response & answer)
  {
    call->lease_answer = answer;
    call->answered.store(true);
    call->release.set_value();
  }

  /// Hand the stop answer to the parked call and let the mock answer.
  void answer_stop(
    const std::shared_ptr<DeferredCall> & call,
    const chaos_interfaces::srv::Stop::Response & answer)
  {
    call->stop_answer = answer;
    call->answered.store(true);
    call->release.set_value();
  }

  /// Release a parked call whose answer the test does not care about.
  void release_parked_call(const std::shared_ptr<DeferredCall> & call)
  {
    if (call && !call->answered.exchange(true)) {
      call->release.set_value();
    }
  }

  /// Install a controlled goal in the mock action server.
  ///
  /// The mock keeps the handle of the accepted goal and holds its terminal
  /// result until the test releases it. The default behaviour of the mock is
  /// unchanged: only the tests that install a controlled goal see the gate.
  std::shared_ptr<ControlledGoal> install_controlled_goal()
  {
    auto controlled = std::make_shared<ControlledGoal>();
    {
      const std::lock_guard<std::mutex> guard(state_->calls_mutex);
      // The mock hands the controlled goals to the accepted goals in the
      // order in which they were installed.
      state_->controlled_queue.push_back(controlled);
    }
    // Every controlled goal belongs to the fixture: the cleanup releases the
    // result of all of them, so no goal of a test keeps the executor of the
    // mock busy.
    controlled_goals_.push_back(controlled);
    controlled_ = controlled;
    return controlled;
  }

  /// The gate that a user feedback callback blocks on.
  ///
  /// The gate is released in the cleanup, so a blocked worker of the
  /// dispatcher never outlives the test.
  std::shared_ptr<UserGate> user_gate()
  {
    if (!user_gate_) {
      user_gate_ = new_user_gate();
    }
    return user_gate_;
  }

  /// A new gate that a user feedback callback blocks on.
  ///
  /// Every gate belongs to the fixture: the cleanup releases all of them, so
  /// a callback of a test never keeps a worker of the process busy.
  std::shared_ptr<UserGate> new_user_gate()
  {
    auto gate = std::make_shared<UserGate>();
    user_gates_.push_back(gate);
    return gate;
  }

  /// Run a goal whose feedback callback calls stop() and waits for its answer.
  ///
  /// The mock holds the terminal result of the goal until the callback is
  /// done, so the callback never runs against a goal that already finished.
  /// The callback records the outcome it really observed in an owned promise.
  StopFromFeedbackResult run_stop_from_feedback(bool defer_stop)
  {
    StopFromFeedbackResult result;
    auto controlled = install_controlled_goal();
    auto outcome = std::make_shared<OutcomeSlot>();
    auto probe = std::make_shared<FeedbackProbe>();

    state_->defer_responses.store(defer_stop);

    auto accepted = robot_->navigate(
      geometry_msgs::msg::PoseStamped(),
      [outcome, probe, robot = robot_](NavigatePose::Feedback feedback) {
        probe->enter();
        StopOutcome observed;
        try {
          // The callback blocks until the service answered: the outcome is
          // the answer of the service, never a timing of the test.
          const auto answer = robot->stop().get();
          observed.success = answer.success;
        } catch (const chaos_sdk::TimeoutError &) {
          observed.timed_out = true;
        } catch (const std::exception & error) {
          observed.error = error.what();
        }
        probe->leave();
        outcome->record(observed);
      });

    if (accepted.wait_for(5s) != std::future_status::ready) {
      return result;
    }
    result.task = accepted.get();
    if (!result.task || !controlled->wait_for_handle(5s)) {
      return result;
    }
    // The response timeout of the services is short: a deferred stop has to
    // run into it.
    robot_->set_response_timeout(0.3);
    // The callback runs and blocks in its stop request.
    publish_feedback_until(
      controlled, 0.5f, [probe]() {return probe->entry_count() >= 1;}, 5s);
    // The callback settles its promise with the outcome it really observed.
    if (outcome->future().wait_for(10s) == std::future_status::ready) {
      result.outcome = outcome->future().get();
      result.callback_ran = true;
    }
    // The server may answer the goal now that the callback is done.
    controlled->release_result();
    return result;
  }

  std::shared_ptr<MockState> state_;
  rclcpp::Node::SharedPtr server_node_;
  rclcpp::Node::SharedPtr client_node_;
  rclcpp::Service<chaos_interfaces::srv::GetDescription>::SharedPtr description_server_;
  rclcpp::Service<chaos_interfaces::srv::ControlLease>::SharedPtr lease_server_;
  rclcpp::Service<chaos_interfaces::srv::Stop>::SharedPtr stop_server_;
  rclcpp_action::Server<NavigatePose>::SharedPtr action_server_;
  rclcpp::CallbackGroup::SharedPtr action_group_;
  rclcpp::CallbackGroup::SharedPtr service_group_;
  rclcpp::executors::MultiThreadedExecutor::SharedPtr executor_;
  std::thread spin_;
  std::shared_ptr<chaos_sdk::Robot> robot_;
  /// The controlled goal of the mock, or nullptr when the mock answers the
  /// goals on its own.
  std::shared_ptr<ControlledGoal> controlled_;
  /// The gate that a user feedback callback blocks on.
  std::shared_ptr<UserGate> user_gate_;
  /// Every controlled goal of the test. The fixture owns all of them: the
  /// cleanup releases the terminal result of each of them.
  std::vector<std::shared_ptr<ControlledGoal>> controlled_goals_;
  /// Every gate of the test. The fixture owns all of them: the cleanup
  /// releases each of them.
  std::vector<std::shared_ptr<UserGate>> user_gates_;
};

// --- The lease cache transition (no ROS involved) --------------------------

TEST(LeaseCacheTransition, a_successful_stop_revokes_the_lease)
{
  const auto next = chaos_sdk::lease_cache_transition(
    "stop", true, "OK", "", "", "token-a", {"token-a", "operator"});
  EXPECT_EQ("", next.token);
  EXPECT_EQ("", next.owner);
}

TEST(LeaseCacheTransition, a_stop_with_a_failed_halt_still_revokes_the_lease)
{
  // The runtime revokes the lease before it issues the halt output, so a
  // failed halt output leaves the session revoked.
  const auto next = chaos_sdk::lease_cache_transition(
    "stop", false, "STOP_FAILED", "", "", "token-a", {"token-a", "operator"});
  EXPECT_EQ("", next.token);
  EXPECT_EQ("", next.owner);
}

TEST(LeaseCacheTransition, a_rejected_stop_keeps_the_lease)
{
  // A rejected stop never reached the revocation.
  const auto next = chaos_sdk::lease_cache_transition(
    "stop", false, "UNAUTHORIZED", "", "", "token-a", {"token-a", "operator"});
  EXPECT_EQ("token-a", next.token);
  EXPECT_EQ("operator", next.owner);
}

TEST(LeaseCacheTransition, a_stop_for_an_old_session_keeps_the_current_lease)
{
  const auto next = chaos_sdk::lease_cache_transition(
    "stop", true, "OK", "", "", "token-a", {"token-b", "other"});
  EXPECT_EQ("token-b", next.token);
  EXPECT_EQ("other", next.owner);
}

TEST(LeaseCacheTransition, a_renewal_never_rotates_the_token)
{
  const auto next = chaos_sdk::lease_cache_transition(
    "renew", true, "OK", "token-b", "operator", "token-a", {"token-a", "operator"});
  EXPECT_EQ("token-a", next.token);
  EXPECT_EQ("operator", next.owner);
}

TEST(LeaseCacheTransition, a_late_renewal_cannot_restore_a_revoked_lease)
{
  const auto next = chaos_sdk::lease_cache_transition(
    "renew", true, "OK", "token-a", "operator", "token-a", {"", ""});
  EXPECT_EQ("", next.token);
  EXPECT_EQ("", next.owner);
}

TEST(LeaseCacheTransition, a_release_clears_the_lease_of_its_own_session_only)
{
  const auto released = chaos_sdk::lease_cache_transition(
    "release", true, "OK", "", "operator", "token-a", {"token-a", "operator"});
  EXPECT_EQ("", released.token);
  EXPECT_EQ("", released.owner);
  const auto stale = chaos_sdk::lease_cache_transition(
    "release", true, "OK", "", "operator", "token-a", {"token-b", "other"});
  EXPECT_EQ("token-b", stale.token);
  EXPECT_EQ("other", stale.owner);
}

TEST(LeaseCacheTransition, a_release_with_a_failed_stop_still_revokes_the_lease)
{
  const auto next = chaos_sdk::lease_cache_transition(
    "release", false, "STOP_FAILED", "", "operator", "token-a", {"token-a", "operator"});
  EXPECT_EQ("", next.token);
  EXPECT_EQ("", next.owner);
}

TEST(LeaseCacheTransition, an_acquire_installs_the_returned_token)
{
  const auto next = chaos_sdk::lease_cache_transition(
    "acquire", true, "OK", "token-b", "other", "", {"token-a", "operator"});
  EXPECT_EQ("token-b", next.token);
  EXPECT_EQ("other", next.owner);
  const auto failed = chaos_sdk::lease_cache_transition(
    "acquire", false, "BUSY", "", "other", "", {"token-a", "operator"});
  EXPECT_EQ("token-a", failed.token);
  EXPECT_EQ("operator", failed.owner);
}

TEST_F(SdkTest, describe_caches_frame_and_timeout)
{
  auto future = robot_->describe();
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  const auto response = future.get();
  EXPECT_EQ("test/base", response.base_frame);
  EXPECT_EQ("test/base", robot_->base_frame());
  EXPECT_DOUBLE_EQ(0.2, robot_->command_timeout_s());
}

TEST_F(SdkTest, acquire_installs_token_and_failure_keeps_state)
{
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  EXPECT_TRUE(future.get().success);
  EXPECT_EQ("token-1", robot_->lease_token());
  EXPECT_EQ("operator", robot_->owner());

  state_->lease_success.store(false);
  auto failed = robot_->acquire("other", 5.0);
  ASSERT_EQ(std::future_status::ready, failed.wait_for(5s));
  EXPECT_FALSE(failed.get().success);
  // A failed acquire must not replace the live lease.
  EXPECT_EQ("token-1", robot_->lease_token());
}

TEST_F(SdkTest, release_stop_failure_revokes_token_but_rejection_keeps_it)
{
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));

  state_->lease_stop_failed.store(true);
  auto stopped = robot_->release();
  ASSERT_EQ(std::future_status::ready, stopped.wait_for(5s));
  EXPECT_FALSE(stopped.get().success);
  EXPECT_EQ("", robot_->lease_token());
  EXPECT_EQ("", robot_->owner());
}

TEST_F(SdkTest, velocity_requires_token_and_frame)
{
  EXPECT_THROW(robot_->velocity(1.0), std::invalid_argument);
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  // No describe() yet: the base frame is unknown.
  EXPECT_THROW(robot_->velocity(1.0), std::invalid_argument);
  auto described = robot_->describe();
  ASSERT_EQ(std::future_status::ready, described.wait_for(5s));
  const std::string request_id = robot_->velocity(0.5, 0.0, 0.0, 0.1);
  EXPECT_FALSE(request_id.empty());
}

TEST_F(SdkTest, silent_service_fails_with_local_timeout)
{
  robot_->set_response_timeout(0.3);
  state_->lease_silent.store(true);
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  EXPECT_THROW(future.get(), chaos_sdk::TimeoutError);
  // The pending slot is released, so the client accepts work again.
  EXPECT_EQ(0u, robot_->pending_count());
}

TEST_F(SdkTest, action_accepts_reports_result_and_cancels)
{
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));

  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  EXPECT_TRUE(task->accepted());
  EXPECT_FALSE(task->goal_id().empty());
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  const auto wrapped = task->result().get();
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, wrapped.code);
  EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, task->state());
}

TEST_F(SdkTest, rejected_action_resolves_without_task)
{
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  state_->action_mode.store(1);
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  EXPECT_EQ(nullptr, goal.get());
}

TEST_F(SdkTest, hanging_action_can_be_canceled_through_the_task)
{
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  state_->action_mode.store(2);
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());
  auto canceled = task->cancel();
  ASSERT_EQ(std::future_status::ready, canceled.wait_for(5s));
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::CANCELED, task->result().get().code);
}

TEST_F(SdkTest, close_resolves_pending_and_rejects_later_calls)
{
  robot_->set_response_timeout(30.0);
  state_->lease_silent.store(true);
  auto future = robot_->acquire("operator", 5.0);
  std::this_thread::sleep_for(200ms);
  robot_->close();
  EXPECT_THROW(future.get(), chaos_sdk::ClientClosedError);
  EXPECT_THROW(robot_->describe(), chaos_sdk::ClientClosedError);
  robot_->close();  // idempotent
}

TEST_F(SdkTest, pending_requests_are_bounded)
{
  robot_->set_max_pending(1);
  robot_->set_response_timeout(30.0);
  state_->lease_silent.store(true);
  auto first = robot_->acquire("operator", 5.0);
  EXPECT_THROW(robot_->acquire("operator", 5.0), chaos_sdk::PendingOverflowError);
  robot_->close();
  EXPECT_THROW(first.get(), chaos_sdk::ClientClosedError);
}

TEST_F(SdkTest, sequential_actions_do_not_exhaust_the_pending_budget)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  // More goals than the pending budget of 64: every completed goal must
  // release its slot, otherwise the budget would be exhausted.
  for (int index = 0; index < 70; ++index) {
    auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
    ASSERT_EQ(std::future_status::ready, goal.wait_for(5s)) << "goal " << index;
    auto task = goal.get();
    ASSERT_NE(nullptr, task) << "goal " << index;
    ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s)) << "goal " << index;
    EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, task->state()) << "goal " << index;
  }
  EXPECT_EQ(0u, robot_->pending_count());
}

TEST_F(SdkTest, late_goal_response_does_not_resurrect_the_task)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  robot_->set_response_timeout(0.3);
  state_->silent_goals.store(1);
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  EXPECT_THROW(goal.get(), chaos_sdk::TimeoutError);
  // The late goal response must not create a task and must not keep the
  // pending budget occupied.
  std::this_thread::sleep_for(1500ms);
  EXPECT_EQ(0u, robot_->pending_count());
}

TEST_F(SdkTest, a_silent_goal_does_not_block_other_actions)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  state_->silent_goals.store(1);
  auto silent = robot_->navigate(geometry_msgs::msg::PoseStamped());
  auto healthy = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, healthy.wait_for(5s));
  auto task = healthy.get();
  ASSERT_NE(nullptr, task);
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, task->result().get().code);
  // The silent goal is still observed, but it did not disturb the other one.
  EXPECT_EQ(1u, robot_->pending_count());
  EXPECT_EQ(std::future_status::timeout, silent.wait_for(200ms));
}

TEST_F(SdkTest, close_during_send_is_safe)
{
  std::atomic<bool> stop{false};
  std::atomic<int> failures{0};
  std::thread sender([this, &stop, &failures]() {
      while (!stop.load()) {
        try {
          auto future = robot_->acquire("operator", 5.0);
          std::future_status status = future.wait_for(std::chrono::milliseconds(5));
          if (status == std::future_status::ready) {
            try {
              future.get();
            } catch (const chaos_sdk::ClientClosedError &) {
              failures.fetch_add(1);
              return;
            } catch (const std::exception &) {
            }
          }
        } catch (const chaos_sdk::ClientClosedError &) {
          failures.fetch_add(1);
          return;
        } catch (const std::exception &) {
        }
      }
    });
  std::this_thread::sleep_for(50ms);
  robot_->close();
  stop.store(true);
  sender.join();
  EXPECT_GT(failures.load(), 0);
  EXPECT_THROW(robot_->describe(), chaos_sdk::ClientClosedError);
}

TEST_F(SdkTest, feedback_callback_exception_does_not_kill_the_executor)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));

  // The goal is controlled: the mock server holds its terminal result, so
  // the test can prove that the feedback callback really entered and really
  // threw before the goal reaches its terminal state. A completed result
  // alone proves nothing about the feedback.
  auto controlled = install_controlled_goal();
  auto probe = std::make_shared<FeedbackProbe>();
  const std::size_t exceptions_before = dispatcher().callback_exception_count();

  auto accepted = robot_->navigate(
    geometry_msgs::msg::PoseStamped(),
    [probe](NavigatePose::Feedback feedback) {
      probe->enter();
      probe->mark_throw();
      throw std::runtime_error("user callback failure");
    });
  ASSERT_EQ(std::future_status::ready, accepted.wait_for(5s));
  auto task = accepted.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());
  ASSERT_TRUE(controlled->wait_for_handle(5s));

  // The callback really entered and really threw.
  ASSERT_TRUE(publish_feedback_until(
    controlled, 0.25f, [probe]() {return probe->entry_count() >= 1;}, 5s));
  ASSERT_TRUE(probe->wait_for_throw(5s));
  // The dispatcher caught the exception instead of losing its worker.
  ASSERT_TRUE(wait_for_callback_exception(exceptions_before, 5s));

  // The goal is still running: the server holds its terminal result.
  EXPECT_EQ(chaos_sdk::TaskState::ACCEPTED, task->state());

  // Now the server answers the goal.
  controlled->release_result();
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, task->result().get().code);
  EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, task->state());

  // The executor is still alive: a following request still works.
  auto described = robot_->describe();
  ASSERT_EQ(std::future_status::ready, described.wait_for(5s));
  EXPECT_EQ("test", described.get().asset_id);
}

TEST_F(SdkTest, a_blocked_feedback_callback_keeps_the_sdk_responsive_and_retires_the_queued_sample)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));

  auto controlled = install_controlled_goal();
  auto probe = std::make_shared<FeedbackProbe>();
  auto gate = user_gate();
  ASSERT_TRUE(wait_for_dispatcher_idle(0, 0, 5s));
  const std::size_t pending_before = dispatcher().pending_count();
  const std::size_t active_before = dispatcher().active_count();

  auto accepted = robot_->navigate(
    geometry_msgs::msg::PoseStamped(),
    [probe, gate](NavigatePose::Feedback feedback) {
      probe->enter();
      // The callback blocks: its worker stays busy and the scope of the
      // Robot stays active, so the next sample of the goal waits in the
      // queue of the dispatcher.
      gate->wait(30s);
      probe->leave();
    });
  ASSERT_EQ(std::future_status::ready, accepted.wait_for(5s));
  auto task = accepted.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());
  ASSERT_TRUE(controlled->wait_for_handle(5s));

  // The first sample reaches the callback, which blocks there.
  ASSERT_TRUE(publish_feedback_until(
    controlled, 0.25f, [probe]() {return probe->entry_count() >= 1;}, 5s));
  ASSERT_TRUE(probe->wait_for_entries(1, 5s));

  // A second sample of the same goal waits in the queue of the dispatcher.
  ASSERT_TRUE(publish_feedback_until(
    controlled, 0.5f,
    [pending_before]() {return dispatcher().pending_count() > pending_before;}, 5s));

  // The blocked callback does not block the SDK: the deferred service runs
  // into its own response timeout.
  robot_->set_response_timeout(0.3);
  state_->defer_responses.store(true);
  auto described = robot_->describe();
  ASSERT_EQ(std::future_status::ready, described.wait_for(2s));
  EXPECT_THROW(described.get(), chaos_sdk::TimeoutError);
  ASSERT_TRUE(wait_for_parked_calls(1));
  release_parked_call(parked_call("describe"));

  // Only the server result is released: the goal reaches its terminal state
  // and the queued sample of the finished goal is retired.
  controlled->release_result();
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, task->result().get().code);
  EXPECT_TRUE(task->accepted());
  EXPECT_FALSE(task->goal_id().empty());
  EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, task->state());
  // The queued sample is gone: the terminal state retired it.
  ASSERT_TRUE(wait_for_pending_count(pending_before, 5s));

  // The user feedback is released last: the blocked callback returns and no
  // stale invocation of the retired sample follows.
  gate->release();
  ASSERT_TRUE(wait_for_dispatcher_idle(pending_before, active_before, 5s));
  EXPECT_EQ(1u, probe->entry_count());
  EXPECT_EQ(1u, probe->completion_count());
}

TEST_F(SdkTest, a_feedback_callback_that_calls_stop_clears_the_cached_lease)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  ASSERT_EQ("token-1", robot_->lease_token());

  const auto run = run_stop_from_feedback(false);
  ASSERT_TRUE(run.callback_ran);
  ASSERT_TRUE(run.outcome.success);
  EXPECT_FALSE(run.outcome.timed_out);
  EXPECT_TRUE(run.outcome.error.empty());
  // The runtime revoked the lease before it halted the motion.
  EXPECT_EQ("", robot_->lease_token());
  EXPECT_EQ("", robot_->owner());

  // The goal of the callback finished after the callback was done.
  ASSERT_NE(nullptr, run.task);
  ASSERT_EQ(std::future_status::ready, run.task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, run.task->result().get().code);

  // The client never acquires a lease implicitly: the motion needs an
  // explicit acquire again.
  EXPECT_THROW(robot_->navigate(geometry_msgs::msg::PoseStamped()), std::invalid_argument);
  auto reacquired = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, reacquired.wait_for(5s));
  EXPECT_TRUE(reacquired.get().success);
  ASSERT_EQ("token-1", robot_->lease_token());

  // The motion works again after the explicit acquire.
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, task->result().get().code);
}

TEST_F(SdkTest, a_feedback_callback_whose_stop_times_out_keeps_the_cached_lease)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  ASSERT_EQ("token-1", robot_->lease_token());

  const auto run = run_stop_from_feedback(true);
  ASSERT_TRUE(run.callback_ran);
  EXPECT_FALSE(run.outcome.success);
  EXPECT_TRUE(run.outcome.timed_out);
  EXPECT_TRUE(run.outcome.error.empty());
  // A transport timeout proves neither a successful stop nor a rejection:
  // the cached lease stays.
  EXPECT_EQ("token-1", robot_->lease_token());
  EXPECT_EQ("operator", robot_->owner());

  // The goal of the callback finished after the callback was done.
  ASSERT_NE(nullptr, run.task);
  ASSERT_EQ(std::future_status::ready, run.task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, run.task->result().get().code);

  // Prove the timed-out stop reached the server, then release it for cleanup.
  ASSERT_TRUE(wait_for_parked_calls(1));
  const auto parked_stop = parked_call("stop");
  ASSERT_NE(nullptr, parked_stop);
  release_parked_call(parked_stop);
}

TEST_F(SdkTest, a_successful_result_is_reported_as_succeeded)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  const auto wrapped = task->result().get();
  ASSERT_NE(nullptr, wrapped.result);
  // The mock server answers with success == true; the SDK must not claim a
  // business failure as a success and vice versa.
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, wrapped.code);
  EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, task->state());
}

TEST(TaskStatePolicy, a_terminal_status_without_a_result_is_never_a_success)
{
  // The payload carries the business outcome. A terminal status without a
  // payload therefore never proves success, not even with success == true.
  EXPECT_EQ(
    chaos_sdk::TaskState::UNKNOWN,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::SUCCEEDED, false, true));
  EXPECT_EQ(
    chaos_sdk::TaskState::UNKNOWN,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::SUCCEEDED, false, false));
  EXPECT_EQ(
    chaos_sdk::TaskState::UNKNOWN,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::ABORTED, false, true));
  // With a payload the classification follows the status and the business
  // outcome.
  EXPECT_EQ(
    chaos_sdk::TaskState::SUCCEEDED,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::SUCCEEDED, true, true));
  EXPECT_EQ(
    chaos_sdk::TaskState::FAILED,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::SUCCEEDED, true, false));
  EXPECT_EQ(
    chaos_sdk::TaskState::CANCELED,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::CANCELED, true, true));
  EXPECT_EQ(
    chaos_sdk::TaskState::UNKNOWN,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::UNKNOWN, true, true));
  EXPECT_EQ(
    chaos_sdk::TaskState::FAILED,
    chaos_sdk::classify_result(rclcpp_action::ResultCode::ABORTED, true, false));
}

TEST_F(SdkTest, the_acceptance_and_the_goal_id_survive_the_release_of_the_handle)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());
  const std::string goal_id = task->goal_id();
  ASSERT_FALSE(goal_id.empty());
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  // The ROS resources of the goal are released with the terminal state, but
  // the readable history must survive that cleanup.
  EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, task->state());
  EXPECT_TRUE(task->accepted());
  EXPECT_EQ(goal_id, task->goal_id());
}

TEST_F(SdkTest, the_target_pose_reaches_the_server_unchanged)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  geometry_msgs::msg::PoseStamped target;
  target.header.frame_id = "test/map";
  target.pose.position.x = 1.5;
  target.pose.position.y = -2.5;
  target.pose.position.z = 0.25;
  target.pose.orientation.w = 1.0;
  auto goal = robot_->navigate(target);
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  const auto wrapped = task->result().get();
  ASSERT_NE(nullptr, wrapped.result);
  EXPECT_EQ("frame=test/map x=1.5 y=-2.5 z=0.25 qx=0 qy=0 qz=0 qw=1", wrapped.result->message);
}

TEST_F(SdkTest, a_cancel_timeout_does_not_overwrite_a_completed_goal)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  robot_->set_response_timeout(0.3);
  robot_->set_action_timeout(5.0);
  // The goal finishes on its own while the cancel request stays unanswered.
  state_->succeed_despite_cancel.store(true);
  state_->slow_cancel.store(true);
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());

  auto canceled = task->cancel();
  ASSERT_EQ(std::future_status::ready, canceled.wait_for(5s));
  // The server answers the cancel request far too late.
  EXPECT_THROW(canceled.get(), chaos_sdk::TimeoutError);

  // The goal reached its terminal state before the cancel timed out. The
  // unanswered cancel must not overwrite that terminal state and must not
  // fail the result of a goal that already finished.
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  const auto wrapped = task->result().get();
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, wrapped.code);
  EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, task->state());
  EXPECT_EQ(0u, robot_->pending_count());
}

TEST_F(SdkTest, a_retired_slot_releases_its_ros_client)
{
  // The pool is the unit that owns the ROS clients of the goals. The weak
  // reference proves that a retired slot really destroys its client instead
  // of only counting the slots.
  //
  // The node of the pool is deliberately not spun: the goal request stays
  // unanswered and the client is owned by the pool alone, so the weak
  // reference shows exactly what the pool releases.
  auto pool_node = std::make_shared<rclcpp::Node>("pool_probe");
  chaos_sdk::ActionSlotPool<NavigatePose> pool(2);
  auto lease = pool.acquire(pool_node, "/robots/test/navigation/to_pose");
  auto weak_client = pool.weak_client(lease.index);
  ASSERT_FALSE(weak_client.expired());

  // The goal request stays unanswered while the slot is retired.
  NavigatePose::Goal goal;
  goal.lease_token = "token-1";
  auto answered = std::make_shared<std::atomic<bool>>(false);
  using Client = rclcpp_action::Client<NavigatePose>;
  Client::SendGoalOptions options;
  options.goal_response_callback =
    [answered](Client::GoalHandle::SharedPtr) {answered->store(true);};
  {
    // Neither the lease nor the local reference may keep the client alive:
    // the pool owns it.
    auto client = lease.client;
    lease.client.reset();
    client->async_send_goal(goal, options);
  }
  pool.release(lease.index, lease.generation, true);
  // The client of the retired slot is gone: the weak reference expired.
  EXPECT_TRUE(weak_client.expired());
  EXPECT_EQ(0u, pool.live_clients());

  // The late goal response must not resurrect the client.
  std::this_thread::sleep_for(1500ms);
  EXPECT_TRUE(weak_client.expired());
  EXPECT_FALSE(answered->load());
}

TEST_F(SdkTest, more_goals_than_the_pool_capacity_release_every_client)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  robot_->set_response_timeout(0.3);
  robot_->set_action_timeout(1.0);
  // More goals than the pool capacity of eight: every timed out goal must
  // retire its slot and release its ROS client, otherwise the pool would be
  // exhausted and the next goal would be refused.
  state_->silent_goals.store(10);
  for (int index = 0; index < 10; ++index) {
    auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
    ASSERT_EQ(std::future_status::ready, goal.wait_for(5s)) << "goal " << index;
    EXPECT_THROW(goal.get(), chaos_sdk::TimeoutError) << "goal " << index;
  }
  // The timed out observations are gone and their clients were released.
  EXPECT_EQ(0u, robot_->pending_count());
  EXPECT_EQ(0u, robot_->action_client_count());
  EXPECT_LE(robot_->action_slot_count(), 8u);
  // A healthy goal still works after the retired slots.
  state_->silent_goals.store(0);
  auto healthy = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, healthy.wait_for(5s));
  auto task = healthy.get();
  ASSERT_NE(nullptr, task);
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, task->result().get().code);
}

TEST_F(SdkTest, silent_goals_release_their_clients_and_keep_the_pool_bounded)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  robot_->set_response_timeout(0.3);
  robot_->set_action_timeout(1.0);
  // Six goals that the server never answers at once: every one of them must
  // time out, release its slot and retire its ROS client.
  state_->silent_goals.store(6);
  std::vector<std::shared_future<std::shared_ptr<chaos_sdk::Robot::NavigationTask>>> goals;
  for (int index = 0; index < 6; ++index) {
    goals.push_back(robot_->navigate(geometry_msgs::msg::PoseStamped()));
  }
  const std::size_t peak_clients = robot_->action_client_count();
  EXPECT_LE(peak_clients, 8u);
  for (auto & goal : goals) {
    ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
    EXPECT_THROW(goal.get(), chaos_sdk::TimeoutError);
  }
  // The timed out observations are gone and their clients were released.
  EXPECT_EQ(0u, robot_->pending_count());
  EXPECT_LE(robot_->action_client_count(), peak_clients);
  EXPECT_LE(robot_->action_slot_count(), 8u);
  // A healthy goal still works after the retired slots.
  state_->silent_goals.store(0);
  auto healthy = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, healthy.wait_for(5s));
  auto task = healthy.get();
  ASSERT_NE(nullptr, task);
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_EQ(rclcpp_action::ResultCode::SUCCEEDED, task->result().get().code);
}

TEST_F(SdkTest, a_late_service_response_does_not_restore_the_lease_after_close)
{
  robot_->set_response_timeout(0.3);
  state_->lease_silent.store(true);
  auto future = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
  EXPECT_THROW(future.get(), chaos_sdk::TimeoutError);
  EXPECT_EQ("", robot_->lease_token());
  robot_->close();
  // The server answers after the timeout and after close(): the claim fails,
  // so the cached lease state stays untouched.
  std::this_thread::sleep_for(1200ms);
  EXPECT_EQ("", robot_->lease_token());
  EXPECT_EQ("", robot_->owner());
}

TEST_F(SdkTest, a_cancel_that_is_never_answered_stops_the_goal_observation)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  robot_->set_response_timeout(0.3);
  robot_->set_action_timeout(5.0);
  state_->action_mode.store(2);  // the goal never finishes
  state_->slow_cancel.store(true);
  auto goal = robot_->navigate(geometry_msgs::msg::PoseStamped());
  ASSERT_EQ(std::future_status::ready, goal.wait_for(5s));
  auto task = goal.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());
  auto canceled = task->cancel();
  ASSERT_EQ(std::future_status::ready, canceled.wait_for(5s));
  // The cancel request was not answered in time.
  EXPECT_THROW(canceled.get(), chaos_sdk::TimeoutError);
  // The observation of the goal itself stopped and everything is settled.
  EXPECT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_THROW(task->result().get(), chaos_sdk::TimeoutError);
  EXPECT_EQ(0u, robot_->pending_count());
}

// --- The lease cache against the real client --------------------------------
//
// The mock server parks the service callbacks, so the test decides when and
// in which order the client sees the answers. That keeps the order of the
// answers under test control without any sleep based timing.

TEST_F(SdkTest, a_successful_stop_revokes_the_cached_lease)
{
  robot_->set_response_timeout(10.0);
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  ASSERT_EQ("token-1", robot_->lease_token());

  state_->defer_responses.store(true);
  auto stopped = robot_->stop();
  ASSERT_TRUE(wait_for_parked_calls(1));
  auto stop_request = parked_call("stop");
  ASSERT_NE(nullptr, stop_request);
  // The stop request carried the token of the current lease.
  EXPECT_EQ("token-1", stop_request->request_token);

  answer_stop(stop_request, stop_answer_ok());
  ASSERT_EQ(std::future_status::ready, stopped.wait_for(5s));
  EXPECT_TRUE(stopped.get().success);
  // The runtime revoked the lease before it halted the motion.
  EXPECT_EQ("", robot_->lease_token());
  EXPECT_EQ("", robot_->owner());
}

TEST_F(SdkTest, a_stop_with_a_failed_halt_still_revokes_the_cached_lease)
{
  robot_->set_response_timeout(10.0);
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  ASSERT_EQ("token-1", robot_->lease_token());

  state_->defer_responses.store(true);
  auto stopped = robot_->stop();
  ASSERT_TRUE(wait_for_parked_calls(1));
  auto stop_request = parked_call("stop");
  ASSERT_NE(nullptr, stop_request);

  chaos_interfaces::srv::Stop::Response answer;
  answer.success = false;
  answer.code = "STOP_FAILED";
  answer.message = "stop output failed and will be retried";
  answer_stop(stop_request, answer);
  ASSERT_EQ(std::future_status::ready, stopped.wait_for(5s));
  EXPECT_FALSE(stopped.get().success);
  // The runtime revoked the lease before it issued the halt output.
  EXPECT_EQ("", robot_->lease_token());
  EXPECT_EQ("", robot_->owner());
}

TEST_F(SdkTest, a_rejected_stop_keeps_the_cached_lease)
{
  robot_->set_response_timeout(10.0);
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  ASSERT_EQ("token-1", robot_->lease_token());

  state_->defer_responses.store(true);
  auto stopped = robot_->stop();
  ASSERT_TRUE(wait_for_parked_calls(1));
  auto stop_request = parked_call("stop");
  ASSERT_NE(nullptr, stop_request);

  answer_stop(stop_request, stop_answer_rejected("UNAUTHORIZED"));
  ASSERT_EQ(std::future_status::ready, stopped.wait_for(5s));
  EXPECT_FALSE(stopped.get().success);
  // A rejected stop never reached the revocation: the lease stays valid.
  EXPECT_EQ("token-1", robot_->lease_token());
  EXPECT_EQ("operator", robot_->owner());
}

TEST_F(SdkTest, a_late_renew_answer_cannot_restore_a_revoked_lease)
{
  robot_->set_response_timeout(10.0);
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  ASSERT_EQ("token-1", robot_->lease_token());

  state_->defer_responses.store(true);
  auto renewal = robot_->renew(5.0);
  auto stopped = robot_->stop();
  ASSERT_TRUE(wait_for_parked_calls(2));
  auto renew_request = parked_call("renew");
  auto stop_request = parked_call("stop");
  ASSERT_NE(nullptr, renew_request);
  ASSERT_NE(nullptr, stop_request);
  // The renewal belongs to the session that the stop ends.
  EXPECT_EQ("token-1", renew_request->request_token);

  answer_stop(stop_request, stop_answer_ok());
  ASSERT_EQ(std::future_status::ready, stopped.wait_for(5s));
  EXPECT_EQ("", robot_->lease_token());

  // The renewal answer arrives after the revocation: it must not install the
  // revoked token again.
  answer_lease(renew_request, lease_answer_ok("token-1"));
  ASSERT_EQ(std::future_status::ready, renewal.wait_for(5s));
  EXPECT_TRUE(renewal.get().success);
  EXPECT_EQ("", robot_->lease_token());
  EXPECT_EQ("", robot_->owner());
}

TEST_F(SdkTest, a_late_stop_answer_keeps_a_newer_lease)
{
  robot_->set_response_timeout(10.0);
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  ASSERT_EQ("token-1", robot_->lease_token());

  state_->defer_responses.store(true);
  auto stopped = robot_->stop();
  ASSERT_TRUE(wait_for_parked_calls(1));
  auto stop_request = parked_call("stop");
  ASSERT_NE(nullptr, stop_request);
  EXPECT_EQ("token-1", stop_request->request_token);

  // A new session starts while the stop is still unanswered.
  auto second = robot_->acquire("other", 5.0);
  ASSERT_TRUE(wait_for_parked_calls(2));
  auto acquire_request = parked_call("acquire");
  ASSERT_NE(nullptr, acquire_request);
  answer_lease(acquire_request, lease_answer_ok("token-2"));
  ASSERT_EQ(std::future_status::ready, second.wait_for(5s));
  ASSERT_EQ("token-2", robot_->lease_token());

  // The late stop belongs to the old session: it must not clear the new lease.
  answer_stop(stop_request, stop_answer_ok());
  ASSERT_EQ(std::future_status::ready, stopped.wait_for(5s));
  EXPECT_TRUE(stopped.get().success);
  EXPECT_EQ("token-2", robot_->lease_token());
  EXPECT_EQ("other", robot_->owner());
}

TEST_F(SdkTest, a_late_renew_answer_keeps_a_newer_lease)
{
  robot_->set_response_timeout(10.0);
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  ASSERT_EQ("token-1", robot_->lease_token());

  state_->defer_responses.store(true);
  auto renewal = robot_->renew(5.0);
  ASSERT_TRUE(wait_for_parked_calls(1));
  auto renew_request = parked_call("renew");
  ASSERT_NE(nullptr, renew_request);
  EXPECT_EQ("token-1", renew_request->request_token);

  auto second = robot_->acquire("other", 5.0);
  ASSERT_TRUE(wait_for_parked_calls(2));
  auto acquire_request = parked_call("acquire");
  ASSERT_NE(nullptr, acquire_request);
  answer_lease(acquire_request, lease_answer_ok("token-2"));
  ASSERT_EQ(std::future_status::ready, second.wait_for(5s));
  ASSERT_EQ("token-2", robot_->lease_token());

  answer_lease(renew_request, lease_answer_ok("token-1"));
  ASSERT_EQ(std::future_status::ready, renewal.wait_for(5s));
  EXPECT_TRUE(renewal.get().success);
  EXPECT_EQ("token-2", robot_->lease_token());
  EXPECT_EQ("other", robot_->owner());
}

TEST_F(SdkTest, a_stop_timeout_keeps_the_cached_lease)
{
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  ASSERT_EQ("token-1", robot_->lease_token());
  robot_->set_response_timeout(0.3);
  state_->defer_responses.store(true);
  auto stopped = robot_->stop();
  ASSERT_TRUE(wait_for_parked_calls(1));
  auto stop_request = parked_call("stop");
  ASSERT_NE(nullptr, stop_request);
  ASSERT_EQ(std::future_status::ready, stopped.wait_for(5s));
  EXPECT_THROW(stopped.get(), chaos_sdk::TimeoutError);
  // A transport timeout proves neither successful stop nor rejection.
  EXPECT_EQ("token-1", robot_->lease_token());
  EXPECT_EQ("operator", robot_->owner());
  answer_stop(stop_request, stop_answer_ok());
}

TEST_F(SdkTest, a_late_release_answer_keeps_a_newer_lease)
{
  robot_->set_response_timeout(10.0);
  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(5s));
  ASSERT_EQ("token-1", robot_->lease_token());

  state_->defer_responses.store(true);
  auto released = robot_->release();
  ASSERT_TRUE(wait_for_parked_calls(1));
  auto release_request = parked_call("release");
  ASSERT_NE(nullptr, release_request);
  EXPECT_EQ("token-1", release_request->request_token);

  // A new session starts while the release is still unanswered.
  auto second = robot_->acquire("other", 5.0);
  ASSERT_TRUE(wait_for_parked_calls(2));
  auto acquire_request = parked_call("acquire");
  ASSERT_NE(nullptr, acquire_request);
  answer_lease(acquire_request, lease_answer_ok("token-2"));
  ASSERT_EQ(std::future_status::ready, second.wait_for(5s));
  ASSERT_EQ("token-2", robot_->lease_token());

  // The late release belongs to the old session: it must not clear the new
  // lease.
  answer_lease(release_request, lease_answer_ok(""));
  ASSERT_EQ(std::future_status::ready, released.wait_for(5s));
  EXPECT_TRUE(released.get().success);
  EXPECT_EQ("token-2", robot_->lease_token());
  EXPECT_EQ("other", robot_->owner());
}

// --- The close of a client that owns its executor -------------------------
//
// The two cases below use a standalone client: it owns its node and its
// executor, so its close cancels that executor and joins its thread. That is
// what a standalone close does and what a close of a client that borrows the
// node of the application does not.

TEST_F(SdkTest, a_close_from_a_feedback_callback_returns_and_settles_the_pending_futures)
{
  ASSERT_TRUE(wait_for_dispatcher_idle(0, 0, 5s));

  // The client owns its node and its executor: the close below joins that
  // executor.
  auto standalone = chaos_sdk::Robot::create_standalone(
    "test", std::nullopt, std::nullopt, 0.5, 5.0, 30.0);
  ASSERT_NE(nullptr, standalone);

  auto lease = standalone->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(10s));
  ASSERT_TRUE(lease.get().success);

  // The goal is controlled: the mock server holds its terminal result, so the
  // callback really runs against a goal that is still alive.
  auto controlled = install_controlled_goal();
  auto returned = std::make_shared<Signal>();

  // The callback refers to the client weakly: a strong reference would keep
  // the client alive through its own callback and would form a cycle.
  std::weak_ptr<chaos_sdk::Robot> weak_robot = standalone;
  auto accepted = standalone->navigate(
    geometry_msgs::msg::PoseStamped(),
    [weak_robot, returned](NavigatePose::Feedback feedback) {
      // The close runs on a worker of the feedback pool, never on the
      // executor of the client.
      if (auto robot = weak_robot.lock()) {
        robot->close();
      }
      returned->raise();
    });
  ASSERT_EQ(std::future_status::ready, accepted.wait_for(10s));
  auto task = accepted.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());
  const std::string goal_id = task->goal_id();
  ASSERT_FALSE(goal_id.empty());
  ASSERT_TRUE(controlled->wait_for_handle(10s));

  // The callback runs and closes the client from there.
  ASSERT_TRUE(publish_feedback_until(
    controlled, 0.25f, [returned]() {return returned->raised();}, 20s));
  // The close returned: the callback really finished.
  ASSERT_EQ(std::future_status::ready, returned->future().wait_for(20s));

  // The pending future of the goal is settled with the close error.
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(5s));
  EXPECT_THROW(task->result().get(), chaos_sdk::ClientClosedError);
  // The readable history of the goal survives the close.
  EXPECT_TRUE(task->accepted());
  EXPECT_EQ(goal_id, task->goal_id());
  // A closed client says nothing about the remote goal: the state is unknown.
  EXPECT_EQ(chaos_sdk::TaskState::UNKNOWN, task->state());
  // A request after the close is rejected.
  EXPECT_THROW(standalone->describe(), chaos_sdk::ClientClosedError);
  EXPECT_EQ(0u, standalone->pending_count());

  // The gate of the mock server is released by the cleanup of the fixture:
  // the controlled goal may finish after the end of the test.
}

TEST_F(SdkTest, an_external_close_publishes_the_terminal_state_before_the_future)
{
  ASSERT_TRUE(wait_for_dispatcher_idle(0, 0, 5s));
  const std::size_t pending_before = dispatcher().pending_count();
  const std::size_t active_before = dispatcher().active_count();

  auto standalone = chaos_sdk::Robot::create_standalone(
    "test", std::nullopt, std::nullopt, 0.5, 5.0, 30.0);
  ASSERT_NE(nullptr, standalone);

  auto lease = standalone->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(10s));
  ASSERT_TRUE(lease.get().success);

  auto controlled = install_controlled_goal();
  auto probe = std::make_shared<FeedbackProbe>();
  auto gate = user_gate();

  auto accepted = standalone->navigate(
    geometry_msgs::msg::PoseStamped(),
    [probe, gate](NavigatePose::Feedback feedback) {
      probe->enter();
      // The callback blocks: its worker stays busy and the scope of the
      // client stays active, so the next sample of the goal waits in the
      // queue of the dispatcher.
      gate->wait(30s);
      probe->leave();
    });
  ASSERT_EQ(std::future_status::ready, accepted.wait_for(10s));
  auto task = accepted.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());
  ASSERT_TRUE(controlled->wait_for_handle(10s));

  // The first sample reaches the callback, which blocks in the gate.
  ASSERT_TRUE(publish_feedback_until(
    controlled, 0.25f, [probe]() {return probe->entry_count() >= 1;}, 20s));

  // A second sample of the same goal waits in the queue of the dispatcher:
  // the callbacks of one client are serialized.
  ASSERT_TRUE(publish_feedback_until(
    controlled, 0.5f,
    [pending_before]() {return dispatcher().pending_count() > pending_before;}, 20s));

  // The service stays pending: the client has to settle it when it closes.
  state_->defer_responses.store(true);
  auto described = standalone->describe();
  ASSERT_TRUE(wait_for_parked_calls(1));
  ASSERT_NE(nullptr, parked_call("describe"));

  // The result future and the shared state are read before the mutex of the
  // task is locked: the accessors of the task take that very mutex, and the
  // cleanup of the close has to wait for it.
  auto shared = task->shared_state();
  ASSERT_NE(nullptr, shared);
  auto result_future = task->result();

  // The close runs on its own thread. It publishes the terminal state and
  // settles the futures first and only then waits for the mutex of the task,
  // which the test holds.
  std::unique_lock<std::mutex> task_lock(shared->mutex);
  CloseOrderGuard guard(&task_lock, gate);
  guard.start([standalone]() {standalone->close();});

  // The cleanup of the close is forced to wait, but the terminal state and
  // the futures are already published.
  ASSERT_EQ(std::future_status::ready, result_future.wait_for(5s));
  EXPECT_THROW(result_future.get(), chaos_sdk::ClientClosedError);
  // The state is an atomic value: it may be read while the mutex is held.
  EXPECT_EQ(chaos_sdk::TaskState::UNKNOWN, task->state());

  // The mutex is released: the cleanup of the close can proceed.
  task_lock.unlock();

  // The close returns and the service future is settled before the feedback
  // gate is released.
  ASSERT_TRUE(guard.wait(20s));
  guard.join();
  EXPECT_FALSE(guard.failed());
  EXPECT_EQ(0u, probe->completion_count())
    << "feedback must still be blocked when close returns";
  ASSERT_EQ(std::future_status::ready, described.wait_for(5s));
  EXPECT_THROW(described.get(), chaos_sdk::ClientClosedError);

  // The queued sample of the closed client is retired: it must not run.
  ASSERT_TRUE(wait_for_pending_count(pending_before, 5s));

  // The feedback gate is released last: the blocked callback returns and no
  // stale invocation of the retired sample follows.
  guard.release();
  ASSERT_TRUE(wait_for_dispatcher_idle(pending_before, active_before, 5s));
  EXPECT_EQ(1u, probe->entry_count());
  EXPECT_EQ(1u, probe->completion_count());

  // The parked answer of the deferred service is released for the cleanup of
  // the mock server.
  release_parked_call(parked_call("describe"));
}

// --- The bounded lifecycle of the process ----------------------------------
//
// The two cases below observe the boundaries of the process: the workers of
// the feedback pool, the global budget of its queue and the order in which
// the SDK releases a user capture.

TEST_F(SdkTest, the_process_keeps_two_feedback_workers_while_the_clients_come_and_go)
{
  // The dispatcher is a process wide resource: two workers and one global
  // queue with a budget of 64 pending jobs. The clients of a process are
  // many, the workers of the process are not.
  ASSERT_TRUE(wait_for_dispatcher_idle(0, 0, 5s));
  EXPECT_EQ(2u, dispatcher().worker_count());

  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(10s));
  ASSERT_TRUE(lease.get().success);

  // Two standalone clients: each of them observes a goal whose feedback
  // callback blocks on its own gate.
  auto first = chaos_sdk::Robot::create_standalone(
    "test", std::nullopt, std::nullopt, 0.5, 5.0, 30.0);
  auto second = chaos_sdk::Robot::create_standalone(
    "test", std::nullopt, std::nullopt, 0.5, 5.0, 30.0);
  ASSERT_NE(nullptr, first);
  ASSERT_NE(nullptr, second);

  auto first_lease = first->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, first_lease.wait_for(20s));
  ASSERT_TRUE(first_lease.get().success);
  auto second_lease = second->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, second_lease.wait_for(20s));
  ASSERT_TRUE(second_lease.get().success);

  // Every goal gets its own controlled goal: the mock server holds the
  // terminal result of both, so the test decides when they finish and when
  // the executor of the mock is free again.
  auto first_controlled = install_controlled_goal();
  auto second_controlled = install_controlled_goal();
  auto first_gate = new_user_gate();
  auto second_gate = new_user_gate();
  auto first_probe = std::make_shared<FeedbackProbe>();
  auto second_probe = std::make_shared<FeedbackProbe>();

  auto first_goal = first->navigate(
    geometry_msgs::msg::PoseStamped(),
    [first_probe, first_gate](NavigatePose::Feedback) {
      // Every invocation counts: a repeated sample must not hide behind a
      // filter, it has to block on the gate like the first one.
      first_probe->enter();
      // The callback blocks: its worker stays busy and the scope of the
      // client stays active.
      first_gate->wait(30s);
      first_probe->leave();
    });
  auto second_goal = second->navigate(
    geometry_msgs::msg::PoseStamped(),
    [second_probe, second_gate](NavigatePose::Feedback) {
      second_probe->enter();
      second_gate->wait(30s);
      second_probe->leave();
    });
  ASSERT_EQ(std::future_status::ready, first_goal.wait_for(20s));
  ASSERT_EQ(std::future_status::ready, second_goal.wait_for(20s));
  auto first_task = first_goal.get();
  auto second_task = second_goal.get();
  ASSERT_NE(nullptr, first_task);
  ASSERT_NE(nullptr, second_task);
  ASSERT_TRUE(first_task->accepted());
  ASSERT_TRUE(second_task->accepted());
  ASSERT_TRUE(first_controlled->wait_for_handle(20s));
  ASSERT_TRUE(second_controlled->wait_for_handle(20s));

  // The first sample of every goal reaches its callback, which blocks there.
  // The mock hands the controlled goals to the accepted goals in the order in
  // which the requests arrive, so the test publishes on both of them until
  // both callbacks really entered.
  ASSERT_TRUE(publish_feedback_until(
    first_controlled, second_controlled, 0.25f,
    [first_probe, second_probe]() {
      return first_probe->entry_count() >= 1 && second_probe->entry_count() >= 1;
    }, 20s, 100ms));

  // Both workers of the process are busy with the blocked callbacks: the
  // pool has exactly two of them, so no third job can run.
  ASSERT_TRUE(wait_for_active_count(2, 20s));
  EXPECT_EQ(2u, dispatcher().active_count());
  EXPECT_EQ(2u, dispatcher().worker_count());

  // The server results are released: the executor of the mock is free again
  // while the user callbacks stay blocked.
  first_controlled->release_result();
  second_controlled->release_result();
  ASSERT_EQ(std::future_status::ready, first_task->result().wait_for(20s));
  ASSERT_EQ(std::future_status::ready, second_task->result().wait_for(20s));
  EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, first_task->state());
  EXPECT_EQ(chaos_sdk::TaskState::SUCCEEDED, second_task->state());

  // More clients come and go: the workers of the process stay the same and
  // the global queue stays inside its budget.
  for (int index = 0; index < 3; ++index) {
    auto extra = chaos_sdk::Robot::create_standalone(
      "extra" + std::to_string(index), std::nullopt, std::nullopt, 0.5, 5.0, 5.0);
    ASSERT_NE(nullptr, extra);
    extra->close();
  }
  EXPECT_EQ(2u, dispatcher().worker_count());
  EXPECT_LE(
    dispatcher().pending_count(),
    chaos_sdk::internal::FeedbackDispatcher::max_pending_jobs());

  // The client of the fixture observes one more goal. Both workers of the
  // process are busy, so the feedback of that goal stays in the queue.
  auto third_controlled = install_controlled_goal();
  auto late_probe = std::make_shared<FeedbackProbe>();
  // A short action timeout: the observation of the goal ends on its own.
  robot_->set_action_timeout(5.0);
  auto third_goal = robot_->navigate(
    geometry_msgs::msg::PoseStamped(),
    [late_probe](NavigatePose::Feedback) {
      late_probe->enter();
    });
  ASSERT_EQ(std::future_status::ready, third_goal.wait_for(20s));
  auto third_task = third_goal.get();
  ASSERT_NE(nullptr, third_task);
  ASSERT_TRUE(third_task->accepted());
  ASSERT_TRUE(third_controlled->wait_for_handle(20s));

  // The sample of the goal is queued: the two workers are busy, so the job
  // waits in the queue of the dispatcher.
  const std::size_t queued_before = dispatcher().pending_count();
  ASSERT_TRUE(publish_feedback_until(
    third_controlled, 0.5f,
    [queued_before]() {return dispatcher().pending_count() > queued_before;}, 20s, 50ms));

  // Both workers of the process are still busy with the blocked callbacks of
  // the two clients and neither of them returned: the sample of the third
  // goal waits in the queue of the dispatcher.
  EXPECT_EQ(2u, dispatcher().active_count());
  EXPECT_EQ(0u, first_probe->completion_count());
  EXPECT_EQ(0u, second_probe->completion_count());

  // The result future is read before the mutex of the shared state is
  // locked: the accessor of the task takes that very mutex.
  auto third_shared = third_task->shared_state();
  ASSERT_NE(nullptr, third_shared);
  auto third_result = third_task->result();

  // The watchdog publishes the terminal state and settles the futures before
  // it cleans up. The test holds the mutex of the shared state, so the
  // cleanup has to wait for it, but the state and the future are already
  // published.
  std::unique_lock<std::mutex> state_lock(third_shared->mutex);
  ASSERT_EQ(std::future_status::timeout, third_result.wait_for(0s));
  ASSERT_EQ(std::future_status::ready, third_result.wait_for(20s));
  EXPECT_THROW(third_result.get(), chaos_sdk::TimeoutError);
  // The terminal state is an atomic value: it may be read while the mutex
  // is held.
  EXPECT_EQ(chaos_sdk::TaskState::TIMED_OUT, third_task->state());
  // The terminal state of the third goal did not release the callbacks of the
  // two clients: they are still blocked and they did not return.
  EXPECT_EQ(2u, dispatcher().active_count());
  EXPECT_EQ(0u, first_probe->completion_count());
  EXPECT_EQ(0u, second_probe->completion_count());
  state_lock.unlock();

  // The queued sample of the retired goal is dropped and the ROS client of
  // its slot is released.
  ASSERT_TRUE(wait_for_pending_count(queued_before, 20s));
  ASSERT_TRUE(wait_for_action_client_count(robot_, 0, 20s));
  EXPECT_EQ(0u, robot_->pending_count());
  EXPECT_EQ(chaos_sdk::TaskState::TIMED_OUT, third_task->state());
  EXPECT_TRUE(third_task->accepted());
  EXPECT_FALSE(third_task->goal_id().empty());

  // Closing one client leaves the other one usable: the clients of the
  // process are independent of each other.
  first->close();
  auto described = second->describe();
  ASSERT_EQ(std::future_status::ready, described.wait_for(20s));
  EXPECT_EQ("test", described.get().asset_id);

  // The gates of the blocked callbacks are released last: the callbacks
  // return and the retired samples never run.
  // Both workers are still busy and neither callback returned before the
  // release: the gate wait is only a failure bound, not a proof.
  EXPECT_EQ(2u, dispatcher().active_count());
  EXPECT_EQ(0u, first_probe->completion_count());
  EXPECT_EQ(0u, second_probe->completion_count());
  first_gate->release();
  second_gate->release();
  ASSERT_TRUE(wait_for_dispatcher_idle(0, 0, 20s));
  EXPECT_EQ(1u, first_probe->entry_count());
  EXPECT_EQ(1u, first_probe->completion_count());
  EXPECT_EQ(1u, second_probe->entry_count());
  EXPECT_EQ(1u, second_probe->completion_count());
  // The sample of the retired goal never ran: it was dropped with the
  // observation of the goal.
  EXPECT_EQ(0u, late_probe->entry_count());
}

TEST_F(SdkTest, the_capture_of_a_timed_out_cancel_is_released_after_its_future_settled)
{
  // The goal is controlled: the mock server holds its terminal result, so
  // the goal stays alive while the cancel request runs into its timeout.
  auto controlled = install_controlled_goal();
  // The server side of the goal is released when the test ends early.
  ScopedGoalRelease release(controlled);

  auto cell = std::make_shared<CancelCell>();
  auto returned = std::make_shared<Signal>();
  cell->robot = robot_;

  // The action timeout stays far behind the timeout of the cancel request:
  // the terminal state of the goal must come from the cancel request.
  robot_->set_action_timeout(30.0);
  // The server answers the cancel request far too late.
  state_->slow_cancel.store(true);

  auto lease = robot_->acquire("operator", 5.0);
  ASSERT_EQ(std::future_status::ready, lease.wait_for(10s));
  ASSERT_TRUE(lease.get().success);

  // The feedback callable owns the guard that records how it was destroyed.
  // The goal publishes no feedback, so the operation of the SDK is the last
  // owner of the capture. The test keeps no copy of it.
  auto guard = std::make_shared<CaptureGuard>(cell, returned);
  std::function<void (NavigatePose::Feedback)> feedback =
    [guard](NavigatePose::Feedback) {
      // No feedback is published: the callback never runs.
    };
  auto accepted = robot_->navigate(
    geometry_msgs::msg::PoseStamped(), std::move(feedback));
  feedback = nullptr;
  guard.reset();
  ASSERT_EQ(std::future_status::ready, accepted.wait_for(10s));
  auto task = accepted.get();
  ASSERT_NE(nullptr, task);
  ASSERT_TRUE(task->accepted());
  ASSERT_TRUE(controlled->wait_for_handle(10s));

  // The cancel request runs into the short response timeout. Its future is
  // stored in the cell before the timeout elapses: the guard observes the
  // future that the SDK has to settle first.
  robot_->set_response_timeout(0.3);
  chaos_sdk::Robot::NavigationTask::CancelFuture cancel_future;
  {
    const std::lock_guard<std::mutex> cell_guard(cell->mutex);
    cancel_future = task->cancel();
    cell->cancel_future = cancel_future;
  }
  ASSERT_TRUE(cancel_future.valid());

  // The cancel request was not answered in time.
  ASSERT_EQ(std::future_status::ready, cancel_future.wait_for(20s));
  EXPECT_THROW(cancel_future.get(), chaos_sdk::TimeoutError);

  // The capture was released: the guard saw the settled future and the
  // re-entered close returned.
  ASSERT_EQ(std::future_status::ready, returned->future().wait_for(20s));
  {
    const std::lock_guard<std::mutex> cell_guard(cell->mutex);
    // The future was already settled when the capture was destroyed: the
    // cleanup of the SDK never runs before the failure of the request.
    EXPECT_TRUE(cell->observed.future_valid);
    EXPECT_TRUE(cell->observed.future_ready);
    // The close was re-entered from the destructor of the capture.
    EXPECT_TRUE(cell->observed.close_called);
    EXPECT_TRUE(cell->observed.close_succeeded);
  }
  // The re-entered close really closed the client.
  EXPECT_THROW(robot_->describe(), chaos_sdk::ClientClosedError);

  // The goal was settled and its history survived the close.
  ASSERT_EQ(std::future_status::ready, task->result().wait_for(10s));
  EXPECT_THROW(task->result().get(), chaos_sdk::TimeoutError);
  EXPECT_TRUE(task->accepted());
  EXPECT_FALSE(task->goal_id().empty());
  EXPECT_EQ(chaos_sdk::TaskState::TIMED_OUT, task->state());
}

}  // namespace
