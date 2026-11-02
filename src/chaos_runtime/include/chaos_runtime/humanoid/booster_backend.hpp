// Booster SDK factory. The implementation is the only humanoid TU that
// includes vendor DDS/nav_msgs headers; generic and ROS code see only backend.hpp.
#pragma once

#include "chaos_runtime/humanoid/backend.hpp"

namespace chaos::humanoid {

std::shared_ptr<HumanoidBackend> createBoosterSdkBackend(
  const BackendConfig & config, OdometryCallback on_odometry,
  MotionModeCallback on_mode);
BackendFactory boosterSdkBackendFactory();
// Registers the name "booster_t2" for the production executable. SDK-OFF
// builds register a factory that throws instead of falling back to a mock.
void registerBoosterSdkBackend();

// One zero after the driver process is already gone. SDK-OFF builds return
// nonzero and do not touch a device. This is not a confirmation that the
// robot has stopped.
int emergencyZero(std::int32_t domain, const std::string & network_interface);

}  // namespace chaos::humanoid
