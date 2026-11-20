#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "chaos_interfaces/action/execute_skill.hpp"
#include "chaos_skill/vocabulary.hpp"

namespace chaos_sdk
{
class Robot;
}

namespace chaos_skill
{

/// Terminal outcome of one skill execution.
struct SkillOutcome
{
  bool success = false;
  std::string code;
  std::string message;
  /// Hub the asset rests in after the attempt; same as the entry hub when the
  /// skill never left it, or the skill's exit hub on success.
  std::string hub_state;
};

/// Feedback sample emitted while a skill runs.
struct SkillFeedback
{
  std::string current_hub;
  float progress = 0.0F;
  std::string code;
};

/// Executes one skill token on a chaos_sdk::Robot against a Vocabulary.
///
/// The executor owns the hub-state bookkeeping: it validates the token against
/// the asset's current hub, performs the lease lifecycle the skill declares,
/// dispatches the matching SDK call, and folds the result back into a hub.
///
/// A skill with `needs_lease` acquires a short lease before dispatch and
/// releases it when the SDK goal reaches a terminal state. A skill that fails
/// to reach its exit hub leaves the asset in the hub it actually stopped in
/// (which for motion skills is the entry hub after a controlled stop).
///
/// Thread safety: one Executor serves one robot; calls are serialized by the
/// caller (the action server). It is not safe to run two skills concurrently.
class Executor
{
public:
  using FeedbackFn = std::function<void(const SkillFeedback &)>;

  Executor(std::shared_ptr<chaos_sdk::Robot> robot, Vocabulary vocab,
    std::string initial_hub,
    /// Frame_id used for NavigatePose goals. When empty the robot's base_frame
    /// is used; most models navigate in a map frame instead, so the skill
    /// server should pass the model's navigation frame explicitly.
    std::string navigation_frame = {});

  /// Current hub the executor believes the asset is in.
  std::string current_hub() const;

  /// Force the hub after an external event moved the asset (e.g. teleop took
  /// over and left it airborne). Only names declared in the vocabulary are
  /// accepted; an undeclared name throws chaos::Rejected.
  void set_hub(const std::string & hub);

  /// The vocabulary this executor was built with.
  const Vocabulary & vocabulary() const {return vocab_;}

  /// Execute one token synchronously, emitting feedback through `on_feedback`.
  ///
  /// The call blocks for the duration of the underlying SDK action. It returns
  /// the outcome once the goal reached a terminal state; a rejected goal and a
  /// failed action both map to success=false with a machine code.
  SkillOutcome execute(
    const std::string & token, const nlohmann::json & arguments,
    const FeedbackFn & on_feedback = nullptr);

  /// Whether an ExecuteSkill goal is currently running.
  bool busy() const {return busy_;}

  /// Ask the executor to cancel the running skill, if any. Cancellation is
  /// best-effort: it maps to the SDK goal's cancel request where one exists.
  void request_cancel();

private:
  SkillOutcome run_navigate(
    const nlohmann::json & args, const FeedbackFn & on_feedback);
  SkillOutcome run_takeoff(
    const nlohmann::json & args, const FeedbackFn & on_feedback);
  SkillOutcome run_land(const FeedbackFn & on_feedback);
  SkillOutcome run_hold(const FeedbackFn & on_feedback);
  SkillOutcome run_stop_motion(const FeedbackFn & on_feedback);

  bool ensure_lease(std::string & code, std::string & message);
  void release_lease();

  std::shared_ptr<chaos_sdk::Robot> robot_;
  Vocabulary vocab_;
  std::string navigation_frame_;
  std::string hub_;
  bool busy_ = false;
  bool cancel_requested_ = false;
  bool lease_acquired_ = false;
};

}  // namespace chaos_skill
