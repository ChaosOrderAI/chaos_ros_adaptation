// ROS-free implementation of the shared asset rules. See
// asset_validation.hpp for the contract.
#include "chaos_runtime/asset_validation.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <vector>

namespace chaos::runtime {

namespace {

// The authoritative plugin table. It is intentionally data, not code: the
// preflight CLI, the runtime startup path and the ROS adapter factory all
// read the same rows.
const std::map<std::string, PluginSpec> & registry() {
  static const std::map<std::string, PluginSpec> table = [] {
    std::map<std::string, PluginSpec> rows;
    {
      // The mock backend synthesises telemetry. It supports every
      // capability, but a ground kind must not claim the flight actions.
      PluginSpec spec;
      spec.plugin = "mock";
      spec.option_names = {"kind"};
      spec.supported_capabilities = {
        "state", "motion.velocity", "motion.stop", "navigation.pose",
        "flight.takeoff", "flight.land"};
      spec.flight_only_for_air = true;
      rows.emplace(spec.plugin, spec);
    }
    {
      // The Lingao ROS2 bridge: odometry in, velocity out.
      PluginSpec spec;
      spec.plugin = "lingao_ros2";
      spec.option_names = {"odom_topic", "cmd_vel_topic"};
      spec.supported_capabilities = {"state", "motion.velocity",
                                     "motion.stop"};
      rows.emplace(spec.plugin, spec);
    }
    {
      // The Unitree Go2 driver: every option is explicit, because the
      // driver process must not guess a DDS domain or a network interface.
      PluginSpec spec;
      spec.plugin = "unitree_go2";
      spec.option_names = {"odom_topic", "cmd_vel_topic", "network_interface",
                           "dds_domain_id", "sport_state_topic",
                           "velocity_frame"};
      spec.required_options = spec.option_names;
      spec.supported_capabilities = {"state", "motion.velocity",
                                     "motion.stop"};
      rows.emplace(spec.plugin, spec);
    }
    {
      // The legacy DJI bridge is read-only: it exposes the state only.
      PluginSpec spec;
      spec.plugin = "dji_psdk_legacy";
      spec.option_names = {"status_topic"};
      spec.supported_capabilities = {"state"};
      rows.emplace(spec.plugin, spec);
    }
    return rows;
  }();
  return table;
}

std::string joinNames(const std::set<std::string> & names) {
  std::ostringstream out;
  bool first = true;
  for (const auto & name : names) {
    if (!first) {
      out << ", ";
    }
    out << name;
    first = false;
  }
  return out.str();
}

}  // namespace

const PluginSpec & pluginSpec(const std::string & plugin) {
  const auto & table = registry();
  const auto found = table.find(plugin);
  if (found == table.end()) {
    throw AssetConfigError("Unknown adapter plugin " + plugin +
                           " (known plugins: " + joinNames(
                             [&] {
                               std::set<std::string> names;
                               for (const auto & entry : table) {
                                 names.insert(entry.first);
                               }
                               return names;
                             }()) +
                           ")");
  }
  return found->second;
}

std::set<std::string> capabilitiesFor(const PluginSpec & spec,
                                       const AssetConfig & asset) {
  std::set<std::string> capabilities = spec.supported_capabilities;
  if (spec.flight_only_for_air) {
    const std::string kind = asset.options.count("kind") != 0
      ? asset.options.at("kind").get<std::string>()
      : asset.kind;
    if (kind == "ugv" || kind == "quadruped") {
      capabilities.erase("flight.takeoff");
      capabilities.erase("flight.land");
    }
  }
  return capabilities;
}

void validateAssetConfig(const AssetConfig & asset) {
  const PluginSpec & spec = pluginSpec(asset.plugin);

  // Options: only the known names, and the required ones must be present.
  // `asset.options` is a JSON object: iterate its members by key.
  std::set<std::string> unknown;
  for (auto it = asset.options.begin(); it != asset.options.end(); ++it) {
    if (spec.option_names.count(it.key()) == 0) {
      unknown.insert(it.key());
    }
  }
  if (!unknown.empty()) {
    throw AssetConfigError("Unsupported " + asset.plugin + " options: " +
                           joinNames(unknown));
  }
  std::set<std::string> missing;
  for (const auto & option : spec.required_options) {
    if (asset.options.count(option) == 0) {
      missing.insert(option);
    }
  }
  if (!missing.empty()) {
    throw AssetConfigError(asset.plugin +
                           " requires explicit options: " + joinNames(missing));
  }

  // Capabilities: the asset may only claim what the plugin implements.
  const std::set<std::string> supported = capabilitiesFor(spec, asset);
  std::set<std::string> unsupported;
  for (const auto & capability : asset.capabilities) {
    if (supported.count(capability) == 0) {
      unsupported.insert(capability);
    }
  }
  if (!unsupported.empty()) {
    throw AssetConfigError("Adapter " + asset.plugin +
                           " does not support " + joinNames(unsupported));
  }
}

bool validateAssetJson(const std::string & json, std::string & error) {
  try {
    const AssetConfig asset = parseAssetJson(json);
    validateAssetConfig(asset);
    return true;
  } catch (const std::exception & failure) {
    error = failure.what();
    return false;
  }
}

bool isAbsoluteTopic(const std::string & topic) {
  if (topic.size() < 2 || topic.front() != '/') {
    return false;
  }
  // The leading '/' has already been consumed, so the next character starts
  // a name: start with false, otherwise every absolute topic would be
  // rejected as a repeated slash.
  bool start = false;
  for (char ch : topic) {
    if (ch == '/') {
      if (start) {
        return false;  // repeated slash
      }
      start = true;
      continue;
    }
    const bool ok = start ? (ch == '_' || (ch >= 'A' && ch <= 'Z') ||
                             (ch >= 'a' && ch <= 'z'))
                          : (ch == '_' || (ch >= 'A' && ch <= 'Z') ||
                             (ch >= 'a' && ch <= 'z') ||
                             (ch >= '0' && ch <= '9'));
    if (!ok) {
      return false;
    }
    start = false;
  }
  return !start;
}

}  // namespace chaos::runtime
