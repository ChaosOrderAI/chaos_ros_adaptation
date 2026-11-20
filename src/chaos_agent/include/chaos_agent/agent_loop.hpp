#pragma once

#include <memory>
#include <string>

#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_agent/llama_client.hpp"
#include "chaos_interfaces/action/execute_skill.hpp"
#include "chaos_interfaces/msg/robot_state.hpp"

namespace chaos_agent
{

/// The agent decision loop for one asset.
///
/// Each iteration:
///   1. builds a snapshot of the asset (RobotState + current hub + task text),
///   2. asks the llama-server for the next skill tool call over the declared
///      vocabulary,
///   3. sends the chosen token to chaos_skill's ExecuteSkill action,
///   4. waits for the result and feeds it back into the snapshot before the
///      next iteration.
///
/// "hold" and "done" are treated as control tokens, not skills: "done" ends
/// the task, "hold" idles one iteration. Everything else is forwarded to the
/// skill server verbatim.
///
/// The loop owns no ROS executor; the host node spins. All work happens on
/// the worker thread `run_once` is called from.
class AgentLoop
{
public:
  using ExecuteSkill = chaos_interfaces::action::ExecuteSkill;

  AgentLoop(
    rclcpp::Node::SharedPtr node,
    std::string asset_id,
    std::string skill_ns,
    nlohmann::json planner_document,
    std::string system_prompt,
    std::string llama_endpoint = "http://127.0.0.1:8000");

  /// Run one decide→execute→observe cycle. Returns false when the task is
  /// done (the model returned the "done" token) or the loop was cancelled.
  ///
  /// The call blocks for the duration of the model request and the skill's
  /// execution; it is meant to be called from a dedicated worker thread.
  bool run_once();

  /// Latest RobotState observed by the loop (nullptr before the first sample).
  chaos_interfaces::msg::RobotState::ConstSharedPtr state() const;

  /// Current hub as last reported by the skill server (empty before first run).
  std::string current_hub() const {return hub_;}

  /// Set the task the agent should accomplish; kept verbatim in the prompt.
  void set_task(std::string task) {task_ = std::move(task);}

  /// The system prompt given to the model on every call (includes the skill
  /// vocabulary summary produced from `planner_document`).
  const std::string & system_prompt() const {return system_prompt_;}

  /// Ask the loop to stop after the current iteration.
  void request_stop() {stop_requested_ = true;}

  /// The most recent model latency, in milliseconds. -1 before the first call.
  long last_latency_ms() const {return last_latency_ms_;}

private:
  nlohmann::json build_user_content() const;
  nlohmann::json build_tools() const;
  std::string token_from_tool(const chaos_agent::LlamaClient::ToolCall & call) const;

  rclcpp::Node::SharedPtr node_;
  std::string asset_id_;
  rclcpp_action::Client<ExecuteSkill>::SharedPtr skill_client_;
  rclcpp::Subscription<chaos_interfaces::msg::RobotState>::SharedPtr state_sub_;
  chaos_interfaces::msg::RobotState::ConstSharedPtr last_state_;

  LlamaClient llama_;
  nlohmann::json planner_document_;
  std::string system_prompt_;
  std::string task_;
  std::string hub_;
  bool stop_requested_ = false;
  long last_latency_ms_ = -1;
};

}  // namespace chaos_agent
