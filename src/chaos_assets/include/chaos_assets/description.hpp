#pragma once

#include <string>
#include <vector>

#include "chaos_assets/text.hpp"

namespace chaos::assets {

// Freeze one asset's URDF/Xacro description.
//
// Returns null when neither the model nor the instance declares a
// description, and {"enabled": false} when the merged declaration disables
// it. The returned snapshot contains the validated URDF structure, the
// normalized XML and the digests of both the source and the expanded
// document.
Json resolve_description(const Json& model_description, const Json& instance_description,
                         const std::string& model_directory, const std::string& inventory_directory,
                         const Json& asset);

// Combine robot description joints and sensor mounting transforms inside
// each DDS domain. Throws ConfigurationError on a frame or transform
// collision.
void validate_description_graph(const std::vector<Json>& assets);

}  // namespace chaos::assets
