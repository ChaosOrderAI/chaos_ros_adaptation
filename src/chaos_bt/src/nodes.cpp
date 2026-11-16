// BehaviorTree.CPP adapters for the native action client.
//
// A tick never waits: it starts a goal or polls the future that the SDK already
// owns. Halting a goal that the server has not accepted yet has nothing to
// cancel, so that one path hands the acceptance future to a short lived waiter
// that cancels the goal if the server accepts it afterwards. The waiter holds
// the session only, never the node or the blackboard.
#include "chaos_bt/nodes.hpp"

#include <atomic>
#include <cmath>
#include <future>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include <geometry_msgs/msg/pose_stamped.hpp>

namespace chaos_bt
{

namespace
{

using NavigatePose = chaos_interfaces::action::NavigatePose;
using Flight = chaos_interfaces::action::Flight;

/// Latest feedback sample copied out of the SDK feedback thread.
struct FeedbackSnapshot
{
  bool has_pose = false;
  double altitude_m = 0.0;
  bool has_altitude = false;
  float progress = 0.0f;
  bool has_progress = false;
  geometry_msgs::msg::PoseStamped pose;
};

/// One goal as the node observes it.
///
/// The node drops its reference when it starts again or is destroyed. A waiter
/// that still holds the session may cancel a late acceptance, but it never
/// writes the blackboard of a later tick.
struct ActionSession
{
  std::atomic<bool> halted{false};
  std::atomic<bool> cancel_sent{false};

  std::mutex feedback_mutex;
  FeedbackSnapshot feedback;

