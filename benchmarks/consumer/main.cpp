// Installed-downstream consumer check: this project is configured against
// the install prefix (not the build tree) and must compile and link purely
// through the exported CMake targets. It exists to catch export/namespace
// regressions that in-tree builds cannot see.
#include <cstdio>

#include "chaos_core/errors.hpp"
#include "chaos_core/time.hpp"
#include "chaos_runtime/control.hpp"
#include "chaos_runtime/go2_driver_core.hpp"
#include "chaos_sensors/health.hpp"
#include "chaos_data/timing.hpp"

int main() {
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
