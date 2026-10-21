#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

#include <rclcpp_action/rclcpp_action.hpp>

namespace chaos_sdk
{

/// Lifecycle of a remote action observed by this client.
enum class TaskState
{
  /// The goal was sent and no acknowledgement arrived yet.
  PENDING,
  /// The server accepted the goal; execution may still be running.
  ACCEPTED,
  /// The server reported success.
  SUCCEEDED,
  /// The server reported a failure (aborted result or a business failure).
  FAILED,
  /// The goal was canceled.
  CANCELED,
  /// The server rejected the goal.
  REJECTED,
  /// The client stopped observing the goal because its local timeout elapsed.
  /// The remote execution state is unknown; this does not mean "stopped".
  TIMED_OUT,
  /// The terminal state could not be determined.
  UNKNOWN,
};

const char * task_state_name(TaskState state);

/// Outcome of a cancel request.
struct CancelOutcome
{
  /// True when the server accepted the cancellation request.
  bool accepted = false;
  /// action_msgs/srv/CancelGoal return code (0 == ERROR_NONE).
  int return_code = -1;
  /// Number of goals the server reported as canceling.
  std::size_t goals_canceling = 0;
};

/// Classify a terminal action outcome.
///
/// ``has_result`` is false when the server reported a terminal status without
/// a result payload. Such an outcome never proves success: the payload is the
/// only source of the business outcome, so the state stays unknown.
inline TaskState classify_result(
  rclcpp_action::ResultCode code, bool has_result, bool business_success)
{
  if (!has_result) {
    return TaskState::UNKNOWN;
  }
  switch (code) {
    case rclcpp_action::ResultCode::SUCCEEDED:
      return business_success ? TaskState::SUCCEEDED : TaskState::FAILED;
    case rclcpp_action::ResultCode::CANCELED:
      return TaskState::CANCELED;
    case rclcpp_action::ResultCode::UNKNOWN:
      return TaskState::UNKNOWN;
    default:
      return TaskState::FAILED;
  }
}

/// Hexadecimal representation of a goal UUID.
inline std::string goal_id_hex(const std::array<uint8_t, 16> & uuid)
{
  static const char * digits = "0123456789abcdef";
  std::string out;
  out.reserve(32);
  for (auto byte : uuid) {
    out.push_back(digits[(byte >> 4) & 0x0F]);
    out.push_back(digits[byte & 0x0F]);
  }
  return out;
}

/// Typed handle for one remote action.
///
/// The task is the user facing part of an in-flight goal. It does not own the
/// ROS resources of the goal: while the goal is observed, the SDK keeps the
/// action client and the goal handle alive; when the goal reaches a terminal
/// state, times out or is closed, the SDK releases them and the task keeps
/// only the readable outcome.
///
/// Ownership and concurrency:
/// * the shared state is owned by the SDK while the observation is live and
///   by the task afterwards,
/// * the goal handle is held strongly while the goal is observed and is
///   released on the terminal state, on a timeout and on close. The handle
///   references the action client weakly, so a task never keeps a client
///   alive,
/// * the cancel requester references the action client weakly as well and is
///   cleared on the terminal state,
/// * the acceptance and the goal id are captured once and kept afterwards:
///   the readable history survives the release of the ROS resources,
/// * every accessor is safe to call concurrently.
template<typename ActionT>
class ActionTask
{
public:
  using ClientT = rclcpp_action::Client<ActionT>;
  using GoalHandle = typename ClientT::GoalHandle;
  using WrappedResult = typename GoalHandle::WrappedResult;
  using CancelResponse = typename ClientT::CancelResponse;
  /// Future of a bounded cancel request.
  using CancelFuture = std::shared_future<CancelOutcome>;

  /// Shared state between the task and the SDK.
  struct Shared
  {
    mutable std::mutex mutex;
    std::atomic<int> task_state{static_cast<int>(TaskState::PENDING)};
    std::shared_future<WrappedResult> result_future;
    /// The goal handle of the observed goal. It is released on the terminal
    /// state, on a timeout and on close, so a finished task does not keep
    /// ROS resources alive.
    typename ClientT::GoalHandle::SharedPtr handle;
    /// True once the server accepted the goal. The flag is kept after the
    /// handle is released: the acceptance is part of the observable history.
    bool was_accepted = false;
    /// Hexadecimal goal id, captured when the goal was accepted. It is kept
    /// after the handle is released.
    std::string goal_id;
    /// Bounded cancel request; cleared on the terminal state so that a
    /// finished task does not keep the client alive.
    std::function<CancelFuture()> cancel_requester;
  };

  ActionTask() = default;

  explicit ActionTask(std::shared_ptr<Shared> shared)
  : shared_(std::move(shared)) {}

  /// True when the server accepted the goal.
  ///
  /// The answer survives the release of the goal handle, so it stays readable
  /// after the goal finished, timed out or was closed.
  bool accepted() const
  {
    auto shared = shared_;
    if (!shared) {
      return false;
    }
    const std::lock_guard<std::mutex> guard(shared->mutex);
    return shared->was_accepted;
  }

  /// Hexadecimal goal UUID, empty when the goal was rejected or released
  /// before an acceptance.
  ///
  /// The id survives the release of the goal handle.
  std::string goal_id() const
  {
    auto shared = shared_;
    if (!shared) {
      return {};
    }
    const std::lock_guard<std::mutex> guard(shared->mutex);
    return shared->goal_id;
  }

  TaskState state() const noexcept
  {
    auto shared = shared_;
    if (!shared) {
      return TaskState::UNKNOWN;
    }
    return static_cast<TaskState>(shared->task_state.load());
  }

  /// Terminal result of the action.
  ///
  /// The future fails with chaos_sdk::TimeoutError when the local timeout
  /// elapsed or the client was closed before a result arrived.
  std::shared_future<WrappedResult> result() const
  {
    auto shared = shared_;
    if (!shared) {
      return std::shared_future<WrappedResult>();
    }
    const std::lock_guard<std::mutex> guard(shared->mutex);
    return shared->result_future;
  }

  /// Request cancellation of the goal through the owning client.
  ///
  /// The returned future is bounded: it settles with the server's cancel
  /// response, with chaos_sdk::TimeoutError when the local response timeout
  /// elapses, or with chaos_sdk::ClientClosedError when the client is closed.
  std::shared_future<CancelOutcome> cancel()
  {
    std::function<CancelFuture()> requester;
    {
      auto shared = shared_;
      if (!shared) {
        return failed_cancel("the action task is not initialised");
      }
      const std::lock_guard<std::mutex> guard(shared->mutex);
      if (!shared->handle) {
        return failed_cancel("the action goal is no longer observed");
      }
      requester = shared->cancel_requester;
    }
    if (!requester) {
      return failed_cancel("the action client is no longer available");
    }
    return requester();
  }

  std::shared_ptr<Shared> shared_state() const {return shared_;}

private:
  static std::shared_future<CancelOutcome> failed_cancel(const std::string & message)
  {
    auto promise = std::make_shared<std::promise<CancelOutcome>>();
    promise->set_exception(std::make_exception_ptr(std::runtime_error(message)));
    return promise->get_future().share();
  }

  std::shared_ptr<Shared> shared_;
};

}  // namespace chaos_sdk