  /// The bounded future of the cancellation request, kept so that a later tick
  /// can report how the server answered it.
  std::mutex cancel_mutex;
  std::shared_future<chaos_sdk::CancelOutcome> cancel_outcome;
};

/// Remember the cancellation request so that a later tick can report it.
template<typename Session>
void store_cancel_outcome(
  const std::shared_ptr<Session> & session,
  const std::shared_future<chaos_sdk::CancelOutcome> & outcome)
{
  const std::lock_guard<std::mutex> guard(session->cancel_mutex);
  session->cancel_outcome = outcome;
}

/// The pose that is published before the first feedback sample arrives.
geometry_msgs::msg::PoseStamped neutral_pose()
{
  geometry_msgs::msg::PoseStamped pose;
  pose.pose.orientation.w = 1.0;
  return pose;
}

template<typename Future>
void cancel_when_accepted(std::shared_ptr<ActionSession> session, Future acceptance)
{
  try {
    acceptance.wait();
    auto task = acceptance.get();
    if (!task || !session->halted.load()) {
      return;
    }
    if (!session->cancel_sent.exchange(true)) {
      // The waiter owns this request. Observe the outcome so that a cancel
      // which could not be sent is not silently dropped. The blackboard of a
      // later tick is not touched here.
      auto outcome = task->cancel();
      store_cancel_outcome(session, outcome);
      try {
        outcome.wait();
      } catch (const std::exception &) {
        // The cancel could not be sent. The remote state stays unknown and the
        // node does not claim that the goal was stopped.
      }
    }
  } catch (const std::exception &) {
    // The acceptance itself failed. There is no goal to cancel, and the remote
    // execution state stays unknown.
  }
}

/// Start the waiter that cancels a goal accepted after the node was halted.
///
/// Returns false when no waiter could be started; the caller then reports the
/// remote state as unknown instead of claiming a pending cancellation.
template<typename Future>
bool defer_cancel(std::shared_ptr<ActionSession> session, Future acceptance)
{
  try {
    std::thread(&cancel_when_accepted<Future>, std::move(session), std::move(acceptance)).detach();
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

void record_feedback(
  const std::shared_ptr<ActionSession> & session, const NavigatePose::Feedback & feedback)
{
  const std::lock_guard<std::mutex> guard(session->feedback_mutex);
  session->feedback.pose = feedback.current_pose;
  session->feedback.has_pose = true;
  session->feedback.progress = feedback.progress;
  session->feedback.has_progress = true;
}

void record_feedback(
  const std::shared_ptr<ActionSession> & session, const Flight::Feedback & feedback)
{
  const std::lock_guard<std::mutex> guard(session->feedback_mutex);
  session->feedback.altitude_m = feedback.altitude_m;
  session->feedback.has_altitude = true;
  session->feedback.progress = feedback.progress;
  session->feedback.has_progress = true;
}

template<typename Feedback>
std::function<void(Feedback)> feedback_callback(const std::shared_ptr<ActionSession> & session)
{
  std::weak_ptr<ActionSession> weak = session;
  return [weak](const Feedback & feedback) {
      auto locked = weak.lock();
      if (!locked || locked->halted.load()) {
        return;
      }
      record_feedback(locked, feedback);
    };
}

std::optional<double> read_ttl(BT::TreeNode & node)
{
  auto ttl = node.getInput<double>("ttl_s");
  if (!ttl) {
    return std::nullopt;
  }
  if (!std::isfinite(ttl.value())) {
    throw BT::RuntimeError("ttl_s must be finite");
  }
  return ttl.value();
}

double read_finite(BT::TreeNode & node, const char * port)
{
  auto value = node.getInput<double>(port);
  if (!value) {
    throw BT::RuntimeError(std::string(port) + " is required: " + value.error());
  }
  if (!std::isfinite(value.value())) {
    throw BT::RuntimeError(std::string(port) + " must be finite");
  }
  return value.value();
}

BT::PortsList action_ports()
{
  return {
    BT::OutputPort<std::string>("goal_id"),
    BT::OutputPort<std::string>("task_state"),
    BT::OutputPort<double>("progress"),
    BT::OutputPort<std::string>("code"),
    BT::OutputPort<std::string>("message"),
  };
}

}  // namespace

template<typename Node>
void ActionNode::write_common(Node & node, const std::string & state, const std::string & code,
  const std::string & message)
{
  node.write_port("task_state", state);
  node.write_port("code", code);
  node.write_port("message", message);
}

template<typename Node, typename Session>
void ActionNode::publish_feedback(Node & node, const std::shared_ptr<Session> & session)
{
  bool has_progress = false;
  float progress = 0.0f;
  bool has_pose = false;
  geometry_msgs::msg::PoseStamped pose;
  bool has_altitude = false;
  double altitude_m = 0.0;
  {
    const std::lock_guard<std::mutex> guard(session->feedback_mutex);
    // Only the fields that the node may publish are copied. A flight node
    // never fills the pose, and copying the whole snapshot would copy it on
    // every tick anyway.
    has_progress = session->feedback.has_progress;
    progress = session->feedback.progress;
    has_altitude = session->feedback.has_altitude;
    altitude_m = session->feedback.altitude_m;
    if (session->feedback.has_pose) {
      has_pose = true;
      pose = session->feedback.pose;
    }
  }
  if (has_progress) {
    node.write_port("progress", static_cast<double>(progress));
  }
  if (has_pose) {
    node.write_port("current_pose", pose);
  }
  if (has_altitude) {
    node.write_port("current_altitude_m", altitude_m);
  }
}

template<typename Node, typename Session>
void ActionNode::report_cancel(Node & node, const std::shared_ptr<Session> & session)
{
  std::shared_future<chaos_sdk::CancelOutcome> outcome;
  {
    const std::lock_guard<std::mutex> guard(session->cancel_mutex);
    outcome = session->cancel_outcome;
  }
  if (!outcome.valid() || outcome.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    return;
  }
  try {
    const auto accepted = outcome.get();
    node.write_port(
      "code", accepted.accepted ? "CANCEL_REQUESTED" : "CANCEL_REJECTED");
    node.write_port(
      "message",
      accepted.accepted ?
        "cancellation requested; the remote goal is not confirmed stopped" :
        "the server refused the cancellation request");
  } catch (const std::exception & error) {
    // The request never reached the server.
    node.write_port("task_state", chaos_sdk::task_state_name(chaos_sdk::TaskState::UNKNOWN));
    node.write_port("code", "REMOTE_UNKNOWN");
    node.write_port("message", error.what());
  }
}

template<typename Node>
void ActionNode::clear_outputs(Node & node)
{
  node.write_port("goal_id", std::string());
  node.write_port("progress", 0.0);
}

/// Report an error that the SDK raised while the goal was sent.
///
/// A client that is closed or a request that timed out leaves the remote state
/// unknown. A request that could not be queued was rejected before it was
/// sent. A configuration error never reaches this function: the caller
/// re-throws it, because the goal never left the process and the tree has to
/// see the mistake instead of a remote failure.
template<typename Node>
BT::NodeStatus ActionNode::failure_of(Node & node, const std::exception & error)
{
  if (dynamic_cast<const chaos_sdk::PendingOverflowError *>(&error) != nullptr) {
    write_common(
      node, chaos_sdk::task_state_name(chaos_sdk::TaskState::REJECTED), "PENDING_OVERFLOW",
      error.what());
    return BT::NodeStatus::FAILURE;
  }
  const bool closed = dynamic_cast<const chaos_sdk::ClientClosedError *>(&error) != nullptr;
  const bool timed_out = dynamic_cast<const chaos_sdk::TimeoutError *>(&error) != nullptr;
  const char * state = closed ? "unknown" : (timed_out ? "timed_out" : "failed");
  const char * code = closed ? "REMOTE_UNKNOWN" : (timed_out ? "TIMED_OUT" : "FAILED");
  write_common(node, state, code, error.what());
  return BT::NodeStatus::FAILURE;
}

/// Poll one action without waiting.
///
/// The acceptance future resolves to the task, or to nullptr when the server
/// rejected the goal. A terminal result is classified with the SDK rules, so a
/// canceled or aborted goal is never reported as a success.
template<typename Node, typename Session, typename Task>
BT::NodeStatus ActionNode::poll_action(
  Node & node, const std::shared_ptr<Session> & session,
  const std::shared_future<std::shared_ptr<Task>> & acceptance)
{
  publish_feedback(node, session);
  if (acceptance.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    write_common(node, chaos_sdk::task_state_name(chaos_sdk::TaskState::PENDING), "", "");
    report_cancel(node, session);
    return BT::NodeStatus::RUNNING;
  }

  std::shared_ptr<Task> task;
  try {
    task = acceptance.get();
  } catch (const std::exception & error) {
    return failure_of(node, error);
  }
  if (!task) {
    write_common(
      node, chaos_sdk::task_state_name(chaos_sdk::TaskState::REJECTED), "REJECTED",
      "the server rejected the goal");
    return BT::NodeStatus::FAILURE;
  }
  node.write_port("goal_id", task->goal_id());

  const auto result = task->result();
  if (result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    write_common(node, chaos_sdk::task_state_name(task->state()), "", "");
    report_cancel(node, session);
    return BT::NodeStatus::RUNNING;
  }

  try {
    const auto wrapped = result.get();
    const bool has_result = wrapped.result != nullptr;
    const auto state = chaos_sdk::classify_result(
      wrapped.code, has_result, has_result && wrapped.result->success);
    write_common(
      node, chaos_sdk::task_state_name(state),
      has_result ? wrapped.result->code : "UNKNOWN",
      has_result ? wrapped.result->message : "the server reported no result");
    return state == chaos_sdk::TaskState::SUCCEEDED ?
           BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
  } catch (const std::exception & error) {
    return failure_of(node, error);
  }
}

/// Request cancellation once, without waiting for the server.
///
/// A cancel response only means that the request was sent. The remote goal is
/// not stopped until a later tick observes the terminal result.
template<typename Node, typename Session, typename Task>
void ActionNode::request_cancel(
  Node & node, const std::shared_ptr<Session> & session,
  const std::shared_future<std::shared_ptr<Task>> & acceptance)
{
  if (session->cancel_sent.exchange(true)) {
    return;
  }
  if (acceptance.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    session->cancel_sent.store(false);
    session->halted.store(true);
    if (!defer_cancel(session, acceptance)) {
      write_common(
        node, chaos_sdk::task_state_name(chaos_sdk::TaskState::UNKNOWN), "REMOTE_UNKNOWN",
        "the goal is not accepted yet and no cancellation could be scheduled");
      return;
    }
    write_common(
      node, chaos_sdk::task_state_name(chaos_sdk::TaskState::PENDING), "CANCEL_DEFERRED",
      "the goal is not accepted yet; a cancellation is requested if it is accepted");
    return;
  }

  try {
    auto task = acceptance.get();
    if (!task) {
      write_common(
        node, chaos_sdk::task_state_name(chaos_sdk::TaskState::REJECTED), "REJECTED",
        "the server rejected the goal");
      return;
    }
    const auto state = task->state();
    if (state != chaos_sdk::TaskState::PENDING && state != chaos_sdk::TaskState::ACCEPTED) {
      // There is nothing left to cancel. The goal either reached a terminal
      // state or its state is unknown.
      if (state == chaos_sdk::TaskState::UNKNOWN) {
        write_common(
          node, chaos_sdk::task_state_name(chaos_sdk::TaskState::UNKNOWN), "REMOTE_UNKNOWN",
          "the remote goal state is unknown");
      } else {
        write_common(
          node, chaos_sdk::task_state_name(state), "",
          "the goal already reached a terminal state");
      }
      return;
    }

    // The cancel request is bounded: the future settles with the server's
    // response, with a timeout, or with an error when the request could not be
    // sent at all.
    const auto outcome = task->cancel();
    store_cancel_outcome(session, outcome);
    if (outcome.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      try {
        const auto accepted = outcome.get();
        write_common(
          node, chaos_sdk::task_state_name(state),
          accepted.accepted ? "CANCEL_REQUESTED" : "CANCEL_REJECTED",
          accepted.accepted ?
            "cancellation requested; the remote goal is not confirmed stopped" :
            "the server refused the cancellation request");
      } catch (const std::exception & error) {
        // The request never reached the server.
        write_common(
          node, chaos_sdk::task_state_name(chaos_sdk::TaskState::UNKNOWN), "REMOTE_UNKNOWN",
          error.what());
      }
      return;
    }
    write_common(
      node, chaos_sdk::task_state_name(state), "CANCEL_REQUESTED",
      "cancellation requested; the remote goal is not confirmed stopped");
  } catch (const std::exception & error) {
    write_common(
      node, chaos_sdk::task_state_name(chaos_sdk::TaskState::UNKNOWN), "REMOTE_UNKNOWN",
      error.what());
  }
}

template<typename Node, typename Session, typename Task>
void ActionNode::safe_halt(
  Node & node, const std::shared_ptr<Session> & session,
  const std::shared_future<std::shared_ptr<Task>> & acceptance)
{
  // The tree may be destroyed while this runs. An exception would escape the
  // tree destructor and abort the process.
  try {
    request_cancel(node, session, acceptance);
  } catch (const std::exception &) {
  }
}

template<typename Task>
bool ActionNode::goal_finished(const std::shared_future<std::shared_ptr<Task>> & acceptance)
{
  if (!acceptance.valid()) {
    return true;
  }
  if (acceptance.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    return false;
  }
  std::shared_ptr<Task> task;
  try {
    task = acceptance.get();
  } catch (const std::exception &) {
    return true;
  }
  if (!task) {
    return true;
  }
  return task->result().wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

namespace
{

/// The state that every action node keeps for the goal it observes.
///
/// The three nodes differ in the ports they read and in the goal they send;
/// everything else is the same. The state is held in one place so that the
/// lifecycle helpers of ActionNode can work on all of them.
template<typename TaskT>
struct ActionState
{
  using Session = ActionSession;

  explicit ActionState(std::shared_ptr<chaos_sdk::Robot> robot_in)
  : robot(std::move(robot_in)) {}

  /// The session dies with the node: the feedback callback and the deferred
  /// cancellation waiter hold it, and they must not write into a node that no
  /// longer exists.
  ~ActionState()
  {
    if (session) {
      session->halted.store(true);
    }
  }

  std::shared_ptr<chaos_sdk::Robot> robot;
  std::shared_ptr<Session> session;
  std::shared_future<std::shared_ptr<TaskT>> acceptance;
};

}  // namespace

struct Navigate::Impl : ActionState<chaos_sdk::Robot::NavigationTask>
{
  using ActionState::ActionState;
};

Navigate::Navigate(
  const std::string & name, const BT::NodeConfig & config, std::shared_ptr<chaos_sdk::Robot> robot)
: ActionNode(name, config), impl_(std::make_unique<Impl>(std::move(robot)))
{
}

Navigate::~Navigate() = default;

BT::PortsList Navigate::providedPorts()
{
  auto ports = action_ports();
  ports.insert(BT::InputPort<std::string>("frame_id"));
  ports.insert(BT::InputPort<double>("x"));
  ports.insert(BT::InputPort<double>("y"));
  ports.insert(BT::InputPort<double>("z"));
  ports.insert(BT::InputPort<double>("yaw"));
  ports.insert(BT::InputPort<double>("ttl_s"));
  ports.insert(BT::OutputPort<geometry_msgs::msg::PoseStamped>("current_pose"));
  return ports;
}

BT::NodeStatus Navigate::onStart()
{
  if (goal_pending(impl_->acceptance)) {
    // The goal of the previous activation has not reached a terminal state
    // yet. A second goal for the same asset would race with it, so the node
    // keeps observing the goal it already sent.
    return onRunning();
  }

  geometry_msgs::msg::PoseStamped target;
  auto frame = getInput<std::string>("frame_id");
  if (!frame || frame.value().empty()) {
    throw BT::RuntimeError("frame_id is required");
  }
  target.header.frame_id = frame.value();
  target.pose.position.x = read_finite(*this, "x");
  target.pose.position.y = read_finite(*this, "y");
  target.pose.position.z = read_finite(*this, "z");
  const double yaw = read_finite(*this, "yaw");
  target.pose.orientation.z = std::sin(yaw / 2.0);
  target.pose.orientation.w = std::cos(yaw / 2.0);
  const auto ttl = read_ttl(*this);

  return start_goal(
    *impl_,
    [&](const std::shared_ptr<ActionSession> & session) {
      return impl_->robot->navigate(
        target, feedback_callback<NavigatePose::Feedback>(session), ttl);
    },
    [&]() {write_port("current_pose", neutral_pose());});
}

BT::NodeStatus Navigate::onRunning()
{
  return observe_goal(*impl_);
}

void Navigate::onHalted()
{
  cancel_goal(*impl_);
}

struct Takeoff::Impl : ActionState<chaos_sdk::Robot::FlightTask>
{
  using ActionState::ActionState;
};

Takeoff::Takeoff(
  const std::string & name, const BT::NodeConfig & config, std::shared_ptr<chaos_sdk::Robot> robot)
: ActionNode(name, config), impl_(std::make_unique<Impl>(std::move(robot)))
{
}

Takeoff::~Takeoff() = default;

BT::PortsList Takeoff::providedPorts()
{
  auto ports = action_ports();
  ports.insert(BT::InputPort<double>("altitude_m"));
  ports.insert(BT::InputPort<double>("ttl_s"));
  ports.insert(BT::OutputPort<double>("current_altitude_m"));
  return ports;
}

BT::NodeStatus Takeoff::onStart()
{
  if (goal_pending(impl_->acceptance)) {
    // The goal of the previous activation has not reached a terminal state
    // yet. A second goal for the same asset would race with it, so the node
    // keeps observing the goal it already sent.
    return onRunning();
  }

  const double altitude = read_finite(*this, "altitude_m");
  const auto ttl = read_ttl(*this);

  return start_goal(
    *impl_,
    [&](const std::shared_ptr<ActionSession> & session) {
      return impl_->robot->takeoff(
        altitude, feedback_callback<Flight::Feedback>(session), ttl);
    },
    [&]() {write_port("current_altitude_m", 0.0);});
}

BT::NodeStatus Takeoff::onRunning()
{
  return observe_goal(*impl_);
}

void Takeoff::onHalted()
{
  cancel_goal(*impl_);
}

struct Land::Impl : ActionState<chaos_sdk::Robot::FlightTask>
{
  using ActionState::ActionState;
};

Land::Land(
  const std::string & name, const BT::NodeConfig & config, std::shared_ptr<chaos_sdk::Robot> robot)
: ActionNode(name, config), impl_(std::make_unique<Impl>(std::move(robot)))
{
}

Land::~Land() = default;

BT::PortsList Land::providedPorts()
{
  auto ports = action_ports();
  ports.insert(BT::InputPort<double>("ttl_s"));
  ports.insert(BT::OutputPort<double>("current_altitude_m"));
  return ports;
}

BT::NodeStatus Land::onStart()
{
  if (goal_pending(impl_->acceptance)) {
    // The goal of the previous activation has not reached a terminal state
    // yet. A second goal for the same asset would race with it, so the node
    // keeps observing the goal it already sent.
    return onRunning();
  }

  const auto ttl = read_ttl(*this);

  return start_goal(
    *impl_,
    [&](const std::shared_ptr<ActionSession> & session) {
      return impl_->robot->land(
        feedback_callback<Flight::Feedback>(session), ttl);
    },
    [&]() {write_port("current_altitude_m", 0.0);});
}

BT::NodeStatus Land::onRunning()
{
  return observe_goal(*impl_);
}

void Land::onHalted()
{
  cancel_goal(*impl_);
}

}  // namespace chaos_bt
