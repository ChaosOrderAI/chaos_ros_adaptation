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
#include <vector>

#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>

#include "chaos_agent/llama_client.hpp"
#include "chaos_interfaces/msg/announcement.hpp"
#include "chaos_interfaces/msg/robot_state.hpp"
#include "chaos_interfaces/msg/skill_event.hpp"
#include "chaos_interfaces/msg/skill_intent.hpp"

namespace chaos_agent
{

/// What the planner may do while one skill is still running.
///
/// A running skill is not replaced. stop cancels it. continue and hold leave
/// it alone. Every other token, including done, is ignored: publishing it
/// would only be refused as busy, or would end the task while the body is
/// still moving.
enum class InflightDecision
{
  /// Leave the running skill alone.
  kWait,
  /// Cancel the running skill and stop motion.
  kStop,
  /// Not a legal choice while a skill is running.
  kIgnore,
};

/// Classify one planner token for the in-flight case.
///
/// The caller strips the "skill_" tool prefix first. "skill_stop" itself is
/// ignored, so a raw tool name cannot skip that step.
InflightDecision decide_inflight(const std::string & token);

/// The agent decision loop for one asset.
///
/// Each iteration:
///   1. builds a snapshot (pose, hub, task, role, teammate progress),
///   2. asks the model for one skill when nothing is running,
///   3. while a skill is running, waits a short slice and may only stop it
///      or leave it alone,
///   4. takes the terminal event into the next snapshot.
///
/// The model is asked again during a long skill only when the world changed
/// (skill progress, teammate progress, or the body moved) or a few seconds
/// have passed, and never in the same slice that started the skill.
///
/// Teammates exchange task progress on `team_status`. That is not a camera
/// and not a shared controller: a role is a task function another robot may
/// share, and sharing it does not merge observations or control. The topic
/// is not the external announcement mapping.
///
/// The agent is a peer of the external communication adapter: both publish on
/// the same intent topic and both follow the same events. The agent holds no
/// device connection and never calls a base capability itself.
///
/// "done" is a control token that never reaches the skill layer; "hold" is an
/// ordinary skill of the vocabulary, so the tool list never names a function
/// twice. While a skill is running, "continue" is the other control token and
/// it is not a vocabulary skill either.
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
    std::string llama_endpoint = "http://127.0.0.1:8000",
    std::string role = {},
    std::vector<std::string> peer_namespaces = {});

  /// Run one decide→execute→observe cycle. Returns false when the task is
  /// done (the model returned the "done" token and nothing is running) or
  /// the loop was cancelled.
  ///
  /// The call blocks for one model request, or for one short slice of a
  /// skill that is already running. It is meant to be called from a
  /// dedicated worker thread.
  bool run_once();

  /// Latest RobotState observed by the loop (nullptr before the first sample).
  chaos_interfaces::msg::RobotState::ConstSharedPtr state() const;

  /// Current hub as last reported by the skill layer.
  std::string current_hub() const;

  /// The snapshot the model is shown: pose, hub, task, role and teammates.
  nlohmann::json observation() const {return build_user_content();}

  /// Set the task the agent should accomplish; kept verbatim in the prompt.
  void set_task(std::string task);

  /// Task function of this robot. Empty means the snapshot carries no role.
  /// A role is not an identity: two robots may share one.
  void set_role(std::string role);

  /// The system prompt given to the model on every call (includes the skill
  /// vocabulary summary produced from `planner_document`).
  const std::string & system_prompt() const {return system_prompt_;}

  /// Ask the loop to stop after the current iteration.
  void request_stop() {stop_requested_ = true;}

  /// The most recent model latency, in milliseconds. -1 before the first call.
  long last_latency_ms() const {return last_latency_ms_;}

