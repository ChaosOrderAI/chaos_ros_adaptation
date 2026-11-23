#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>

#include "chaos_agent/llama_client.hpp"
#include "chaos_interfaces/msg/robot_state.hpp"
#include "chaos_interfaces/msg/skill_event.hpp"
#include "chaos_interfaces/msg/skill_intent.hpp"

namespace chaos_agent
{

/// The agent decision loop for one asset.
///
/// Each iteration:
///   1. builds a snapshot of the asset (RobotState + current hub + task text),
///   2. asks the llama-server for the next skill tool call over the declared
///      vocabulary,
///   3. publishes the chosen token as a skill intent on the skill ingress,
///   4. waits for the terminal event and feeds it back into the snapshot
///      before the next iteration.
///
/// The agent is a peer of the external communication adapter: both publish on
/// the same intent topic and both follow the same events. The agent holds no
/// device connection and never calls a base capability itself.
///
/// "done" is a control token that never reaches the skill layer; "hold" is an
/// ordinary skill of the vocabulary, so the tool list never names a function
/// twice.
///
/// The loop owns no ROS executor; the host node spins. All work happens on
/// the worker thread `run_once` is called from.
class AgentLoop
{
public:
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

  /// Current hub as last reported by the skill layer.
  std::string current_hub() const;

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
  void on_event(const chaos_interfaces::msg::SkillEvent::ConstSharedPtr & event);
  /// Wait for the terminal event of one request.
  std::optional<chaos_interfaces::msg::SkillEvent> await_terminal(
    const std::string & request_id, std::chrono::seconds timeout);
  /// Take over the hub and the last outcome of a terminal event.
  void adopt(const chaos_interfaces::msg::SkillEvent & event);

  rclcpp::Node::SharedPtr node_;
  std::string asset_id_;
  rclcpp::Publisher<chaos_interfaces::msg::SkillIntent>::SharedPtr intent_pub_;
  rclcpp::Subscription<chaos_interfaces::msg::SkillEvent>::SharedPtr event_sub_;
  rclcpp::Subscription<chaos_interfaces::msg::RobotState>::SharedPtr state_sub_;

  mutable std::mutex state_mutex_;
  chaos_interfaces::msg::RobotState::ConstSharedPtr last_state_;

  mutable std::mutex mutex_;
  std::condition_variable signal_;
  std::map<std::string, chaos_interfaces::msg::SkillEvent> terminal_;
  /// Insertion order of `terminal_`. Eviction drops the oldest event that is
  /// not the request the loop is waiting on; map order is not that order.
  std::deque<std::string> terminal_order_;
  /// The request whose outcome did not arrive in time. The skill may still be
  /// running, so the loop waits for it instead of sending a second intent.
  std::string outstanding_;
  /// How often the loop waited for the outstanding request without an answer.
  int outstanding_waits_ = 0;

  LlamaClient llama_;
  nlohmann::json planner_document_;
  std::string system_prompt_;
  std::string task_;
  std::string hub_;
  /// The outcome of the last skill, handed back to the model.
  nlohmann::json last_result_ = nlohmann::json::object();
  /// Makes the request ids of one agent process unique.
  std::uint64_t run_id_ = 0;
  std::uint64_t sequence_ = 0;
  bool stop_requested_ = false;
  long last_latency_ms_ = -1;
};

}  // namespace chaos_agent
