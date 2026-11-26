// Control validation with monotonic leases and bounded Unix ingress deadlines.
// No ROS dependency; all timing uses an
// injected monotonic clock.
//
// Invariants are encapsulated: lease owner/token and expiry values can be
// read but never mutated from outside. The runtime interacts through
// lease()/velocity()/velocityNs()/require()/expire()/revoke()/clearVelocity()
// only.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>

#include "chaos_core/errors.hpp"

namespace chaos::control {

// ROS builtin_interfaces/Time equivalent used at the domain boundary. The
// ROS wire type carries int32 seconds; values outside the checked ROS range
// are rejected by capture() instead of overflowing.
struct RosTime {
  std::int64_t sec = 0;
  std::int64_t nanosec = 0;
};

double finite_positive(double value, const std::string & name);

struct Limits {
  double linear_mps = 1.0;
  double angular_rps = 1.0;
  double command_timeout_s = 0.5;
  double state_timeout_s = 2.0;
  double lease_max_s = 30.0;
  double action_timeout_s = 120.0;
  double max_altitude_m = 20.0;

  // Unknown keys are ignored; present-but-invalid values are rejected,
  // mirroring Limits.from_dict.
  static Limits fromMap(const std::map<std::string, double> & values);
};

// Constant-time equality for lease tokens: no early exit on content or
// length, so comparison time does not leak token prefix information.
bool constant_time_equal(const std::string & a, const std::string & b);

// An ingress deadline cannot be extended by a later wall-clock correction.
// The monotonic bound is captured from the monotonic reading taken first;
// preemption between the two clock reads may shorten the allowed interval
// but must never extend it.
class CommandDeadline {
public:
  // Captures the deadline from the supplied clocks. Production callers use
  // the no-argument overload, which reads system and monotonic time.
  // Stamps outside the ROS int32-second range are invalid.
  static CommandDeadline capture(const RosTime & stamp,
                                 std::int64_t wall_now_ns,
                                 std::int64_t monotonic_now_ns);
  static CommandDeadline capture(const RosTime & stamp);

  // Throws Rejected("INVALID_ARGUMENT") for an invalid stamp and
  // Rejected("STALE_COMMAND") once either bound has passed. A zero Unix
  // timestamp means "no deadline" and is always accepted.
  void require(std::int64_t wall_now_ns, std::int64_t monotonic_now_ns) const;
  void require() const;

  bool valid() const { return valid_; }

private:
  CommandDeadline(std::int64_t unix_ns, std::int64_t monotonic_ns, bool valid)
      : unix_ns_(unix_ns), monotonic_ns_(monotonic_ns), valid_(valid) {}

  std::int64_t unix_ns_ = 0;
  std::int64_t monotonic_ns_ = 0;
  bool valid_ = true;
};

// Lease and velocity validation. Called under the runtime lock; the injected
// clock makes all timing rules deterministic and testable.
class ControlGuard {
public:
  explicit ControlGuard(Limits limits,
                        std::function<double()> clock = nullptr,
                        std::function<std::string()> token_factory = nullptr);

  double remaining() const;
  bool expire();

  // Throws Rejected("NO_CONTROL") when no current lease matches.
  void require(const std::string & token);

  // Returns {token, remaining_seconds}. Throws Rejected on any violation.
  // Token generation failure aborts with std::runtime_error rather than
  // ever arming a lease with an empty token.
  std::pair<std::string, double> lease(const std::string & operation,
                                       const std::string & owner,
                                       const std::string & token = "",
                                       double duration_s = 5.0);

  // Validates and arms a velocity sample. Throws Rejected on any violation;
  // a rejected packet never modifies the velocity expiry or the per-lease
  // source-timestamp watermark. Kept for double-precision callers; it
  // scales the stamps to integer nanoseconds with a bound that is checked
  // BEFORE the integer conversion and delegates to velocityNs(). The
  // validation order is part of the contract: the token, the TTL, the
  // frame and the velocity components are rejected before the source
  // stamp, so a bad token stays NO_CONTROL and a bad frame stays
  // INVALID_FRAME even when the stamp is unusable.
  void velocity(const std::string & token, double stamp_s, double ros_now_s,
                double valid_for_s, const std::string & frame,
                const std::string & base_frame,
                const std::array<double, 6> & values);

  // Same contract as velocity() with the source stamp and ROS now carried as
  // integer nanoseconds, preserving adjacent-1ns ordering. Within one lease
  // the source stamp must strictly increase; an equal or backward stamp is
  // rejected with STALE_COMMAND. The watermark survives renew and resets on
  // a new acquire (and on release/stop revocation).
  void velocityNs(const std::string & token, std::int64_t stamp_ns,
                  std::int64_t ros_now_ns, double valid_for_s,
                  const std::string & frame, const std::string & base_frame,
                  const std::array<double, 6> & values);

  bool velocityTimedOut() const;

  // Revokes the current lease: the owner, the token, the lease expiry, the
  // armed velocity sample and the per-lease stamp watermark are cleared.
  // This is the internal-domain operation of an accepted stop. It neither
  // re-reads the clock nor re-validates the caller, so a lease that
  // expires between the caller's token check and the revocation cannot
  // turn an accepted stop into NO_CONTROL and skip the halt output. The
  // caller must validate the token first: an invalid token or an expired
  // request must never revoke a valid lease.
  void revoke();

  // Read-only views of the guarded state. Values are returned by value so
  // callers never observe a partially updated lease.
  std::string owner() const { return owner_; }
  std::string token() const { return token_; }
  double leaseExpires() const { return expires_; }
  double velocityExpires() const { return velocity_expires_; }
  bool leaseActive() const { return !token_.empty(); }
  const Limits & limits() const { return limits_; }

  // Cancels any armed velocity sample without touching the lease. The
  // runtime calls this once stop output has been issued.
  void clearVelocity() { velocity_expires_ = 0.0; }

private:
  Limits limits_;
  std::function<double()> clock_;
  std::function<std::string()> token_factory_;
  std::string owner_;
  std::string token_;
  double expires_ = 0.0;
  double velocity_expires_ = 0.0;
  // Highest accepted source stamp for the current lease; 0 means "none".
  // Strictly increasing per lease so a replayed command is stale even while
  // fresh. Committed only after every other validation has passed.
  std::int64_t stamp_watermark_ns_ = 0;
};

// Navigation target validation. Yaw-only planar orientation for the first
// adapters; roll/pitch are rejected, never silently discarded.
void validate_pose(const std::string & frame, const std::string & expected_frame,
                   const std::array<double, 3> & position,
                   const std::array<double, 4> & quaternion,
                   const std::string & kind, double max_altitude);

}  // namespace chaos::control
