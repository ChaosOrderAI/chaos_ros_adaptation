#pragma once

#include <string>

#include "chaos_assets/text.hpp"

namespace chaos::assets {

// Resolve the local recording policy for one asset without starting clocks,
// ROS or file writers. Returns null when neither model nor instance declares
// a policy.
Json resolve_collection(const Json& model, const Json& instance, const std::string& model_dir,
                        const std::string& instance_dir, const Json& asset);

}  // namespace chaos::assets
