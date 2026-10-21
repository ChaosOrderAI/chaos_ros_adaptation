#pragma once

#include <string>
#include <vector>

#include "chaos_assets/text.hpp"

namespace chaos::assets {

// An immutable, fully validated inventory snapshot.
//
// The snapshot owns its data: every accessor returns a detached copy, so a
// caller can never modify a checked configuration through a nested value.
class Inventory {
 public:
  Inventory(std::string path, Json hosts, std::vector<Json> assets, Json transports);

  const std::string& path() const { return path_; }
  const Json& hosts() const { return hosts_; }
  const std::vector<Json>& assets() const { return assets_; }
  const Json& transports() const { return transports_; }

  // The resolved snapshot used by the configuration CLI and the launch
  // planner.
  Json as_dict() const;

  // The enabled assets of one host, optionally restricted to asset_ids.
  // Throws ConfigurationError for an unknown host, for an asset that is not
  // available on the host and for an empty selection.
  std::vector<Json> select(const std::string& host_id,
                           const std::vector<std::string>& asset_ids = {}) const;

 private:
  std::string path_;
  Json hosts_;
  std::vector<Json> assets_;
  Json transports_;
};

// Validate an inventory without starting ROS, importing plugins or opening
// devices.
//
// ``mode`` is an explicit backend override (mock/real); an empty value means
// each asset uses its configured backend. Paths are relative to their
// declaring file. Throws ConfigurationError for every rejected input.
Inventory load_inventory(const std::string& path, const std::string& mode = "");

}  // namespace chaos::assets
