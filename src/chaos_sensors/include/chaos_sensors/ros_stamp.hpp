#pragma once

#include <cstdint>

#include <builtin_interfaces/msg/time.hpp>

#include "chaos_core/time.hpp"

namespace chaos::sensors {

// A simulated clock can be negative. rosStampFromNs normalizes the remainder
// so the nanosecond field is never an invalid huge unsigned value.
inline void stampFromNs(std::int64_t ns, builtin_interfaces::msg::Time & stamp) {
  std::int64_t sec = 0;
  std::int64_t nanosec = 0;
  chaos::rosStampFromNs(ns, sec, nanosec);
  stamp.sec = static_cast<std::int32_t>(sec);
  stamp.nanosec = static_cast<std::uint32_t>(nanosec);
}

}  // namespace chaos::sensors
