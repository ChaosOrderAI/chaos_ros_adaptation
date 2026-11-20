#include "chaos_skill/executor.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

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

/// The robot state as the payload of the "observe" skill.
nlohmann::json state_json(const chaos_interfaces::msg::RobotState & state)
{
  return {
    {"stamp_sec", state.stamp.sec},
    {"stamp_nanosec", state.stamp.nanosec},
    {"asset_id", state.asset_id},
    {"model", state.model},
    {"operational_state", state.operational_state},
    {"connected", state.connected},
    {"ready", state.ready},
    {"pose_valid", state.pose_valid},
    {"pose", {
        {"frame_id", state.pose.header.frame_id},
        {"x", state.pose.pose.position.x},
        {"y", state.pose.pose.position.y},
        {"z", state.pose.pose.position.z},
        {"qx", state.pose.pose.orientation.x},
        {"qy", state.pose.pose.orientation.y},
        {"qz", state.pose.pose.orientation.z},
        {"qw", state.pose.pose.orientation.w},
      }},
    {"twist", {
        {"frame_id", state.twist.header.frame_id},
        {"linear_x", state.twist.twist.linear.x},
        {"linear_y", state.twist.twist.linear.y},
        {"linear_z", state.twist.twist.linear.z},
        {"angular_z", state.twist.twist.angular.z},
      }},
    {"battery_percentage", state.battery_percentage},
    {"detail", state.detail},
  };
}

/// The base capability a token binds to.
///
/// The binding is the truth about what a token reaches; the vocabulary
/// declares the same thing. A mismatch is refused, so the declaration in the
/// vocabulary can never drift away from the dispatch.
std::optional<Capability> capability_of(const std::string & token)
{
  if (token == "takeoff" || token == "land" || token == "stop") {
    return Capability::Motion;
  }
  if (token == "navigate_to") {
    return Capability::Navigation;
  }
  if (token == "observe") {
    return Capability::Sensor;
  }
  if (token == "announce") {
    return Capability::Communication;
  }
  return std::nullopt;
}

/// Releases the busy flag when the executor leaves a skill, on every path.
class ReservationGuard
{
public:
  explicit ReservationGuard(Executor * executor) :executor_(executor) {}
  ~ReservationGuard() {executor_->release_reservation();}
  ReservationGuard(const ReservationGuard &) = delete;
  ReservationGuard & operator=(const ReservationGuard &) = delete;

private:
  Executor * executor_;
};

}  // namespace

Executor::Executor(
  std::shared_ptr<chaos_sdk::Robot> robot, Vocabulary vocab, std::string initial_hub,
  std::string navigation_frame, std::shared_ptr<CapabilityPorts> ports)
: robot_(std::move(robot)), ports_(std::move(ports)), vocab_(std::move(vocab)),
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

std::string Executor::current_hub() const
{
  const std::lock_guard<std::mutex> guard(state_mutex_);
  return hub_;
}

void Executor::set_hub(const std::string & hub)
{
  const std::lock_guard<std::mutex> guard(state_mutex_);
  if (!vocab_.is_hub(hub)) {
    throw chaos::Rejected("skill_exec.undeclared_hub",
      "hub '" + hub + "' is not in the vocabulary");
  }
  hub_ = hub;
}

void Executor::request_cancel()
{
  const std::lock_guard<std::mutex> guard(state_mutex_);
  cancel_requested_ = true;
}

bool Executor::busy() const
{
  const std::lock_guard<std::mutex> guard(state_mutex_);
  return busy_;
}

bool Executor::ensure_lease(std::string & code, std::string & message)
{
  {
    const std::lock_guard<std::mutex> guard(state_mutex_);
    if (lease_acquired_) {
      return true;
    }
  }
  auto future = robot_->acquire("chaos_skill", kLeaseDuration.count());
  const auto response = future.get();
  if (!response.success) {
    code = response.code;
    message = response.message;
    return false;
  }
  const std::lock_guard<std::mutex> guard(state_mutex_);
  lease_acquired_ = true;
  return true;
}

void Executor::release_lease()
{
  {
    const std::lock_guard<std::mutex> guard(state_mutex_);
    if (!lease_acquired_) {
      return;
    }
    lease_acquired_ = false;
  }
  try {
    robot_->release().get();
  } catch (const std::exception &) {
    // A failed release is logged by the SDK; the lease expires on its own.
  }
}

void Executor::release_reservation()
{
  const std::lock_guard<std::mutex> guard(state_mutex_);
  busy_ = false;
}

