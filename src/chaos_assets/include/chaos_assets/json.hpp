#pragma once

#include <cstdint>
#include <string>

#include "chaos_assets/pyfmt.hpp"
#include "chaos_assets/text.hpp"

namespace chaos::assets {

// Canonical serialization used for config_digest. Keys are sorted, items and
// key/value pairs use Python's default ", "/": " separators, floats use the
// shortest round-trip representation, and non-finite numbers are rejected so
// a digest can never silently depend on platform NaN spellings.
std::string canonical_json(const Json& value);

// sha256 hex digest of the canonical JSON encoding.
std::string config_digest(const Json& value);

}  // namespace chaos::assets
