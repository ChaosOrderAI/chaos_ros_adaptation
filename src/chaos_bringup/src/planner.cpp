#include "chaos_bringup/planner.hpp"

#include <unistd.h>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "chaos_assets/errors.hpp"
#include "chaos_assets/inventory.hpp"
#include "chaos_bringup/driver_registry.hpp"
#include "installed_executable.hpp"

namespace chaos::bringup {

namespace {

using chaos::assets::ConfigurationError;
using chaos::assets::Inventory;

// A stable key for a value that may be absent. The JSON encoding keeps a
// missing value distinguishable from an empty string.
// The value of an option, or the marker of an absent one.
//
// The marker keeps a missing option distinguishable from an empty string, so
// two assets without an option collide with each other and not with an asset
// that sets the option.
std::string option_of(const Json& options, const std::string& name) {
  const auto found = options.find(name);
  if (found == options.end() || found->is_null()) return "null";
  return found->dump();
}

std::string string_of(const Json& value, const std::string& where) {
  if (!value.is_string()) {
    throw ConfigurationError(where + ": expected a string");
  }
  return value.get_ref<const std::string&>();
}

// Whether a value carries information.
//
// The reference implementation used the truthiness of the value: an empty
// event topic is the normal case of a profile without events, and it must not
// be reported as an invalid topic name.
bool is_truthy(const Json& value) {
  if (value.is_null()) return false;
  if (value.is_boolean()) return value.get<bool>();
  if (value.is_number()) return value.get<double>() != 0.0;
  if (value.is_string()) return !value.get_ref<const std::string&>().empty();
  return !value.empty();
}

// Look up an executable on PATH.
std::string which(const std::string& executable) {
  if (executable.find('/') != std::string::npos) {
    if (::access(executable.c_str(), X_OK) == 0) return executable;
    return std::string();
  }
  const char* path = std::getenv("PATH");
  if (path == nullptr) return std::string();
  const std::string search(path);
  size_t start = 0;
  while (start <= search.size()) {
    const size_t end = search.find(':', start);
    const std::string directory =
        search.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!directory.empty()) {
      const std::string candidate = directory + "/" + executable;
      if (::access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return std::string();
}

// The identity of the DDS endpoint of an asset.
//
// The endpoint of a plugin is the vendor DDS domain and the network
// interface of the process. A plugin that binds its endpoint to the host
// owns the endpoint together with the host. A plugin whose vendor topics
// are unsuffixed owns the whole domain: a second asset in the same domain
// would receive the state of the first one, so the host is not part of the
// identity and the domain is owned conservatively.
std::string dds_connection_of(const Json& asset, const Json& options,
                              const DriverMetadata& metadata) {
  const std::string domain = option_of(options, "dds_domain_id");
  if (metadata.dds_ownership == DdsOwnership::kDomainEndpoint) {
    return "dds_domain_id=" + domain;
  }
  return asset.at("host").get_ref<const std::string&>() + "|" +
         option_of(options, "network_interface") + "|" + domain;
}

LaunchPlan plan_inventory(const Inventory& inventory, const std::string& host_id,
                          const std::vector<std::string>& asset_ids, bool enable_mqtt,
                          bool check_files, const MqttConfigFactory& mqtt_config) {
  std::set<std::string> selected_ids;
  for (const Json& asset : inventory.select(host_id, asset_ids)) {
    selected_ids.insert(asset.at("id").get_ref<const std::string&>());
  }
  std::set<std::string> client_ids;
  std::set<std::string> topics;
  std::set<std::string> outputs;
  // The owned DDS endpoints and the owned odometry outputs, with the family
  // of the plugin that owns them.
  std::set<std::string> dds_connections;
  std::map<std::string, std::string> state_outputs;
  // Hosts whose sync board client already has an owner item.
  std::set<std::string> board_hosts;
  std::vector<Json> plan;

  for (const Json& asset : inventory.assets()) {
    if (!asset.at("enabled").get<bool>()) continue;
    const std::string asset_id = asset.at("id").get_ref<const std::string&>();
    const Json& options = asset.at("driver").at("options");
    const bool real = asset.at("mode").get_ref<const std::string&>() == "real";
    if (real) {
      // A real asset with a managed driver owns a DDS endpoint and an
      // odometry output. The registry knows how the plugin owns its
      // endpoint, so a further vendor plugin follows the same rules.
      const std::string plugin = asset.at("driver").at("plugin").get_ref<const std::string&>();
      const DriverMetadata* metadata = driver_metadata_for(plugin);
      if (metadata != nullptr) {
        const std::string connection = dds_connection_of(asset, options, *metadata);
        if (!dds_connections.insert(connection).second) {
          throw ConfigurationError("multiple " + std::string(metadata->family) +
                                   " assets own the same DDS connection: " + connection);
        }
        const std::string state_output =
            std::to_string(asset.at("ros_domain_id").get<int64_t>()) + "|" +
            option_of(options, "odom_topic");
        if (!state_outputs.emplace(state_output, metadata->family).second) {
          throw ConfigurationError("multiple " + std::string(metadata->family) +
                                   " assets own the same odometry output: " + state_output);
        }
      }
    }
    if (real && options.contains("cmd_vel_topic")) {
      const std::string output = std::to_string(asset.at("ros_domain_id").get<int64_t>()) + "|" +
                                  option_of(options, "cmd_vel_topic");
      if (!outputs.insert(output).second) {
        throw ConfigurationError("multiple assets own the same velocity output: " + output);
      }
    }
    Json item = Json::object();
    item["asset"] = asset;
    item["mqtt"] = Json();
    // The sync board is a per-host resource. Every selected real asset on that
    // host carries the normalized board block so its own cameras can derive a
    // trigger pairing process, but exactly one item owns the board client.
    item["board"] = Json();
    item["board_owner"] = false;
    // Only an asset that will actually be launched may claim the host board:
    // an unselected asset must not take ownership away from the selected one.
    if (real && selected_ids.count(asset_id) != 0) {
      const std::string& host = asset.at("host").get_ref<const std::string&>();
      const Json& host_config = inventory.hosts().at(host);
      const auto board = host_config.find("board");
      if (board != host_config.end()) {
        item["board"] = *board;
        item["board_owner"] = board_hosts.insert(host).second;
      }
    }
    const Json& transport = asset.at("transport");
    if (enable_mqtt && !transport.is_null()) {
      if (!mqtt_config) {
        throw ConfigurationError(
            "MQTT planning requires the chaos_mqtt adapter configuration generator");
      }
      const std::string profile_name = transport.get_ref<const std::string&>();
      const Json& profile = inventory.transports().at(profile_name);
      const Json config = mqtt_config(asset, profile);
      const Json& mqtt = config.at("mqtt");
      const std::string client_id = string_of(mqtt.at("client_id"), "MQTT client_id");
      if (!client_ids.insert(client_id).second) {
        throw ConfigurationError("duplicate MQTT client_id: " + client_id);
      }
      std::vector<std::string> used_topics;
      used_topics.push_back(string_of(mqtt.at("command_topic"), "MQTT command_topic"));
      used_topics.push_back(string_of(mqtt.at("response_topic"), "MQTT response_topic"));
      const auto event_topic = mqtt.find("event_topic");
      if (event_topic != mqtt.end() && is_truthy(*event_topic)) {
        used_topics.push_back(string_of(*event_topic, "MQTT event_topic"));
      }
      for (const Json& mapping : config.at("topic_mappings")) {
        used_topics.push_back(string_of(mapping.at("mqtt_topic"), "MQTT topic mapping"));
      }
      for (const std::string& topic : used_topics) {
        if (topic.empty() || topic.find('\0') != std::string::npos ||
            topic.find('+') != std::string::npos || topic.find('#') != std::string::npos) {
          throw ConfigurationError("MQTT requires nonempty exact topic names");
        }
        if (!topics.insert(topic).second) {
          throw ConfigurationError("duplicate MQTT topic: " + topic);
        }
      }
      std::string executable = string_of(profile.at("executable"), "transport executable");
      if (check_files && selected_ids.count(asset_id) != 0) {
        executable = resolve_mqtt_executable(executable);
        const auto host = mqtt.find("host");
        if (host == mqtt.end() || host->is_null() || host->get_ref<const std::string&>().empty()) {
          throw ConfigurationError("MQTT broker host is required");
        }
        for (const char* key : {"ca_file", "client_certificate_file", "client_key_file"}) {
          const auto value = mqtt.find(key);
          if (value == mqtt.end() || value->is_null() ||
              !std::filesystem::is_regular_file(value->get_ref<const std::string&>())) {
            throw ConfigurationError(std::string("MQTT ") + key + " file does not exist");
          }
        }
      }
      Json mqtt_item = Json::object();
      mqtt_item["executable"] = executable;
      mqtt_item["config"] = config;
      item["mqtt"] = mqtt_item;
    }
    if (selected_ids.count(asset_id) != 0) {
      plan.push_back(item);
    }
  }
  for (const auto& entry : state_outputs) {
    if (outputs.count(entry.first) != 0) {
      throw ConfigurationError(entry.second + " odometry output conflicts with a velocity output");
    }
  }
  return LaunchPlan::from_items(plan);
}

}  // namespace

LaunchPlan LaunchPlan::from_items(std::vector<Json> items) {
  LaunchPlan plan;
  plan.encoded_.reserve(items.size());
  for (const Json& item : items) {
    plan.encoded_.push_back(item.dump());
  }
  return plan;
}

Json LaunchPlan::at(std::size_t index) const {
  if (index >= encoded_.size()) {
    throw ConfigurationError("launch plan index out of range");
  }
  return Json::parse(encoded_[index]);
}

std::vector<Json> LaunchPlan::items() const {
  std::vector<Json> result;
  result.reserve(encoded_.size());
  for (const std::string& encoded : encoded_) {
    result.push_back(Json::parse(encoded));
  }
  return result;
}

std::string LaunchPlan::to_json() const {
  return "[" + [&] {
    std::string joined;
    for (size_t index = 0; index < encoded_.size(); ++index) {
      if (index) joined += ", ";
      joined += encoded_[index];
    }
    return joined;
  }() + "]";
}

LaunchPlan prepare_plan(const PlanRequest& request, const MqttConfigFactory& mqtt_config) {
  const Inventory inventory =
      chaos::assets::load_inventory(request.inventory_path, request.mode);
  return plan_inventory(inventory, request.host_id, request.asset_ids, request.enable_mqtt,
                        request.check_files, mqtt_config);
}

LaunchPlan prepare_edge_plan(const std::string& inventory_path, const std::string& asset_id,
                             bool enable_mqtt, bool check_files,
                             const MqttConfigFactory& mqtt_config) {
  if (!std::filesystem::path(inventory_path).is_absolute()) {
    throw ConfigurationError("edge inventory must be an absolute path");
  }
  const Inventory inventory = chaos::assets::load_inventory(inventory_path);
  std::vector<Json> enabled;
  for (const Json& asset : inventory.assets()) {
    if (asset.at("enabled").get<bool>()) enabled.push_back(asset);
  }
  if (inventory.hosts().size() != 1 || enabled.size() != 1) {
    throw ConfigurationError("edge deployment requires one host and exactly one enabled asset");
  }
  const Json& asset = enabled.front();
  const std::string configured = asset.at("id").get_ref<const std::string&>();
  if (asset_id.empty() || configured != asset_id) {
    throw ConfigurationError("edge asset_id must match the configured asset: " + configured);
  }
  if (asset.at("mode").get_ref<const std::string&>() != "real") {
    throw ConfigurationError(
        "edge deployment requires an explicitly configured real backend; use system.launch.py for mock");
  }
  if (enable_mqtt && asset.at("transport").is_null()) {
    throw ConfigurationError(
        "edge MQTT is enabled but this asset has no transport profile; configure transport or "
        "explicitly disable MQTT");
  }
  return plan_inventory(inventory, asset.at("host").get_ref<const std::string&>(), {asset_id},
                        enable_mqtt, check_files, mqtt_config);
}

std::string resolve_mqtt_executable(const std::string& executable,
                                    const PackagePrefixResolver& prefix_resolver) {
  if (executable == "mqtt_transport" || executable == "mqtt_ros2_adapter") {
    return resolve_installed_executable(
        "chaos_mqtt", "mqtt_transport", prefix_resolver,
        "Unified chaos_mqtt package not found; build the workspace with colcon and source its "
        "install/local_setup.bash",
        [](const std::filesystem::path& path) {
          return "Installed MQTT executable not found or not executable: " + path.string();
        });
  }
  const std::string found = which(executable);
  if (found.empty()) {
    throw ConfigurationError("MQTT executable not found or not executable: " + executable);
  }
  return std::filesystem::absolute(found).string();
}

}  // namespace chaos::bringup