private:
  struct PoseSample
  {
    bool valid = false;
    double x = 0;
    double y = 0;
    double yaw = 0;
  };

  /// Latest task-progress message from one peer, as this robot received it.
  struct Teammate
  {
    nlohmann::json body = nlohmann::json::object();
    std::chrono::steady_clock::time_point received{};
  };

  nlohmann::json build_user_content() const;
  nlohmann::json build_tools() const;
  /// Tools that are legal while a skill is running: stop, if the vocabulary
  /// has it, and continue.
  nlohmann::json build_inflight_tools() const;
  std::string token_from_tool(const chaos_agent::LlamaClient::ToolCall & call) const;
  bool vocabulary_has(const std::string & token) const;
  void on_event(const chaos_interfaces::msg::SkillEvent::ConstSharedPtr & event);
  void on_team_status(const chaos_interfaces::msg::Announcement::ConstSharedPtr & msg);
  /// Wait for the terminal event of one request.
  std::optional<chaos_interfaces::msg::SkillEvent> await_terminal(
    const std::string & request_id, std::chrono::seconds timeout);
  /// Take over the hub and the last outcome of a terminal event.
  void adopt(const chaos_interfaces::msg::SkillEvent & event);
  /// Drop the in-flight skill. The caller holds `mutex_`.
  void clear_inflight();
  /// Publish task progress for peers. Not an image and not a command.
  void publish_team_status();
  /// Phase string of the progress message. The caller holds `mutex_`.
  std::string phase_locked() const;
  /// Teammate rows for the snapshot. The caller holds `mutex_`.
  nlohmann::json teammate_rows_locked() const;
  /// Stable text of the teammate bodies. The caller holds `mutex_`.
  std::string teammate_digest_locked() const;
  /// Pose used to notice that the body moved. Invalid until pose_valid.
  PoseSample sample_pose() const;
  /// True when progress, a teammate, or the pose changed since the last
  /// replan, or the replan period has elapsed.
  bool should_replan();
  /// Remember the world the model is about to be asked about.
  void note_replan_baseline();
  /// Ask whether to stop the skill in `waiting`, or leave it alone.
  /// Never publishes a second motion skill.
  bool replan_while_running(const std::string & waiting);
  /// Publish one stop intent. Does not replace the outstanding request.
  void publish_stop();
  /// Warn at most once per late period while a skill has no terminal event.
  void warn_if_late(const std::string & waiting);
  /// Next request id. The caller holds `mutex_`.
  std::string next_request_id();

  rclcpp::Node::SharedPtr node_;
  std::string asset_id_;
  std::string role_;
  rclcpp::Publisher<chaos_interfaces::msg::SkillIntent>::SharedPtr intent_pub_;
  rclcpp::Subscription<chaos_interfaces::msg::SkillEvent>::SharedPtr event_sub_;
  rclcpp::Subscription<chaos_interfaces::msg::RobotState>::SharedPtr state_sub_;
  rclcpp::Publisher<chaos_interfaces::msg::Announcement>::SharedPtr team_pub_;
  std::vector<rclcpp::Subscription<chaos_interfaces::msg::Announcement>::SharedPtr> team_subs_;

  mutable std::mutex state_mutex_;
  chaos_interfaces::msg::RobotState::ConstSharedPtr last_state_;

  mutable std::mutex mutex_;
  std::condition_variable signal_;
  std::map<std::string, chaos_interfaces::msg::SkillEvent> terminal_;
  /// Insertion order of `terminal_`. Eviction drops the oldest event that is
  /// not the request the loop is waiting on; map order is not that order.
  std::deque<std::string> terminal_order_;
  /// The request whose outcome has not arrived. The skill may still be
  /// running, so the loop waits for it instead of sending a second intent.
  std::string outstanding_;
  /// Token of the outstanding skill, empty when nothing is running.
  std::string in_flight_token_;
  float in_flight_progress_ = 0.F;
  std::chrono::steady_clock::time_point in_flight_since_{};
  /// A stop was already published for the outstanding skill.
  bool stop_sent_ = false;
  std::chrono::steady_clock::time_point last_late_warn_{};
  /// World at the previous replan, so an unchanged skill does not call the
  /// model on every slice.
  std::chrono::steady_clock::time_point last_replan_{};
  float replan_progress_ = 0.F;
  std::string replan_digest_;
  PoseSample replan_pose_{};
  bool have_replan_pose_ = false;
  /// Latest progress message of each peer, keyed by that peer's asset id.
  std::map<std::string, Teammate> teammates_;

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
