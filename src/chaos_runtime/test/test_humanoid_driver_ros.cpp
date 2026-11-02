#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <chaos_interfaces/msg/driver_velocity_command.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>

#include "chaos_runtime/driver_deadline.hpp"
#include "chaos_runtime/humanoid/driver_node.hpp"

namespace chaos::humanoid {
namespace {
using Clock = std::chrono::steady_clock;

class FakeBackend : public HumanoidBackend {
public:
  FakeBackend(OdometryCallback odom, MotionModeCallback mode)
      : on_odom(std::move(odom)), on_mode(std::move(mode)) {}
  int move(double vx, double vy, double wz) override {
    std::lock_guard<std::mutex> guard(mutex);
    calls.push_back({vx, vy, wz});
    return 0;
  }
  void stop() override {
    std::lock_guard<std::mutex> guard(mutex);
    stopped = true;
  }
  std::vector<std::array<double, 3>> snapshot() {
    std::lock_guard<std::mutex> guard(mutex);
    return calls;
  }

  OdometryCallback on_odom;
  MotionModeCallback on_mode;
  mutable std::mutex mutex;
  std::vector<std::array<double, 3>> calls;
  bool stopped = false;
};

TEST(HumanoidDriverRos, FakeBackendUsesRealTransportWatchdogAndShutdown) {
  rclcpp::init(0, nullptr);
  std::shared_ptr<FakeBackend> fake;
  rclcpp::NodeOptions options;
  const auto domain = rclcpp::contexts::get_global_default_context()->get_domain_id();
  const std::string asset = std::string(
    R"({"id":"t2","model":"booster-t2","kind":"humanoid","mode":"real",)"
    R"("namespace":"/t2","ros_domain_id":)" + std::to_string(domain) +
    R"(,"frames":{"base":"base_link","odom":"odom","map":"map"},)"
    R"("capabilities":["state","motion.velocity","motion.stop"],)"
    R"("limits":{"linear_mps":1.0,"angular_rps":1.0,"command_timeout_s":1.0,)"
    R"("state_timeout_s":1.0,"lease_max_s":30.0,"action_timeout_s":120.0},)"
    R"("driver":{"plugin":"humanoid","options":{"backend":"booster_t2",)"
    R"("network_interface":"lo","dds_domain_id":0,"state_topic":"rt/odom",)"
    R"("robot_state_topic":"rt/robot_states","cmd_vel_topic":"/t2/cmd_vel",)"
    R"("odom_topic":"/t2/odom","velocity_frame":"body",)"
    R"("source_odom_frame":"odom","source_base_frame":"base_link"}}})");
  options.parameter_overrides({rclcpp::Parameter("asset_json", asset)});
  auto node = std::make_shared<HumanoidDriverNode>(
    [&fake](const BackendConfig &, OdometryCallback odom, MotionModeCallback mode) {
      fake = std::make_shared<FakeBackend>(std::move(odom), std::move(mode));
      return fake;
    }, options);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  std::thread spinner([&] { executor.spin(); });

  std::mutex odom_mutex;
  std::vector<nav_msgs::msg::Odometry> published;
  auto odom_sub = node->create_subscription<nav_msgs::msg::Odometry>(
    "/t2/odom", rclcpp::QoS(10), [&](const nav_msgs::msg::Odometry & message) {
      std::lock_guard<std::mutex> guard(odom_mutex);
      published.push_back(message);
    });
  auto command_pub = node->create_publisher<chaos_interfaces::msg::DriverVelocityCommand>(
    "/t2/cmd_vel", rclcpp::QoS(10));
  auto wait = [](const std::function<bool()> & predicate) {
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < deadline) {
      if (predicate()) return true;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
  };
  ASSERT_TRUE(wait([&] { return command_pub->get_subscription_count() != 0; }));

  Odometry telemetry;
  telemetry.stamp = Stamp{node->get_clock()->now().nanoseconds() / 1000000000LL,
    node->get_clock()->now().nanoseconds() % 1000000000LL};
  telemetry.frame_id = "odom";
  telemetry.child_frame_id = "base_link";
  telemetry.position = {1.0, 2.0, 0.0};
  fake->on_odom(telemetry);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_TRUE(fake->snapshot().empty());  // odom alone is not readiness
  fake->on_mode(true);
  ASSERT_TRUE(wait([&] { return node->driverStatus().initialized; }));

  const auto source_ns = node->get_clock()->now().nanoseconds();
  telemetry.stamp = Stamp{source_ns / 1000000000LL, source_ns % 1000000000LL};
  fake->on_odom(telemetry);
  const std::int64_t issued = runtime::driver_deadline::monotonicNowNs();
  chaos_interfaces::msg::DriverVelocityCommand command;
  command.header.stamp = node->get_clock()->now();
  command.header.frame_id = "base_link";
  command.clock_domain_id = runtime::driver_deadline::localClockDomainId();
  command.issued_monotonic_ns = issued;
  command.expires_monotonic_ns = issued + 500000000LL;
  command.velocity.linear.x = 0.25;
  command_pub->publish(command);
  ASSERT_TRUE(wait([&] {
    const auto calls = fake->snapshot();
    return !calls.empty() && calls.back()[0] == 0.25;
  }));
  ASSERT_TRUE(wait([&] {
    std::lock_guard<std::mutex> guard(odom_mutex);
    return !published.empty();
  }));
  {
    std::lock_guard<std::mutex> guard(odom_mutex);
    const auto stamp = published.back().header.stamp;
    EXPECT_EQ(static_cast<std::int64_t>(stamp.sec) * 1000000000LL + stamp.nanosec,
              source_ns);
    EXPECT_DOUBLE_EQ(published.back().pose.pose.position.x, 1.0);
  }

  node->close();
  EXPECT_EQ(fake->snapshot().back()[0], 0.0);
  {
    std::lock_guard<std::mutex> guard(fake->mutex);
    EXPECT_TRUE(fake->stopped);
  }
  executor.cancel();
  spinner.join();
  node.reset();
  rclcpp::shutdown();
}

}  // namespace
}  // namespace chaos::humanoid
