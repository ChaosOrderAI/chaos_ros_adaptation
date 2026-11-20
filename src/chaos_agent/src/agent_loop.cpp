#include "chaos_agent/agent_loop.hpp"

#include <chrono>
#include <utility>

namespace chaos_agent
{

using chaos_interfaces::action::ExecuteSkill;
using chaos_interfaces::msg::RobotState;

namespace
{

constexpr std::chrono::seconds kSkillGoalTimeout{30};

/// The control tokens that never reach the skill server.
constexpr const char * kDoneToken = "done";

std::string summarize_vocab(const nlohmann::json & doc)
{
  std::string out;
  for (const auto & skill : doc.value("skills", nlohmann::json::array())) {
    out += "- " + skill.value("token", "") + ": " + skill.value("description", "") + "\n";
  }
  return out;
}

}  // namespace

AgentLoop::AgentLoop(
  rclcpp::Node::SharedPtr node,
  std::string asset_id,
  std::string skill_ns,
  nlohmann::json planner_document,
  std::string system_prompt,
  std::string llama_endpoint)
: node_(std::move(node)),
  asset_id_(std::move(asset_id)),
  llama_(std::move(llama_endpoint)),
  planner_document_(std::move(planner_document)),
  system_prompt_(std::move(system_prompt))
{
  if (!node_) {
    throw std::invalid_argument("AgentLoop requires a node");
  }

  if (system_prompt_.empty()) {
    system_prompt_ =
      "You are the onboard planner for robot " + asset_id_ +
      ". At each step you are shown the robot's current hub state and task. "
      "Choose exactly one skill tool call from the declared vocabulary. "
      "A skill only starts when its entry hub matches the current hub; "
      "if none fits, choose hold. Return done when the task is complete.\n\n"
      "Available skills:\n" + summarize_vocab(planner_document_);
  }

  // The agent node already lives in the asset's namespace, so the action and
  // state topic are addressed relatively; skill_ns is only a fallback when
  // the node is placed outside it.
  const std::string prefix = skill_ns.empty() || skill_ns == node_->get_namespace()
    ? "" : (skill_ns + "/");
  skill_client_ = rclcpp_action::create_client<ExecuteSkill>(node_, prefix + "execute_skill");
  state_sub_ = node_->create_subscription<RobotState>(
    prefix + "state", rclcpp::QoS(1).best_effort(),
    [this](RobotState::ConstSharedPtr msg) {last_state_ = std::move(msg);});
}

RobotState::ConstSharedPtr AgentLoop::state() const {return last_state_;}

nlohmann::json AgentLoop::build_user_content() const
{
  nlohmann::json snapshot = nlohmann::json::object();
  snapshot["asset_id"] = asset_id_;
  snapshot["current_hub"] = hub_.empty() ? nlohmann::json(nullptr) : nlohmann::json(hub_);
  snapshot["task"] = task_;

  if (last_state_) {
    snapshot["state"] = {
      {"operational_state", last_state_->operational_state},
      {"connected", last_state_->connected},
      {"ready", last_state_->ready},
      {"pose_valid", last_state_->pose_valid},
      {"battery_percentage", last_state_->battery_percentage},
      {"pose", {
          {"frame_id", last_state_->pose.header.frame_id},
          {"x", last_state_->pose.pose.position.x},
          {"y", last_state_->pose.pose.position.y},
          {"z", last_state_->pose.pose.position.z},
        }},
    };
  } else {
    snapshot["state"] = nullptr;
  }
  return snapshot;
}

nlohmann::json AgentLoop::build_tools() const
{
  nlohmann::json tools = nlohmann::json::array();
  for (const auto & skill : planner_document_.value("skills", nlohmann::json::array())) {
    tools.push_back({
      {"type", "function"},
      {"function", {
          {"name", "skill_" + skill.value("token", "")},
          {"description",
            "[" + skill.value("entry_hub", "") + " -> " + skill.value("exit_hub", "") +
              "] " + skill.value("description", "")},
          {"parameters", {
              {"type", "object"},
              {"properties", skill.value("arguments_schema", nlohmann::json::object())},
            }},
        }},
    });
  }
  // Control tokens, same shape so the model picks uniformly.
  tools.push_back({
    {"type", "function"},
    {"function", {
        {"name", "skill_hold"},
        {"description", "stay in the current hub for one step"},
        {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}},
      }},
  });
  tools.push_back({
    {"type", "function"},
    {"function", {
        {"name", "skill_done"},
        {"description", "the task is complete; stop deciding"},
        {"parameters", {{"type", "object"}, {"properties", nlohmann::json::object()}}},
      }},
  });
  return tools;
}

std::string AgentLoop::token_from_tool(const LlamaClient::ToolCall & call) const
{
  static const std::string prefix = "skill_";
  if (call.name.rfind(prefix, 0) == 0) {
    return call.name.substr(prefix.size());
  }
  return call.name;
}

bool AgentLoop::run_once()
{
  if (stop_requested_) {
    return false;
  }

  const auto response = llama_.chat(
    system_prompt_, build_user_content(), build_tools(),
    {{"temperature", 0.2}, {"max_tokens", 64}});
  last_latency_ms_ = response.latency.count();
  if (!response.ok) {
    RCLCPP_WARN(node_->get_logger(), "llama call failed: %s %s",
      response.code.c_str(), response.message.c_str());
    return true;  // transient: try again next iteration
  }
  if (response.tool_calls.empty()) {
    RCLCPP_WARN(node_->get_logger(), "llama returned no tool call (content=%s)",
      response.content.c_str());
    return true;
  }

  const auto & call = response.tool_calls.front();
  const std::string token = token_from_tool(call);
  if (token == kDoneToken) {
    RCLCPP_INFO(node_->get_logger(), "agent reported done");
    return false;
  }

  ExecuteSkill::Goal goal;
  goal.token.token = token;
  goal.token.arguments_json = call.arguments.dump();
  goal.token.source = asset_id_;
  goal.token.decided_at = node_->now();

  if (!skill_client_->wait_for_action_server(std::chrono::seconds(2))) {
    RCLCPP_WARN(node_->get_logger(), "skill server not available");
    return true;
  }
  auto goal_future = skill_client_->async_send_goal(goal);
  if (goal_future.wait_for(kSkillGoalTimeout) != std::future_status::ready) {
    RCLCPP_WARN(node_->get_logger(), "skill goal send timed out");
    return true;
  }
  auto handle = goal_future.get();
  if (!handle) {
    RCLCPP_WARN(node_->get_logger(), "skill %s rejected", token.c_str());
    return true;
  }
  auto result_future = skill_client_->async_get_result(handle);
  if (result_future.wait_for(kSkillGoalTimeout) != std::future_status::ready) {
    skill_client_->async_cancel_goal(handle);
    return true;
  }
  const auto result = result_future.get();
  if (result.code == rclcpp_action::ResultCode::SUCCEEDED && result.result->success) {
    hub_ = result.result->hub_state;
  }
  RCLCPP_INFO(node_->get_logger(), "skill %s -> %s (hub=%s, code=%s)",
    token.c_str(), result.result->success ? "ok" : "fail",
    result.result->hub_state.c_str(), result.result->code.c_str());
  return true;
}

}  // namespace chaos_agent
