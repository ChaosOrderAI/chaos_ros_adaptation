// rclcpp facade; see runtime_node.hpp for the threading model.
#include "chaos_runtime/runtime_node.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <functional>
#include <utility>

#include <geometry_msgs/msg/pose.hpp>
#include <geometry_msgs/msg/twist.hpp>

#include "chaos_core/clock.hpp"
#include "chaos_runtime/ros_stamp.hpp"
#include "chaos_runtime/asset_config.hpp"
#include "chaos_runtime/driver_deadline.hpp"
#include "chaos_runtime/observation.hpp"
#include "chaos_runtime/ros_adapters.hpp"
#include "chaos_runtime/trace.hpp"

namespace chaos::runtime {

namespace {
using NavigatePose = chaos_interfaces::action::NavigatePose;
using Flight = chaos_interfaces::action::Flight;

// Fills a ROS pose from the ROS-free adapter snapshot. The header stamp and
// frame stay with the caller, so the state channel keeps its publication-stamp
// fallback and the action feedback its optional acquisition stamp.
void fillPose(const AdapterSnapshot & snapshot,
              geometry_msgs::msg::Pose & pose) {
  pose.position.x = snapshot.position[0];
  pose.position.y = snapshot.position[1];
  pose.position.z = snapshot.position[2];
  pose.orientation.x = snapshot.quaternion[0];
  pose.orientation.y = snapshot.quaternion[1];
  pose.orientation.z = snapshot.quaternion[2];
  pose.orientation.w = snapshot.quaternion[3];
}

// Fills a ROS twist from a 6-vector (linear, then angular).
void fillTwist(const std::array<double, 6> & values,
               geometry_msgs::msg::Twist & twist) {
  twist.linear.x = values[0];
  twist.linear.y = values[1];
  twist.linear.z = values[2];
  twist.angular.x = values[3];
  twist.angular.y = values[4];
  twist.angular.z = values[5];
}

// The ROS control trace sink. It is owned by the observation service, which
// destroys it with the registration: the sink holds the publisher by value
// and borrows nothing, so the node is never reached from a delivery.
class RosTraceSink : public TraceSink {
public:
  explicit RosTraceSink(
    rclcpp::Publisher<chaos_interfaces::msg::ControlTrace>::SharedPtr publisher)
    : publisher_(std::move(publisher)) {}

  void publish(const TraceEvent & event) override {
    chaos_interfaces::msg::ControlTrace message;
    stampFromNs(event.stamp_ns, message.header.stamp);
    message.header.frame_id = event.frame;
    message.asset_id = event.asset_id;
    message.sequence = event.sequence;
    message.monotonic_ns = event.monotonic_ns;
    message.request_id = event.request_id;
    message.command = event.command;
    message.stage = event.stage;
    message.code = event.code;
    if (event.velocity.has_value() && event.velocity->valid) {
      message.velocity_valid = true;
      fillTwist(event.velocity->values, message.velocity);
      message.valid_for_s = event.velocity->valid_for_s;
    } else {
      message.velocity_valid = false;
      message.valid_for_s = 0.0;
    }
    publisher_->publish(message);
  }

private:
  rclcpp::Publisher<chaos_interfaces::msg::ControlTrace>::SharedPtr publisher_;
};

// Fills the typed action result from the ROS-free operation outcome.
template <typename ActionT>
void fillActionResult(typename ActionT::Result & result,
                       const OperationResult & outcome) {
  result.success = outcome.success;
  result.code = outcome.code;
  result.message = outcome.message;
}

// Delivers a typed terminal result by aborting the goal with it.
template <typename ActionT>
void abortGoal(std::shared_ptr<rclcpp_action::ServerGoalHandle<ActionT>> handle,
               const OperationResult & outcome) {
  auto message = std::make_shared<typename ActionT::Result>();
  fillActionResult<ActionT>(*message, outcome);
  handle->abort(message);
}

template <typename ActionT>
class TypedGoalContext : public GoalContext {
public:
  explicit TypedGoalContext(
    std::shared_ptr<rclcpp_action::ServerGoalHandle<ActionT>> handle)
      : handle_(std::move(handle)) {}

