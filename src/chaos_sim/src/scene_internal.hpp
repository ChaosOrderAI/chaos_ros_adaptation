#pragma once

#include "chaos_sim/scene.hpp"

#include <mujoco/mujoco.h>

#include <memory>

namespace chaos::sim {

using SpecPtr = std::unique_ptr<mjSpec, decltype(&mj_deleteSpec)>;
SpecPtr loadComposedSpec(const WorldConfig& config, SceneStats& stats);

}  // namespace chaos::sim
