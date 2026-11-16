#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace chaos::sim {

struct SceneConfig {
  std::string path;  // PLY point cloud / 3D Gaussian PLY, or PCD point cloud
  double voxel_size = 0.1;
  double point_radius = 0.03;
  std::size_t max_geoms = 20000;
  bool collision = true;
};

struct WorldConfig {
  std::string model_path;
  double kp = 40.0;
  double kd = 2.0;
  bool discard_visual = true;
  SceneConfig scene;
};

struct State {
  std::array<double, 3> base_position{};
  std::array<double, 4> base_orientation{0, 0, 0, 1};  // xyzw
  std::array<double, 6> base_twist{};  // body frame: linear xyz, angular xyz
  std::vector<double> position, velocity, effort;
  double simulation_time = 0;
};

// One MuJoCo world with 1 to 8 floating roots that share one compiled scene.
// Owned and used by a single thread. Supports named hinge/slide joints driven
// by unit-gear torque motors. A single root keeps the original whole-world API.
class World {
 public:
  explicit World(const WorldConfig& config);
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  const std::vector<std::string>& jointNames() const;
  std::size_t robotCount() const;
  const std::string& robotName(std::size_t index) const;
  const std::vector<std::string>& robotJointNames(std::size_t index) const;
  double timestep() const;
  std::size_t modelBytes() const;
  std::size_t arenaBytes() const;
  std::size_t scenePoints() const;
  std::size_t sceneGeoms() const;
  bool setTargets(const std::vector<std::string>& names,
                  const std::vector<double>& positions, std::string& error);
  void hold();  // Capture actual joint positions; never rewrites physical state.
  void holdRobot(std::size_t index);
  void reset();
  void resetRobot(std::size_t index);  // One root; the others keep their pose and targets.
  void step();
  void readState(State& state);  // Every actuator, and the first root's base.
  void readRobot(std::size_t index, State& state);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace chaos::sim
