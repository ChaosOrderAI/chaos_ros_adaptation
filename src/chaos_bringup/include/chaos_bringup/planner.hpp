#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace chaos::bringup {

using Json = nlohmann::ordered_json;

// Produces the MQTT adapter configuration of one asset.
//
// The adapter configuration format belongs to chaos_mqtt; the planner only
// consumes it to validate the MQTT identity of an asset. Injecting the
// producer keeps this package independent of the transport implementation.
using MqttConfigFactory = std::function<Json(const Json& asset, const Json& transport_profile)>;

// An immutable startup snapshot.
//
// The plan keeps the encoded form of every item, so a caller can never
// modify a checked plan through a nested document. The planner never
// reloads an input file.
class LaunchPlan {
 public:
  // Build a plan from the resolved items.
  static LaunchPlan from_items(std::vector<Json> items);

  std::size_t size() const { return encoded_.size(); }

  // One detached item.
  Json at(std::size_t index) const;

  // Every detached item.
  std::vector<Json> items() const;

  // The plan as a JSON array.
  std::string to_json() const;

 private:
  std::vector<std::string> encoded_;
};

// The input of a launch plan.
struct PlanRequest {
  std::string inventory_path;
  std::string host_id;
  std::string mode;  // empty uses the configured backend
  std::vector<std::string> asset_ids;
  bool enable_mqtt = false;
  bool check_files = true;
};

// Validate the selected assets and freeze the startup snapshot.
//
// Throws ConfigurationError for an unknown host, for an unavailable asset,
// for a collision of the velocity output, of the Go2 DDS connection or of
// the Go2 odometry output, and for a duplicate MQTT client id or topic.
LaunchPlan prepare_plan(const PlanRequest& request, const MqttConfigFactory& mqtt_config);

// Onboard deployment is bound to one real asset on one host.
LaunchPlan prepare_edge_plan(const std::string& inventory_path, const std::string& asset_id,
                             bool enable_mqtt, bool check_files,
                             const MqttConfigFactory& mqtt_config);

// Resolves the install prefix of a ROS package.
//
// The default resolves the package through the sourced ROS installation. An
// injected resolver keeps the resolution testable and lets a deployment use
// a different package index.
using PackagePrefixResolver = std::function<std::string(const std::string& package)>;

// Resolve the MQTT transport executable of a transport profile.
//
// The former default names are resolved through the sourced ROS
// installation; any other name is looked up on PATH.
std::string resolve_mqtt_executable(const std::string& executable,
                                    const PackagePrefixResolver& prefix_resolver = {});

}  // namespace chaos::bringup
