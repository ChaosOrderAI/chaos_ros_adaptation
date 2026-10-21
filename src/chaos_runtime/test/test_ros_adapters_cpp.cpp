// ROS-level adapter tests: the status boundary must fail closed.
//
// The legacy DJI adapter is the only control-facing consumer of an external
// status payload, so a malformed payload must never be able to impersonate
// a ready state. These tests drive the real adapter over a real ROS
// subscription.
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include "chaos_runtime/asset_config.hpp"
#include "chaos_runtime/ros_adapters.hpp"

namespace chaos::runtime {
namespace {

using Clock = std::chrono::steady_clock;

class AdapterFixture : public ::testing::Test {
protected:
  void SetUp() override {
    rclcpp::init(0, nullptr);
    node_ = std::make_shared<rclcpp::Node>("dji_status_test");
    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
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

  void spinFor(std::chrono::milliseconds duration) {
    const auto deadline = Clock::now() + duration;
    while (Clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  std::shared_ptr<rclcpp::Node> node_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::thread spin_;
};

// Builds a DJI asset with a dedicated status topic so the cases stay
// independent.
AssetConfig djiAsset(const std::string & status_topic) {
  AssetConfig asset;
  asset.id = "dji-legacy";
  asset.model = "m3td";
  asset.kind = "ugv";
  asset.mode = "real";
  asset.name_space = "/dji";
  asset.frames = {"base_link", "odom", "map"};
  asset.capabilities = {"state"};
  asset.plugin = "dji_psdk_legacy";
  asset.options = {{"status_topic", status_topic}};
  asset.limits.state_timeout_s = 30.0;
  return asset;
}

// Publishes a status payload and returns the resulting snapshot.
AdapterSnapshot publishStatus(const std::string & topic,
                              const std::string & payload,
                              std::chrono::milliseconds settle =
                                std::chrono::milliseconds(300)) {
  auto node = std::make_shared<rclcpp::Node>("dji_status_publisher");
  auto publisher = node->create_publisher<std_msgs::msg::String>(topic, 10);
  // Wait for the subscription to appear.
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  while (publisher->get_subscription_count() == 0 && Clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std_msgs::msg::String message;
  message.data = payload;
  publisher->publish(message);
  std::this_thread::sleep_for(settle);
  return AdapterSnapshot();
}

}  // namespace

// The adapter keeps its state in the object under test, so the cases below
// create one adapter per payload and read its snapshot.
class DjiStatusSaxTest : public AdapterFixture {
protected:
  AdapterSnapshot observe(const std::string & topic,
                          const std::string & payload) {
    const AssetConfig asset = djiAsset(topic);
    DjiLegacyAdapter adapter(*node_, asset);
    auto publisher = node_->create_publisher<std_msgs::msg::String>(topic, 10);
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (publisher->get_subscription_count() == 0 &&
           Clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std_msgs::msg::String message;
    message.data = payload;
    publisher->publish(message);
    // Give the executor time to deliver the message.
    const auto settle = Clock::now() + std::chrono::milliseconds(300);
    while (Clock::now() < settle) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return adapter.snapshot();
  }
};

TEST_F(DjiStatusSaxTest, TopLevelBooleansAreAccepted) {
  const AdapterSnapshot snapshot =
    observe("/dji/status/ok", R"({"ok":true,"flight_telemetry_fresh":true})");
  EXPECT_TRUE(snapshot.connected);
}

TEST_F(DjiStatusSaxTest, ArrayValueCannotImpersonateABoolean) {
  // {"ok":[true]} must not set ok: the value is not a boolean.
  const AdapterSnapshot snapshot =
    observe("/dji/status/array", R"({"ok":[true],"flight_telemetry_fresh":true})");
  EXPECT_FALSE(snapshot.connected);
}

TEST_F(DjiStatusSaxTest, DuplicateKeyDoesNotKeepTheEarlierTrue) {
  // {"ok":true,"ok":0} must not retain the earlier true.
  const AdapterSnapshot snapshot =
    observe("/dji/status/duplicate",
            R"({"ok":true,"ok":0,"flight_telemetry_fresh":true})");
  EXPECT_FALSE(snapshot.connected);
}

TEST_F(DjiStatusSaxTest, NestedObjectCannotImpersonateATopLevelBoolean) {
  const AdapterSnapshot snapshot =
    observe("/dji/status/nested",
            R"({"flight_telemetry_fresh":true,"ok":{"value":true}})");
  EXPECT_FALSE(snapshot.connected);
}

TEST_F(DjiStatusSaxTest, ArrayFlightFreshnessIsRejected) {
  const AdapterSnapshot snapshot =
    observe("/dji/status/flight_array",
            R"({"ok":true,"flight_telemetry_fresh":[true]})");
  EXPECT_FALSE(snapshot.connected);
}

TEST_F(DjiStatusSaxTest, NonObjectRootIsRejected) {
  const AdapterSnapshot snapshot =
    observe("/dji/status/root", R"([1,2,3])");
  EXPECT_FALSE(snapshot.connected);
}

TEST_F(DjiStatusSaxTest, MalformedPayloadDoesNotCrashTheAdapter) {
  const AdapterSnapshot snapshot =
    observe("/dji/status/malformed", "{not json");
  EXPECT_FALSE(snapshot.connected);
  // The adapter is still usable after a malformed payload.
  const AdapterSnapshot second =
    observe("/dji/status/malformed", R"({"ok":true,"flight_telemetry_fresh":true})");
  EXPECT_TRUE(second.connected);
}

}  // namespace chaos::runtime
