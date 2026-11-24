#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "chaos_interfaces/action/execute_skill.hpp"
#include "chaos_skill/capability.hpp"
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
  /// Structured payload of the skill, e.g. the sensor snapshot of "observe".
  nlohmann::json payload = nlohmann::json::object();
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
/// dispatches the matching base capability, and folds the result back into a
/// hub.
///
/// The executor is the only place where the skill layer reaches the platform:
/// motion and navigation go through the SDK robot, sensors and communication
/// go through the CapabilityPorts. It never talks to a broker or a driver.
///
/// A skill with `needs_lease` acquires a lease before dispatch, renews it until the goal finishes, and
/// releases it when the SDK goal reaches a terminal state. A skill that fails
/// to reach its exit hub leaves the asset in the hub it actually stopped in
/// (which for motion skills is the entry hub after a controlled stop).
///
/// Thread safety: one Executor serves one robot and runs one skill at a time.
/// A call that arrives while another skill runs is rejected with
/// "skill.busy" instead of waiting, except "stop": that token cancels the
/// running skill and issues the platform stop on the lease the skill already
/// holds. The hub, the busy flag and the lease bookkeeping are guarded by one
/// mutex, so a reader never observes a half-updated hub; the long SDK calls
/// run outside that mutex.
class Executor
{
public:
  using FeedbackFn = std::function<void(const SkillFeedback &)>;

  Executor(std::shared_ptr<chaos_sdk::Robot> robot, Vocabulary vocab,
    std::string initial_hub,
    /// Frame_id used for NavigatePose goals. When empty the robot's base_frame
    /// is used; most models navigate in a map frame instead, so the skill
    /// server should pass the model's navigation frame explicitly.
    std::string navigation_frame = {},
    /// Base capabilities besides the SDK robot. Optional: an executor without
    /// ports refuses the skills that need them with "skill.no_ports".
    std::shared_ptr<CapabilityPorts> ports = nullptr);

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
  ///
  /// `origin` names the request that triggered the skill. It reaches the
  /// capabilities that answer back to the source, such as the communication
  /// channel.
  SkillOutcome execute(
    const std::string & token, const nlohmann::json & arguments,
    const FeedbackFn & on_feedback = nullptr,
    const SkillOrigin & origin = {});

  /// Whether a skill is currently running.
  bool busy() const;

  /// Ask the executor to cancel the running skill, if any. Cancellation is
  /// best-effort: it maps to the SDK goal's cancel request where one exists.
  void request_cancel();

  /// Release the busy flag of a finished skill. Internal: the executor calls
  /// it when a skill leaves, on every path.
  void release_reservation();

private:
  SkillOutcome run_navigate(
    const nlohmann::json & args, const FeedbackFn & on_feedback,
    const std::string & control_token, const std::optional<double> & goal_ttl,
    bool renew);
  SkillOutcome run_takeoff(
    const nlohmann::json & args, const FeedbackFn & on_feedback,
    const std::string & control_token, const std::optional<double> & goal_ttl,
    bool renew);
  SkillOutcome run_land(
    const FeedbackFn & on_feedback, const std::string & control_token,
    const std::optional<double> & goal_ttl, bool renew);
  SkillOutcome run_hold(const FeedbackFn & on_feedback);
  SkillOutcome run_stop_motion(const FeedbackFn & on_feedback, const std::string & control_token);
  SkillOutcome run_observe(const nlohmann::json & args);
  SkillOutcome run_announce(const nlohmann::json & args, const SkillOrigin & origin);

  bool ensure_lease(std::string & code, std::string & message);
  /// Extend the lease this executor acquired. A caller's lease is never renewed.
  bool renew_lease(std::string & code, std::string & message);
  bool cancellation_requested() const;
  void release_lease();

  std::shared_ptr<chaos_sdk::Robot> robot_;
  std::shared_ptr<CapabilityPorts> ports_;
  Vocabulary vocab_;
  std::string navigation_frame_;
  std::string hub_;
  mutable std::mutex state_mutex_;
  bool busy_ = false;
  bool cancel_requested_ = false;
  bool lease_acquired_ = false;
  /// Lease token the running skill was given by its caller. Empty when this
  /// executor acquired the lease itself; the SDK client then holds the token.
  std::string active_lease_token_;
};

}  // namespace chaos_skill
