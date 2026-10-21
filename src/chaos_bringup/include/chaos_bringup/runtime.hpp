#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "chaos_assets/text.hpp"
#include "chaos_bringup/planner.hpp"

namespace chaos::bringup {

// The no-hardware interface of the runtime.
//
// The runtime owns the validation of an asset and the detection of its native
// backends. The planner never probes a vendor SDK itself; it calls these two
// interfaces of the runtime, which answer before any ROS node or hardware
// channel is opened:
//
//   robot_runtime --validate-asset     reads a bounded asset document from
//                                      standard input and answers
//                                      {"valid": true} on standard output,
//   go2_driver --build-capabilities    answers {"unitree_sdk2": true|false}.
//
// Both interfaces are executed under a deadline and with a bounded output, so
// a stuck or chatty runtime cannot block the startup.
struct RuntimeInterface {
  // The package and the executable of the asset validation.
  std::string validate_package = "chaos_runtime";
  std::string validate_executable = "robot_runtime";
  std::string validate_argument = "--validate-asset";
  // The package and the executable of the capability report.
  std::string capabilities_package = "chaos_runtime";
  std::string capabilities_executable = "go2_driver";
  std::string capabilities_argument = "--build-capabilities";
  // An explicit path of the interface, which replaces the lookup in the
  // package index.
  //
  // The default resolves the executable in the sourced ROS installation, so
  // the same build that provides the node provides the interface. An
  // explicit path keeps the interface testable and lets a deployment use an
  // interface of another build.
  std::string validate_path;
  std::string capabilities_path;
  // The deadline of a single call.
  std::chrono::milliseconds timeout{5000};
  // The largest output of a single call.
  std::size_t output_limit{64u * 1024u};
};

// The capability name of a driver plugin.
//
// The report of the runtime uses the name of the capability, which differs
// from the plugin name for the Go2 driver.
std::string capability_of_plugin(const std::string& plugin);

// Validate one asset with the runtime.
//
// The asset document is written to the standard input of the runtime, so the
// runtime applies its own rules for the plugin, the capabilities and the
// watchdog of the asset. The asset of the caller is not modified.
//
// Throws ConfigurationError when the runtime rejects the asset, when the
// runtime cannot be reached or when the call fails.
void validate_asset_with_runtime(const Json& asset, const RuntimeInterface& interface);

// Whether the runtime provides the native backend of a driver plugin.
//
// The runtime reports which backends its build contains. A runtime that
// cannot be reached denies the backend, so a real asset fails loudly instead
// of starting without its driver.
bool native_backend_available(const std::string& plugin, const RuntimeInterface& interface);

}  // namespace chaos::bringup
