// Native go2_driver entry point.
//
// Two modes:
//   * default: start the driver node (rclcpp) and bridge the configured Go2
//     to the Unitree SDK2 backend.
//   * --build-capabilities: report whether this binary was compiled with
//     the real SDK backend. This mode is handled before rclcpp::init and
//     before any SDK factory call, so it never creates a ROS context, never
//     initialises the vendor DDS transport and never touches a device.
//
// The capability query answers exactly one question: "does this binary
// contain the real SDK backend?". It says nothing about hardware
// availability, network reachability or the robot's state.
//
// Exit codes: 0 success, 1 startup failure.
#include <cstdio>
#include <iostream>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "chaos_runtime/go2/go2_driver_node.hpp"

namespace {

// The compile-time capability flag. It is the same condition the backend
// translation unit is compiled under, so the answer cannot drift from the
// binary content.
#if defined(CHAOS_GO2_WITH_UNITREE_SDK)
constexpr bool kUnitreeSdk2Compiled = true;
#else
constexpr bool kUnitreeSdk2Compiled = false;
#endif

// --build-capabilities: no ROS, no SDK, no hardware.
int runBuildCapabilities() {
  std::cout << "{\"unitree_sdk2\":"
            << (kUnitreeSdk2Compiled ? "true" : "false") << "}\n";
  return 0;
}

}  // namespace

int main(int argc, char ** argv) {
  // The capability query is handled before rclcpp::init and before the SDK
  // factory can be constructed.
  for (int index = 1; index < argc; ++index) {
    if (std::string(argv[index]) == "--build-capabilities") {
      return runBuildCapabilities();
    }
  }

  try {
    rclcpp::init(argc, argv);
    std::shared_ptr<chaos::go2::Go2DriverNode> node;
    try {
      node = std::make_shared<chaos::go2::Go2DriverNode>();
    } catch (const std::exception & error) {
      RCLCPP_FATAL(rclcpp::get_logger("go2_driver"), "startup failed: %s",
                   error.what());
      rclcpp::shutdown();
      return 1;
    }
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.spin();
    executor.remove_node(node);
    // Emit the bounded final zero while the ROS context is still alive.
    node.reset();
    rclcpp::shutdown();
    return 0;
  } catch (const std::exception & error) {
    // Argument parsing and context setup failures must not terminate the
    // process through std::terminate.
    fprintf(stderr, "go2_driver failed: %s\n", error.what());
    return 1;
  }
}