SkillOutcome Executor::execute(
  const std::string & token, const nlohmann::json & arguments,
  const FeedbackFn & on_feedback, const SkillOrigin & origin)
{
  SkillOutcome outcome;

  const auto * def = vocab_.find(token);
  if (!def) {
    outcome.code = "skill.unknown_token";
    outcome.message = "unknown skill token '" + token + "'";
    outcome.hub_state = current_hub();
    return outcome;
  }

  // One skill runs at a time. The reservation is taken before the lease and
  // before the dispatch, so two callers can never both pass the check.
  {
    const std::lock_guard<std::mutex> guard(state_mutex_);
    outcome.hub_state = hub_;
    if (busy_) {
      outcome.code = "skill.busy";
      outcome.message = "another skill is already running";
      return outcome;
    }
    busy_ = true;
    cancel_requested_ = false;
  }
  const ReservationGuard reservation(this);

  const auto bound = capability_of(token);
  if (!bound) {
    outcome.code = "skill.unimplemented";
    outcome.message = "token '" + token + "' has no binding to a base capability";
    return outcome;
  }
  if (*bound != def->capability) {
    outcome.code = "skill.capability_mismatch";
    outcome.message = "skill '" + token + "' is declared as '" +
      std::string(to_string(def->capability)) + "' but binds to '" +
      std::string(to_string(*bound)) + "'";
    return outcome;
  }

  const auto entry_hub = current_hub();
  // The hub precondition binds a planning decision. A direct operator
  // command is not a planning decision: the hub is the state machine of the
  // planning layer and must not veto it.
  if (origin.enforce_hub) {
    if (auto err = vocab_.check_entry(token, entry_hub)) {
      outcome.code = "skill.hub_mismatch";
      outcome.message = *err;
      return outcome;
    }
  }
  // A caller that already holds the lease runs the skill with its own token.
  // The runtime admits one lease holder, so the skill layer must not take a
  // second one, and it must not release the lease of the caller either.
  const bool owns_lease = origin.control_token.empty();
  if (owns_lease && def->needs_lease && !ensure_lease(outcome.code, outcome.message)) {
    return outcome;
  }
  const std::string control_token = origin.control_token;

  try {
    if (token == "navigate_to") {
      outcome = run_navigate(arguments, on_feedback, control_token);
    } else if (token == "takeoff") {
      outcome = run_takeoff(arguments, on_feedback, control_token);
    } else if (token == "land") {
      outcome = run_land(on_feedback, control_token);
    } else if (token == "hold") {
      outcome = run_hold(on_feedback);
    } else if (token == "stop") {
      outcome = run_stop_motion(on_feedback, control_token);
    } else if (token == "observe") {
      outcome = run_observe(arguments);
    } else if (token == "announce") {
      outcome = run_announce(arguments, origin);
    }
  } catch (const chaos::Rejected & e) {
    outcome.code = e.code();
    outcome.message = e.message();
  } catch (const std::exception & e) {
    outcome.code = "skill.internal_error";
    outcome.message = e.what();
  }

  if (outcome.success) {
    if (auto hub = vocab_.exit_hub(token, entry_hub)) {
      const std::lock_guard<std::mutex> guard(state_mutex_);
      hub_ = *hub;
    }
    outcome.hub_state = current_hub();
  } else {
    outcome.hub_state = entry_hub;
  }
  if (owns_lease && def->needs_lease) {
    release_lease();
  }
  return outcome;
}

