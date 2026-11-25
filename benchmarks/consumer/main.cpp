// Installed-downstream consumer check: this project is configured against
// the install prefix (not the build tree) and must compile and link purely
// through the exported CMake targets. It exists to catch export/namespace
// regressions that in-tree builds cannot see.
#include <cstdio>
#include "chaos_assets/yaml.hpp"
#include "chaos_bringup/planner.hpp"
#include "chaos_sdk/robot.hpp"
#include "chaos_sdk/bt/nodes.hpp"
#include "chaos_mqtt/protocol.hpp"
#include "chaos_data/export.hpp"
#include "chaos_data/ros_message.hpp"

#include "chaos_core/errors.hpp"
#include "chaos_core/time.hpp"
#include "chaos_runtime/control.hpp"
#include "chaos_runtime/go2_driver_core.hpp"
#include "chaos_sensors/health.hpp"
#include "chaos_data/timing.hpp"

int main() {
  // Exercise exported policy APIs without constructing a ROS context or
  // accessing a device. The volatile function pointers also force link-time
  // resolution of I/O-facing APIs without running those operations.
  const auto parsed = chaos::assets::load_yaml_string("enabled: true", "consumer");
  if (!parsed.at("enabled").get<bool>()) return 2;
  if (chaos::bringup::LaunchPlan::from_items({}).size() != 0) return 3;
  if (chaos_sdk::validate_ttl(1.0) != 1.0) return 4;
  const auto reply = chaos_mqtt::response("consumer_01", "system.describe", true, "OK", "");
  if (!reply.is_object()) return 5;
  auto * volatile export_entry = &chaos::data::exportDataset;
  auto * volatile sdk_entry = &chaos_sdk::Robot::create_standalone;
  auto * volatile bt_entry = &chaos_sdk::bt::register_nodes;
  (void)export_entry;
  (void)sdk_entry;
  (void)bt_entry;
  std::int64_t ns = 0;
  if (!chaos::rosStampToNs(1, 0, ns)) {
    return 1;
  }
  chaos::control::ControlGuard guard(chaos::control::Limits{},
                                     [] { return 100.0; },
                                     [] { return std::string("tok"); });
  const auto lease = guard.lease("acquire", "consumer", "", 5.0);
  guard.velocity(lease.first, 200.0, 200.0, 0.5, "base", "base",
                 {0.1, 0, 0, 0, 0, 0});
  chaos::go2::Go2AssetConfig config;
  config.base_frame = "base";
  config.odom_frame = "odom";
  chaos::go2::Go2DriverCore driver(config, [] { return 1.0; },
                                   [] { return 10000000000LL; });
  chaos::sensors::HealthTracker tracker("frame", 2.0,
                                        [] { return 10000000000LL; });
  tracker.observe(1, 1, "frame");
  const auto sample = chaos::data::parseTracking("", 0, 0);
  std::printf("consumer ok lease=%zu health=%d timing=%s\n",
              lease.first.size(), tracker.snapshot().level,
              sample.status.c_str());
  return 0;
}
