#include "chaos_skill/executor.hpp"

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

#include <geometry_msgs/msg/pose_stamped.hpp>

#include "chaos_core/errors.hpp"
#include "chaos_sdk/action_task.hpp"
#include "chaos_sdk/robot.hpp"

namespace chaos_skill
{

namespace
{

constexpr std::chrono::seconds kLeaseDuration{10};

template<typename T>
T arg(const nlohmann::json & args, const char * key)
{
  const auto it = args.find(key);
  if (it == args.end()) {
    throw chaos::Rejected("skill_args.missing",
      std::string("argument '") + key + "' is required");
  }
  return it->get<T>();
}

}  // namespace

Executor::Executor(
  std::shared_ptr<chaos_sdk::Robot> robot, Vocabulary vocab, std::string initial_hub,
  std::string navigation_frame)
: robot_(std::move(robot)), vocab_(std::move(vocab)),
  navigation_frame_(std::move(navigation_frame)), hub_(std::move(initial_hub))
{
  if (!robot_) {
    throw chaos::Rejected("skill_exec.invalid", "executor requires a robot client");
  }
  if (!vocab_.is_hub(hub_)) {
    throw chaos::Rejected("skill_exec.undeclared_hub",
      "initial hub '" + hub_ + "' is not in the vocabulary");
  }
}

std::string Executor::current_hub() const {return hub_;}

void Executor::set_hub(const std::string & hub)
{
  if (!vocab_.is_hub(hub)) {
    throw chaos::Rejected("skill_exec.undeclared_hub",
      "hub '" + hub + "' is not in the vocabulary");
  }
  hub_ = hub;
}

void Executor::request_cancel()
{
  cancel_requested_ = true;
}

bool Executor::ensure_lease(std::string & code, std::string & message)
{
  if (lease_acquired_) {
    return true;
  }
  auto future = robot_->acquire("chaos_skill", kLeaseDuration.count());
  const auto response = future.get();
  if (!response.success) {
    code = response.code;
    message = response.message;
    return false;
  }
  lease_acquired_ = true;
  return true;
}

void Executor::release_lease()
{
  if (!lease_acquired_) {
    return;
  }
  try {
    robot_->release().get();
  } catch (const std::exception &) {
    // A failed release is logged by the SDK; the lease expires on its own.
  }
  lease_acquired_ = false;
}

SkillOutcome Executor::execute(
  const std::string & token, const nlohmann::json & arguments,
  const FeedbackFn & on_feedback)
{
  SkillOutcome outcome;
  outcome.hub_state = hub_;

  const auto * def = vocab_.find(token);
  if (!def) {
    outcome.code = "skill.unknown_token";
    outcome.message = "unknown skill token '" + token + "'";
    return outcome;
  }
  if (busy_) {
    outcome.code = "skill.busy";
    outcome.message = "another skill is already running";
    return outcome;
  }
  if (auto err = vocab_.check_entry(token, hub_)) {
    outcome.code = "skill.hub_mismatch";
    outcome.message = *err;
    return outcome;
  }
  if (def->needs_lease && !ensure_lease(outcome.code, outcome.message)) {
    return outcome;
  }

  busy_ = true;
  cancel_requested_ = false;
  const auto guard = std::unique_ptr<bool, void (*)(bool *)>(
    &busy_, [](bool * b) {*b = false;});

  try {
    if (token == "navigate_to") {
      outcome = run_navigate(arguments, on_feedback);
    } else if (token == "takeoff") {
      outcome = run_takeoff(arguments, on_feedback);
    } else if (token == "land") {
      outcome = run_land(on_feedback);
    } else if (token == "hold") {
      outcome = run_hold(on_feedback);
    } else if (token == "stop") {
      outcome = run_stop_motion(on_feedback);
    } else {
      outcome.code = "skill.unimplemented";
      outcome.message = "token '" + token + "' is declared but has no executor binding";
    }
  } catch (const chaos::Rejected & e) {
    outcome.code = e.code();
    outcome.message = e.message();
  } catch (const std::exception & e) {
    outcome.code = "skill.internal_error";
    outcome.message = e.what();
  }

  if (outcome.success) {
    if (auto hub = vocab_.exit_hub(token, hub_)) {
      hub_ = *hub;
    }
    outcome.hub_state = hub_;
  }
  if (def->needs_lease) {
    release_lease();
  }
  return outcome;
}

SkillOutcome Executor::run_navigate(
  const nlohmann::json & args, const FeedbackFn & on_feedback)
{
  SkillOutcome outcome;
  outcome.hub_state = hub_;

  geometry_msgs::msg::PoseStamped target;
  // The navigation frame is a property of the deployment, not the caller:
  // default to the configured navigation frame, fall back to the asset's base
  // frame only when the deployment did not name one.
  target.header.frame_id = args.value("frame_id",
    navigation_frame_.empty() ? robot_->base_frame() : navigation_frame_);
  target.pose.position.x = arg<double>(args, "x");
  target.pose.position.y = arg<double>(args, "y");
  target.pose.position.z = args.value("z", 0.0);
  const double yaw = args.value("yaw", 0.0);
  target.pose.orientation.w = std::cos(yaw / 2.0);
  target.pose.orientation.z = std::sin(yaw / 2.0);

  auto acceptance = robot_->navigate(
    target,
    [on_feedback, this](chaos_interfaces::action::NavigatePose::Feedback fb) {
      if (on_feedback) {
        on_feedback({hub_, fb.progress, "navigate_to"});
      }
    }).get();

  if (!acceptance) {
    outcome.code = "skill.goal_rejected";
    outcome.message = "navigate goal was rejected";
    return outcome;
  }
  const auto result = acceptance->result().get();
  outcome.success = result.code == rclcpp_action::ResultCode::SUCCEEDED &&
    result.result->success;
  outcome.code = result.result->code;
  outcome.message = result.result->message;
  return outcome;
}

SkillOutcome Executor::run_takeoff(
  const nlohmann::json & args, const FeedbackFn & on_feedback)
{
  SkillOutcome outcome;
  outcome.hub_state = hub_;
  const double altitude = arg<double>(args, "altitude_m");

  auto acceptance = robot_->takeoff(
    altitude,
    [on_feedback, this](chaos_interfaces::action::Flight::Feedback fb) {
      if (on_feedback) {
        on_feedback({hub_, fb.progress, "takeoff"});
      }
    }).get();

  if (!acceptance) {
    outcome.code = "skill.goal_rejected";
    outcome.message = "takeoff goal was rejected";
    return outcome;
  }
  const auto result = acceptance->result().get();
  outcome.success = result.code == rclcpp_action::ResultCode::SUCCEEDED &&
    result.result->success;
  outcome.code = result.result->code;
  outcome.message = result.result->message;
  return outcome;
}

SkillOutcome Executor::run_land(const FeedbackFn & on_feedback)
{
  SkillOutcome outcome;
  outcome.hub_state = hub_;

  auto acceptance = robot_->land(
    [on_feedback, this](chaos_interfaces::action::Flight::Feedback fb) {
      if (on_feedback) {
        on_feedback({hub_, fb.progress, "land"});
      }
    }).get();

  if (!acceptance) {
    outcome.code = "skill.goal_rejected";
    outcome.message = "land goal was rejected";
    return outcome;
  }
  const auto result = acceptance->result().get();
  outcome.success = result.code == rclcpp_action::ResultCode::SUCCEEDED &&
    result.result->success;
  outcome.code = result.result->code;
  outcome.message = result.result->message;
  return outcome;
}

SkillOutcome Executor::run_hold(const FeedbackFn & /*on_feedback*/)
{
  // "hold" is the passive skill: keep the current hub until the next token.
  // It never needs a lease and always succeeds.
  SkillOutcome outcome;
  outcome.success = true;
  outcome.hub_state = hub_;
  return outcome;
}

SkillOutcome Executor::run_stop_motion(const FeedbackFn & /*on_feedback*/)
{
  SkillOutcome outcome;
  outcome.hub_state = hub_;
  const auto response = robot_->stop().get();
  outcome.success = response.success;
  outcome.code = response.code;
  outcome.message = response.message;
  return outcome;
}

}  // namespace chaos_skill
