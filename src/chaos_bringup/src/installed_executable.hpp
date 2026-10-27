#pragma once

// Shared lookup of an installed ROS executable. Callers keep their own error
// text: tests and operators match those strings, and the MQTT path includes
// the resolved file while the others name package/executable.
#include <unistd.h>

#include <filesystem>
#include <functional>
#include <string>

#include <ament_index_cpp/get_package_prefix.hpp>

#include "chaos_assets/errors.hpp"

namespace chaos::bringup {

inline std::string resolve_installed_executable(
    const std::string & package, const std::string & executable,
    const std::function<std::string(const std::string &)> & prefix_resolver,
    const std::string & missing_package,
    const std::function<std::string(const std::filesystem::path &)> & missing_file) {
  std::string prefix;
  try {
    prefix = prefix_resolver ? prefix_resolver(package)
                             : ament_index_cpp::get_package_prefix(package);
  } catch (const std::exception &) {
    throw chaos::assets::ConfigurationError(missing_package);
  }
  const std::filesystem::path path =
      std::filesystem::path(prefix) / "lib" / package / executable;
  if (!std::filesystem::is_regular_file(path) || ::access(path.c_str(), X_OK) != 0) {
    throw chaos::assets::ConfigurationError(missing_file(path));
  }
  return std::filesystem::absolute(path).string();
}

}  // namespace chaos::bringup
