#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// Shared validators. Error categories and identifying text stay equivalent to
// the original implementation; wording may differ slightly where the original
// embedded Python-specific reprs.
inline const Json& record(const Json& value, const std::string& where,
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

inline const std::string& text(const Json& value, const std::string& where,
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
  const auto* version = data.find("schema_version");
  if (version == nullptr || !version->is_number_integer() || version->get<int64_t>() != 1) {
    throw ConfigurationError(where + ": schema_version must be 1");
  }
}

// Booleans are numbers nowhere: YAML bools must never satisfy numeric checks.
inline bool is_finite_number(const Json& value) {
  if (value.is_boolean()) return false;
  if (value.is_number_integer()) return true;
  if (value.is_number_float()) return std::isfinite(value.get<double>());
  return false;
}

inline double finite_number(const Json& value, const std::string& where) {
  if (!is_finite_number(value)) {
    throw ConfigurationError(where + ": expected a positive finite number");
  }
  return value.is_number_integer() ? static_cast<double>(value.get<int64_t>()) : value.get<double>();
}

inline double positive(const Json& value, const std::string& where, double minimum, double maximum) {
  bool ok = false;
  double number = 0.0;
  if (!value.is_boolean() && value.is_number()) {
    number = value.is_number_integer() ? static_cast<double>(value.get<int64_t>()) : value.get<double>();
    ok = std::isfinite(number) && minimum <= number && number <= maximum;
  }
  if (!ok) {
    throw ConfigurationError(where + ": expected a positive finite number in [" + py_num(minimum) +
                             ", " + py_num(maximum) + "]");
  }
  return number;
}

inline int64_t integer(const Json& value, const std::string& where, int64_t low, int64_t high) {
  if (!value.is_number_integer() || value.get<int64_t>() < low || value.get<int64_t>() > high) {
    throw ConfigurationError(where + ": expected integer " + std::to_string(low) + ".." + std::to_string(high));
  }
  return value.get<int64_t>();
}

}  // namespace chaos::assets
