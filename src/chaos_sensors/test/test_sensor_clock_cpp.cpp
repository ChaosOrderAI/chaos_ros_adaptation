// Native ROS-level test of the sensor clock semantics.
//
// The two clocks have different duties and this test proves that they are
// not mixed up:
//
//   * the mock sensor *simulates* an acquisition, so its sampling cadence
//     follows the ROS clock. A frozen /clock stops the synthetic samples,
//     because their stamp would otherwise repeat;
//   * the sensor monitor reports *liveness*, so its diagnostic timer runs on
//     the steady clock. A frozen /clock must not stop the health reporting.
//
// The test drives a real /clock publisher, so the behaviour is observed
// through the same rclcpp timer machinery the nodes use in production.
//
// No hardware, no vendor SDK and no broker are involved.
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rosgraph_msgs/msg/clock.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "chaos_core/time.hpp"
#include "chaos_sensors/mock_sensor_node.hpp"
#include "chaos_sensors/sensor_monitor_node.hpp"

namespace chaos::sensors {
namespace {

using Clock = std::chrono::steady_clock;

constexpr const char * kAssetNamespace = "/clock_test_asset";
constexpr const char * kStreamTopic = "/clock_test_asset/lidar/color";

std::string sensorJson() {
  return R"({"id":"lidar","kind":"lidar","namespace":"/clock_test_asset",
    "frame_id":"lidar","enabled":true,"driver":{"mode":"mock"},
    "streams":{"color":{"type":"sensor_msgs/msg/Image",
      "topic":"/clock_test_asset/lidar/color","frame_id":"lidar",
      "rate_hz":20.0,"timeout_s":1.0,"monitor":true}}})";
}

std::string assetJson() {
  return std::string(R"({"id":"clock_test_asset",
    "namespace":"/clock_test_asset","sensors":{"lidar":)") + sensorJson() + "}}";
}

// A publisher of /clock that can be frozen and resumed. The simulated time
// only advances while the source is running.
class SimulatedClock {
public:
  SimulatedClock(rclcpp::Node * node)
      : publisher_(node->create_publisher<rosgraph_msgs::msg::Clock>(
          "/clock", rclcpp::QoS(1))) {
    thread_ = std::thread([this] { run(); });
  }

  ~SimulatedClock() {
    running_ = false;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  // Freezes the simulated time: no further /clock message is published.
  void freeze() { frozen_ = true; }

  void resume() { frozen_ = false; }

  bool frozen() const { return frozen_; }

private:
  void run() {
    // Start at one second: the nodes are created before the clock source
    // starts, and a large jump would make the timers catch up at once.
    std::int64_t nanosec = 1'000'000'000LL;
    while (running_.load()) {
      if (!frozen_.load()) {
        nanosec += 50'000'000LL;
        rosgraph_msgs::msg::Clock message;
        message.clock.sec = static_cast<std::int32_t>(nanosec / 1'000'000'000LL);
        message.clock.nanosec = static_cast<std::uint32_t>(
          nanosec % 1'000'000'000LL);
        publisher_->publish(message);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }

  rclcpp::Publisher<rosgraph_msgs::msg::Clock>::SharedPtr publisher_;
  std::atomic<bool> running_{true};
  std::atomic<bool> frozen_{false};
  std::thread thread_;
};

// Counts the received samples under a mutex: the executor thread writes, the
// test thread reads.
class Counters {
public:
  void addImage() {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      ++images_;
    }
  }

  void addDiagnostics() {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      ++diagnostics_;
    }
  }

  std::size_t images() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return images_;
  }

  std::size_t diagnostics() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return diagnostics_;
  }

private:
  mutable std::mutex mutex_;
  std::size_t images_ = 0;
  std::size_t diagnostics_ = 0;
};

class SensorClockFixture : public ::testing::Test {
protected:
  void SetUp() override {
    rclcpp::init(0, nullptr);
    counters_ = std::make_shared<Counters>();

    // Both nodes run on the simulated clock, so the /clock publisher of the
    // test is their only time source.
    rclcpp::NodeOptions mock_options;
    mock_options.parameter_overrides(
      {rclcpp::Parameter("sensor_json", sensorJson()),
       rclcpp::Parameter("use_sim_time", true)});
    mock_node_ = createMockSensorNode(mock_options);

    rclcpp::NodeOptions monitor_options;
    monitor_options.parameter_overrides(
      {rclcpp::Parameter("asset_json", assetJson()),
       rclcpp::Parameter("use_sim_time", true)});
    monitor_node_ = createSensorMonitorNode(monitor_options);

    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(mock_node_);
    executor_->add_node(monitor_node_);
    spin_ = std::thread([this] { executor_->spin(); });

    // The clock source is a separate node, so the publisher exists before
    // the first /clock message.
    clock_node_ = std::make_shared<rclcpp::Node>("sensor_clock_test_source");
    clock_ = std::make_shared<SimulatedClock>(clock_node_.get());

    image_sub_ = mock_node_->create_subscription<sensor_msgs::msg::Image>(
      kStreamTopic, rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::Image &) { counters_->addImage(); });
    diagnostics_sub_ =
      monitor_node_->create_subscription<diagnostic_msgs::msg::DiagnosticArray>(
        std::string(kAssetNamespace) + "/sensors/diagnostics",
        rclcpp::QoS(10),
        [this](const diagnostic_msgs::msg::DiagnosticArray &) {
          counters_->addDiagnostics();
        });
  }

