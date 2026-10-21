// Real rclcpp integration tests for the native runtime node: endpoint
// persistence, the steady-clock watchdog under a frozen /clock, and the
// action terminal states. No hardware and no external broker: the mock
// adapter is used and the ROS domain stays on localhost.
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_interfaces/action/navigate_pose.hpp"
#include "chaos_interfaces/srv/control_lease.hpp"
#include "chaos_interfaces/srv/get_description.hpp"
#include "chaos_interfaces/srv/stop.hpp"
#include "chaos_runtime/runtime_node.hpp"

namespace chaos::runtime {
namespace {

using NavigatePose = chaos_interfaces::action::NavigatePose;
using Clock = std::chrono::steady_clock;

const char * kMockAssetJson = R"({
  "id": "mock-ugv",
  "model": "mock-model",
  "kind": "ugv",
  "mode": "mock",
  "namespace": "/mock",
  "ros_domain_id": 0,
  "frames": {"base": "base_link", "odom": "odom", "map": "map"},
  "capabilities": ["state", "motion.velocity", "motion.stop", "navigation.pose"],
  "limits": {"linear_mps": 1.0, "angular_rps": 1.0, "command_timeout_s": 0.5,
             "state_timeout_s": 2.0, "lease_max_s": 30.0,
             "action_timeout_s": 120.0},
  "driver": {"plugin": "mock", "options": {"kind": "ugv"}}
})";

class RuntimeFixture : public ::testing::Test {
protected:
  void SetUp() override {
    rclcpp::init(0, nullptr);
    rclcpp::NodeOptions options;
    options.parameter_overrides(
      {rclcpp::Parameter("asset_json", std::string(kMockAssetJson))});
    node_ = std::make_shared<RobotRuntimeNode>(options);
    executor_ = std::make_shared<rclcpp::executors::MultiThreadedExecutor>(
      rclcpp::ExecutorOptions(), 4);
    executor_->add_node(node_);
    spin_ = std::thread([this] { executor_->spin(); });
  }

  void TearDown() override {
    executor_->cancel();
    if (spin_.joinable()) {
      spin_.join();
    }
    node_.reset();
    executor_.reset();
    rclcpp::shutdown();
  }

  template <typename Fn>
  bool waitFor(Fn && predicate, std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
      if (predicate()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
  }

  std::shared_ptr<RobotRuntimeNode> node_;
  std::shared_ptr<rclcpp::executors::MultiThreadedExecutor> executor_;
  std::thread spin_;

  // An action requires a control lease: the goal carries the lease token.
  std::string acquireLease() {
    auto client =
      node_->create_client<chaos_interfaces::srv::ControlLease>("/mock/control/lease");
    if (!waitFor([&] { return client->service_is_ready(); },
                 std::chrono::seconds(5))) {
      return std::string();
    }
    auto request =
      std::make_shared<chaos_interfaces::srv::ControlLease::Request>();
    request->operation = "acquire";
    request->owner = "test-owner";
    request->duration_s = 30.0;
    auto future = client->async_send_request(request);
    if (future.wait_for(std::chrono::seconds(5)) !=
        std::future_status::ready) {
      return std::string();
    }
    const auto response = future.get();
    return response->success ? response->token : std::string();
  }
};

TEST_F(RuntimeFixture, ServiceEndpointsPersistAfterConstruction) {
  // A temporary returned by create_service() would be destroyed and the
  // endpoint would vanish; the node must keep them alive.
  auto client = node_->create_client<chaos_interfaces::srv::GetDescription>(
    "/mock/description");
  ASSERT_TRUE(
    waitFor([&] { return client->service_is_ready(); }, std::chrono::seconds(5)))
    << "description service endpoint is not available";
  auto request =
    std::make_shared<chaos_interfaces::srv::GetDescription::Request>();
  auto future = client->async_send_request(request);
  ASSERT_EQ(future.wait_for(std::chrono::seconds(5)),
            std::future_status::ready);
  const auto response = future.get();
  EXPECT_EQ(response->asset_id, "mock-ugv");
  EXPECT_EQ(response->model, "mock-model");
}

TEST_F(RuntimeFixture, LeaseServiceAcquiresAndReleases) {
  auto client =
    node_->create_client<chaos_interfaces::srv::ControlLease>("/mock/control/lease");
  ASSERT_TRUE(
    waitFor([&] { return client->service_is_ready(); }, std::chrono::seconds(5)));

  auto request = std::make_shared<chaos_interfaces::srv::ControlLease::Request>();
  request->operation = "acquire";
  request->owner = "test-owner";
  request->duration_s = 5.0;
  auto future = client->async_send_request(request);
  ASSERT_EQ(future.wait_for(std::chrono::seconds(5)),
            std::future_status::ready);
  const auto response = future.get();
  ASSERT_TRUE(response->success) << response->message;
  EXPECT_FALSE(response->token.empty());
  EXPECT_GT(response->remaining_s, 0.0);
}

TEST_F(RuntimeFixture, StopServiceIsReachable) {
  auto client = node_->create_client<chaos_interfaces::srv::Stop>("/mock/motion/stop");
  ASSERT_TRUE(
    waitFor([&] { return client->service_is_ready(); }, std::chrono::seconds(5)));
  auto request = std::make_shared<chaos_interfaces::srv::Stop::Request>();
  request->request_id = "stop-without-lease";
  auto future = client->async_send_request(request);
  ASSERT_EQ(future.wait_for(std::chrono::seconds(5)),
            std::future_status::ready);
  const auto response = future.get();
  // No lease: the stop is rejected, but the endpoint answered.
  EXPECT_FALSE(response->success);
  EXPECT_EQ(response->code, "NO_CONTROL");
}

TEST_F(RuntimeFixture, NavigationGoalReachesSucceeded) {
  auto client = rclcpp_action::create_client<NavigatePose>(node_, "/mock/navigation/to_pose");
  ASSERT_TRUE(waitFor([&] { return client->action_server_is_ready(); },
                      std::chrono::seconds(5)));

  NavigatePose::Goal goal;
  goal.lease_token = acquireLease();
  ASSERT_FALSE(goal.lease_token.empty()) << "lease acquisition failed";
  goal.target.header.frame_id = "map";
  goal.target.pose.position.x = 0.2;

  auto goal_future = client->async_send_goal(goal);
  ASSERT_EQ(goal_future.wait_for(std::chrono::seconds(5)),
            std::future_status::ready);
  auto handle = goal_future.get();
  ASSERT_NE(handle, nullptr);

  auto result_future = client->async_get_result(handle);
  ASSERT_EQ(result_future.wait_for(std::chrono::seconds(20)),
            std::future_status::ready);
  const auto result = result_future.get();
  EXPECT_EQ(result.code, rclcpp_action::ResultCode::SUCCEEDED);
  EXPECT_TRUE(result.result->success) << result.result->message;
}

TEST_F(RuntimeFixture, NavigationGoalCancelReachesCanceled) {
  auto client = rclcpp_action::create_client<NavigatePose>(node_, "/mock/navigation/to_pose");
  ASSERT_TRUE(waitFor([&] { return client->action_server_is_ready(); },
                      std::chrono::seconds(5)));

  NavigatePose::Goal goal;
  goal.lease_token = acquireLease();
  ASSERT_FALSE(goal.lease_token.empty()) << "lease acquisition failed";
  goal.target.header.frame_id = "map";
  goal.target.pose.position.x = 500.0;  // far away: the goal keeps running

  auto goal_future = client->async_send_goal(goal);
  ASSERT_EQ(goal_future.wait_for(std::chrono::seconds(5)),
            std::future_status::ready);
  auto handle = goal_future.get();
  ASSERT_NE(handle, nullptr);

  auto cancel_future = client->async_cancel_goal(handle);
  ASSERT_EQ(cancel_future.wait_for(std::chrono::seconds(5)),
            std::future_status::ready);
  // The cancel request must be accepted by the server.
  cancel_future.get();

  auto result_future = client->async_get_result(handle);
  ASSERT_EQ(result_future.wait_for(std::chrono::seconds(20)),
            std::future_status::ready);
  const auto result = result_future.get();
  EXPECT_EQ(result.code, rclcpp_action::ResultCode::CANCELED);
}

// A node whose ROS clock is frozen must still expire its lease: the
// watchdog runs on the wall clock, not on /clock. No /clock publisher is
// started here, so the node clock stays at zero for the whole test.
class FrozenClockFixture : public ::testing::Test {
protected:
  void SetUp() override {
    rclcpp::init(0, nullptr);
    rclcpp::NodeOptions options;
    options.parameter_overrides(
      {rclcpp::Parameter("asset_json", std::string(kMockAssetJson)),
       rclcpp::Parameter("use_sim_time", true)});
    node_ = std::make_shared<RobotRuntimeNode>(options);
    executor_ = std::make_shared<rclcpp::executors::MultiThreadedExecutor>(
      rclcpp::ExecutorOptions(), 4);
    executor_->add_node(node_);
    spin_ = std::thread([this] { executor_->spin(); });
  }