  bool isCancelRequested() override {
    return handle_->is_canceling();
  }
  bool isActive() override { return handle_->is_active(); }
  void publishFeedback(double progress,
                       const AdapterSnapshot & snapshot) override {
    auto feedback = std::make_shared<typename ActionT::Feedback>();
    fillFeedback(*feedback, progress, snapshot);
    handle_->publish_feedback(feedback);
  }
  void succeed(const OperationResult & outcome) override {
    auto message = std::make_shared<typename ActionT::Result>();
    fillActionResult<ActionT>(*message, outcome);
    handle_->succeed(message);
  }
  void canceled(const OperationResult & outcome) override {
    auto message = std::make_shared<typename ActionT::Result>();
    fillActionResult<ActionT>(*message, outcome);
    handle_->canceled(message);
  }
  void abort(const OperationResult & outcome) override {
    auto message = std::make_shared<typename ActionT::Result>();
    fillActionResult<ActionT>(*message, outcome);
    handle_->abort(message);
  }

private:
  static void fillFeedback(typename ActionT::Feedback & feedback,
                           double progress,
                           const AdapterSnapshot & snapshot);
  std::shared_ptr<rclcpp_action::ServerGoalHandle<ActionT>> handle_;
};

template <>
void TypedGoalContext<NavigatePose>::fillFeedback(
  NavigatePose::Feedback & feedback, double progress,
  const AdapterSnapshot & snapshot) {
  feedback.current_pose.header.frame_id = snapshot.pose_frame;
  if (snapshot.source_stamp_ns.has_value()) {
    stampFromNs(*snapshot.source_stamp_ns, feedback.current_pose.header.stamp);
  }
  fillPose(snapshot, feedback.current_pose.pose);
  feedback.progress = static_cast<float>(progress);
}

template <>
void TypedGoalContext<Flight>::fillFeedback(
  Flight::Feedback & feedback, double progress,
  const AdapterSnapshot & snapshot) {
  feedback.altitude_m = snapshot.position[2];
  feedback.progress = static_cast<float>(progress);
}

}  // namespace

RobotRuntimeNode::RobotRuntimeNode(const rclcpp::NodeOptions & options)
    : rclcpp::Node("robot_runtime", options) {
  declare_parameter<std::string>("asset_json", "{}");
  asset_ = parseAssetJson(get_parameter("asset_json").as_string());

  clocks_.steady_s = chaos::monotonicSeconds;
  clocks_.mono_ns = chaos::monotonicNs;
  clocks_.wall_ns = chaos::systemNs;
  clocks_.ros_ns = [this] {
    return get_clock()->now().nanoseconds();
  };
  // The driver deadline clock is Linux CLOCK_MONOTONIC, the same named
  // clock as the one the adapter and the supervised driver use. It is
  // assigned explicitly here: its epoch is never inferred from the steady
  // clock and never from the ROS clock.
  clocks_.driver_mono_ns = driver_deadline::monotonicNowNs;

  services_group_ = create_callback_group(
    rclcpp::CallbackGroupType::MutuallyExclusive);
  action_group_ = create_callback_group(
    rclcpp::CallbackGroupType::MutuallyExclusive);
  timer_group_ = create_callback_group(
    rclcpp::CallbackGroupType::MutuallyExclusive);

  const rclcpp::QoS state_qos = rclcpp::QoS(1)
                                  .reliability(rclcpp::ReliabilityPolicy::Reliable)
                                  .durability(rclcpp::DurabilityPolicy::TransientLocal);
  state_pub_ = create_publisher<chaos_interfaces::msg::RobotState>(
    endpoint("state"), state_qos);
  results_pub_ = create_publisher<chaos_interfaces::msg::CommandResult>(
    endpoint("command_results"), 50);
  trace_pub_ = create_publisher<chaos_interfaces::msg::ControlTrace>(
    endpoint("control_trace"), 100);

  // The sink bundle is registered before the coordinator exists: the
  // coordinator owns nothing but the endpoint, and the service owns the
  // sinks, the publishers and the callback captures until the registration
  // is retired. Every capture owns what it needs by value, so a sink never
  // borrows the node and never queries the coordinator at delivery time.
  ObservationSinks sinks;
  sinks.trace = std::make_shared<RosTraceSink>(trace_pub_);
  sinks.results = [publisher = results_pub_](const CommandResultView & view) {
    chaos_interfaces::msg::CommandResult message;
    stampFromNs(view.stamp_ns, message.stamp);
    message.request_id = view.request_id;
    message.success = view.success;
    message.code = view.code;
    message.message = view.message;
    publisher->publish(message);
  };
  sinks.state = [publisher = state_pub_](const StateView & view) {
    chaos_interfaces::msg::RobotState state;
    stampFromNs(view.stamp_ns, state.stamp);
    state.asset_id = view.asset_id;
    state.model = view.model;
    state.connected = view.connected;
    state.ready = view.ready;
    state.pose_valid = view.pose_valid;
    state.operational_state = view.operational_state;
    const AdapterSnapshot & snapshot = view.snapshot;
    if (snapshot.source_stamp_ns.has_value()) {
      stampFromNs(*snapshot.source_stamp_ns, state.pose.header.stamp);
    } else {
      stampFromNs(view.stamp_ns, state.pose.header.stamp);
    }
    state.pose.header.frame_id = snapshot.pose_frame;
    fillPose(snapshot, state.pose.pose);
    state.twist.header = state.pose.header;
    state.twist.header.frame_id = snapshot.twist_frame;
    fillTwist(snapshot.velocity, state.twist.twist);
    state.battery_percentage =
      static_cast<float>(snapshot.battery);  // NaN stays NaN
    state.detail = view.detail;
    publisher->publish(state);
  };
  // The log sink owns the logger by value, so a delivery on the log worker
  // never reaches back into the node, and it maps the level of the line
  // onto the ROS logger.
  sinks.log = [logger = get_logger()](const std::string & level,
                                      const std::string & text) {
    if (level == "error") {
      RCLCPP_ERROR(logger, "%s", text.c_str());
    } else if (level == "warning") {
      RCLCPP_WARN(logger, "%s", text.c_str());
    } else {
      RCLCPP_INFO(logger, "%s", text.c_str());
    }
  };
  // The generation belongs to the process manager: the node borrows it for
  // the registration only and keeps the endpoint it receives.
  observations_ = ObservationService::process()->registerProducer(
    std::move(sinks));

  auto adapter = createAdapter(asset_.plugin, *this, asset_);
  adapter->update(0.0);
  coordinator_ = std::make_unique<ControlCoordinator>(asset_, std::move(adapter),
                                                      clocks_, observations_);

  // Endpoints must be stored: rclcpp::Node keeps only weak references, so a
  // temporary returned here would be destroyed immediately and the endpoint
  // would silently disappear from the ROS graph.
  description_service_ = create_service<chaos_interfaces::srv::GetDescription>(
    endpoint("description"),
    [this](const std::shared_ptr<chaos_interfaces::srv::GetDescription::Request>
             request,
           std::shared_ptr<chaos_interfaces::srv::GetDescription::Response>
             response) { onDescribe(request, response); },
    rclcpp::ServicesQoS(), services_group_);
  lease_service_ = create_service<chaos_interfaces::srv::ControlLease>(
    endpoint("control/lease"),
    [this](const std::shared_ptr<chaos_interfaces::srv::ControlLease::Request>
             request,
           std::shared_ptr<chaos_interfaces::srv::ControlLease::Response>
             response) { onLease(request, response); },
    rclcpp::ServicesQoS(), services_group_);
  stop_service_ = create_service<chaos_interfaces::srv::Stop>(
    endpoint("motion/stop"),
    [this](const std::shared_ptr<chaos_interfaces::srv::Stop::Request> request,
           std::shared_ptr<chaos_interfaces::srv::Stop::Response> response) {
      onStop(request, response);
    },
    rclcpp::ServicesQoS(), services_group_);
  velocity_sub_ = create_subscription<chaos_interfaces::msg::VelocityCommand>(
    endpoint("motion/velocity"), rclcpp::QoS(1),
    [this](const chaos_interfaces::msg::VelocityCommand & message) {
      onVelocity(message);
    });

  makeActionServer<NavigatePose>(OperationKind::kNavigatePose,
                                 endpoint("navigation/to_pose"));
  makeActionServer<Flight>(OperationKind::kTakeoff, endpoint("flight/takeoff"));
  makeActionServer<Flight>(OperationKind::kLand, endpoint("flight/land"));

  // Steady-clock watchdog: control expiry must run even when /clock is
  // paused or jumps backwards. create_wall_timer is driven by the system
  // steady clock, never by the node's ROS clock.
  timer_ = create_wall_timer(
    std::chrono::milliseconds(50), [this] { coordinator_->tick(); },
    timer_group_);

  // The startup line travels through the observation channel: the log
  // channel delivers on its own worker, so the constructing thread never
  // produces an observation synchronously. The formatting and the admission
  // are inside one protected block, so a failing allocation drops the line
  // instead of unwinding a control that is already constructed.
  try {
    observations_->log("info", asset_.id + ": adapter=" + asset_.plugin +
                               ", capabilities ready");
  } catch (...) {
    observations_->recordConstructionFailure(ObservationChannel::log);
  }
}

RobotRuntimeNode::~RobotRuntimeNode() {
  // Two-phase shutdown (R9): the coordinator is closed first so a running
  // goal leaves its loop and publishes a terminal result; the worker is
  // joined next; only then is the adapter closed, so the adapter is never
  // touched concurrently by the worker and the shutdown path. The endpoint
  // is closed last, after every final control event has been admitted.
  timer_->cancel();
  coordinator_->close();
  if (action_worker_) {
    action_worker_->stop();
  }
  coordinator_->finalize();
  observations_->close();
}

// --- Action worker --------------------------------------------------------

RobotRuntimeNode::ActionWorker::~ActionWorker() { stop(); }

bool RobotRuntimeNode::ActionWorker::submit(std::function<void()> job) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    // Bounded: the coordinator admits at most one reservation, so this
    // should never exceed one pending job; refuse rather than grow.
    if (stopping_ || jobs_.size() >= 4) {
      return false;
    }
    jobs_.push_back(std::move(job));
  }
  cv_.notify_one();
  return true;
}

