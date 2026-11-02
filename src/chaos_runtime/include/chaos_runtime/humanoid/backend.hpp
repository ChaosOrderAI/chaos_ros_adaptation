// Vendor-neutral boundary between the supervised humanoid driver and a
// locomotion SDK. Telemetry is copied into these types by each backend; no
// vendor or ROS message crosses this interface.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace chaos::humanoid {

struct Stamp {
  std::int64_t sec = 0;
  std::int64_t nanosec = 0;
};

struct Odometry {
  Stamp stamp{};
  std::string frame_id;
  std::string child_frame_id;
  std::array<double, 3> position{0.0, 0.0, 0.0};
  std::array<double, 4> quaternion_xyzw{0.0, 0.0, 0.0, 1.0};
  std::array<double, 6> twist{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
};

using OdometryCallback = std::function<void(const Odometry &)>;
// True means that the backend's current, vendor-specific mode permits planar
// locomotion. The generic driver deliberately has no mode-transition API.
using MotionModeCallback = std::function<void(bool motion_enabled)>;

class HumanoidBackend {
public:
  virtual ~HumanoidBackend() = default;
  // Local submission result only; zero does not confirm physical motion.
  virtual int move(double vx, double vy, double yaw_rate) = 0;
  // Close telemetry before the callback owner is destroyed. Implementations
  // must make this idempotent and prevent later callbacks into the owner.
  virtual void stop() {}
};

struct BackendConfig {
  int dds_domain_id = 0;
  std::string network_interface;
  std::string state_topic;
  std::string robot_state_topic;
};

using BackendFactory = std::function<std::shared_ptr<HumanoidBackend>(
  const BackendConfig &, OdometryCallback, MotionModeCallback)>;

// Process-local registry. Registration and lookup are thread-safe; factory
// invocation happens outside the registry lock. Duplicate/empty names and
// empty factories are rejected.
void registerBackendFactory(const std::string & name, BackendFactory factory);
BackendFactory backendFactory(const std::string & name);
std::shared_ptr<HumanoidBackend> createBackend(
  const std::string & name, const BackendConfig & config,
  OdometryCallback on_odometry, MotionModeCallback on_mode);

}  // namespace chaos::humanoid
