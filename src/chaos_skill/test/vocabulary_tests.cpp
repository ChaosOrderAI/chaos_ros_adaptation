// The skill vocabulary is the contract between the planner and the executor:
// both read the same document, so a malformed one has to be rejected before
// any process starts.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "chaos_core/errors.hpp"
#include "chaos_skill/capability.hpp"
#include "chaos_skill/vocabulary.hpp"

namespace
{

nlohmann::json demo_vocabulary()
{
  return nlohmann::json::parse(R"json(
  {
    "hub_states": ["idle", "airborne"],
    "skills": [
      {"token": "takeoff", "entry_hub": "idle", "exit_hub": "airborne",
       "capability": "motion", "needs_lease": true,
       "arguments_schema": {"altitude_m": {"type": "number"}}},
      {"token": "navigate_to", "entry_hub": "idle", "exit_hub": "idle",
       "capability": "navigation", "needs_lease": true, "arguments_schema": {}},
      {"token": "observe", "entry_hub": "idle", "exit_hub": "idle",
       "capability": "sensor", "needs_lease": false, "any_entry_hub": true,
       "arguments_schema": {}},
      {"token": "announce", "entry_hub": "idle", "exit_hub": "idle",
       "capability": "communication", "needs_lease": false, "any_entry_hub": true,
       "arguments_schema": {}}
    ]
  })json");
}

}  // namespace

TEST(Vocabulary, ParsesTheDeclaredSkillsAndTheirCapabilityFamily)
{
  const auto vocab = chaos_skill::Vocabulary::from_json(demo_vocabulary());

  ASSERT_EQ(vocab.hub_states().size(), 2u);
  ASSERT_EQ(vocab.tokens().size(), 4u);

  const auto * takeoff = vocab.find("takeoff");
  ASSERT_NE(takeoff, nullptr);
  EXPECT_EQ(takeoff->entry_hub, "idle");
  EXPECT_EQ(takeoff->exit_hub, "airborne");
  EXPECT_TRUE(takeoff->needs_lease);
  EXPECT_EQ(takeoff->capability, chaos_skill::Capability::Motion);

  const auto * observe = vocab.find("observe");
  ASSERT_NE(observe, nullptr);
  EXPECT_EQ(observe->capability, chaos_skill::Capability::Sensor);
  EXPECT_TRUE(observe->any_entry_hub);
  EXPECT_FALSE(observe->needs_lease);

  const auto * announce = vocab.find("announce");
  ASSERT_NE(announce, nullptr);
  EXPECT_EQ(announce->capability, chaos_skill::Capability::Communication);
}

TEST(Vocabulary, RejectsASkillWithoutACapabilityFamily)
{
  auto doc = demo_vocabulary();
  doc["skills"][0].erase("capability");

  EXPECT_THROW(chaos_skill::Vocabulary::from_json(doc), chaos::Rejected);
}

TEST(Vocabulary, RejectsAnUnknownCapabilityFamily)
{
  auto doc = demo_vocabulary();
  doc["skills"][0]["capability"] = "teleport";

  EXPECT_THROW(chaos_skill::Vocabulary::from_json(doc), chaos::Rejected);
}

TEST(Vocabulary, RejectsADuplicateTokenAndAnUndeclaredHub)
{
  auto duplicate = demo_vocabulary();
  duplicate["skills"][1]["token"] = "takeoff";
  EXPECT_THROW(chaos_skill::Vocabulary::from_json(duplicate), chaos::Rejected);

  auto undeclared = demo_vocabulary();
  undeclared["skills"][0]["entry_hub"] = "flying";
  EXPECT_THROW(chaos_skill::Vocabulary::from_json(undeclared), chaos::Rejected);
}

TEST(Vocabulary, ChecksTheEntryHubOfAToken)
{
  const auto vocab = chaos_skill::Vocabulary::from_json(demo_vocabulary());

  EXPECT_FALSE(vocab.check_entry("takeoff", "idle").has_value());
  EXPECT_TRUE(vocab.check_entry("takeoff", "airborne").has_value());
  EXPECT_TRUE(vocab.check_entry("nope", "idle").has_value());
  // An any_entry_hub skill starts from every declared hub.
  EXPECT_FALSE(vocab.check_entry("observe", "idle").has_value());
  EXPECT_FALSE(vocab.check_entry("observe", "airborne").has_value());
  // A hub that the vocabulary does not name is never a legal entry.
  EXPECT_TRUE(vocab.check_entry("observe", "unknown").has_value());
}

TEST(Vocabulary, ReportsTheHubASkillLeavesTheAssetIn)
{
  const auto vocab = chaos_skill::Vocabulary::from_json(demo_vocabulary());

  ASSERT_TRUE(vocab.exit_hub("takeoff", "idle").has_value());
  EXPECT_EQ(*vocab.exit_hub("takeoff", "idle"), "airborne");
  // An any_entry_hub skill keeps the asset where it was.
  ASSERT_TRUE(vocab.exit_hub("observe", "airborne").has_value());
  EXPECT_EQ(*vocab.exit_hub("observe", "airborne"), "airborne");
  EXPECT_FALSE(vocab.exit_hub("nope", "idle").has_value());
}

TEST(Vocabulary, ThePlannerDocumentNamesTheCapabilityOfEverySkill)
{
  const auto vocab = chaos_skill::Vocabulary::from_json(demo_vocabulary());
  const auto document = vocab.planner_document();

  ASSERT_TRUE(document.contains("skills"));
  for (const auto & skill : document["skills"]) {
    EXPECT_TRUE(skill.contains("capability")) << skill.value("token", "");
    EXPECT_FALSE(skill.value("capability", "").empty());
  }
}

TEST(Capability, ParsesEveryDeclaredFamily)
{
  EXPECT_EQ(chaos_skill::capability_from_string("motion"), chaos_skill::Capability::Motion);
  EXPECT_EQ(
    chaos_skill::capability_from_string("navigation"), chaos_skill::Capability::Navigation);
  EXPECT_EQ(chaos_skill::capability_from_string("sensor"), chaos_skill::Capability::Sensor);
  EXPECT_EQ(
    chaos_skill::capability_from_string("communication"),
    chaos_skill::Capability::Communication);
  EXPECT_FALSE(chaos_skill::capability_from_string("teleport").has_value());

  EXPECT_STREQ(chaos_skill::to_string(chaos_skill::Capability::Motion), "motion");
  EXPECT_STREQ(chaos_skill::to_string(chaos_skill::Capability::Communication), "communication");
}
