// Native robot_runtime entry point.
//
// Two modes:
//   * default: start the runtime node (rclcpp) and serve the asset-scoped
//     ROS contract.
//   * --validate-asset: read one bounded JSON asset from stdin, validate it
//     against the shared rules and report the result. This mode never
//     initializes ROS, never opens a device and never touches the network,
//     so the launch tooling can preflight an asset safely.
//
// Exit codes: 0 success, 1 validation or startup failure.
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "chaos_runtime/asset_validation.hpp"
#include "chaos_runtime/runtime_node.hpp"

namespace {

// The preflight reads a bounded amount: an asset is a small JSON document,
// and an unbounded read would let a malformed caller exhaust the memory of
// the process that is supposed to protect the launch.
constexpr std::size_t kMaxAssetBytes = 1u << 20;  // 1 MiB

// Reads the asset from stdin. Returns false when the input is too large or
// cannot be read.
bool readAsset(std::string & out) {
  out.clear();
  char buffer[4096];
  while (out.size() < kMaxAssetBytes) {
    std::cin.read(buffer, sizeof(buffer));
    const std::streamsize count = std::cin.gcount();
    if (count > 0) {
      out.append(buffer, static_cast<std::size_t>(count));
    }
    if (std::cin.eof()) {
      break;
    }
    if (std::cin.bad()) {
      return false;
    }
  }
  return out.size() < kMaxAssetBytes;
}

// --validate-asset: no ROS, no hardware, no network.
int runValidateAsset() {
  std::string json;
  if (!readAsset(json)) {
    std::cerr << "asset_json is too large (limit " << kMaxAssetBytes
              << " bytes)\n";
    return 1;
  }
  if (json.find_first_not_of(" \t\r\n") == std::string::npos) {
    std::cerr << "asset_json is empty\n";
    return 1;
  }
  std::string error;
  if (!chaos::runtime::validateAssetJson(json, error)) {
    std::cerr << error << "\n";
    return 1;
  }
  std::cout << "{\"valid\":true}\n";
  return 0;
}

}  // namespace

int main(int argc, char ** argv) {
  // The preflight is handled before rclcpp::init: it must not create a ROS
  // context, join the network or touch a device.
  for (int index = 1; index < argc; ++index) {
    if (std::string(argv[index]) == "--validate-asset") {
      return runValidateAsset();
    }
  }

  try {
    rclcpp::init(argc, argv);
    std::shared_ptr<chaos::runtime::RobotRuntimeNode> node;
    rclcpp::executors::MultiThreadedExecutor executor(
      rclcpp::ExecutorOptions(), 4);
    try {
      node = std::make_shared<chaos::runtime::RobotRuntimeNode>();
    } catch (const std::exception & error) {
      RCLCPP_FATAL(rclcpp::get_logger("robot_runtime"), "startup failed: %s",
                   error.what());
      rclcpp::shutdown();
      return 1;
    }
    executor.add_node(node);
    executor.spin();
    executor.remove_node(node);
    // Close the runtime (bounded final stop output) while the ROS context is
    // still alive, mirroring the Python signal handling contract.
    node.reset();
    rclcpp::shutdown();
    return 0;
  } catch (const std::exception & error) {
    // Argument parsing and context setup failures must not terminate the
    // process through std::terminate.
    fprintf(stderr, "robot_runtime failed: %s\n", error.what());
    return 1;
  }
}