void RobotRuntimeNode::ActionWorker::stop() {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    stopping_ = true;
    jobs_.clear();
  }
  cv_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

bool RobotRuntimeNode::ActionWorker::stopping() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return stopping_;
}

void RobotRuntimeNode::ActionWorker::run() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (true) {
    cv_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
    if (stopping_) {
      return;
    }
    auto job = std::move(jobs_.front());
    jobs_.pop_front();
    lock.unlock();
    try {
      job();
    } catch (const std::exception & error) {
      // A failing job must never reach std::terminate and take the whole
      // runtime down with it.
      RCLCPP_ERROR(rclcpp::get_logger("robot_runtime"),
                   "Action job failed: %s", error.what());
    } catch (...) {
      RCLCPP_ERROR(rclcpp::get_logger("robot_runtime"),
                   "Action job failed with an unknown exception");
    }
    lock.lock();
  }
}

std::string RobotRuntimeNode::endpoint(const std::string & name) const {
  std::string prefix = asset_.name_space;
  while (!prefix.empty() && prefix.back() == '/') {
    prefix.pop_back();
  }
  return prefix + "/" + name;
}

RobotRuntimeNode::GoalTokensPtr RobotRuntimeNode::tokensFor(
  const rclcpp_action::GoalUUID & uuid) {
  std::lock_guard<std::mutex> guard(tokens_mutex_);
  auto & entry = goal_tokens_[uuid];
  if (!entry) {
    entry = std::make_shared<GoalTokens>();
  }
  return entry;
}

