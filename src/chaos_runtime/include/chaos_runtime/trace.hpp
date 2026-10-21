// Control trace events. The coordinator sanitizes every field before the
// event reaches the sink: correlation IDs are metadata, never free-form
// input or credentials, and codes are forced into the documented shape.
// A failing sink must never affect control decisions (channel isolation).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace chaos::runtime {

struct TraceVelocity {
  bool valid = false;
  std::array<double, 6> values{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  double valid_for_s = 0.0;
};

struct TraceEvent {
  std::int64_t stamp_ns = 0;      // ROS clock, for the message header
  std::int64_t monotonic_ns = 0;
  std::string frame;              // base frame
  std::string asset_id;
  std::uint64_t sequence = 0;     // assigned by the coordinator, pre-publish
  std::string request_id;         // sanitized; empty when invalid
  std::string command;
  std::string stage;
  std::string code;               // sanitized; UNKNOWN when invalid
  std::optional<TraceVelocity> velocity;
};

class TraceSink {
public:
  virtual ~TraceSink() = default;
  // May throw; the coordinator isolates trace failures from control.
  virtual void publish(const TraceEvent & event) = 0;
};

// Best-effort sink that drops every event; useful for tests.
class NullTraceSink : public TraceSink {
public:
  void publish(const TraceEvent &) override {}
};

}  // namespace chaos::runtime
