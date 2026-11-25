// Typed observation payloads shared by the runtime coordinator and the
// ROS-free observation service.
//
// This header is the single canonical definition site of these types;
// coordinator.hpp includes it rather than duplicating them.
#pragma once

#include <cstdint>
#include <string>

#include "chaos_runtime/adapter.hpp"
#include "chaos_runtime/trace.hpp"

namespace chaos::runtime {

// Terminal result of a command, as published to the result channel.
struct CommandResultView {
  std::int64_t stamp_ns = 0;
  std::string request_id;
  bool success = false;
  std::string code;
  std::string message;
};

// Coordinator state snapshot, as published to the state channel.
struct StateView {
  std::int64_t stamp_ns = 0;
  std::string asset_id;
  std::string model;
  bool connected = false;
  bool ready = false;
  bool pose_valid = false;
  std::string operational_state;
  AdapterSnapshot snapshot;
  std::string detail;
};

}  // namespace chaos::runtime