  void TearDown() override {
    clock_.reset();
    image_sub_.reset();
    diagnostics_sub_.reset();
    executor_->cancel();
    if (spin_.joinable()) {
      spin_.join();
    }
    mock_node_.reset();
    monitor_node_.reset();
    clock_node_.reset();
    executor_.reset();
    rclcpp::shutdown();
  }

  // Bounded, condition based wait.
  template <typename Predicate>
  bool waitFor(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
      if (predicate()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return predicate();
  }

  std::shared_ptr<Counters> counters_;
  std::shared_ptr<rclcpp::Node> mock_node_;
  std::shared_ptr<rclcpp::Node> monitor_node_;
  std::shared_ptr<rclcpp::Node> clock_node_;
  std::shared_ptr<SimulatedClock> clock_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::thread spin_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_sub_;
  rclcpp::Subscription<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr
    diagnostics_sub_;
};

TEST_F(SensorClockFixture, FrozenClockStopsTheSimulationButNotTheLiveness) {
  // The simulated clock runs: the mock publishes its synthetic samples.
  ASSERT_TRUE(waitFor([&] { return counters_->images() > 0; },
                       std::chrono::seconds(10)))
    << "the mock sensor never published while the simulated clock ran";
  ASSERT_TRUE(waitFor([&] { return counters_->diagnostics() > 0; },
                       std::chrono::seconds(10)))
    << "the sensor monitor never reported while the simulated clock ran";

  // The clock is frozen: the simulation stops, the liveness does not.
  clock_->freeze();
  // Drain samples already queued before the clock source observed freeze.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const auto images_at_freeze = counters_->images();
  const auto diagnostics_at_freeze = counters_->diagnostics();
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  EXPECT_EQ(counters_->images(), images_at_freeze)
    << "the mock sensor kept publishing while the ROS clock was frozen";
  EXPECT_GT(counters_->diagnostics(), diagnostics_at_freeze)
    << "the sensor monitor stopped reporting while the ROS clock was frozen";

  // The clock runs again: the simulation resumes.
  clock_->resume();
  ASSERT_TRUE(waitFor([&] { return counters_->images() > images_at_freeze; },
                       std::chrono::seconds(10)))
    << "the mock sensor did not resume after the ROS clock advanced again";
}

TEST(SensorClockRule, NegativeNanosecondsProduceAValidStamp) {
  // A clock that was moved before the epoch must still yield a valid ROS
  // time: the nanosecond field is never negative.
  std::int64_t sec = 0;
  std::int64_t nanosec = 0;
  chaos::rosStampFromNs(-1LL, sec, nanosec);
  EXPECT_EQ(sec, -1);
  EXPECT_EQ(nanosec, 999'999'999LL);

  chaos::rosStampFromNs(-1'500'000'000LL, sec, nanosec);
  EXPECT_EQ(sec, -2);
  EXPECT_EQ(nanosec, 500'000'000LL);

  chaos::rosStampFromNs(1'500'000'000LL, sec, nanosec);
  EXPECT_EQ(sec, 1);
  EXPECT_EQ(nanosec, 500'000'000LL);

  // The result is accepted by the inverse conversion.
  std::int64_t round_trip = 0;
  ASSERT_TRUE(chaos::rosStampToNs(1, 500'000'000LL, round_trip));
  EXPECT_EQ(round_trip, 1'500'000'000LL);
}

}  // namespace
}  // namespace chaos::sensors
