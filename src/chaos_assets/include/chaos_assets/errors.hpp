#pragma once

#include <stdexcept>
#include <string>

namespace chaos::assets {

// Configuration/rejection error, mirroring the original Python
// ``chaos_assets.ConfigurationError`` (a ValueError subclass).
class ConfigurationError : public std::runtime_error {
public:
  explicit ConfigurationError(const std::string& message) : std::runtime_error(message) {}
};

}  // namespace chaos::assets
