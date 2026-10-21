// Strict asset_json parsing at the runtime trust boundary. The inventory
// resolver normalizes assets before launch, but the node is its own trust
// boundary: every field used here is type-checked, and booleans masquerading
// as numbers are rejected instead of being silently converted by C++.
//
// Units and frames:
//   * every *_s limit is seconds, every *_mps / *_rps limit is metres per
//     second resp. radians per second, max_altitude_m is metres;
//   * `frames.base` is the frame a velocity command must be expressed in,
//     `frames.odom` is the child frame of the published odometry, and
//     `frames.map` is the world frame of a navigation target. The names are
//     opaque strings here; only the runtime adapter interprets them;
//   * `namespace` is the absolute ROS name space that owns every business
//     endpoint of the asset. It is never derived from the node name or from
//     the launch file.
//
// Clocks: the asset carries no clock. It only carries the ROS domain id the
// process must run in, which the node compares against its own context.
//
// Threading: the parsed asset is an immutable value. It is copied into the
// coordinator and the adapters at construction time and is not shared
// afterwards.
//
// Errors: every rejection throws AssetConfigError with a message that names
// the offending field. There is no partially filled result: a failed parse
// leaves nothing behind.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "chaos_runtime/control.hpp"

namespace chaos::runtime {

class AssetConfigError : public std::runtime_error {
public:
  explicit AssetConfigError(const std::string & message)
      : std::runtime_error(message) {}
};

struct AssetFrames {
  // Frame names, used exactly as the asset spells them. The runtime never
  // rewrites them and never guesses a fallback: a missing frame is an error.
  std::string base;
  std::string odom;
  std::string map;
};

struct AssetConfig {
  std::string id;
  std::string model;
  std::string model_version = "1";
  std::string kind = "ugv";
  std::string mode;
  std::string name_space;
  std::int64_t ros_domain_id = 0;
  AssetFrames frames;
  std::set<std::string> capabilities;
  control::Limits limits;
  std::string plugin;
  nlohmann::json options;
  // Normalized sensor entries (validated by chaos_assets at launch; the
  // monitor re-validates what it consumes).
  nlohmann::json sensors = nlohmann::json::object();
};

// Throws AssetConfigError on any malformed field. `raw` must be the
// asset_json parameter text (at most 1 MiB, a single JSON object).
AssetConfig parseAssetJson(const std::string & raw);

// Typed field accessors shared with the sensor configuration parser.
// Each rejects JSON booleans explicitly even where is_number() would
// already exclude them, so the intent is documented at the boundary.
double requireNumber(const nlohmann::json & object, const std::string & key,
                     double minimum, double maximum,
                     const std::string & where);
bool requireBool(const nlohmann::json & object, const std::string & key,
                 bool default_value, const std::string & where);
std::string requireString(const nlohmann::json & object,
                          const std::string & key,
                          const std::string & where);

}  // namespace chaos::runtime