SkillOutcome Executor::run_navigate(
  const nlohmann::json & args, const FeedbackFn & on_feedback,
  const std::string & control_token)
{
  SkillOutcome outcome;
  outcome.hub_state = current_hub();

  geometry_msgs::msg::PoseStamped target;
  // The navigation frame is a property of the deployment, not the caller:
  // default to the configured navigation frame, fall back to the asset's base
  // frame only when the deployment did not name one.
  target.header.frame_id = args.value("frame_id",
    navigation_frame_.empty() ? robot_->base_frame() : navigation_frame_);
  target.pose.position.x = arg<double>(args, "x");
  target.pose.position.y = arg<double>(args, "y");
  target.pose.position.z = args.value("z", 0.0);
  if (args.contains("qw")) {
    // A quaternion is passed through unchanged: a caller that has a full
    // orientation must not lose it to a yaw conversion.
    target.pose.orientation.x = args.value("qx", 0.0);
    target.pose.orientation.y = args.value("qy", 0.0);
    target.pose.orientation.z = args.value("qz", 0.0);
    target.pose.orientation.w = args.value("qw", 1.0);
  } else {
    const double yaw = args.value("yaw", 0.0);
    target.pose.orientation.w = std::cos(yaw / 2.0);
    target.pose.orientation.z = std::sin(yaw / 2.0);
  }

  auto acceptance = robot_->navigate(
    target,
    [on_feedback, this](chaos_interfaces::action::NavigatePose::Feedback fb) {
      if (on_feedback) {
        on_feedback({current_hub(), fb.progress, "navigate_to"});
      }
    },
    std::nullopt,
    control_token.empty() ? std::nullopt : std::optional<std::string>(control_token)).get();

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
  const nlohmann::json & args, const FeedbackFn & on_feedback,
  const std::string & control_token)
{
  SkillOutcome outcome;
  outcome.hub_state = current_hub();
  const double altitude = arg<double>(args, "altitude_m");

  auto acceptance = robot_->takeoff(
    altitude,
    [on_feedback, this](chaos_interfaces::action::Flight::Feedback fb) {
      if (on_feedback) {
        on_feedback({current_hub(), fb.progress, "takeoff"});
      }
    },
    std::nullopt,
    control_token.empty() ? std::nullopt : std::optional<std::string>(control_token)).get();

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

SkillOutcome Executor::run_land(
  const FeedbackFn & on_feedback, const std::string & control_token)
{
  SkillOutcome outcome;
  outcome.hub_state = current_hub();

  auto acceptance = robot_->land(
    [on_feedback, this](chaos_interfaces::action::Flight::Feedback fb) {
      if (on_feedback) {
        on_feedback({current_hub(), fb.progress, "land"});
      }
    },
    std::nullopt,
    control_token.empty() ? std::nullopt : std::optional<std::string>(control_token)).get();

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
  outcome.hub_state = current_hub();
  return outcome;
}

SkillOutcome Executor::run_stop_motion(
  const FeedbackFn & /*on_feedback*/, const std::string & control_token)
{
  SkillOutcome outcome;
  outcome.hub_state = current_hub();
  const auto response = robot_->stop(
    std::nullopt, std::nullopt,
    control_token.empty() ? std::nullopt : std::optional<std::string>(control_token)).get();
  outcome.success = response.success;
  outcome.code = response.code;
  outcome.message = response.message;
  return outcome;
}

SkillOutcome Executor::run_observe(const nlohmann::json & args)
{
  SkillOutcome outcome;
  outcome.hub_state = current_hub();

  const auto state = robot_->state();
  if (!state) {
    outcome.code = "skill.no_state";
    outcome.message = "the asset has not reported a state yet";
    return outcome;
  }

  nlohmann::json payload = nlohmann::json::object();
  payload["state"] = state_json(*state);

  nlohmann::json aux = nlohmann::json::object();
  if (ports_) {
    std::vector<std::string> streams = ports_->aux_streams();
    if (args.contains("streams")) {
      if (!args["streams"].is_array()) {
        outcome.code = "skill_args.invalid";
        outcome.message = "argument 'streams' must be an array of stream names";
        return outcome;
      }
      streams.clear();
      for (const auto & name : args["streams"]) {
        if (!name.is_string()) {
          outcome.code = "skill_args.invalid";
          outcome.message = "argument 'streams' must be an array of stream names";
          return outcome;
        }
        streams.push_back(name.get<std::string>());
      }
    }
    for (const auto & stream : streams) {
      const auto reading = ports_->read_aux(stream);
      aux[stream] = {
        {"valid", reading.valid},
        {"frame_id", reading.frame_id},
        {"stamp_ns", reading.stamp_ns},
      };
    }
  }
  payload["aux"] = aux;

  outcome.success = true;
  outcome.payload = payload;
  return outcome;
}

SkillOutcome Executor::run_announce(
  const nlohmann::json & args, const SkillOrigin & origin)
{
  SkillOutcome outcome;
  outcome.hub_state = current_hub();

  if (!ports_) {
    outcome.code = "skill.no_ports";
    outcome.message = "this deployment has no communication channel";
    return outcome;
  }

  const auto topic = args.value("topic", std::string{"status"});
  std::string payload_json;
  if (args.contains("payload_json")) {
    if (!args["payload_json"].is_string()) {
      outcome.code = "skill_args.invalid";
      outcome.message = "argument 'payload_json' must be a JSON-encoded string";
      return outcome;
    }
    payload_json = args["payload_json"].get<std::string>();
  } else if (args.contains("payload")) {
    payload_json = args["payload"].dump();
  } else {
    payload_json = nlohmann::json::object().dump();
  }

  std::string code;
  std::string message;
  if (!ports_->announce(topic, payload_json, origin, code, message)) {
    outcome.code = code.empty() ? "skill.announce_failed" : code;
    outcome.message = message;
    return outcome;
  }

  outcome.success = true;
  outcome.payload = {
    {"topic", topic},
    {"bytes", static_cast<std::int64_t>(payload_json.size())},
  };
  return outcome;
}

}  // namespace chaos_skill
