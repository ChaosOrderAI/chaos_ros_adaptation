// chaos_skill_server: one skill server per asset.
//
// The server is the single funnel of the skill layer. The top-level sources
// are peers above it: the onboard agent and the external communication
// adapter both publish a SkillIntent, and the server executes it through the
// one hub-state Executor. The Executor is the only place where the skill
// layer reaches the platform, so every skill ends up in the same lease,
// precondition and hub bookkeeping.
//
// The ExecuteSkill action stays as the local facade for callers that want a
// request/response with feedback: a behaviour tree, a tool, an operator.

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_core/errors.hpp"
#include "chaos_interfaces/action/execute_skill.hpp"
#include "chaos_interfaces/srv/skill_request.hpp"
#include "chaos_sdk/robot.hpp"
#include "chaos_skill/executor.hpp"
#include "chaos_skill/intent_ingress.hpp"
#include "chaos_skill/server_ports.hpp"
#include "chaos_skill/vocabulary.hpp"

namespace
{

using chaos_interfaces::action::ExecuteSkill;
using chaos_interfaces::srv::SkillRequest;
using GoalHandle = rclcpp_action::ServerGoalHandle<ExecuteSkill>;

class SkillServer : public rclcpp::Node
{
public:
  SkillServer()
  : Node("chaos_skill_server")
  {
    declare_parameter("asset_id", std::string{});
    declare_parameter("vocabulary_json", std::string{});
    declare_parameter("initial_hub", std::string{});
    declare_parameter("namespace", std::string{});
    // Navigation frame for NavigatePose goals; defaults to <asset_id>/map,
    // matching the runtime's per-asset frame convention.
    declare_parameter("navigation_frame", std::string{});
    // The ingress the top-level sources publish their intents on.
    declare_parameter("intent_topic", std::string{"skill_intent"});
    declare_parameter("event_topic", std::string{"skill_event"});
    // The generic communication channel the "announce" skill writes to.
    declare_parameter("announcement_topic", std::string{"communication/announcement"});
    // Optional sensor streams the "observe" skill may read.
    declare_parameter("sensor_cloud_topic", std::string{});
    declare_parameter("sensor_imu_topic", std::string{});

    const auto asset_id = get_parameter("asset_id").as_string();
    const auto vocab_json = get_parameter("vocabulary_json").as_string();
    auto initial_hub = get_parameter("initial_hub").as_string();
    const auto ns = get_parameter("namespace").as_string();
    auto navigation_frame = get_parameter("navigation_frame").as_string();
    if (navigation_frame.empty()) {
      navigation_frame = asset_id + "/map";
    }

    if (asset_id.empty()) {
      throw chaos::Rejected("skill_server.config", "asset_id parameter is required");
    }
    if (vocab_json.empty()) {
      throw chaos::Rejected("skill_server.config", "vocabulary_json parameter is required");
    }
    const auto doc = nlohmann::json::parse(vocab_json);
    auto vocab = chaos_skill::Vocabulary::from_json(doc);
    if (initial_hub.empty()) {
      initial_hub = vocab.hub_states().front();
    }

    robot_ = chaos_sdk::Robot::create_standalone(
      asset_id,
      ns.empty() ? std::nullopt : std::optional<std::string>(ns));

    chaos_skill::ServerPortsOptions port_options;
    port_options.announcement_topic =
      get_parameter("announcement_topic").as_string();
    port_options.cloud_topic = get_parameter("sensor_cloud_topic").as_string();
    port_options.imu_topic = get_parameter("sensor_imu_topic").as_string();
    ports_ = std::make_shared<chaos_skill::ServerPorts>(this, port_options);

    executor_ = std::make_unique<chaos_skill::Executor>(
      robot_, vocab, initial_hub, navigation_frame, ports_);

    action_ = rclcpp_action::create_server<ExecuteSkill>(
      this, "execute_skill",
      [this](const auto & uuid, const auto & goal) {return on_goal(uuid, goal);},
      [this](const auto & handle) {return on_cancel(handle);},
      [this](const auto & handle) {on_accepted(handle);});

    service_ = create_service<SkillRequest>(
      "skill_request",
      [this](
        const std::shared_ptr<SkillRequest::Request> request,
        std::shared_ptr<SkillRequest::Response> response) {
        on_skill_request(request, response);
      });

    RCLCPP_INFO(get_logger(), "skill server up for asset %s in hub %s",
      asset_id.c_str(), executor_->current_hub().c_str());
  }

  /// Start the ingress. It runs after the node exists as a shared pointer,
  /// because the ingress subscribes with it.
  void start()
  {
    chaos_skill::IntentIngressOptions options;
    options.intent_topic = get_parameter("intent_topic").as_string();
    options.event_topic = get_parameter("event_topic").as_string();
    ingress_ = std::make_unique<chaos_skill::IntentIngress>(
      shared_from_this(), *executor_, options);
    ingress_->start();
    RCLCPP_INFO(get_logger(), "skill ingress listening on %s",
      options.intent_topic.c_str());
  }

