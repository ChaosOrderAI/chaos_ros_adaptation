#pragma once

#include "chaos_sim/world.hpp"

#include <cstddef>
#include <string>

namespace chaos::sim {

struct SceneStats {
  std::size_t input_points = 0;
  std::size_t geoms = 0;
  bool gaussian = false;
};

// Export the same robot/scene composition used by World, for the visual viewer.
SceneStats exportSceneModel(const WorldConfig& config, const std::string& output_path);

}  // namespace chaos::sim
