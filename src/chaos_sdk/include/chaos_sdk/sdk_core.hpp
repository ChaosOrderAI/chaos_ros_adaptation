#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace chaos_sdk
{

/// The local response timeout elapsed before the runtime answered.
///
/// This is a client-side observation limit. It never means that the remote
/// execution stopped; the remote state stays unknown.
class TimeoutError : public std::runtime_error
{
public:
  explicit TimeoutError(const std::string & what)
  : std::runtime_error(what) {}
};

/// The client was closed while a request was still in flight.
class ClientClosedError : public std::runtime_error
{
public:
  explicit ClientClosedError(const std::string & what)
  : std::runtime_error(what) {}
};

/// Too many requests are in flight; the client refuses to grow unbounded.
class PendingOverflowError : public std::runtime_error
{
public:
  explicit PendingOverflowError(const std::string & what)
  : std::runtime_error(what) {}
};

/// Validate a command TTL: finite numeric in (0, 30] seconds. Booleans and
/// non-finite values are rejected. Throws std::invalid_argument.
double validate_ttl(double value);

/// Unix UTC deadline for a request that may start ``ttl_s`` from ``now_ns``
/// (system-clock nanoseconds), returned as (sec, nanosec) ROS Time fields.
/// Rounding down cannot grant a command extra validity. The result is
/// independent of the ROS simulation clock.
std::pair<std::int32_t, std::uint32_t> deadline_timespec(double ttl_s, std::int64_t now_ns);

/// Validate a velocity sample; all components must be finite.
void validate_velocity(double x, double y, double z, double yaw);

/// Random RFC 4122 version 4 UUID in 32-char hex form (correlation ID).
std::string new_request_id();

/// Compute the client-side lease state after a ControlLease response.
/// Mirrors the wire contract: a successful response installs the returned
/// token (and the request owner when non-empty); release with STOP_FAILED
/// and no token clears a revoked lease; any other failure preserves the
/// current state.
std::pair<std::string, std::string> lease_transition(
  const std::string & operation,
  bool success,
  const std::string & code,
  const std::string & response_token,
  const std::string & request_owner,
  const std::string & current_token,
  const std::string & current_owner);

}  // namespace chaos_sdk
