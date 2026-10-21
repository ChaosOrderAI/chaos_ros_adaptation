// One-to-one port of the former config test contract against the native C++
// generator.
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "chaos_mqtt/adapter_config.hpp"

namespace
{

using chaos_mqtt::build_adapter_config;
using nlohmann::json;

TEST(AdapterConfig, GeneratorIsPureScopedAndHasNoLegacyRoutes)
{
  json profile{
    {"config", json{{"mqtt", json{{"host", "example.invalid"}, {"client_id", "edge_{asset_id}"}}}}},
    {"executable", "/path/to/mqtt_ros2_adapter"},
  };
  json asset{
    {"id", "vehicle_001"},
    {"namespace", "/robots/vehicle_001"},
    {"capabilities", json{"state", "motion.stop", "motion.velocity"}},
  };
  const json config = build_adapter_config(asset, profile);
  EXPECT_EQ(config["route_profile"], "custom");
  EXPECT_EQ(config["ros2"]["topic_prefix"], "robots/vehicle_001/mqtt");
  EXPECT_EQ(config["mqtt"]["client_id"], "edge_vehicle_001");
  // The input profile must not be mutated (deep copy semantics).
  EXPECT_EQ(profile["config"]["mqtt"]["client_id"], "edge_{asset_id}");
  std::set<std::string> commands;
  for (const auto & route : config["routes"]) {
    commands.insert(route["command"]);
    EXPECT_EQ(route["topic_suffix"], "command");
  }
  EXPECT_EQ(commands,
    (std::set<std::string>{"system.describe", "control.acquire", "control.renew",
      "control.release", "motion.stop", "motion.velocity"}));
  for (const auto & mapping : config["topic_mappings"]) {
    EXPECT_EQ(mapping["direction"], "ros2_to_mqtt");
  }
  std::set<std::string> ros_topics;
  std::map<std::string, std::string> delivery;
  for (const auto & mapping : config["topic_mappings"]) {
    ros_topics.insert(mapping["ros_topic"]);
    delivery[mapping["name"]] = mapping["delivery"];
  }
  EXPECT_EQ(ros_topics,
    (std::set<std::string>{"/robots/vehicle_001/mqtt/state",
      "/robots/vehicle_001/mqtt/task_events"}));
  EXPECT_EQ(config["mqtt"]["command_topic"], "robots/vehicle_001/commands");
  EXPECT_EQ(delivery,
    (std::map<std::string, std::string>{
      {"chaos_state", "latest"}, {"chaos_task_events", "latest_nonterminal"}}));
  EXPECT_EQ(config["action_timeout_ms"], 360000);
}

TEST(AdapterConfig, GeneratorCannotExpireAcceptedActionsBeforeGatewayTimeout)
{
  const json asset{{"id", "unit"}, {"namespace", "/robots/unit"}};
  for (const auto & timeout : {json(0), json(300000), json(true), json("360000"),
           json(86400001), json(nullptr), json(360000.5)})
  {
    EXPECT_THROW(
      build_adapter_config(asset, json{{"config", json{{"action_timeout_ms", timeout}}}}),
      std::invalid_argument) << timeout.dump();
  }
}

TEST(AdapterConfig, CollectionStatusIsOptionalReadOnlyAndDoesNotExposeSensorData)
{
  json asset{
    {"id", "unit"},
    {"namespace", "/robots/unit"},
    {"data_collection", json{{"enabled", true}}},
  };
  const json config = build_adapter_config(asset, json{{"config", json::object()}});
  ASSERT_EQ(config["topic_mappings"].size(), 3);
  const json & mapping = config["topic_mappings"][2];
  EXPECT_EQ(mapping["ros_topic"], "/robots/unit/data_collection/status");
  EXPECT_EQ(mapping["mqtt_topic"], "robots/unit/data_collection/status");
  EXPECT_EQ(mapping["direction"], "ros2_to_mqtt");
  EXPECT_EQ(mapping["ros"]["durability"], "transient_local");
  EXPECT_EQ(mapping["mqtt"]["retain"], false);
  for (const auto & route : config["routes"]) {
    EXPECT_EQ(route["command"].get<std::string>().find("record"), std::string::npos);
  }
  asset["data_collection"]["enabled"] = false;
  EXPECT_EQ(build_adapter_config(asset, json{{"config", json::object()}})["topic_mappings"].size(), 2);
}

TEST(AdapterConfig, GeneratorRejectsInvalidNames)
{
  const json profile = json{{"config", json::object()}};
  for (const json & asset : {
    json{{"id", "bad/id"}, {"namespace", "/robots/unit"}},
    json{{"id", "unit"}, {"namespace", "/robots//unit"}},
    json{{"id", "unit"}, {"namespace", "relative"}},
    json{{"id", "unit"}, {"namespace", "/"}},
    json{{"namespace", "/robots/unit"}},
  }) {
    EXPECT_THROW(build_adapter_config(asset, profile), std::invalid_argument) << asset.dump();
  }
}

TEST(AdapterConfig, ExplicitTopicsArePreservedAndNonStringConfigIsRejected)
{
  json asset{{"id", "unit"}, {"namespace", "/robots/unit"}};
  json profile{
    {"config", json{
      {"mqtt", json{{"command_topic", "custom/commands"}, {"response_topic", "custom/responses"},
          {"client_id", "explicit"}}},
      {"response_timeout_ms", 5000},
    }},
  };
  const json config = build_adapter_config(asset, profile);
  EXPECT_EQ(config["mqtt"]["command_topic"], "custom/commands");
  EXPECT_EQ(config["mqtt"]["response_topic"], "custom/responses");
  EXPECT_EQ(config["mqtt"]["client_id"], "explicit");
  EXPECT_EQ(config["mqtt"]["event_topic"], "");
  EXPECT_EQ(config["response_timeout_ms"], 5000);
  EXPECT_EQ(config["ros2"]["node_name"], "unit_mqtt_bridge");
  EXPECT_THROW(build_adapter_config(asset, json{{"config", json::array()}}), std::invalid_argument);
  EXPECT_THROW(
    build_adapter_config(asset, json{{"config", json{{"mqtt", json::array()}}}}),
    std::invalid_argument);
}

}  // namespace
