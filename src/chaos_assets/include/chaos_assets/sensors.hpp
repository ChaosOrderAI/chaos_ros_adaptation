#pragma once

#include <string>
#include <vector>

#include "chaos_assets/text.hpp"

namespace chaos::assets {

// Resolve sensor declarations for one asset without importing ROS or opening
// hardware. Model and instance declarations are validated independently,
// merged, and normalized; mock assets always force mock drivers.
Json resolve_sensors(const Json& model_sensors, const Json& instance_sensors,
                     const std::string& model_directory, const std::string& inventory_directory,
                     const Json& asset);

// Cross-asset collision detection per DDS domain: reserved frames, duplicate
// frames/topics, and sensor transform cycles.
void validate_sensor_graph(const std::vector<Json>& assets);

// Relative ROS topic resolution against an asset namespace.
std::string resolve_topic(const Json& value, const std::string& namespace_, const std::string& where);

extern const std::regex kFrameRe;

}  // namespace chaos::assets
