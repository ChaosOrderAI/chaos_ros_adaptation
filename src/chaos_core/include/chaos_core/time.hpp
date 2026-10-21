// Validate an external, nonnegative timestamp before converting to nanoseconds.
// ROS permits signed seconds, but Chaos command/source timestamps use the
// nonnegative portion of that range. This policy does not apply to the
// steady clock used for local watchdogs or to general ROS time serialization.
#pragma once

#include <cstdint>

namespace chaos {

// Converts (sec, nanosec) to nanoseconds. Returns false unless
// 0 <= sec <= INT32_MAX and 0 <= nanosec < 1e9. On success
// out is at most ~2.1e18, so downstream int64 arithmetic (deadline sums,
// source barriers, age differences) cannot overflow.
inline bool rosStampToNs(std::int64_t sec, std::int64_t nanosec,
                         std::int64_t & out) {
  constexpr std::int64_t kMaxRosSec = 2147483647LL;
  constexpr std::int64_t kNanoseconds = 1'000'000'000LL;
  if (sec < 0 || sec > kMaxRosSec || nanosec < 0 || nanosec >= kNanoseconds) {
    return false;
  }
  out = sec * kNanoseconds + nanosec;
  return true;
}

// Splits a nanosecond count into a ROS time pair.
//
// This is the inverse of rosStampToNs() and uses floor division, so the
// result always satisfies 0 <= nanosec < 1e9. That matters for a clock that
// was moved before the epoch: a plain remainder would produce a negative
// nanosecond field, which is not a valid ROS time and cannot be published.
// The seconds field is signed, because a simulated clock may legitimately be
// negative; the caller that needs the nonnegative command/source policy
// applies rosStampToNs() to the result.
inline void rosStampFromNs(std::int64_t ns, std::int64_t & sec,
                           std::int64_t & nanosec) {
  constexpr std::int64_t kNanoseconds = 1'000'000'000LL;
  sec = ns / kNanoseconds;
  nanosec = ns % kNanoseconds;
  if (nanosec < 0) {
    nanosec += kNanoseconds;
    --sec;
  }
}

}  // namespace chaos
