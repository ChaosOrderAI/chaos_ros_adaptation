// Overflow-safe conversions between ROS-style timestamps and nanoseconds.
// ROS builtin_interfaces/Time carries signed 32-bit seconds; domain code
// must reject anything outside that range instead of overflowing int64
// arithmetic. All monotonic/watchdog math depends on these bounds.
#pragma once

#include <cstdint>

namespace chaos {

// Converts (sec, nanosec) to nanoseconds. Returns false unless
// 0 <= sec <= INT32_MAX (ROS Time range) and 0 <= nanosec < 1e9. On success
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

}  // namespace chaos
