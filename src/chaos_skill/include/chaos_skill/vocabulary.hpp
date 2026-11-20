#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "chaos_skill/capability.hpp"

namespace chaos_skill
{

/// One discrete team skill as seen by a planner.
///
/// A skill is defined at the vocabulary level, not per asset: the same token
/// means the same team behaviour on every asset that declares it. Each skill
/// starts and ends at one of the declared hub states, so any legal sequence
/// of skills composes without a controller switch between them.
struct SkillDef
{
  /// The token a planner emits, e.g. "navigate_to", "takeoff", "hold".
  std::string token;
  /// The hub state the asset must be in before the skill may start.
  std::string entry_hub;
  /// The hub state the asset is left in when the skill succeeds.
  std::string exit_hub;
  /// JSON-schema-ish description of the arguments the skill accepts. Empty
  /// object `{}` means the skill takes no arguments.
  nlohmann::json arguments_schema = nlohmann::json::object();
  /// Human-readable description shown to a planner in the prompt.
  std::string description;
  /// Whether the skill requires an active control lease when it runs.
  bool needs_lease = true;
  /// Skills that do not change the hub state (e.g. "hold", "report") may be
  /// invoked from any entry hub; the entry_hub is then informational only.
  bool any_entry_hub = false;
  /// The base capability family this skill invokes.
  ///
  /// The skill layer is the only layer that reaches the platform, so the
  /// family is declared with the skill rather than inferred from the token:
  /// the vocabulary is the one place that says which base function a token
  /// ends up calling.
  Capability capability = Capability::Motion;
};

/// The whole skill vocabulary of one asset configuration.
///
/// The vocabulary is parsed once at startup from a versioned YAML asset
/// resource; it is immutable for the lifetime of the process. Hub states form
/// a closed set: the runtime rejects a token whose entry or exit hub is not
/// declared.
class Vocabulary
{
public:
  Vocabulary() = default;

  /// Parse and validate a vocabulary JSON document.
  ///
  /// Expected shape:
  ///   {
  ///     "hub_states": ["idle", "holding", "airborne"],
  ///     "skills": [
  ///       {"token": "takeoff", "entry_hub": "idle", "exit_hub": "airborne",
  ///        "arguments_schema": {"altitude_m": "number"}, "needs_lease": true}
  ///     ]
  ///   }
  ///
  /// Throws chaos::Rejected on a malformed document, an undeclared hub, a
  /// duplicate token, or an empty vocabulary.
  static Vocabulary from_json(const nlohmann::json & doc);

  /// Look up a skill by token; nullptr when the vocabulary does not name it.
  const SkillDef * find(const std::string & token) const;

  /// All declared hub states, in declaration order.
  const std::vector<std::string> & hub_states() const {return hub_states_;}

  /// Whether a hub name is part of the closed set.
  bool is_hub(const std::string & name) const;

  /// The JSON document a planner sees: one entry per skill with token,
  /// description and the argument schema. This is what an agent turns into a
  /// tool list for the LLM.
  nlohmann::json planner_document() const;

  /// All skill tokens in declaration order.
  std::vector<std::string> tokens() const;

  /// Validate that a token may be executed from `current_hub`.
  ///
  /// Returns a human-readable rejection message, or std::nullopt when the
  /// skill may start. A skill with `any_entry_hub` accepts every declared hub.
  std::optional<std::string> check_entry(
    const std::string & token, const std::string & current_hub) const;

  /// The hub the asset rests in after a successful execution of `token` from
  /// `current_hub`. For any_entry_hub skills the exit hub is returned as
  /// declared; the caller may treat "same as entry" as a pass-through.
  std::optional<std::string> exit_hub(
    const std::string & token, const std::string & current_hub) const;

private:
  std::vector<std::string> hub_states_;
  std::set<std::string> hub_set_;
  std::map<std::string, SkillDef> skills_;
  std::vector<std::string> token_order_;
};

}  // namespace chaos_skill