RobotRuntimeNode::GoalTokensPtr RobotRuntimeNode::findTokens(
  const rclcpp_action::GoalUUID & uuid) {
  std::lock_guard<std::mutex> guard(tokens_mutex_);
  const auto found = goal_tokens_.find(uuid);
  return found == goal_tokens_.end() ? nullptr : found->second;
}

void RobotRuntimeNode::dropTokens(const rclcpp_action::GoalUUID & uuid) {
  std::lock_guard<std::mutex> guard(tokens_mutex_);
  goal_tokens_.erase(uuid);
}

void RobotRuntimeNode::onDescribe(
  const std::shared_ptr<chaos_interfaces::srv::GetDescription::Request>,
  std::shared_ptr<chaos_interfaces::srv::GetDescription::Response> response)
  const {
  response->asset_id = asset_.id;
  response->model = asset_.model;
  response->model_version = asset_.model_version;
  response->capabilities.assign(asset_.capabilities.begin(),
                                asset_.capabilities.end());
  std::sort(response->capabilities.begin(), response->capabilities.end());
  response->base_frame = asset_.frames.base;
  response->odom_frame = asset_.frames.odom;
  response->map_frame = asset_.frames.map;
  response->linear_limit_mps = asset_.limits.linear_mps;
  response->angular_limit_rps = asset_.limits.angular_rps;
  response->command_timeout_s = asset_.limits.command_timeout_s;
  response->lease_max_s = asset_.limits.lease_max_s;
}

