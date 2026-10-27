// Device adapter contract. Deliberately ROS-free: the coordinator owns all
// control decisions, and adapters only translate to/from device transports.
// Mirrors chaos_runtime/adapters/base.py plus the concrete backends.
//
// Threading: the runtime serializes every call under its control mutex, but
// transport callbacks (subscriptions) mutate adapter state concurrently, so
// adapters guard their own caches with a small internal mutex.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <limits>
#include <string>

#include "chaos_runtime/control.hpp"
#include "chaos_runtime/asset_config.hpp"

namespace chaos::runtime {

// Pose battery NaN means "unknown": the ROS facade renders it as NaN so the
// JSON consumers see null, never a fabricated 0.
struct AdapterSnapshot {
  bool connected = false;
  bool ready = false;
  bool pose_valid = false;
  std::array<double, 3> position{0.0, 0.0, 0.0};
  std::array<double, 4> quaternion{0.0, 0.0, 0.0, 1.0};
  std::array<double, 6> velocity{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  double battery = std::numeric_limits<double>::quiet_NaN();
  std::string detail = "Waiting for telemetry";
  std::string pose_frame;
  std::string twist_frame;
  // Acquisition stamp in nanoseconds, when the backend exposes one.
  std::optional<std::int64_t> source_stamp_ns;
};

enum class OperationKind { kNavigatePose, kTakeoff, kLand };

struct PollResult {
  bool done = false;
  bool success = false;
  std::string code = "UNSUPPORTED";
  std::string message = "No active operation";
  double progress = 0.0;
};

class Adapter {
public:
  virtual ~Adapter() = default;

  // Must report stale data as invalid/!ready; never refresh from caches.
  virtual AdapterSnapshot snapshot() = 0;

  // Throws Rejected on unsupported axes/ranges. Called before arming.
  virtual void validateVelocity(const std::array<double, 6> & values) {
    (void)values;
    throw chaos::Rejected("UNSUPPORTED", "Adapter does not support velocity");
  }
  virtual void setVelocity(const std::array<double, 6> & values) {
    (void)values;
    throw chaos::Rejected("UNSUPPORTED", "Adapter does not support velocity");
  }

  // Motion with an explicit, immutable absolute deadline.
  //
  // `source_stamp_ns` is the ORIGINAL source stamp of the admitted command
  // and `expires_mono_ns` is the absolute Linux CLOCK_MONOTONIC deadline D
  // that the caller computed once. The deadline is handed over unchanged: an
  // adapter never re-anchors, re-stamps or extends it, and it never adds a
  // default validity interval of its own.
  //
  // The base implementation delegates to setVelocity(), so the adapters that
  // have no transport deadline (mock, Lingao) keep their existing behaviour.
  // The adapters whose transport carries a deadline override it.
  virtual void setVelocityUntil(const std::array<double, 6> & values,
                                std::int64_t source_stamp_ns,
                                std::int64_t expires_mono_ns) {
    (void)source_stamp_ns;
    (void)expires_mono_ns;
    setVelocity(values);
  }

  // stop() issues output only for owned outputs. A successful stop means
  // stop output was issued, not physical confirmation.
  virtual void stop() {}
  virtual void controlledStop() { stop(); }

  virtual void startOperation(OperationKind kind,
                              const std::array<double, 4> & target) {
    (void)kind;
    (void)target;
    throw chaos::Rejected("UNSUPPORTED", "Adapter does not support operations");
  }
  virtual PollResult pollOperation() {
    return PollResult{};
  }
  virtual void update(double dt) { (void)dt; }
  virtual void close() { stop(); }
};

}  // namespace chaos::runtime
