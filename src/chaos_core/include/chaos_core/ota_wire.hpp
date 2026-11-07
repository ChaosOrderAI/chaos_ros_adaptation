// Shared wire validation for software-update package identity fields.
// Used by the runtime coordinator and the MQTT command parser.
#pragma once

#include <cstdint>
#include <string>

namespace chaos {

constexpr std::uint64_t kOtaMaxPackageBytes = 2147483648ULL;
constexpr std::size_t kOtaTokenMaxLen = 128;

inline bool isOtaTokenChar(char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
         (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-';
}

inline bool isOtaToken(const std::string & value) {
  if (value.empty() || value.size() > kOtaTokenMaxLen) {
    return false;
  }
  for (char ch : value) {
    if (!isOtaTokenChar(ch)) {
      return false;
    }
  }
  return true;
}

inline bool isOtaSha256(const std::string & value) {
  if (value.size() != 64) {
    return false;
  }
  for (char ch : value) {
    const bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    if (!hex) {
      return false;
    }
  }
  return true;
}

}  // namespace chaos