void RobotRuntimeNode::onLease(
  const std::shared_ptr<chaos_interfaces::srv::ControlLease::Request> request,
  std::shared_ptr<chaos_interfaces::srv::ControlLease::Response> response) {
  LeaseRequestView view;
  view.operation = request->operation;
  view.owner = request->owner;
  view.token = request->token;
  view.duration_s = request->duration_s;
  view.expires_at = control::RosTime{request->expires_at.sec,
                                     static_cast<std::int64_t>(
                                       request->expires_at.nanosec)};
  const LeaseResponseView result = coordinator_->lease(view);
  response->success = result.success;
  response->code = result.code;
  response->message = result.message;
  response->token = result.token;
  response->remaining_s = result.remaining_s;
}

void RobotRuntimeNode::onStop(
  const std::shared_ptr<chaos_interfaces::srv::Stop::Request> request,
  std::shared_ptr<chaos_interfaces::srv::Stop::Response> response) {
  StopRequestView view;
  view.lease_token = request->lease_token;
  view.request_id = request->request_id;
  view.expires_at = control::RosTime{request->expires_at.sec,
                                     static_cast<std::int64_t>(
                                       request->expires_at.nanosec)};
  const StopResponseView result = coordinator_->stop(view);
  response->success = result.success;
  response->code = result.code;
  response->message = result.message;
}

void RobotRuntimeNode::onVelocity(
  const chaos_interfaces::msg::VelocityCommand & message) {
  VelocityRequestView view;
  view.stamp_sec = message.header.stamp.sec;
  view.stamp_nanosec = message.header.stamp.nanosec;
  view.frame_id = message.header.frame_id;
  view.lease_token = message.lease_token;
  view.request_id = message.request_id;
  view.valid_for_s = message.valid_for_s;
  view.velocity = {message.velocity.linear.x, message.velocity.linear.y,
                   message.velocity.linear.z, message.velocity.angular.x,
                   message.velocity.angular.y, message.velocity.angular.z};
  coordinator_->velocity(view);
}

