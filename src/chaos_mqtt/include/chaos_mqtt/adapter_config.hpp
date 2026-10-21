#pragma once

#include <nlohmann/json.hpp>

namespace chaos_mqtt
{

/// Generate the existing Rust adapter configuration from resolved asset data.
///
/// ``transport_profile["config"]`` contains Rust network/mqtt settings. MQTT
/// strings can use ``{asset_id}``. Explicit client IDs and command topics are
/// preserved; callers must reject collisions between assets. Credentials are
/// referenced by paths/environment names, never loaded here. Returns an
/// independent deep copy; the inputs are never mutated. Throws
/// std::invalid_argument on invalid asset names or timeout ranges.
nlohmann::json build_adapter_config(
  const nlohmann::json & asset,
  const nlohmann::json & transport_profile);

}  // namespace chaos_mqtt
