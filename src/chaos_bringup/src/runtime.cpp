#include "chaos_bringup/runtime.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include "chaos_assets/errors.hpp"
#include "chaos_assets/process.hpp"
#include "chaos_bringup/driver_registry.hpp"
#include "installed_executable.hpp"

namespace chaos::bringup {

namespace {

using chaos::assets::ConfigurationError;

// Resolve a ROS executable of a package.
//
// The executable is looked up in the sourced ROS installation, so the same
// build that provides the node provides the interface.
std::string resolve_executable(const std::string& package, const std::string& executable) {
  return resolve_installed_executable(
      package, executable, {},
      "the runtime package '" + package + "' is unavailable; build and source its ROS workspace",
      [&](const std::filesystem::path&) {
        return "the runtime executable is unavailable: " + package + "/" + executable;
      });
}

// The executable of one interface.
//
// An explicit path is used unchanged, so a deployment can point at another
// build of the runtime. Otherwise the executable is resolved in the sourced
// ROS installation.
std::string interface_executable(const std::string& explicit_path, const std::string& package,
                                  const std::string& executable) {
  if (!explicit_path.empty()) return explicit_path;
  return resolve_executable(package, executable);
}

}  // namespace

std::string capability_of_plugin(const std::string& plugin) {
  // The report of the runtime uses the capability key of the plugin.
  const DriverMetadata* metadata = driver_metadata_for(plugin);
  if (metadata != nullptr) return metadata->capability_key;
  return plugin;
}

void validate_asset_with_runtime(const Json& asset, const RuntimeInterface& interface) {
  const std::string executable = interface_executable(interface.validate_path,
                                                       interface.validate_package,
                                                       interface.validate_executable);
  std::vector<std::string> command{executable, interface.validate_argument};
  const std::string payload = asset.dump();
  const chaos::assets::ProcessResult result =
      chaos::assets::run_bounded(command, "the runtime asset validation", interface.timeout,
                                 interface.output_limit, payload);
  if (result.exit_code != 0) {
    std::string detail = result.err;
    if (detail.empty()) detail = result.out;
    while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r')) detail.pop_back();
    throw ConfigurationError("the runtime rejected the asset: " +
                             (detail.empty() ? std::string("exit code ") +
                                                   std::to_string(result.exit_code)
                                              : detail));
  }
  // The protocol of the runtime is a JSON object with the field "valid": an
  // exit code alone does not validate the asset.
  const Json answer = Json::parse(result.out, nullptr, false);
  if (!answer.is_object()) {
    throw ConfigurationError(
        "the runtime did not answer with a JSON object; the asset is not validated: " +
        result.out);
  }
  const auto valid = answer.find("valid");
  if (valid == answer.end() || !valid->is_boolean()) {
    throw ConfigurationError(
        "the runtime did not answer with the field \"valid\"; the asset is not validated: " +
        result.out);
  }
  if (!valid->get<bool>()) {
    std::string detail = answer.value("reason", std::string());
    throw ConfigurationError("the runtime rejected the asset: " +
                             (detail.empty() ? std::string("the asset is not valid") : detail));
  }
}

bool native_backend_available(const std::string& plugin, const RuntimeInterface& interface) {
  const DriverMetadata* metadata = driver_metadata_for(plugin);
  std::string executable;
  try {
    // An explicit path replaces the lookup in the package index, also for a
    // plugin with its own reporting executable. A plugin reports its backend
    // with its own executable while the interface keeps its default; a custom
    // executable of the interface wins over the one of the plugin.
    if (!interface.capabilities_path.empty()) {
      executable = interface.capabilities_path;
    } else {
      const bool own_executable =
          metadata != nullptr &&
          interface.capabilities_executable == RuntimeInterface::kDefaultCapabilitiesExecutable;
      executable = resolve_executable(interface.capabilities_package,
                                      own_executable ? metadata->capabilities_executable
                                                     : interface.capabilities_executable);
    }
  } catch (const ConfigurationError&) {
    // A runtime that cannot be reached denies the backend, so a real asset
    // never starts without its driver.
    return false;
  }
  std::vector<std::string> command{executable, interface.capabilities_argument};
  const chaos::assets::ProcessResult result =
      chaos::assets::run_bounded(command, "the runtime capability report", interface.timeout,
                                 interface.output_limit);
  if (result.exit_code != 0) return false;
  const Json report = Json::parse(result.out, nullptr, false);
  if (!report.is_object()) return false;
  const auto found = report.find(capability_of_plugin(plugin));
  if (found == report.end() || !found->is_boolean()) return false;
  return found->get<bool>();
}

}  // namespace chaos::bringup
