#pragma once

// Native driver launch, build capability and DDS ownership metadata.

#include <array>
#include <string>

namespace chaos::bringup {

// Ownership keys used by the launch planner.
enum class DdsOwnership {
  kHostEndpoint,    // host, interface and vendor DDS domain
  kDomainEndpoint,  // entire vendor DDS domain
};

struct DriverMetadata {
  const char* plugin;
  const char* family;
  const char* capability_key;
  const char* capabilities_executable;
  const char* driver_executable;
  const char* driver_name;
  const char* label;
  DdsOwnership dds_ownership;
};

// Go2 retains host-scoped planning; network isolation is configured separately.
// Humanoid reserves the domain because its vendor topics have no device suffix.
inline const std::array<DriverMetadata, 2>& driver_registry() {
  static constexpr std::array<DriverMetadata, 2> registry = {{
      DriverMetadata{"unitree_go2", "Go2", "unitree_sdk2", "go2_driver", "go2_driver",
                     "go2_driver", "Go2 driver", DdsOwnership::kHostEndpoint},
      DriverMetadata{"humanoid", "humanoid", "booster_sdk", "humanoid_driver", "humanoid_driver",
                     "humanoid_driver", "Humanoid driver", DdsOwnership::kDomainEndpoint},
  }};
  return registry;
}

// The metadata of a plugin, or null when the plugin has no managed driver.
inline const DriverMetadata* driver_metadata_for(const std::string& plugin) {
  for (const DriverMetadata& entry : driver_registry()) {
    if (plugin == entry.plugin) return &entry;
  }
  return nullptr;
}

}  // namespace chaos::bringup
