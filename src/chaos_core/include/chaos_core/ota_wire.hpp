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

/// Reason why a software-update request was rejected.
///
/// Empty when the request is valid. The runtime coordinator and the MQTT
/// command parser share these rules, so a request that one of them accepts
/// is accepted by the other as well.
inline std::string ota_request_error(
  const std::string & artifact_id,
  const std::string & version,
  const std::string & sha256,
  std::uint64_t size_bytes)
{
  if (!isOtaToken(artifact_id)) {
    return "artifact_id must be 1..128 letters, digits, '.', '_' or '-'";
  }
  if (!isOtaToken(version)) {
    return "version must be 1..128 letters, digits, '.', '_' or '-'";
  }
  if (!isOtaSha256(sha256)) {
    return "sha256 must be 64 lowercase hex characters";
  }
  if (size_bytes < 1 || size_bytes > kOtaMaxPackageBytes) {
    return "size_bytes must be in 1..2147483648";
  }
  return {};
}

}  // namespace chaos

