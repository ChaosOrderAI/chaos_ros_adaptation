// Clock helpers for Chaos C++ domain libraries. No ROS dependency.
//
// Monotonic clocks own every timeout, lease, and watchdog decision.
// System (wall) clocks are used only for ingress/source-time semantics and
// must never extend a monotonic deadline.
#pragma once

#include <chrono>
#include <cstdint>

namespace chaos {

inline double monotonicSeconds() {
  return std::chrono::duration<double>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline std::int64_t monotonicNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline std::int64_t systemNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}

}  // namespace chaos
