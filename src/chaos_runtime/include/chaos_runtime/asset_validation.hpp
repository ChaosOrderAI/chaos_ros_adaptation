// ROS-free asset validation, shared by the runtime startup path and the
// `--validate-asset` preflight CLI.
//
// Why this exists: the launch tooling needs to preflight an asset before a
// node starts, without initializing ROS and without touching hardware. The
// rules must be exactly the rules the runtime applies when it starts, so
// there is a single implementation and no mirrored copy in another
// language.
//
// The registry below is the authoritative list of driver plugins, their
// option names and their supported capabilities. The concrete ROS adapters
// (Lingao / Go2 / DJI) and the mock adapter consume the same table, so a
// plugin cannot drift between the preflight and the runtime.
//
// Units: all topic names are absolute ROS topic names; capability names are
// the contract strings used by the ROS interface ("state",
// "motion.velocity", "motion.stop", "navigation.pose", "flight.takeoff",
// "flight.land").
#pragma once

#include <set>
#include <string>

#include "chaos_runtime/asset_config.hpp"

namespace chaos::runtime {

// Describes one driver plugin. `required_options` must be present in the
// asset; `option_names` are the only accepted options; a capability that
// is not in `supported_capabilities` is rejected.
struct PluginSpec {
  std::string plugin;
  std::set<std::string> option_names;
  std::set<std::string> required_options;
  std::set<std::string> supported_capabilities;
  // When true the plugin drops the flight capabilities for a ground kind
  // ("ugv", "quadruped"). The mock adapter uses this to mirror the
  // pre-migration behaviour.
  bool flight_only_for_air = false;
};

// Throws AssetConfigError when the plugin is unknown.
const PluginSpec & pluginSpec(const std::string & plugin);

// The capabilities the plugin supports for this concrete asset. Ground
// kinds lose the flight capabilities when the plugin says so.
std::set<std::string> capabilitiesFor(const PluginSpec & spec,
                                       const AssetConfig & asset);

// Throws AssetConfigError with a human readable message when the asset is
// not usable: unknown plugin, unknown or missing option, unsupported
// capability, or a kind the plugin does not support.
void validateAssetConfig(const AssetConfig & asset);

// Parses and validates a JSON asset. Returns false and fills `error` with
// the reason; never throws, so it is safe to call from a CLI entry point.
bool validateAssetJson(const std::string & json, std::string & error);

// An absolute ROS topic name: '/'-separated identifiers, no remap
// substitution, no repeated slash, no wildcard. ROS-free by construction.
bool isAbsoluteTopic(const std::string & topic);

}  // namespace chaos::runtime
