// ROS-free preflight for the generic humanoid plugin and its currently
// supported backends. Both --validate-asset and runtime construction call it.
#pragma once

#include "chaos_runtime/asset_config.hpp"

namespace chaos::runtime {

// Validates kind/mode, every option value and real-only capabilities. Option
// presence and unknown names are checked by the shared plugin registry.
void validateHumanoidOptions(const AssetConfig & asset);

}  // namespace chaos::runtime
