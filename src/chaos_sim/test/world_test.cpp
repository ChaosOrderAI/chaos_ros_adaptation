#include "chaos_sim/world.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {
std::atomic<std::size_t> allocations{0};
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void close(double a, double b, const char* message) { check(std::abs(a - b) < 1e-11, message); }
template <class F> void throws(F action, const char* message) {
  bool rejected = false;
  try { action(); } catch (const std::exception&) { rejected = true; }
  check(rejected, message);
}
void same(const chaos::sim::State& a, const chaos::sim::State& b) {
  close(a.simulation_time, b.simulation_time, "time differs");
  for (std::size_t i = 0; i < a.position.size(); ++i) {
    close(a.position[i], b.position[i], "joint position differs");
    close(a.velocity[i], b.velocity[i], "joint velocity differs");
    close(a.effort[i], b.effort[i], "joint force differs");
  }
  for (int i = 0; i < 3; ++i) close(a.base_position[i], b.base_position[i], "base differs");
}
}  // namespace

void* operator new(std::size_t size) {
  allocations.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

int main(int argc, char** argv) {
  using chaos::sim::State;
  using chaos::sim::World;
  using chaos::sim::WorldConfig;
  try {
    check(argc >= 2, "Pass the test fixture directory");
    const std::string dir = argv[1];
    const WorldConfig config{dir + "/tiny.xml"};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    throws([&] { World w({""}); }, "empty path accepted");
    throws([&] { World w({dir + "/absent.xml"}); }, "missing model accepted");
    throws([&] { World w({config.model_path, nan}); }, "NaN gain accepted");
    throws([&] { World w({config.model_path, 1, inf}); }, "infinite gain accepted");
    throws([&] { World w({config.model_path, -1}); }, "negative gain accepted");
    throws([&] { World w({dir + "/unsupported.xml"}); }, "non-unit gear accepted");
    throws([&] { World w({dir + "/unlimited.xml"}); }, "unlimited actuator accepted");

    World world(config), reference(config);
    check(world.jointNames() == std::vector<std::string>({"hinge", "slide"}), "wrong joint order");
    check(world.modelBytes() > 0 && world.arenaBytes() > 0, "missing memory metrics");
    State initial, state, other;
    world.readState(initial);
    close(initial.position[0], .2, "home key not loaded");
    close(initial.position[1], .03, "home slide not loaded");
    close(initial.velocity[0], 0, "keyframe velocity not cleared");
    close(initial.simulation_time, 0, "keyframe time not cleared");
    close(initial.base_orientation[3], 1, "quaternion is not xyzw");

    std::string error;
    check(world.setTargets({"hinge"}, {.7}, error), "valid target rejected");
    check(reference.setTargets({"hinge"}, {.7}, error), "reference target rejected");
    check(!world.setTargets({"hinge", "absent"}, {-.8, 0}, error), "unknown accepted");
    check(!world.setTargets({"hinge", "hinge"}, {-.8, 0}, error), "duplicate accepted");
    check(!world.setTargets({"hinge", "slide"}, {-.8, .2}, error), "range violation accepted");
    check(!world.setTargets({"hinge", "slide"}, {-.8, nan}, error), "NaN accepted");
    check(!world.setTargets({"hinge"}, {inf}, error), "infinity accepted");
    check(!world.setTargets({}, {}, error), "empty command accepted");
    check(!world.setTargets({"hinge"}, {}, error), "length mismatch accepted");
    for (int i = 0; i < 50; ++i) { world.step(); reference.step(); }
    world.readState(state);
    reference.readState(other);
    same(state, other);  // Every rejected command must leave all previous targets intact.
    check(state.position[0] > initial.position[0] + .05, "PD did not move physical joint");
    check(state.base_position[2] < initial.base_position[2] - .01, "free root did not fall under gravity");
    check(state.base_twist[2] < 0, "body-frame vertical twist has wrong sign");
    check(std::abs(state.effort[0]) <= 2 + 1e-12, "motor ctrlrange not respected");
    close(state.simulation_time, 50 * world.timestep(), "step time incorrect");

    // Timeout/pause callers capture hold once: no pose or velocity teleport,
    // and the next torque is damping, not the previous commanded target.
    const State before_hold = state;
    world.hold();
    world.readState(state);
    same(before_hold, state);
    world.step();
    world.readState(state);
    close(state.effort[0], std::fmax(-2, std::fmin(2, -2 * before_hold.velocity[0])),
          "hold did not capture current joint position");
    world.reset();
    world.readState(state);
    same(initial, state);
    reference.reset();
    for (int i = 0; i < 20; ++i) { world.step(); reference.step(); }
    world.readState(state);
    reference.readState(other);
    same(state, other);

    // The hot path must reuse all C++ scratch and outgoing state buffers.
    const std::vector<std::string> names{"hinge"};
    const std::vector<double> targets{.3};
    check(world.setTargets(names, targets, error), "warmup rejected");
    world.readState(state);
    const auto* positions = state.position.data();
    const auto* velocities = state.velocity.data();
    const auto* efforts = state.effort.data();
    const auto before_allocations = allocations.load();
    for (int i = 0; i < 200; ++i) {
      check(world.setTargets(names, targets, error), "hot-path target rejected");
      world.step();
      world.readState(state);
    }
    check(allocations.load() == before_allocations, "hot path allocated C++ memory");
    check(positions == state.position.data() && velocities == state.velocity.data() &&
              efforts == state.effort.data(), "state buffers changed");

    World no_home({dir + "/no_home.xml"});
    no_home.readState(state);
    close(state.position[0], 0, "qpos0 fallback failed");
    World overflowing({config.model_path, std::numeric_limits<double>::max(), 0});
    check(overflowing.setTargets({"hinge"}, {-1}, error), "extreme gain target rejected");
    // A finite configuration can still overflow its PD multiplication.
    check(overflowing.setTargets({"slide"}, {-.1}, error), "slide target rejected");
    // Hinge error -1.2 exceeds unity, forcing overflow with DBL_MAX.
    throws([&] { overflowing.step(); }, "non-finite PD torque was hidden");

    for (int i = 2; i < argc; ++i) {
      World compact({argv[i]});
      World visual({argv[i], 40, 2, false});
      check(compact.modelBytes() <= visual.modelBytes(), "discardvisual increased model memory");
      for (int tick = 0; tick < 1000; ++tick) compact.step();
      compact.readState(state);
      std::cout << "model=" << argv[i] << " joints=" << compact.jointNames().size()
                << " compact_bytes=" << compact.modelBytes() << " visual_bytes=" << visual.modelBytes()
                << " arena_bytes=" << compact.arenaBytes() << '\n';
    }
    std::cout << "World tests passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "World tests failed: " << e.what() << '\n';
    return 1;
  }
}
