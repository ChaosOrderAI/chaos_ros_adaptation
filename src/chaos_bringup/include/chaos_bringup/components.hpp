#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "chaos_assets/text.hpp"
#include "chaos_bringup/planner.hpp"

namespace chaos::bringup {

// One managed child process of a launch.
//
// The native planner derives the complete specification of every process;
// the launch glue only turns a specification into a ROS action. This keeps
// the component derivation - which fields go to which node, in which
// namespace, with which parameters - inside the tested native code.
struct ProcessSpec {
  std::string label;      // the label used by the shared exit policy
  std::string package;    // the ROS package that owns the executable
  std::string executable;  // the executable inside that package
  std::string name;       // the node name
  std::string node_namespace;  // the node namespace
  // The command line arguments and the parameter payload of the process.
  //
  // Both are always arrays, also for a process without arguments or
  // parameters: the launch glue iterates them without a special case, and a
  // null document would fail there.
  Json arguments = Json::array();
  Json parameters = Json::array();
  std::vector<std::pair<std::string, std::string>> remappings;
  bool transport = false;  // true for a transport process without a ROS node
  Json command = Json::array();  // the argument vector of a transport process
};

// The capabilities of the build, injected by the caller.
//
// The planner never probes a vendor SDK itself. The runtime owns the
// detection of its native backends, and the data collector owns the list of
// registered storage plugins; both are passed in.
struct PreflightOptions {
  // The registered rosbag2 storage writers of this build.
  std::vector<std::string> registered_writers;
  // Whether the runtime provides the native backend of a driver plugin.
  std::function<bool(const std::string& plugin)> backend_available;
  // Whether a ROS executable is available.
  //
  // The default resolves the executable in the sourced ROS installation. An
  // injected resolver keeps the preflight testable without a complete ROS
  // installation, and lets a deployment use a different search path.
  std::function<bool(const std::string& package, const std::string& executable)>
      executable_available;
  // The validation of an asset by the runtime.
  //
  // The runtime owns the rules for the plugin, the capabilities and the
  // watchdog of an asset. The validator is called for every selected asset
  // before a single process action is created, so an asset that the runtime
  // rejects never reaches a process.
  std::function<void(const Json& asset)> validate_asset;
  // Whether the files of the plan are checked on disk.
  bool check_files = true;
};

// The asset without the expanded robot description.
//
// Copying megabytes of URDF into the parameter payload of unrelated nodes is
// wasteful and leaks the description into unrelated logs; the publisher and
// the private snapshot keep the full document.
Json component_asset(const Json& asset);

// The sensor summary of one asset. Driver parameters are omitted because
// device URLs can contain credentials.
Json sensor_summary(const Json& asset);

// The robot description summary of one asset. The expanded XML stays out of
// the dry run log and of its potentially large output.
Json description_summary(const Json& asset);

// The recording policy summary of one asset.
Json collection_summary(const Json& asset);

// The dry run summary of a plan.
Json plan_summary(const LaunchPlan& plan);

// Check every selected component before a single process action is created.
//
// Throws ConfigurationError for an unavailable executable, for an
// unavailable storage plugin, for an unwritable output directory and for a
// driver plugin whose native backend is not part of this build.
void preflight(const LaunchPlan& plan, const PreflightOptions& options);

// Copy the explicit sensor parameter files into the launch directory.
//
// The copy closes the gap between the preflight and the startup of the
// child: the driver reads the checked snapshot even when a deployment
// replaces the original file. The result maps "<asset>/<sensor>" to the list
// of copied files.
Json snapshot_parameter_files(const LaunchPlan& plan, const std::string& root);

// Validate a ROS parameter file of a sensor driver.
//
// The file must contain a nonempty ROS parameter mapping under a node
// selector that matches the driver node, and every parameter must be a
// finite ROS scalar.
void validate_parameter_file(const std::string& content, const std::string& node_name,
                             const std::string& source);

// Resolve a ROS executable without importing or invoking a vendor driver.
void require_executable(const std::string& package, const std::string& executable);

// Check the nearest existing ancestor of an output directory without
// creating any filesystem entry.
void check_output_directory(const std::string& path);

// The managed processes of one asset.
std::vector<ProcessSpec> driver_processes(const Json& asset);
std::vector<ProcessSpec> description_processes(const Json& asset);
std::vector<ProcessSpec> sensor_processes(const Json& asset, const Json& parameter_files);
std::vector<ProcessSpec> collection_processes(const Json& asset);
// The sync board client and the trigger pairing processes of one plan item.
//
// Takes the whole plan item (asset, board, board_owner). The board client is
// derived only for the item that owns the host board; every enabled camera
// whose time_sync.source is board_trigger gets its own pairing process.
// Mock assets derive neither.
std::vector<ProcessSpec> time_sync_processes(const Json& item);

}  // namespace chaos::bringup
