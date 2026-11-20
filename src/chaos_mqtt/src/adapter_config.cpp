#include "chaos_mqtt/adapter_config.hpp"

#include <algorithm>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "chaos_mqtt/protocol.hpp"

namespace chaos_mqtt
{
namespace
{

const std::regex kAssetIdPattern("[A-Za-z_][A-Za-z0-9_]*");

bool is_name_segment(const std::string & segment)
{
  return std::regex_match(segment, kAssetIdPattern);
}

void require_object(const nlohmann::json & value, const char * what)
{
  if (!value.is_object()) {
    throw std::invalid_argument(std::string(what) + " must be a mapping");
  }
}

nlohmann::json route(std::string command)
{
  return nlohmann::json{
    {"command", std::move(command)},
    {"topic_suffix", "command"},
    {"msg_type", "std_msgs/String"},
  };
}

nlohmann::json topic_mapping(
  const std::string & name, const std::string & asset_id, const std::string & namespace_,
  const std::string & ros_suffix, const std::string & mqtt_suffix, int qos, int ros_depth,
  const char * ros_durability)
{
  return nlohmann::json{
    {"name", name},
    {"direction", "ros2_to_mqtt"},
    {"mqtt_topic", "robots/" + asset_id + "/" + mqtt_suffix},
    {"ros_topic", namespace_ + "/" + ros_suffix},
    {"msg_type", "std_msgs/msg/String"},
    {"delivery", name == "chaos_task_events" ? "latest_nonterminal" : "latest"},
    {"mqtt", nlohmann::json{{"qos", qos}, {"retain", false}}},
    {"ros", nlohmann::json{{"depth", ros_depth}, {"reliability", "reliable"},
        {"durability", ros_durability}}},
    {"max_payload_bytes", 16384},
  };
}

}  // namespace

nlohmann::json build_adapter_config(
  const nlohmann::json & asset,
  const nlohmann::json & transport_profile)
{
  const auto id = asset.contains("id") ? asset.find("id") : asset.find("asset_id");
  if (id == asset.end() || !id->is_string() || !is_name_segment(id->get<std::string>())) {
    throw std::invalid_argument("asset id must be a ROS name segment");
  }
  const std::string asset_id = id->get<std::string>();
  const auto namespace_it = asset.find("namespace");
  if (namespace_it == asset.end() || !namespace_it->is_string()) {
    throw std::invalid_argument("asset namespace must be an absolute ROS namespace");
  }
  const std::string & namespace_ = namespace_it->get_ref<const std::string &>();
  if (namespace_.empty() || namespace_.front() != '/') {
    throw std::invalid_argument("asset namespace must be an absolute ROS namespace");
  }
  std::size_t start = 1;
  while (start <= namespace_.size()) {
    const std::size_t end = namespace_.find('/', start);
    const std::string segment = namespace_.substr(
      start, end == std::string::npos ? std::string::npos : end - start);
    if (segment.empty() || !is_name_segment(segment)) {
      throw std::invalid_argument("asset namespace must be an absolute ROS namespace");
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  const auto profile_config = transport_profile.find("config");
  nlohmann::json config = profile_config == transport_profile.end() ?
    nlohmann::json::object() : *profile_config;
  require_object(config, "transport config");

  nlohmann::json mqtt = config.contains("mqtt") ? config.at("mqtt") : nlohmann::json::object();
  require_object(mqtt, "transport config.mqtt");
  for (auto item = mqtt.begin(); item != mqtt.end(); ++item) {
    if (item->is_string()) {
      std::string value = item->get<std::string>();
      const std::string token = "{asset_id}";
      std::size_t position = 0;
      while ((position = value.find(token, position)) != std::string::npos) {
        value.replace(position, token.size(), asset_id);
        position += asset_id.size();
      }
      *item = std::move(value);
    }
  }
  auto set_default = [&mqtt](const char * key, nlohmann::json value) {
    if (!mqtt.contains(key)) {
      mqtt[key] = std::move(value);
    }
  };
  set_default("client_id", "chaos_" + asset_id);
  set_default("command_topic", "robots/" + asset_id + "/commands");
  set_default("response_topic", "robots/" + asset_id + "/responses");
  // The Rust envelope event path is reserved. Task events use topic_mappings.
  set_default("event_topic", "");
  config["mqtt"] = std::move(mqtt);
  config["route_profile"] = "custom";
  std::string topic_prefix = namespace_;
  topic_prefix.erase(0, 1);
  config["ros2"] = nlohmann::json{
    {"node_name", asset_id + "_mqtt_bridge"},
    {"topic_prefix", topic_prefix + "/mqtt"},
    {"qos_depth", 32},
  };
  if (!config.contains("response_timeout_ms")) {
    config["response_timeout_ms"] = 30000;
  }
  // Must exceed the gateway's action observation timeout (300 seconds by
  // default); it bounds orphan ACCEPTED records after a gateway failure.
  if (!config.contains("action_timeout_ms")) {
    config["action_timeout_ms"] = 360000;
  }
  const auto & timeout = config.at("action_timeout_ms");
  if (!timeout.is_number_integer() ||
    timeout.get<std::int64_t>() < 360000 || timeout.get<std::int64_t>() > 86400000)
  {
    throw std::invalid_argument("action_timeout_ms must be in 360000..86400000 for the unified gateway");
  }

  std::vector<std::string> capabilities;
  const auto capability_it = asset.find("capabilities");
  if (capability_it != asset.end() && capability_it->is_array()) {
    for (const auto & capability : *capability_it) {
      if (capability.is_string()) {
        capabilities.push_back(capability.get<std::string>());
      }
    }
  }
  // Emit routes in the stable capability definition order the Rust adapter
  // and tests rely on.
  static const char * kOrder[] = {
    "system.describe", "system.ota", "control.acquire", "control.renew", "control.release",
    "motion.stop", "motion.velocity", "navigation.to_pose", "flight.takeoff", "flight.land"};
  nlohmann::json routes = nlohmann::json::array();
  for (const char * command : kOrder) {
    const auto & capability = kCommandCapabilities.at(command);
    if (!capability.has_value() ||
      std::find(capabilities.begin(), capabilities.end(), *capability) != capabilities.end())
    {
      routes.push_back(route(command));
    }
  }
  config["routes"] = std::move(routes);

  // Controlled outward-only mappings. Incoming control always uses the facade.
  config["topic_mappings"] = nlohmann::json::array({
    topic_mapping("chaos_state", asset_id, namespace_, "mqtt/state", "state", 0, 32, "volatile"),
    topic_mapping("chaos_task_events", asset_id, namespace_, "mqtt/task_events", "task_events", 1,
      32, "volatile"),
    // The communication channel of the skill layer leaves the machine on this
    // mapping. The skill layer publishes a typed message; the gateway encodes
    // it for the wire.
    topic_mapping("chaos_announcement", asset_id, namespace_, "mqtt/announcement", "announcement",
      1, 32, "volatile"),
  });
  const auto data_collection = asset.find("data_collection");
  if (data_collection != asset.end() && data_collection->is_object() &&
    data_collection->contains("enabled") && data_collection->at("enabled").is_boolean() &&
    data_collection->at("enabled").get<bool>())
  {
    config["topic_mappings"].push_back(nlohmann::json{
      {"name", "chaos_data_collection_status"},
      {"direction", "ros2_to_mqtt"},
      {"mqtt_topic", "robots/" + asset_id + "/data_collection/status"},
      {"ros_topic", namespace_ + "/data_collection/status"},
      {"msg_type", "std_msgs/msg/String"},
      {"delivery", "latest"},
      {"mqtt", nlohmann::json{{"qos", 0}, {"retain", false}}},
      {"ros", nlohmann::json{{"depth", 1}, {"reliability", "reliable"},
          {"durability", "transient_local"}}},
      {"max_payload_bytes", 16384},
    });
  }
  return config;
}

}  // namespace chaos_mqtt
