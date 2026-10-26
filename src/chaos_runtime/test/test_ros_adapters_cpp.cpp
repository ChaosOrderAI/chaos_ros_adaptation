// ROS-level adapter tests: the status boundary must fail closed, and the
// Go2 internal transport must carry its deadline unchanged.
//
// The legacy DJI adapter is the only control-facing consumer of an external
// status payload, so a malformed payload must never be able to impersonate
// a ready state. These tests drive the real adapter over a real ROS
// subscription.
//
// The Go2 cases observe the internal command topic of the adapter: the
// original source stamp, the immutable absolute deadline, the actual issue
// time and the clock-domain identity must survive the transport unchanged,
// and every refused command must stay unpublished.
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <chaos_interfaces/msg/driver_velocity_command.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>

#include "chaos_runtime/asset_config.hpp"
#include "chaos_runtime/driver_deadline.hpp"
#include "chaos_runtime/ros_adapters.hpp"

namespace chaos::runtime {
namespace {

using Clock = std::chrono::steady_clock;

class AdapterFixture : public ::testing::Test {
protected:
  void SetUp() override {
    rclcpp::init(0, nullptr);
    // The node is shared by the DJI status cases and by the Go2 transport
    // cases, so its name is neutral.
    node_ = std::make_shared<rclcpp::Node>("adapter_test");
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

// --- Go2 internal transport ------------------------------------------------

// The internal topics of the Go2 transport cases. They are absolute, so the
// cases stay independent of each other and of any other asset.
constexpr char kGo2CmdTopic[] = "/go2_transport_test/cmd_vel";
constexpr char kGo2OdomTopic[] = "/go2_transport_test/odom";

// A Go2 asset with every required option. The adapter refuses an asset
// without an explicit option, so the test states all of them.
AssetConfig go2Asset() {
  AssetConfig asset;
  asset.id = "go2-transport";
  asset.model = "unitree-go2";
  asset.kind = "quadruped";
  asset.mode = "real";
  asset.name_space = "/go2_transport_test";
  asset.frames = {"base_link", "odom", "map"};
  asset.capabilities = {"state", "motion.velocity", "motion.stop"};
  asset.plugin = "unitree_go2";
  asset.options = {{"odom_topic", kGo2OdomTopic},
                   {"cmd_vel_topic", kGo2CmdTopic},
                   {"network_interface", "lo"},
                   {"dds_domain_id", 196},
                   {"sport_state_topic", "rt/sportmodestate"},
                   {"velocity_frame", "body"}};
  asset.limits.linear_mps = 1.0;
  asset.limits.angular_rps = 1.0;
  asset.limits.command_timeout_s = 5.0;
  asset.limits.state_timeout_s = 1.0;
  return asset;
}

// Returns the rejection code of a refused call, or an empty string when the
// call was accepted. The code is part of the fail-closed contract: a
// refused command must be refused with a stable, machine-readable reason.
std::string rejectionCode(const std::function<void()> & call) {
  try {
    call();
  } catch (const chaos::Rejected & error) {
    return error.code();
  }
  return std::string();
}

// Observes the internal command topic of the Go2 adapter. The adapter owns
// the publisher; the test only listens and compares the delivered command
// with the clock samples it took around the call.
class Go2TransportFixture : public AdapterFixture {
protected:
  void SetUp() override {
    AdapterFixture::SetUp();
    // The subscription is created on the node the executor already spins, so
    // the executor is never mutated while it runs.
    cmd_sub_ = node_->create_subscription<chaos_interfaces::msg::DriverVelocityCommand>(
      kGo2CmdTopic, rclcpp::QoS(10),
      [this](const chaos_interfaces::msg::DriverVelocityCommand & message) {
        std::lock_guard<std::mutex> guard(commands_mutex_);
        commands_.push_back(message);
      });
  }

  void TearDown() override {
    // The subscription is released before the node and the executor go
    // away.
    cmd_sub_.reset();
    AdapterFixture::TearDown();
  }

  // The commands observed on the internal topic.
  std::vector<chaos_interfaces::msg::DriverVelocityCommand> commands() const {
    std::lock_guard<std::mutex> guard(commands_mutex_);
    return commands_;
  }

  // Waits until the publisher of the adapter is visible to the subscription.
  // Without it a command could be published before the graph knows the
  // subscription, and the case would observe nothing.
  bool waitForPublisher(std::chrono::milliseconds timeout =
                          std::chrono::seconds(5)) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
      if (cmd_sub_->get_publisher_count() > 0) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return cmd_sub_->get_publisher_count() > 0;
  }

  // Waits until at least `count` commands were observed.
  bool waitForCommands(std::size_t count,
                       std::chrono::milliseconds timeout =
                         std::chrono::seconds(5)) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
      if (commands().size() >= count) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return commands().size() >= count;
  }

