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
// HOST LIFETIME
//   The preflight runs before rclcpp::init, before the observation service
//   and before any helper thread starts.
//
//   The normal mode owns its signals. The automatic rclcpp signal handling is
//   disabled, so the ROS context stays valid until the final control stop.
//   The scoped SIGINT/SIGTERM handlers only set a volatile flag and write one
//   byte into a nonblocking self-pipe. One host supervisor thread observes
//   that request on a normal thread and cancels the executor spin; it never
//   depends on the ROS clock or on an executor timer.
//
//   The signal, an ordinary spin return, a node constructor failure and a
//   spin exception all use the same ordered cleanup path (the host helper
//   runtime_shutdown.hpp):
//     1. mark the shutdown, arm the outer deadline (5 s), cancel the spin
//     2. destroy the runtime node (close -> action worker join -> finalize ->
//        endpoint close) while the ROS context is still valid
//     3. observation requestStop() and joinFor(2 s), limited by the remaining
//        outer time
//     4. destroy the executor, shut the context down, mark the cleanup
//        complete, wake and join the supervisor, restore the handlers
//
//   A failure that happens before the supervisor thread exists (the context
//   setup, the service start or the executor construction) exits the process
//   immediately with the failure status: there is no node and no teardown that
//   could be waited for.
//
// Exit codes: 0 success, 1 validation or startup failure, 2 observation join
// timeout, 3 outer host shutdown timeout.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "chaos_runtime/asset_validation.hpp"
#include "chaos_runtime/observation.hpp"
#include "chaos_runtime/runtime_node.hpp"
#include "chaos_runtime/runtime_shutdown.hpp"

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

// The ROS host runtime.
//
// It owns the executor and the node. The host shutdown helper only uses this
// interface, so the ordered cleanup path is the same one for the real ROS
// runtime and for the child harness of the host test task.
class RosHostRuntime : public chaos::runtime::host::HostRuntime {
public:
  RosHostRuntime()
    : executor_(std::make_unique<rclcpp::executors::MultiThreadedExecutor>(
          rclcpp::ExecutorOptions(), 4)) {}

  void createNode() override {
    node_ = std::make_shared<chaos::runtime::RobotRuntimeNode>();
    executor_->add_node(node_);
  }

  void spin() override { executor_->spin(); }

  void cancelSpin() override { executor_->cancel(); }

  void destroyNode() override {
    if (!node_) {
      // No runtime node was created: no stop result is fabricated.
      return;
    }
    std::shared_ptr<chaos::runtime::RobotRuntimeNode> node = std::move(node_);
    try {
      executor_->remove_node(node);
    } catch (const std::exception &) {
      // The removal is best effort: the accepted teardown order of the node
      // runs anyway.
    }
    // The node destructor runs the accepted order: close -> action worker
    // join -> finalize -> endpoint close. The ROS context is still valid
    // here, so the final control stop is attempted.
    node.reset();
  }

  void destroyExecutor() override { executor_.reset(); }

  void shutdownContext() override { rclcpp::shutdown(); }

private:
  std::unique_ptr<rclcpp::executors::MultiThreadedExecutor> executor_;
  std::shared_ptr<chaos::runtime::RobotRuntimeNode> node_;
};

}  // namespace

int main(int argc, char ** argv) {
  // The preflight is handled before rclcpp::init: it must not create a ROS
  // context, join the network or touch a device.
  for (int index = 1; index < argc; ++index) {
    if (std::string(argv[index]) == "--validate-asset") {
      return runValidateAsset();
    }
  }

  // The fixed budget: 2 s for the observation join and 5 s for the outer
  // host shutdown, both measured with the steady clock.
  const chaos::runtime::host::ShutdownBudgets budgets;
  // The strong observation service owner. It is retained from here until the
  // end of the host cleanup: the host never releases the last owner of a
  // live worker pool.
  std::shared_ptr<chaos::runtime::ObservationService> service;

  try {
    // The automatic rclcpp signal handling is disabled: the host owns
    // SIGINT and SIGTERM, so the context survives until the final control
    // stop.
    rclcpp::init(
      argc, argv, rclcpp::InitOptions(), rclcpp::SignalHandlerOptions::None);

    // The service is started before the node construction: the node and the
    // endpoint register into it.
    service = chaos::runtime::ObservationService::process();

    RosHostRuntime runtime;
    chaos::runtime::host::HostShutdown shutdown(budgets, runtime, *service);
    return shutdown.run();
  } catch (const std::exception &) {
    // The failure happened before the protected host flow: the supervisor
    // thread does not exist yet, so no deadline is armed and no teardown
    // could be waited for. The process fails immediately.
    std::_Exit(chaos::runtime::host::kExitFailure);
  }
}
