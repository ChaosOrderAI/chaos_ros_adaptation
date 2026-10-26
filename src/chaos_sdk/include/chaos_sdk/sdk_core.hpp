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

/// The client-side lease cache: the token and the owner of the session that
/// the client believes it holds.
struct LeaseCache
{
  std::string token;
  std::string owner;
};

/// Compute the client-side lease cache after a response that belongs to one
/// request.
///
/// ``request_token`` is the token that the request carried and ``current`` is
/// the cache that the client holds now. A response may only change the cache
/// while the session it belongs to is still the current one: a response for a
/// revoked, a released or a replaced session must neither install an old
/// token again nor clear a newer lease.
///
/// The rules are:
/// * ``acquire`` installs the returned token and the request owner when the
///   response is successful; a failure keeps the current cache.
/// * ``renew`` keeps the current cache when the response is successful,
///   because a renewal never rotates the token; a failure keeps it as well.
/// * ``release`` clears the cache when the response is successful. A
///   ``STOP_FAILED`` response without a token clears it too, because the
///   runtime revokes the lease before it issues the stop output.
/// * ``stop`` clears the cache when the response is successful and when it
///   reports ``STOP_FAILED``, because the runtime revokes the lease before it
///   halts the motion. Every other code (an unauthorized or an expired
///   request) keeps the current cache: such a request never reached the
///   revocation.
LeaseCache lease_cache_transition(
  const std::string & operation,
  bool success,
  const std::string & code,
  const std::string & response_token,
  const std::string & request_owner,
  const std::string & request_token,
  const LeaseCache & current);

/// Compute the client-side lease state after a ControlLease response.
/// Mirrors the wire contract: a successful response installs the returned
/// token (and the request owner when non-empty); release with STOP_FAILED
/// and no token clears a revoked lease; any other failure preserves the
/// current state.
///
/// The response is assumed to belong to the session that the client holds
/// now. Use lease_cache_transition() when the token of the request is known:
/// it additionally rejects a stale answer.
std::pair<std::string, std::string> lease_transition(
  const std::string & operation,
  bool success,
  const std::string & code,
  const std::string & response_token,
  const std::string & request_owner,
  const std::string & current_token,
  const std::string & current_owner);

}  // namespace chaos_sdk
