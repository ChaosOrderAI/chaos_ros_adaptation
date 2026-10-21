// SDK boundary for the Go2 driver process. The Unitree SDK2 backend is
// compiled only when CHAOS_GO2_WITH_UNITREE_SDK=ON and the SDK prefix is
// provided; otherwise the factory fails explicitly at startup so a real
// deployment can never silently fall back to a mock.
//
// The Move path is the vendor no-reply variant with a 50 ms vendor timeout
// configured through SetTimeout. That timeout is a configuration of the SDK,
// not a measured upper bound of this driver's write path: the real timing on
// a robot has not been measured. The synchronous StopMove/Damp/StandUp paths
// are never used here. The return code confirms only the local output, never
// a physical robot response.
//
// The telemetry callback receives a typed value snapshot, never a borrowed
// pointer into a vendor message: the Unitree SDK2 delivers float arrays
// (std::array<float, N>), which are converted element by element into the
// double view below. No reinterpret_cast is used anywhere on this path.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace chaos::go2 {

// Typed telemetry snapshot. Owns its data, so it stays valid for the whole
// callback invocation and cannot dangle when the vendor reader thread
// recycles its message.
struct SportStateSnapshot {
  bool has_tick = false;
  std::uint32_t tick = 0;
  bool has_stamp = false;
  std::int64_t sec = 0;
  std::int64_t nanosec = 0;
  std::array<double, 3> position{{0.0, 0.0, 0.0}};
  std::array<double, 3> velocity{{0.0, 0.0, 0.0}};
  double yaw_speed = 0.0;
  // IMU quaternion in wxyz order, as delivered by the vendor message.
  std::array<double, 4> quaternion{{1.0, 0.0, 0.0, 0.0}};
  std::array<double, 3> gyroscope{{0.0, 0.0, 0.0}};
  std::int32_t error_code = 0;
};

// Invoked from the vendor reader thread (production) or from the test
// thread (fake backend).
using SportStateCallback = std::function<void(const SportStateSnapshot &)>;

class Go2SdkBackend {
public:
  virtual ~Go2SdkBackend() = default;
  // Bounded no-reply velocity output; returns 0 on local success.
  virtual int move(double vx, double vy, double wz) = 0;
  // Stops the telemetry source. Must be called before the callback owner
  // is destroyed, so no telemetry can arrive after the owner is gone.
  virtual void stop() {}
};

// The backend factory receives the telemetry callback, so the production
// backend and a fake backend wire the telemetry path identically. A fake
// backend that only implements move() cannot exercise the readiness and
// watchdog path, because it could never deliver telemetry.
using Go2BackendFactory =
  std::function<std::shared_ptr<Go2SdkBackend>(SportStateCallback)>;

// Throws std::runtime_error when the build has no Unitree SDK2 support or
// initialization fails. `domain` and `network_interface` select the vendor
// DDS transport; `sport_state_topic` selects the telemetry channel.
std::unique_ptr<Go2SdkBackend> createUnitreeSdkBackend(
  int domain, const std::string & network_interface,
  const std::string & sport_state_topic, SportStateCallback on_state);

// The production factory, bound to the Unitree transport parameters.
Go2BackendFactory unitreeSdkBackendFactory(
  int domain, const std::string & network_interface,
  const std::string & sport_state_topic);

}  // namespace chaos::go2
