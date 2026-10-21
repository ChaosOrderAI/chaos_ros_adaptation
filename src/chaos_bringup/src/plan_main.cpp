// Native launch planner command line entry point.
//
// The command keeps the planner contract of the Python implementation:
//   chaos_bringup_plan --inventory PATH --host HOST [--mode {mock,real}]
//                       [--assets a,b] [--enable-mqtt] [--dry-run]
//   chaos_bringup_plan --edge --inventory PATH --asset-id ID [--enable-mqtt]
//
// The MQTT adapter configuration is produced by chaos_mqtt. When that
// package is not available at build time, the adapter configurations can be
// supplied explicitly with --mqtt-adapter-config.

#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "chaos_assets/errors.hpp"
#include "chaos_bringup/components.hpp"
#include "chaos_bringup/planner.hpp"

namespace {

void usage() {
  std::cerr << "usage: chaos_bringup_plan --inventory PATH --host HOST "
               "[--mode {mock,real}] [--assets a,b] [--enable-mqtt] [--dry-run] [--summary]\n"
               "       chaos_bringup_plan --edge --inventory PATH --asset-id ID [--enable-mqtt]\n";
}

chaos::bringup::Json read_json(const std::string& path) {
  std::ifstream stream(path);
  if (!stream) {
    throw chaos::assets::ConfigurationError("cannot read " + path);
  }
  chaos::bringup::Json value;
  stream >> value;
  return value;
}

}  // namespace

int main(int argc, char** argv) {
  std::string inventory;
  std::string host;
  std::string mode;
  std::string assets;
  std::string asset_id;
  std::string mqtt_adapter_config;
  bool enable_mqtt = false;
  bool dry_run = false;
  bool edge = false;
  bool summary_only = false;

  const std::vector<std::string> arguments(argv + 1, argv + argc);
  for (size_t index = 0; index < arguments.size(); ++index) {
    const std::string& argument = arguments[index];
    auto value_of = [&](const std::string& name) -> std::string {
      if (index + 1 >= arguments.size()) {
        std::cerr << "chaos_bringup_plan: " << name << " requires a value\n";
        std::exit(2);
      }
      return arguments[++index];
    };
    if (argument == "--inventory") {
      inventory = value_of("--inventory");
    } else if (argument == "--host") {
      host = value_of("--host");
    } else if (argument == "--mode") {
      mode = value_of("--mode");
    } else if (argument == "--assets") {
      assets = value_of("--assets");
    } else if (argument == "--asset-id") {
      asset_id = value_of("--asset-id");
    } else if (argument == "--mqtt-adapter-config") {
      mqtt_adapter_config = value_of("--mqtt-adapter-config");
    } else if (argument == "--enable-mqtt") {
      enable_mqtt = true;
    } else if (argument == "--dry-run") {
      dry_run = true;
    } else if (argument == "--summary") {
      summary_only = true;
    } else if (argument == "--edge") {
      edge = true;
    } else if (argument == "--help" || argument == "-h") {
      usage();
      return 0;
    } else {
      std::cerr << "chaos_bringup_plan: unknown option " << argument << "\n";
      usage();
      return 2;
    }
  }
  if (inventory.empty()) {
    usage();
    return 2;
  }
  if (!edge && host.empty()) {
    std::cerr << "chaos_bringup_plan: --host is required\n";
    return 2;
  }
  if (edge && asset_id.empty()) {
    std::cerr << "chaos_bringup_plan: --asset-id is required for an edge plan\n";
    return 2;
  }
  if (!mode.empty() && mode != "mock" && mode != "real") {
    std::cerr << "chaos_bringup_plan: --mode must be mock or real\n";
    return 2;
  }

  try {
    // The adapter configurations are supplied explicitly; the transport
    // implementation owns their content.
    std::map<std::string, chaos::bringup::Json> supplied;
    chaos::bringup::MqttConfigFactory factory;
    if (!mqtt_adapter_config.empty()) {
      const chaos::bringup::Json document = read_json(mqtt_adapter_config);
      if (!document.is_object()) {
        throw chaos::assets::ConfigurationError(
            "the MQTT adapter configuration must be an object keyed by asset id");
      }
      for (auto it = document.begin(); it != document.end(); ++it) {
        supplied[it.key()] = it.value();
      }
      factory = [&supplied](const chaos::bringup::Json& asset,
                           const chaos::bringup::Json&) -> chaos::bringup::Json {
        const std::string id = asset.at("id").get_ref<const std::string&>();
        const auto found = supplied.find(id);
        if (found == supplied.end()) {
          throw chaos::assets::ConfigurationError(
              "no MQTT adapter configuration supplied for asset " + id);
        }
        return found->second;
      };
    }

    chaos::bringup::LaunchPlan plan;
    if (edge) {
      plan = chaos::bringup::prepare_edge_plan(inventory, asset_id, enable_mqtt, !dry_run, factory);
    } else {
      chaos::bringup::PlanRequest request;
      request.inventory_path = inventory;
      request.host_id = host;
      request.mode = mode;
      request.enable_mqtt = enable_mqtt;
      request.check_files = !dry_run;
      size_t start = 0;
      while (start <= assets.size()) {
        const size_t end = assets.find(',', start);
        const std::string value =
            assets.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!value.empty()) request.asset_ids.push_back(value);
        if (end == std::string::npos) break;
        start = end + 1;
      }
      plan = chaos::bringup::prepare_plan(request, factory);
    }
    if (summary_only) {
      std::cout << chaos::bringup::plan_summary(plan).dump(2) << "\n";
      return 0;
    }
    std::cout << plan.to_json() << "\n";
  } catch (const chaos::assets::ConfigurationError& error) {
    std::cerr << "configuration error: " << error.what() << "\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "configuration error: " << error.what() << "\n";
    return 2;
  }
  return 0;
}
