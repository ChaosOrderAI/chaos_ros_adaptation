#pragma once

#include <string>

#include "chaos_assets/text.hpp"

namespace chaos::assets {

// Resolve a configured path against its declaring file's directory.
// ``package://name/relative`` URIs resolve through the ament resource index
// (requires a sourced ROS workspace). Result is a normalized absolute path.
std::string resolve_path(const Json& value, const std::string& base, const std::string& where);

}  // namespace chaos::assets