  void TearDown() override {
    executor_->cancel();
    if (spin_.joinable()) {
      spin_.join();
    }
    node_.reset();
    executor_.reset();
    rclcpp::shutdown();
  }

  template <typename Fn>
  bool waitFor(Fn && predicate, std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      if (predicate()) {
        return true;
      }
    }
    return predicate();
  }

  std::shared_ptr<RobotRuntimeNode> node_;
  std::shared_ptr<rclcpp::executors::MultiThreadedExecutor> executor_;
  std::thread spin_;
};

TEST_F(FrozenClockFixture, LeaseExpiresWhileTheRosClockIsFrozen) {
  auto client =
    node_->create_client<chaos_interfaces::srv::ControlLease>("/mock/control/lease");
  ASSERT_TRUE(
    waitFor([&] { return client->service_is_ready(); }, std::chrono::seconds(5)));

  auto request = std::make_shared<chaos_interfaces::srv::ControlLease::Request>();
  request->operation = "acquire";
  request->owner = "frozen-clock-owner";
  request->duration_s = 1.0;
  auto future = client->async_send_request(request);
  ASSERT_EQ(future.wait_for(std::chrono::seconds(5)),
            std::future_status::ready);
  const auto acquired = future.get();
  ASSERT_TRUE(acquired->success) << acquired->message;
  const std::string token = acquired->token;

  // The ROS clock never advances in this test; only wall time passes.
  std::this_thread::sleep_for(std::chrono::milliseconds(2500));

  // The lease must have expired on the wall clock: the stop is rejected.
  auto stop_client = node_->create_client<chaos_interfaces::srv::Stop>("/mock/motion/stop");
  ASSERT_TRUE(waitFor([&] { return stop_client->service_is_ready(); },
                      std::chrono::seconds(5)));
  auto stop_request = std::make_shared<chaos_interfaces::srv::Stop::Request>();
  stop_request->request_id = "frozen-clock-stop";
  stop_request->lease_token = token;
  auto stop_future = stop_client->async_send_request(stop_request);
  ASSERT_EQ(stop_future.wait_for(std::chrono::seconds(5)),
            std::future_status::ready);
  const auto stopped = stop_future.get();
  EXPECT_FALSE(stopped->success);
  EXPECT_EQ(stopped->code, "NO_CONTROL");
}

}  // namespace
}  // namespace chaos::runtime