  ~SkillServer() override
  {
    if (ingress_) {
      ingress_->stop();
    }
  }

private:
  rclcpp_action::GoalResponse on_goal(
    const rclcpp_action::GoalUUID & /*uuid*/,
    const ExecuteSkill::Goal::ConstSharedPtr & goal)
  {
    if (chaos_skill::deadline_passed(goal->expires_at)) {
      RCLCPP_WARN(get_logger(), "rejecting skill %s: the request expired",
        goal->token.token.c_str());
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (executor_->busy()) {
      RCLCPP_WARN(get_logger(), "rejecting skill %s: busy", goal->token.token.c_str());
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (const auto err = executor_->vocabulary().check_entry(
          goal->token.token, executor_->current_hub()))
    {
      RCLCPP_WARN(get_logger(), "rejecting skill %s: %s",
        goal->token.token.c_str(), err->c_str());
      return rclcpp_action::GoalResponse::REJECT;
    }
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  rclcpp_action::CancelResponse on_cancel(const std::shared_ptr<GoalHandle> &)
  {
    executor_->request_cancel();
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  void on_accepted(const std::shared_ptr<GoalHandle> & handle)
  {
    std::thread([this, handle] {run_goal(handle);}).detach();
  }

  void run_goal(const std::shared_ptr<GoalHandle> & handle)
  {
    const auto goal = handle->get_goal();
    auto result = std::make_shared<ExecuteSkill::Result>();
    auto feedback = std::make_shared<ExecuteSkill::Feedback>();

    nlohmann::json args = nlohmann::json::object();
    if (!goal->token.arguments_json.empty()) {
      try {
        args = nlohmann::json::parse(goal->token.arguments_json);
      } catch (const std::exception & e) {
        result->success = false;
        result->code = "skill.args_invalid";
        result->message = e.what();
        result->hub_state = executor_->current_hub();
        handle->abort(result);
        return;
      }
    }

    chaos_skill::SkillOrigin origin;
    origin.source = goal->token.source;
    origin.deadline_ns = chaos_skill::deadline_to_ns(goal->expires_at);

    chaos_skill::SkillOutcome outcome;
    try {
      // `feedback` is captured by value: the SDK invokes this callback on
      // another thread, possibly after run_goal has returned.
      outcome = executor_->execute(
        goal->token.token, args,
        [this, handle, feedback](const chaos_skill::SkillFeedback & fb) {
          if (!handle->is_active()) {
            return;
          }
          feedback->current_hub = fb.current_hub;
          feedback->progress = fb.progress;
          feedback->code = fb.code;
          handle->publish_feedback(feedback);
        },
        origin);
    } catch (const std::exception & error) {
      outcome.success = false;
      outcome.code = "skill.internal_error";
      outcome.message = error.what();
      outcome.hub_state = executor_->current_hub();
    }

    if (!handle->is_active()) {
      return;
    }
    result->success = outcome.success;
    result->code = outcome.code;
    result->message = outcome.message;
    result->hub_state = outcome.hub_state;
    if (outcome.success) {
      handle->succeed(result);
    } else {
      handle->abort(result);
    }
  }

  void on_skill_request(
    const SkillRequest::Request::SharedPtr request,
    const SkillRequest::Response::SharedPtr response)
  {
    if (chaos_skill::deadline_passed(request->expires_at)) {
      response->accepted = false;
      response->code = "skill.expired";
      response->message = "the request expired before it was accepted";
      return;
    }
    if (executor_->busy()) {
      response->accepted = false;
      response->code = "skill.busy";
      response->message = "another skill is running";
      return;
    }
    if (const auto err = executor_->vocabulary().check_entry(
          request->token.token, executor_->current_hub()))
    {
      response->accepted = false;
      response->code = "skill.hub_mismatch";
      response->message = *err;
      return;
    }

    nlohmann::json args = nlohmann::json::object();
    if (!request->token.arguments_json.empty()) {
      try {
        args = nlohmann::json::parse(request->token.arguments_json);
      } catch (const std::exception & error) {
        response->accepted = false;
        response->code = "skill.args_invalid";
        response->message = error.what();
        return;
      }
    }

    chaos_skill::SkillOrigin origin;
    origin.source = request->token.source;
    origin.deadline_ns = chaos_skill::deadline_to_ns(request->expires_at);
    const auto token = request->token.token;
    // The service only reports acceptance. The skill itself runs off this
    // callback: blocking the node executor would stall every other subscription.
    std::thread([this, token, args, origin] {
      try {
        executor_->execute(token, args, nullptr, origin);
      } catch (const std::exception & error) {
        RCLCPP_ERROR(get_logger(), "skill_request %s failed: %s",
          token.c_str(), error.what());
      }
    }).detach();
    response->accepted = true;
    response->code = "ACCEPTED";
    response->message = "skill accepted";
  }

  std::shared_ptr<chaos_sdk::Robot> robot_;
  std::shared_ptr<chaos_skill::ServerPorts> ports_;
  std::unique_ptr<chaos_skill::Executor> executor_;
  std::unique_ptr<chaos_skill::IntentIngress> ingress_;
  rclcpp_action::Server<ExecuteSkill>::SharedPtr action_;
  rclcpp::Service<SkillRequest>::SharedPtr service_;
};

}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<SkillServer>();
  node->start();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
