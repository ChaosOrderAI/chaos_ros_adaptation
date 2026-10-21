#pragma once

#include <string>

#include "chaos_assets/text.hpp"

namespace chaos::assets {

// Parse a YAML document into the resolved Json tree.
//
// The loader enforces the same external contract as the original
// PyYAML-based pipeline: plain scalars resolve with YAML 1.1/PyYAML
// implicit typing (bools, nulls, decimal/hex/octal/binary/sexagesimal
// integers, floats including .inf/.nan), quoted scalars always stay
// strings, duplicate mapping keys are rejected, nesting depth is bounded,
// and integer scalars must fit signed 64 bits (anything larger is rejected
// at the boundary with a clear error).
Json load_yaml_string(const std::string& content, const std::string& where);

// Read a file (UTF-8, at most 1 MiB) and parse it as YAML.
Json load_yaml_file(const std::string& path);

}  // namespace chaos::assets
