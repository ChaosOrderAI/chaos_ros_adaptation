#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <regex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "chaos_assets/errors.hpp"
#include "chaos_assets/pyfmt.hpp"

namespace chaos::assets {

// The resolved configuration tree uses an insertion-ordered JSON document so
// CLI output and digest input have stable, inspectable field order.
using Json = nlohmann::ordered_json;

// Safely read a JSON integer as signed 64 bit. nlohmann also stores
// unsigned values; anything outside signed range is rejected instead of
// silently narrowing.
inline bool int64_value(const Json& value, int64_t* out) {
  if (value.is_number_unsigned()) {
    const uint64_t raw = value.get<uint64_t>();
    if (raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) return false;
    *out = static_cast<int64_t>(raw);
    return true;
  }
  if (value.is_number_integer()) {
    *out = value.get<int64_t>();
    return true;
  }
  return false;
}

// Shared validators. Error categories and identifying text stay equivalent
// to the original implementation; wording may differ slightly where the
// original embedded Python-specific reprs.
//
// The checked value is returned by value: a caller can pass a temporary
// document without inheriting a dangling reference, and the result of
// record() never depends on the lifetime of its argument.
// Validate a mapping and return it unchanged.
//
// The result is returned by value: a caller can pass a temporary document
// without inheriting a dangling reference, and the result never depends on
// the lifetime of the argument.
inline Json record(const Json& value, const std::string& where,
                   const std::vector<std::string>& allowed = {}) {
  if (!value.is_object()) {
    throw ConfigurationError(where + ": expected a mapping");
  }
  if (!allowed.empty()) {
    std::vector<std::string> unknown;
    for (auto it = value.begin(); it != value.end(); ++it) {
      if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end()) {
        unknown.push_back(it.key());
      }
    }
    if (!unknown.empty()) {
      std::sort(unknown.begin(), unknown.end());
      std::string names = "[";
      for (size_t i = 0; i < unknown.size(); ++i) {
        if (i) names += ", ";
        names += "'" + unknown[i] + "'";
      }
      names += "]";
      throw ConfigurationError(where + ": unknown fields " + names);
    }
  }
  return value;
}

inline std::string text(const Json& value, const std::string& where,
                        const std::regex* pattern = nullptr) {
  if (!value.is_string() || value.get_ref<const std::string&>().empty() ||
      (pattern != nullptr && !std::regex_match(value.get_ref<const std::string&>(), *pattern))) {
    throw ConfigurationError(where + ": invalid nonempty string '" +
                             (value.is_string() ? value.get_ref<const std::string&>() : value.type_name()) + "'");
  }
  return value.get_ref<const std::string&>();
}

// Convenience overload for contexts that already hold a plain string.
inline void text(const std::string& value, const std::string& where, const std::regex* pattern = nullptr) {
  if (value.empty() || (pattern != nullptr && !std::regex_match(value, *pattern))) {
    throw ConfigurationError(where + ": invalid nonempty string '" + value + "'");
  }
}

inline void schema(const Json& data, const std::string& where) {
  const auto version = data.find("schema_version");
  int64_t number = 0;
  if (version == data.end() || !int64_value(*version, &number) || number != 1) {
    throw ConfigurationError(where + ": schema_version must be 1");
  }
}

// Booleans are numbers nowhere: YAML bools must never satisfy numeric checks.
inline bool is_finite_number(const Json& value) {
  if (value.is_boolean()) return false;
  if (value.is_number_integer() || value.is_number_unsigned()) return true;
  if (value.is_number_float()) return std::isfinite(value.get<double>());
  return false;
}

inline double finite_number(const Json& value, const std::string& where) {
  if (!is_finite_number(value)) {
    throw ConfigurationError(where + ": expected a positive finite number");
  }
  int64_t integer = 0;
  return int64_value(value, &integer) ? static_cast<double>(integer) : value.get<double>();
}

inline double positive(const Json& value, const std::string& where, double minimum, double maximum) {
  bool ok = false;
  double number = 0.0;
  if (!value.is_boolean() && value.is_number()) {
    int64_t integer = 0;
    number = int64_value(value, &integer) ? static_cast<double>(integer) : value.get<double>();
    ok = std::isfinite(number) && minimum <= number && number <= maximum;
  }
  if (!ok) {
    throw ConfigurationError(where + ": expected a positive finite number in [" + py_num(minimum) +
                             ", " + py_num(maximum) + "]");
  }
  return number;
}

inline int64_t integer(const Json& value, const std::string& where, int64_t low, int64_t high) {
  int64_t number = 0;
  if (!int64_value(value, &number) || number < low || number > high) {
    throw ConfigurationError(where + ": expected integer " + std::to_string(low) + ".." + std::to_string(high));
  }
  return number;
}

// Deep merge matching the original ``_merge``: mappings merge recursively,
// anything else (including explicit nulls) replaces.
inline Json deep_merge(const Json& base, const Json& over) {
  if (base.is_object() && over.is_object()) {
    Json result = base;
    for (auto it = over.begin(); it != over.end(); ++it) {
      auto existing = result.find(it.key());
      if (existing != result.end()) {
        result[it.key()] = deep_merge(*existing, it.value());
      } else {
        result[it.key()] = it.value();
      }
    }
    return result;
  }
  return over;
}

}  // namespace chaos::assets
