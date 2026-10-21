#pragma once

#include <string>

namespace chaos::assets {

// The resolved inventory in the stable command line format.
//
// The native ``asset_config`` executable and the Python compatibility
// shim share this function, so both produce byte identical output.
std::string resolve_inventory_json(const std::string& path, const std::string& mode,
                                   const std::string& host);

}  // namespace chaos::assets
