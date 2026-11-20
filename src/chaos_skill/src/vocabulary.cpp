#include "chaos_skill/vocabulary.hpp"

#include <utility>

#include "chaos_core/errors.hpp"

namespace chaos_skill
{

namespace
{

[[noreturn]] void reject(const std::string & code, const std::string & message)
{
  throw chaos::Rejected(code, message);
}

std::string require_string(const nlohmann::json & doc, const char * key,
  const std::string & where)
{
  const auto it = doc.find(key);
  if (it == doc.end() || !it->is_string() || it->get<std::string>().empty()) {
    reject("skill_vocab.invalid", where + " requires a non-empty string '" + key + "'");
  }
  return it->get<std::string>();
}

}  // namespace

Vocabulary Vocabulary::from_json(const nlohmann::json & doc)
{
  if (!doc.is_object()) {
    reject("skill_vocab.invalid", "vocabulary document must be a JSON object");
  }

  Vocabulary vocab;

  const auto hubs_it = doc.find("hub_states");
  if (hubs_it == doc.end() || !hubs_it->is_array() || hubs_it->empty()) {
    reject("skill_vocab.invalid", "'hub_states' must be a non-empty array");
  }
  for (const auto & hub : *hubs_it) {
    if (!hub.is_string() || hub.get<std::string>().empty()) {
      reject("skill_vocab.invalid", "every hub state must be a non-empty string");
    }
    const std::string name = hub.get<std::string>();
    if (!vocab.hub_set_.insert(name).second) {
      reject("skill_vocab.duplicate", "duplicate hub state '" + name + "'");
    }
    vocab.hub_states_.push_back(name);
  }

  const auto skills_it = doc.find("skills");
  if (skills_it == doc.end() || !skills_it->is_array() || skills_it->empty()) {
    reject("skill_vocab.invalid", "'skills' must be a non-empty array");
  }
  for (const auto & entry : *skills_it) {
    if (!entry.is_object()) {
      reject("skill_vocab.invalid", "every skill entry must be an object");
    }
    SkillDef def;
    def.token = require_string(entry, "token", "skill");
    def.entry_hub = require_string(entry, "entry_hub", "skill '" + def.token + "'");
    def.exit_hub = require_string(entry, "exit_hub", "skill '" + def.token + "'");
    if (entry.contains("description") && entry["description"].is_string()) {
      def.description = entry["description"].get<std::string>();
    }
    if (entry.contains("arguments_schema")) {
      if (!entry["arguments_schema"].is_object()) {
        reject("skill_vocab.invalid",
          "'arguments_schema' of skill '" + def.token + "' must be an object");
      }
      def.arguments_schema = entry["arguments_schema"];
    }
    if (entry.contains("needs_lease")) {
      def.needs_lease = entry["needs_lease"].get<bool>();
    }
    if (entry.contains("any_entry_hub")) {
      def.any_entry_hub = entry["any_entry_hub"].get<bool>();
    }
    // The capability family is required: the vocabulary is the one place that
    // says which base function a token ends up calling.
    const auto capability_name = require_string(entry, "capability", "skill '" + def.token + "'");
    const auto capability = capability_from_string(capability_name);
    if (!capability) {
      reject("skill_vocab.invalid",
        "skill '" + def.token + "' names an unknown capability '" + capability_name + "'");
    }
    def.capability = *capability;
    if (!vocab.hub_set_.count(def.entry_hub)) {
      reject("skill_vocab.undeclared_hub",
        "skill '" + def.token + "' enters from undeclared hub '" + def.entry_hub + "'");
    }
    if (!vocab.hub_set_.count(def.exit_hub)) {
      reject("skill_vocab.undeclared_hub",
        "skill '" + def.token + "' exits into undeclared hub '" + def.exit_hub + "'");
    }
    if (vocab.skills_.count(def.token)) {
      reject("skill_vocab.duplicate", "duplicate skill token '" + def.token + "'");
    }
    vocab.token_order_.push_back(def.token);
    vocab.skills_.emplace(def.token, std::move(def));
  }

  return vocab;
}

const SkillDef * Vocabulary::find(const std::string & token) const
{
  const auto it = skills_.find(token);
  return it == skills_.end() ? nullptr : &it->second;
}

bool Vocabulary::is_hub(const std::string & name) const
{
  return hub_set_.count(name) != 0;
}

nlohmann::json Vocabulary::planner_document() const
{
  nlohmann::json skills = nlohmann::json::array();
  for (const auto & token : token_order_) {
    const auto & def = skills_.at(token);
    skills.push_back({
      {"token", def.token},
      {"description", def.description},
      {"entry_hub", def.entry_hub},
      {"exit_hub", def.exit_hub},
      {"capability", std::string(to_string(def.capability))},
      // The planner needs both: a skill that may start from every hub must
      // not be presented as if it needed one, and a skill that takes a lease
      // has to be planned around.
      {"any_entry_hub", def.any_entry_hub},
      {"needs_lease", def.needs_lease},
      {"arguments_schema", def.arguments_schema},
    });
  }
  return {
    {"hub_states", hub_states_},
    {"skills", std::move(skills)},
  };
}

std::vector<std::string> Vocabulary::tokens() const
{
  return token_order_;
}

std::optional<std::string> Vocabulary::check_entry(
  const std::string & token, const std::string & current_hub) const
{
  const auto * def = find(token);
  if (!def) {
    return "unknown skill token '" + token + "'";
  }
  if (!is_hub(current_hub)) {
    return "current hub '" + current_hub + "' is not declared";
  }
  if (def->any_entry_hub) {
    return std::nullopt;
  }
  if (def->entry_hub != current_hub) {
    return "skill '" + token + "' requires hub '" + def->entry_hub +
      "', asset is in '" + current_hub + "'";
  }
  return std::nullopt;
}

std::optional<std::string> Vocabulary::exit_hub(
  const std::string & token, const std::string & current_hub) const
{
  const auto * def = find(token);
  if (!def) {
    return std::nullopt;
  }
  if (def->any_entry_hub) {
    return current_hub;
  }
  return def->exit_hub;
}

}  // namespace chaos_skill
