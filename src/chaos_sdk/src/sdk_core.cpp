#include "chaos_sdk/sdk_core.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>

namespace chaos_sdk
{

double validate_ttl(double value)
{
  if (!std::isfinite(value) || value <= 0.0 || value > 30.0) {
    throw std::invalid_argument("command TTL must be finite and in (0, 30] seconds");
  }
  return value;
}

std::pair<std::int32_t, std::uint32_t> deadline_timespec(double ttl_s, std::int64_t now_ns)
{
  // Truncating the TTL can only shorten it, but a positive TTL must never
  // expire immediately: a sub-nanosecond TTL floors to one nanosecond. The
  // rounding happens in integer arithmetic: a double cannot represent every
  // whole nanosecond count, so scaling the TTL in floating point would both
  // mis-count and, past 2^63 nanoseconds, overflow to infinity.
  const double ttl = validate_ttl(ttl_s);
  const auto ttl_seconds = static_cast<std::int64_t>(ttl);
  const double fraction = ttl - static_cast<double>(ttl_seconds);
  // The whole count is kept in a wider type: the largest accepted TTL is 30
  // seconds, but the intermediate product of the seconds still has to be
  // exact before it is added to the current time.
  __int128 ttl_whole = static_cast<__int128>(ttl_seconds) * 1000000000LL +
    static_cast<__int128>(static_cast<std::int64_t>(fraction * 1e9));
  if (ttl_whole < 1) {
    ttl_whole = 1;
  }
  // The sum must not wrap: a deadline that overflows the signed range would
  // land in the past and expire the command at once. The addition happens in
  // a wider type, so the check itself cannot overflow either.
  const __int128 deadline_wide = static_cast<__int128>(now_ns) + ttl_whole;
  if (deadline_wide > static_cast<__int128>(std::numeric_limits<std::int64_t>::max())) {
    throw std::invalid_argument("deadline is out of the signed 64-bit range");
  }
  const auto deadline_ns = static_cast<std::int64_t>(deadline_wide);
  return {
    static_cast<std::int32_t>(deadline_ns / 1000000000),
    static_cast<std::uint32_t>(deadline_ns % 1000000000)};
}

void validate_velocity(double x, double y, double z, double yaw)
{
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(yaw)) {
    throw std::invalid_argument("Velocity must be finite");
  }
}

namespace
{

std::mt19937_64 & rng()
{
  static std::mt19937_64 engine{[]() {
    std::random_device device;
    std::seed_seq seed{
      device(), device(), device(), device(), device(), device(), device(), device()};
    return std::mt19937_64(seed);
  }()};
  return engine;
}

}  // namespace

std::string new_request_id()
{
  static std::mutex mutex;
  std::uint8_t bytes[16];
  {
    const std::lock_guard<std::mutex> guard(mutex);
    for (int offset = 0; offset < 16; offset += 8) {
      const auto value = rng()();
      std::memcpy(bytes + offset, &value, 8);
    }
  }
  bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
  bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);
  std::ostringstream hex;
  hex << std::hex;
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      hex << '-';
    }
    hex.width(2);
    hex.fill('0');
    hex << static_cast<int>(bytes[i]);
  }
  return hex.str();
}

LeaseCache lease_cache_transition(
  const std::string & operation,
  bool success,
  const std::string & code,
  const std::string & response_token,
  const std::string & request_owner,
  const std::string & request_token,
  const LeaseCache & current)
{
  // The operations that act on an existing session may only change the cache
  // while that session is still the current one. An answer for a session that
  // was revoked, released or replaced in the meantime is stale: it must
  // neither install an old token again nor clear a newer lease.
  const bool session_scoped =
    operation == "renew" || operation == "release" || operation == "stop";
  if (session_scoped && current.token != request_token) {
    return current;
  }
  if (operation == "acquire") {
    if (!success) {
      return current;
    }
    return {response_token, response_token.empty() ? std::string() : request_owner};
  }
  if (operation == "renew") {
    // A renewal never rotates the token: the session stays the same one, so
    // the current cache is kept for a successful and for a failed answer.
    return current;
  }
  if (operation == "release") {
    // The runtime revokes the lease before it issues the stop output, so a
    // STOP_FAILED answer means that the lease is gone even though the stop
    // output failed.
    const bool revoked = success || (code == "STOP_FAILED" && response_token.empty());
    return revoked ? LeaseCache{} : current;
  }
  if (operation == "stop") {
    // The runtime revokes the lease before it halts the motion, so an
    // accepted stop ends the session even when the stop output failed. A
    // rejected request (an unauthorized or an expired one) never reached the
    // revocation and keeps the lease.
    const bool revoked = success || code == "STOP_FAILED";
    return revoked ? LeaseCache{} : current;
  }
  return current;
}

std::pair<std::string, std::string> lease_transition(
  const std::string & operation,
  bool success,
  const std::string & code,
  const std::string & response_token,
  const std::string & request_owner,
  const std::string & current_token,
  const std::string & current_owner)
{
  // Preserve the legacy helper's response-based public contract. Robot uses
  // lease_cache_transition() with the original request token instead.
  const bool released_with_stop_failure = operation == "release" &&
    code == "STOP_FAILED" && response_token.empty();
  if (success || released_with_stop_failure) {
    return {response_token, response_token.empty() ? std::string() : request_owner};
  }
  return {current_token, current_owner};
}

}  // namespace chaos_sdk
