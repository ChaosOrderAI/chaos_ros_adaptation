#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace chaos::sim {

struct WorldConfig {
  std::string model_path;
  double kp = 40.0;
  double kd = 2.0;
  bool discard_visual = true;
};

struct State {
  std::array<double, 3> base_position{};
  std::array<double, 4> base_orientation{0, 0, 0, 1};  // xyzw
  std::array<double, 6> base_twist{};  // body frame: linear xyz, angular xyz
  std::vector<double> position, velocity, effort;
  double simulation_time = 0;
};

// One floating-base MuJoCo world, owned and used by a single thread.
// Supports named hinge/slide joints driven by unit-gear torque motors.
class World {
 public:
  explicit World(const WorldConfig& config);
  ~World();
  World(const World&) = delete;
  World& operator=(const World&) = delete;

  const std::vector<std::string>& jointNames() const;
  double timestep() const;
  std::size_t modelBytes() const;
  std::size_t arenaBytes() const;
  bool setTargets(const std::vector<std::string>& names,
                  const std::vector<double>& positions, std::string& error);
  void hold();  // Capture actual joint positions; never rewrites physical state.
  void reset();
  void step();
  void readState(State& state);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace chaos::sim