template <typename ActionT>
void RobotRuntimeNode::makeActionServer(OperationKind kind,
                                        const std::string & name) {
  using GoalHandle = rclcpp_action::ServerGoalHandle<ActionT>;
  typename rclcpp_action::Server<ActionT>::GoalCallback goal_callback =
    [this, kind](const rclcpp_action::GoalUUID & uuid,
                 std::shared_ptr<const typename ActionT::Goal> goal) {
      // Start before reserving control: allocation/thread failures cannot
      // leave an accepted goal without a worker to finish its reservation.
      if (!action_worker_) {
        try {
          action_worker_ = std::make_unique<ActionWorker>();
        } catch (const std::exception &) {
          return rclcpp_action::GoalResponse::REJECT;
        }
      }
      GoalRequestView view;
      view.lease_token = goal->lease_token;
      view.expires_at = control::RosTime{goal->expires_at.sec,
                                         static_cast<std::int64_t>(
                                           goal->expires_at.nanosec)};
      if constexpr (std::is_same_v<ActionT, NavigatePose>) {
        view.target_frame = goal->target.header.frame_id;
        view.position = {goal->target.pose.position.x,
                         goal->target.pose.position.y,
                         goal->target.pose.position.z};
        view.orientation = {goal->target.pose.orientation.x,
                            goal->target.pose.orientation.y,
                            goal->target.pose.orientation.z,
                            goal->target.pose.orientation.w};
      } else {
        view.altitude_m = goal->altitude_m;
      }
      const GoalTokensPtr tokens = tokensFor(uuid);
      // The terminal publisher resolves the action handle lazily: it may be
      // called before the accepted callback installed the handle (lost
      // reservation), in which case the result is parked and delivered as
      // soon as the handle is known.
      const auto decision = coordinator_->goal(
        kind, view, tokens.get(),
        [tokens](const OperationResult & outcome) {
          std::function<void(const OperationResult &)> terminal;
          {
            std::lock_guard<std::mutex> guard(tokens->terminal_mutex);
            terminal = tokens->terminal;
            if (!terminal) {
              tokens->pending_terminal = outcome;
              return;
            }
          }
          terminal(outcome);
        });
      if (decision != ControlCoordinator::GoalDecision::kAccept) {
        // A rejected goal must not leave an entry in the token cache.
        dropTokens(uuid);
        return rclcpp_action::GoalResponse::REJECT;
      }
      // DEFER: execution is scheduled by the accepted callback on the
      // single action worker; the worker calls execute() so clients see
      // the EXECUTING transition at the right time.
      return rclcpp_action::GoalResponse::ACCEPT_AND_DEFER;
    };
  typename rclcpp_action::Server<ActionT>::CancelCallback cancel_callback =
    [this](const std::shared_ptr<GoalHandle> goal_handle) {
      // rclcpp_action only accepts a cancel for a goal that is executing.
      // The accepted callback transitions the goal to EXECUTING on the
      // executor thread, and both callbacks run in the same mutually
      // exclusive group, so a cancel can never observe a goal that has not
      // started yet.
      if (!goal_handle->is_executing() && !goal_handle->is_canceling()) {
        return rclcpp_action::CancelResponse::REJECT;
      }
      // The goal may have finished between the status check and this
      // lookup; its entry is gone then and the cancel has nothing to stop.
      const GoalTokensPtr tokens = findTokens(goal_handle->get_goal_id());
      if (!tokens) {
        return rclcpp_action::CancelResponse::REJECT;
      }
      const auto decision = coordinator_->cancel(tokens.get(),
                                                 goal_handle.get());
      return decision == ControlCoordinator::CancelDecision::kAccept
               ? rclcpp_action::CancelResponse::ACCEPT
               : rclcpp_action::CancelResponse::REJECT;
    };
  typename rclcpp_action::Server<ActionT>::AcceptedCallback accepted_callback =
    [this, kind](const std::shared_ptr<GoalHandle> goal_handle) {
      const GoalTokensPtr tokens = tokensFor(goal_handle->get_goal_id());
      std::optional<OperationResult> parked;
      {
        std::lock_guard<std::mutex> guard(tokens->terminal_mutex);
        tokens->terminal = [goal_handle](const OperationResult & outcome) {
          abortGoal<ActionT>(goal_handle, outcome);
        };
        parked = tokens->pending_terminal;
        tokens->pending_terminal.reset();
      }
      coordinator_->accepted(tokens.get(), goal_handle.get());
      // Transition the goal to EXECUTING here, on the executor thread: the
      // worker must not be the one that makes the goal cancelable, because
      // rclcpp_action rejects a cancel for a goal that is still accepted.
      // No execute callback is registered, so execute() only performs the
      // status transition and returns; it does not run the goal.
      try {
        goal_handle->execute();
      } catch (const std::exception & error) {
        RCLCPP_ERROR(rclcpp::get_logger("robot_runtime"),
                     "Failed to start the goal execution: %s", error.what());
      }
      if (parked.has_value()) {
        // The reservation was already dropped; deliver the typed terminal
        // result instead of executing the goal.
        {
          std::lock_guard<std::mutex> guard(tokens->terminal_mutex);
          tokens->terminal(*parked);
        }
        dropTokens(goal_handle->get_goal_id());
        return;
      }
      // Never block the executor and never spawn a per-goal thread: hand
      // the goal to the single persistent action worker.
      const bool queued = action_worker_->submit(
        [this, kind, tokens, goal_handle] {
          if (action_worker_->stopping()) {
            // The runtime is shutting down: deliver a terminal result
            // instead of starting motion.
            abortGoal<ActionT>(goal_handle,
                               OperationResult{false, "SHUTDOWN",
                                               "Runtime stopped"});
            dropTokens(goal_handle->get_goal_id());
            return;
          }
          executeGoal<ActionT>(kind, goal_handle->get_goal_id(), goal_handle);
        });
      if (!queued) {
        // The worker is stopping or full: the client must still receive a
        // typed terminal result.
        abortGoal<ActionT>(goal_handle,
                           OperationResult{false, "BUSY",
                                           "Action worker unavailable"});
        dropTokens(goal_handle->get_goal_id());
      }
    };
  auto server = rclcpp_action::create_server<ActionT>(
    this, name, goal_callback, cancel_callback, accepted_callback,
    rcl_action_server_get_default_options(), action_group_);
  action_servers_.push_back(server);
}

template <typename ActionT>
void RobotRuntimeNode::executeGoal(
  OperationKind kind, const rclcpp_action::GoalUUID & uuid,
  std::shared_ptr<rclcpp_action::ServerGoalHandle<ActionT>> goal_handle) {
  const GoalTokensPtr tokens = tokensFor(uuid);
  TypedGoalContext<ActionT> ctx(goal_handle);
  coordinator_->runOperation(kind, tokens.get(), goal_handle.get(), ctx);
  dropTokens(uuid);
}

}  // namespace chaos::runtime
