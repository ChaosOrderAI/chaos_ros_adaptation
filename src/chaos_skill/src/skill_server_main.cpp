// chaos_skill_server: one ExecuteSkill action server per asset.
//
// Loads the versioned skill vocabulary for the asset, creates a
// chaos_sdk::Robot client against the runtime, and serializes incoming skill
// requests through the hub-state Executor. An agent or an operator talks to it
// through the ExecuteSkill action and the SkillRequest service; the server
// owns the hub bookkeeping and the lease lifecycle.

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
    executor_ = std::make_unique<chaos_skill::Executor>(
      robot_, vocab, initial_hub, navigation_frame);

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

private:
  rclcpp_action::GoalResponse on_goal(
    const rclcpp_action::GoalUUID & /*uuid*/,
    const ExecuteSkill::Goal::ConstSharedPtr & goal)
  {
    if (executor_->busy()) {
      RCLCPP_WARN(get_logger(), "rejecting skill %s: busy", goal->token.token.c_str());
      return rclcpp_action::GoalResponse::REJECT;
    }
    if (const auto err = executor_->vocabulary().check_entry(
          goal->token.token, executor_->current_hub())) {
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

    const auto outcome = executor_->execute(
      goal->token.token, args,
      [this, handle, &feedback](const chaos_skill::SkillFeedback & fb) {
        if (!handle->is_active()) {
          return;
        }
        feedback->current_hub = fb.current_hub;
        feedback->progress = fb.progress;
        feedback->code = fb.code;
        handle->publish_feedback(feedback);
      });

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
    if (executor_->busy()) {
      response->accepted = false;
      response->code = "skill.busy";
      response->message = "another skill is running";
      return;
    }
    if (const auto err = executor_->vocabulary().check_entry(
          request->token.token, executor_->current_hub())) {
      response->accepted = false;
      response->code = "skill.hub_mismatch";
      response->message = *err;
      return;
    }
    response->accepted = true;
  }

  std::shared_ptr<chaos_sdk::Robot> robot_;
  std::unique_ptr<chaos_skill::Executor> executor_;
  rclcpp_action::Server<ExecuteSkill>::SharedPtr action_;
  rclcpp::Service<SkillRequest>::SharedPtr service_;
};

}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<SkillServer>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