  rclcpp::Subscription<chaos_interfaces::msg::DriverVelocityCommand>::SharedPtr
    cmd_sub_;
  mutable std::mutex commands_mutex_;
  std::vector<chaos_interfaces::msg::DriverVelocityCommand> commands_;
};

TEST_F(Go2TransportFixture, MotionCarriesTheOriginalSourceAndTheUnchangedDeadline) {
  Go2Adapter adapter(*node_, go2Asset());
  ASSERT_TRUE(waitForPublisher())
    << "the publisher of the adapter never became visible";

  // The original source stamp of the admitted command. It is provenance
  // only, but it must survive the transport unchanged.
  const std::int64_t source_ns = 1700000000123456789LL;
  // The immutable absolute deadline of the admitted command. The adapter
  // publishes it unchanged: a later issue time never extends it.
  const std::int64_t deadline =
    driver_deadline::monotonicNowNs() + 500000000LL;

  // The issue time of the command is an actual CLOCK_MONOTONIC reading
  // taken inside the call, so it is bounded by the two real clock samples
  // around it.
  const std::int64_t before = driver_deadline::monotonicNowNs();
  adapter.setVelocityUntil({0.25, -0.1, 0.0, 0.0, 0.0, 0.5}, source_ns,
                            deadline);
  const std::int64_t after = driver_deadline::monotonicNowNs();

  ASSERT_TRUE(waitForCommands(1));
  const auto observed = commands();
  ASSERT_EQ(observed.size(), 1U);
  const auto & command = observed.front();

  // The original source stamp, preserved exactly.
  EXPECT_EQ(command.header.stamp.sec,
            static_cast<std::int32_t>(source_ns / 1000000000LL));
  EXPECT_EQ(command.header.stamp.nanosec,
            static_cast<std::uint32_t>(source_ns % 1000000000LL));
  // The configured base frame and the cached clock domain of the process.
  EXPECT_EQ(command.header.frame_id, "base_link");
  EXPECT_EQ(command.clock_domain_id, driver_deadline::localClockDomainId());
  // The deadline is delivered unchanged.
  EXPECT_EQ(command.expires_monotonic_ns, deadline);
  // The issue time is an actual reading between the two samples.
  EXPECT_GT(command.issued_monotonic_ns, 0);
  EXPECT_GE(command.issued_monotonic_ns, before);
  EXPECT_LE(command.issued_monotonic_ns, after);
  // This is ordinary motion, not a stop barrier.
  EXPECT_FALSE(command.stop);
  // All six components are carried unchanged.
  EXPECT_DOUBLE_EQ(command.velocity.linear.x, 0.25);
  EXPECT_DOUBLE_EQ(command.velocity.linear.y, -0.1);
  EXPECT_DOUBLE_EQ(command.velocity.linear.z, 0.0);
  EXPECT_DOUBLE_EQ(command.velocity.angular.x, 0.0);
  EXPECT_DOUBLE_EQ(command.velocity.angular.y, 0.0);
  EXPECT_DOUBLE_EQ(command.velocity.angular.z, 0.5);
}

TEST_F(Go2TransportFixture, MotionWithoutAUsableDeadlineIsRefused) {
  Go2Adapter adapter(*node_, go2Asset());
  ASSERT_TRUE(waitForPublisher());

  const std::int64_t source_ns = 1700000000123456789LL;
  const std::int64_t now = driver_deadline::monotonicNowNs();
  const std::array<double, 6> values{0.25, 0.0, 0.0, 0.0, 0.0, 0.0};

  // The legacy path carries no deadline: a nonzero command without an
  // absolute deadline is unbounded in time and must be refused instead of
  // being granted an invented validity interval.
  EXPECT_EQ(rejectionCode([&] { adapter.setVelocity(values); }),
            "INVALID_ARGUMENT");
  // A missing deadline.
  EXPECT_EQ(rejectionCode([&] {
              adapter.setVelocityUntil(values, source_ns, 0);
            }),
            "INVALID_ARGUMENT");
  // A deadline that has already passed.
  EXPECT_EQ(rejectionCode([&] {
              adapter.setVelocityUntil(values, source_ns, now - 1);
            }),
            "STALE_COMMAND");
  // A missing source stamp.
  EXPECT_EQ(rejectionCode([&] {
              adapter.setVelocityUntil(values, 0, now + 500000000LL);
            }),
            "INVALID_ARGUMENT");
  // A source stamp that does not fit the ROS header range.
  EXPECT_EQ(rejectionCode([&] {
              adapter.setVelocityUntil(values, 4000000000000000000LL,
                                       now + 500000000LL);
            }),
            "INVALID_ARGUMENT");

  // A refused command never reaches the transport.
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(commands().empty()) << "a refused command was published";
}

TEST_F(Go2TransportFixture, UnsupportedAxesMustBeExactlyZero) {
  Go2Adapter adapter(*node_, go2Asset());
  ASSERT_TRUE(waitForPublisher());

  const std::int64_t source_ns = 1700000000123456789LL;
  const std::int64_t deadline =
    driver_deadline::monotonicNowNs() + 500000000LL;

  // A tiny but nonzero value on an unsupported axis is not a zero. The
  // driver refuses the same axes with the same exact-zero rule, so the
  // adapter must not pass such a value on.
  EXPECT_EQ(rejectionCode([&] {
              adapter.setVelocityUntil({0.0, 0.0, 1e-10, 0.0, 0.0, 0.0},
                                       source_ns, deadline);
            }),
            "UNSUPPORTED");
  EXPECT_EQ(rejectionCode([&] {
              adapter.setVelocityUntil({0.0, 0.0, 0.0, 1e-10, 0.0, 0.0},
                                       source_ns, deadline);
            }),
            "UNSUPPORTED");
  EXPECT_EQ(rejectionCode([&] {
              adapter.setVelocityUntil({0.0, 0.0, 0.0, 0.0, 1e-10, 0.0},
                                       source_ns, deadline);
            }),
            "UNSUPPORTED");

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_TRUE(commands().empty()) << "a refused command was published";
}

TEST_F(Go2TransportFixture, ZeroCommandStopsWithoutFreshTelemetry) {
  Go2Adapter adapter(*node_, go2Asset());
  ASSERT_TRUE(waitForPublisher());

  // The adapter never received telemetry. The explicit stop is a barrier
  // and must not depend on a fresh sample.
  EXPECT_FALSE(adapter.snapshot().ready);

  adapter.setVelocity({0.0, 0.0, 0.0, 0.0, 0.0, 0.0});
  ASSERT_TRUE(waitForCommands(1));
  {
    const auto observed = commands();
    ASSERT_EQ(observed.size(), 1U);
    const auto & command = observed.front();

    // Six exact zeros, the stop flag, no deadline and a current issue time.
    EXPECT_TRUE(command.stop);
    EXPECT_DOUBLE_EQ(command.velocity.linear.x, 0.0);
    EXPECT_DOUBLE_EQ(command.velocity.linear.y, 0.0);
    EXPECT_DOUBLE_EQ(command.velocity.linear.z, 0.0);
    EXPECT_DOUBLE_EQ(command.velocity.angular.x, 0.0);
    EXPECT_DOUBLE_EQ(command.velocity.angular.y, 0.0);
    EXPECT_DOUBLE_EQ(command.velocity.angular.z, 0.0);
    EXPECT_EQ(command.expires_monotonic_ns, 0);
    EXPECT_GT(command.issued_monotonic_ns, 0);
    // The configured frame and the cached domain are carried by a stop too.
    EXPECT_EQ(command.header.frame_id, "base_link");
    EXPECT_EQ(command.clock_domain_id, driver_deadline::localClockDomainId());
    // The source stamp of a stop may be zero.
    EXPECT_EQ(command.header.stamp.sec, 0);
    EXPECT_EQ(command.header.stamp.nanosec, 0U);
  }

  // A motion command claims the output ownership, so the stop that follows
  // publishes the barrier and releases the ownership again.
  const std::int64_t source_ns = 1700000000123456789LL;
  const std::int64_t deadline =
    driver_deadline::monotonicNowNs() + 500000000LL;
  adapter.setVelocityUntil({0.25, 0.0, 0.0, 0.0, 0.0, 0.0}, source_ns,
                            deadline);
  ASSERT_TRUE(waitForCommands(2));
  adapter.stop();
  ASSERT_TRUE(waitForCommands(3));
  {
    const auto observed = commands();
    ASSERT_EQ(observed.size(), 3U);
    EXPECT_FALSE(observed[1].stop);
    EXPECT_TRUE(observed[2].stop);
  }

  // The ownership was released by the stop: a second stop publishes nothing.
  adapter.stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(commands().size(), 3U) << "a stop was published without an owned output";
}

// The adapters without a transport deadline keep their existing behaviour:
// the shared base implementation delegates to setVelocity(), so the mock and
// the Lingao adapter are unaffected by the deadline API.
TEST(GenericAdapterFallbackTest, SetVelocityUntilDelegatesToSetVelocity) {
  struct RecordingAdapter : Adapter {
    AdapterSnapshot snapshot() override { return {}; }
    std::array<double, 6> values{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    bool called = false;
    void setVelocity(const std::array<double, 6> & delivered) override {
      values = delivered;
      called = true;
    }
  };

  RecordingAdapter adapter;
  const std::array<double, 6> values{0.1, 0.0, 0.0, 0.0, 0.0, 0.2};
  adapter.setVelocityUntil(values, 1700000000123456789LL, 123456789LL);
  EXPECT_TRUE(adapter.called);
  EXPECT_EQ(adapter.values, values);
}

}  // namespace chaos::runtime
