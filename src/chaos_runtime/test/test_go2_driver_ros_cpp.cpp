// ROS-level Go2 driver test with a fake SDK backend.
//
// The test exercises the real driver node over real ROS messages: the
// telemetry path, the command path, the odometry publication, the source
// identity rule, the telemetry expiry, an illegal telemetry sample, the SDK
// write fault and the shutdown handshake. It is deliberately distinct from
// the ROS-free core tests (test_go2_driver_core_cpp), which never touch DDS.
//
// The command path uses the internal driver transport: the command carries
// the original source stamp, an actual CLOCK_MONOTONIC issue time, the
// immutable absolute deadline D and the clock-domain identity of this boot
// instance and time namespace. The driver takes the deadline exactly as it
// was delivered: it never converts, re-ages or extends it.
//
// The fake backend is injected through the same factory the production
// Unitree backend uses, so the wiring under test is the production wiring.
//
// Concurrency contract exercised here:
//   * the fake owns the telemetry source. stop() is a real stop: it refuses
//     new deliveries and waits until the in-flight delivery has returned,
//     so the callback can never reach a released core.
//   * a delivery can be blocked on a barrier, so the test can prove that
//     close() waits for an in-flight callback instead of racing it.
//   * every observation is taken under a mutex, because the executor thread
//     writes the observations while the test thread reads them.
//   * every wait is bounded and condition based; no test relies on a sleep
//     to order two threads.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
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
#include "chaos_runtime/go2/go2_driver_node.hpp"
#include "chaos_runtime/go2/sdk_backend.hpp"

namespace chaos::go2 {
namespace {

using Clock = std::chrono::steady_clock;

// Observations collected by the executor thread and read by the test thread.
struct Observations {
  mutable std::mutex mutex;
  std::condition_variable changed;
  std::vector<nav_msgs::msg::Odometry> odometry;

  void add(const nav_msgs::msg::Odometry & message) {
    {
      std::lock_guard<std::mutex> guard(mutex);
      odometry.push_back(message);
    }
    changed.notify_all();
  }

  std::vector<nav_msgs::msg::Odometry> snapshot() const {
    std::lock_guard<std::mutex> guard(mutex);
    return odometry;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> guard(mutex);
    return odometry.size();
  }
};

// Fake SDK backend: records the velocity writes and delivers telemetry
// through the callback it received from the factory.
//
// The telemetry source is real: a delivery is counted as in flight while
// the callback runs, and stop() waits for the in-flight delivery to return.
class FakeBackend : public Go2SdkBackend {
public:
  explicit FakeBackend(SportStateCallback callback)
      : callback_(std::move(callback)) {}

  int move(double vx, double vy, double wz) override {
    std::lock_guard<std::mutex> guard(mutex_);
    calls_.push_back({vx, vy, wz});
    // The CLOCK_MONOTONIC sample of the write. It lets a test prove that a
    // nonzero output never happened at or after a delivered deadline.
    call_stamps_.push_back(runtime::driver_deadline::monotonicNowNs());
    return fail_moves_ ? -1 : 0;
  }

  // A real stop: no new delivery is started and the in-flight delivery is
  // awaited, so the callback cannot outlive the owner of the callback
  // target.
  void stop() override {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      stopped_ = true;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return in_flight_ == 0; });
  }

  bool stopped() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return stopped_;
  }

  // Blocks the next delivery on a barrier, so the test can hold a callback
  // in flight.
  void blockDeliveries() {
    std::lock_guard<std::mutex> guard(mutex_);
    blocked_ = true;
  }

  void releaseDeliveries() {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      blocked_ = false;
    }
    barrier_.notify_all();
  }

  // The number of deliveries that are currently held at the barrier. The
  // test uses it to make sure that a delivery really is in flight before it
  // starts the shutdown; otherwise the delivery could still be refused by
  // stop() and the shutdown would have nothing to wait for.
  std::size_t blockedDeliveries() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return blocked_in_flight_;
  }

  // Delivers a telemetry snapshot. Returns false when the source is stopped
  // or when the delivery was abandoned.
  bool publish(const SportStateSnapshot & snapshot) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (stopped_) {
        return false;
      }
      ++in_flight_;
      if (blocked_) {
        // Wait on the barrier while the lock is owned; wait_for releases it
        // while it waits. The release is explicit: stopping the source must
        // not let an in-flight callback through.
        ++blocked_in_flight_;
        barrier_.wait_for(lock, std::chrono::seconds(30),
                          [this] { return !blocked_; });
        --blocked_in_flight_;
      }
    }
    // The callback runs without the lock held, so it can call back into the
    // driver without a lock inversion.
    callback_(snapshot);
    {
      std::lock_guard<std::mutex> guard(mutex_);
      --in_flight_;
      if (in_flight_ == 0) {
        idle_.notify_all();
      }
    }
    return true;
  }

  std::vector<std::array<double, 3>> calls() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return calls_;
  }

  // The CLOCK_MONOTONIC sample of every recorded write, parallel to
  // calls().
  std::vector<std::int64_t> callStamps() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return call_stamps_;
  }

  std::size_t callCount() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return calls_.size();
  }

  void setFailMoves(bool value) {
    std::lock_guard<std::mutex> guard(mutex_);
    fail_moves_ = value;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable idle_;
  std::condition_variable barrier_;
  SportStateCallback callback_;
  std::vector<std::array<double, 3>> calls_;
  std::vector<std::int64_t> call_stamps_;
  int in_flight_ = 0;
  std::size_t blocked_in_flight_ = 0;
  bool fail_moves_ = false;
  bool stopped_ = false;
  bool blocked_ = false;
};

struct Shared {
  std::mutex mutex;
  std::shared_ptr<FakeBackend> backend;
};

// The telemetry source: a thread that publishes at a fixed rate until it is
// stopped. stop() joins the thread, so no delivery can start after the join.
class TelemetrySource {
public:
  TelemetrySource(std::shared_ptr<FakeBackend> backend, std::uint32_t first_tick)
      : backend_(std::move(backend)), tick_(first_tick) {}

  void start() {
    thread_ = std::thread([this] {
      while (running_.load()) {
        SportStateSnapshot snapshot;
        snapshot.has_tick = true;
        snapshot.tick = tick_++;
        snapshot.position = {1.0, 2.0, 3.0};
        snapshot.velocity = {0.1, 0.0, 0.0};
        snapshot.quaternion = {1.0, 0.0, 0.0, 0.0};
        if (!backend_->publish(snapshot)) {
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    });
  }

  void stop() {
    running_ = false;
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  ~TelemetrySource() { stop(); }

private:
  std::shared_ptr<FakeBackend> backend_;
  std::atomic<bool> running_{true};
  std::uint32_t tick_;
  std::thread thread_;
};

class Go2DriverFixture : public ::testing::Test {
protected:
  void SetUp() override {
    rclcpp::init(0, nullptr);
    // The clock-domain identity of this process. The driver compares it with
    // the identity of every command, so the fake transport publishes the
    // same one. A domain of another boot instance or time namespace is
    // delivered explicitly by the cases that need it.
    domain_ = runtime::driver_deadline::localClockDomainId();
    shared_ = std::make_shared<Shared>();
    const std::int64_t domain =
      rclcpp::contexts::get_global_default_context()->get_domain_id();
    rclcpp::NodeOptions options;
    options.parameter_overrides(
      {rclcpp::Parameter("asset_json", assetJson(domain))});
    node_ = std::make_shared<Go2DriverNode>(
      [this](SportStateCallback callback) {
        auto backend = std::make_shared<FakeBackend>(std::move(callback));
        std::lock_guard<std::mutex> guard(shared_->mutex);
        shared_->backend = backend;
        return backend;
      },
      options);
    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(node_);
    spin_ = std::thread([this] { executor_->spin(); });
    observations_ = std::make_shared<Observations>();
    odom_sub_ = node_->create_subscription<nav_msgs::msg::Odometry>(
      "/go2_fake/odom", rclcpp::QoS(10),
      [this](const nav_msgs::msg::Odometry & message) {
        observations_->add(message);
      });
  }

  void TearDown() override {
    // The telemetry source is stopped before the node is destroyed, so no
    // callback can reach a released core.
    if (telemetry_) {
      telemetry_->stop();
      telemetry_.reset();
    }
    if (cmd_thread_.joinable()) {
      cmd_running_ = false;
      cmd_thread_.join();
    }
    executor_->cancel();
    if (spin_.joinable()) {
      spin_.join();
    }
    node_.reset();
    executor_.reset();
    rclcpp::shutdown();
  }

  static std::string assetJson(std::int64_t domain) {
    return std::string(
      R"({"id":"go2_fake","model":"unitree-go2","kind":"quadruped",)"
      R"("mode":"real","namespace":"/go2_fake","ros_domain_id":)" +
      std::to_string(domain) +
      R"(,"frames":{"base":"base_link","odom":"odom","map":"map"},)"
      R"("capabilities":["state","motion.velocity","motion.stop"],)"
      R"("limits":{"linear_mps":1.0,"angular_rps":1.0,"command_timeout_s":5.0,)"
      R"("state_timeout_s":1.0,"lease_max_s":30.0,"action_timeout_s":120.0},)"
      R"("driver":{"plugin":"unitree_go2","options":{"odom_topic":"/go2_fake/odom",)"
      R"("cmd_vel_topic":"/go2_fake/cmd_vel","network_interface":"lo",)"
      R"("dds_domain_id":196,"sport_state_topic":"rt/sportmodestate",)"
      R"("velocity_frame":"body"}}})");
  }

  std::shared_ptr<FakeBackend> backend() {
    std::lock_guard<std::mutex> guard(shared_->mutex);
    return shared_->backend;
  }

  // Bounded, condition based wait.
  template <typename Predicate>
  bool waitFor(Predicate predicate, std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
      if (predicate()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
  }

  void startTelemetry() {
    telemetry_ = std::make_unique<TelemetrySource>(backend(), 1);
    telemetry_->start();
  }

  // The validity interval of a published command. It is shorter than the
  // driver cap (the asset allows 5 s) and far longer than the telemetry
  // timeout (1 s), so a zero output inside a case can never be confused with
  // a command expiry.
  static constexpr std::int64_t kCommandIntervalNs = 4000000000LL;

  // Publishes a velocity command over the internal transport.
  //
  // The issue time is an actual CLOCK_MONOTONIC reading and the deadline is
  // the absolute value that the transport delivers unchanged. The issue
  // watermark and the publish call share one lock, so the published order is
  // the issued order: the driver never receives an own command as a
  // backward one.
  void publishCommand(double vx) {
    publishCommandWith(vx, 0, 0, false, domain_);
  }

  // Publishes one internal command with an explicit transport payload.
  //
  // `issued` <= 0 asks for a fresh issue time, `deadline` <= 0 asks for the
  // default validity interval (a stop never carries a deadline). The caller
  // can pass an issue time and a deadline of its own to deliver a command
  // whose interval is already consumed, or a clock domain that does not
  // match the driver.
  std::int64_t publishCommandWith(double vx, std::int64_t issued,
                                   std::int64_t deadline, bool stop,
                                   const std::string & domain) {
    std::lock_guard<std::mutex> guard(publish_mutex_);
    if (issued <= 0) {
      issued = nextIssueLocked();
    }
    if (deadline <= 0 && !stop) {
      deadline = issued + kCommandIntervalNs;
    }
    last_issued_ = std::max(last_issued_, issued);
    chaos_interfaces::msg::DriverVelocityCommand command;
    command.header.frame_id = "base_link";
    // The original source stamp of the admitted command. It is provenance
    // only and it never extends the deadline.
    command.header.stamp = node_->get_clock()->now();
    command.clock_domain_id = domain;
    command.issued_monotonic_ns = issued;
    command.expires_monotonic_ns = deadline;
    command.stop = stop;
    command.velocity.linear.x = stop ? 0.0 : vx;
    ensureCommandPublisher();
    cmd_pub_->publish(command);
    return issued;
  }

  // Establish discovery before sampling a deliberately short interval.
  void ensureCommandPublisher() {
    if (!cmd_pub_) {
      cmd_pub_ =
        node_->create_publisher<chaos_interfaces::msg::DriverVelocityCommand>(
          "/go2_fake/cmd_vel", rclcpp::QoS(10));
      const auto discovery_deadline = Clock::now() + std::chrono::seconds(5);
      while (cmd_pub_->get_subscription_count() == 0 &&
             Clock::now() < discovery_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
  }

  // The issue time of the next command. It is an actual CLOCK_MONOTONIC
  // reading: the transport barrier of the driver requires the issue time to
  // rise strictly, so the helper waits for the clock to advance instead of
  // fabricating a successor value. The caller must hold the publish lock.
  std::int64_t nextIssueLocked() {
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    std::int64_t issued = runtime::driver_deadline::monotonicNowNs();
    while (issued <= last_issued_ && Clock::now() < deadline) {
      issued = runtime::driver_deadline::monotonicNowNs();
    }
    return issued;
  }

  // Publishes an explicit stop barrier: six exact zeros, a current issue
  // time and no deadline.
  std::int64_t publishStop() {
    return publishCommandWith(0.0, 0, 0, true, domain_);
  }

  // Publishes the command until the driver actually writes it. A command
  // that arrives before the first fresh telemetry is rejected by the core
  // (it requires fresh telemetry), so the command is republished until the
  // driver accepts it. The wait is bounded.
  //
  // `since` restricts the search to the writes that happened after the
  // marker, so a test that must observe a *new* write is not satisfied by an
  // older one that happens to carry the same value.
  bool publishUntilWritten(double vx, std::chrono::milliseconds timeout,
                           std::size_t since = 0) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
      publishCommand(vx);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      const auto calls = backend()->calls();
      for (std::size_t i = since; i < calls.size(); ++i) {
        if (std::abs(calls[i][0] - vx) < 1e-9) {
          return true;
        }
      }
    }
    return false;
  }

  // A command source that keeps a command valid: the command TTL is longer
  // than the telemetry timeout, so a zero output caused by the telemetry
  // expiry cannot be confused with a command expiry.
  void startCommandSource(double vx) {
    cmd_running_ = true;
    cmd_thread_ = std::thread([this, vx] {
      while (cmd_running_.load()) {
        publishCommand(vx);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
    });
  }

  bool sawNonzeroAfter(std::size_t index) {
    const auto calls = backend()->calls();
    for (std::size_t i = index; i < calls.size(); ++i) {
      if (std::abs(calls[i][0]) > 1e-9) {
        return true;
      }
    }
    return false;
  }

  bool sawValueAfter(std::size_t index, double vx) {
    const auto calls = backend()->calls();
    for (std::size_t i = index; i < calls.size(); ++i) {
      if (std::abs(calls[i][0] - vx) < 1e-9) {
        return true;
      }
    }
    return false;
  }

  bool sawZeroAfter(std::size_t index) {
    const auto calls = backend()->calls();
    for (std::size_t i = index; i < calls.size(); ++i) {
      if (std::abs(calls[i][0]) < 1e-9) {
        return true;
      }
    }
    return false;
  }

  // True when an odometry sample with the given source position was
  // observed. The source position is carried into the odometry unchanged, so
  // it identifies the sample that produced it.
  bool sawPosition(double x) {
    for (const auto & message : observations_->snapshot()) {
      if (std::abs(message.pose.pose.position.x - x) < 1e-6) {
        return true;
      }
    }
    return false;
  }

  // Waits until the odometry publication is quiet: the count must not grow
  // over a settling window. Used after the telemetry source was stopped, to
  // make sure that only the samples injected by the test can still arrive.
  bool waitQuiet(std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    return waitFor(
      [&] {
        const auto first = observations_->size();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return observations_->size() == first;
      },
      timeout);
  }

  std::shared_ptr<Shared> shared_;
  std::shared_ptr<Go2DriverNode> node_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::thread spin_;
  std::unique_ptr<TelemetrySource> telemetry_;
  std::shared_ptr<Observations> observations_;
  // The subscription and the publisher are members: a temporary would be
  // destroyed and the endpoint would disappear from the graph.
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Publisher<chaos_interfaces::msg::DriverVelocityCommand>::SharedPtr
    cmd_pub_;
  std::thread cmd_thread_;
  std::atomic<bool> cmd_running_{false};
  // The clock domain of this process and the watermark of the issued
  // commands. The publish lock keeps the watermark and the published order
  // consistent, also when the command source thread and the test thread
  // publish at the same time.
  std::string domain_;
  std::mutex publish_mutex_;
  std::int64_t last_issued_ = 0;
};

TEST_F(Go2DriverFixture, FakeBackendDeliversTelemetryAndObservesMoveCalls) {
  auto fake = backend();
  ASSERT_TRUE(fake != nullptr);

  // Without telemetry the driver must not move the robot: it writes no
  // nonzero output at all. It may legitimately write nothing, because the
  // core is not initialised before the first fresh sample.
  publishCommand(0.25);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_FALSE(sawNonzeroAfter(0))
    << "the driver wrote a nonzero output without fresh telemetry";

  // Fresh telemetry makes the driver ready; the command is then written.
  startTelemetry();
  ASSERT_TRUE(publishUntilWritten(0.25, std::chrono::seconds(5)));

  // The odometry carries the telemetry source values, and the publication
  // is not restamped: the stamp is the acquisition time of the sample.
  ASSERT_TRUE(waitFor([&] { return observations_->size() > 0; },
                       std::chrono::seconds(5)));
  const auto odometry = observations_->snapshot();
  const auto & odom = odometry.back();
  EXPECT_NEAR(odom.pose.pose.position.x, 1.0, 1e-6);
  EXPECT_NEAR(odom.pose.pose.position.y, 2.0, 1e-6);
  EXPECT_NEAR(odom.pose.pose.position.z, 3.0, 1e-6);
  EXPECT_GT(odom.header.stamp.sec, 0);
}

TEST_F(Go2DriverFixture, RepeatedSourceIdentityDoesNotRefreshTheOdometry) {
  auto fake = backend();
  ASSERT_TRUE(fake != nullptr);
  startTelemetry();
  // The command source keeps a valid command alive (the command TTL is 5 s,
  // the state timeout is 1 s), so a zero output inside this test can only
  // come from the telemetry expiry and never from a command expiry.
  startCommandSource(0.25);
  ASSERT_TRUE(publishUntilWritten(0.25, std::chrono::seconds(5)));
  ASSERT_TRUE(waitFor([&] { return observations_->size() > 0; },
                       std::chrono::seconds(5)));

  // The background source is stopped first, so the only samples that can
  // arrive are the ones the test injects.
  telemetry_->stop();
  ASSERT_TRUE(waitQuiet())
    << "the odometry kept being published after the telemetry stopped";

  // A new source identity is accepted and produces an odometry sample.
  const std::uint32_t fresh_tick = 4242;
  SportStateSnapshot fresh;
  fresh.has_tick = true;
  fresh.tick = fresh_tick;
  fresh.position = {7.0, 7.0, 7.0};
  fresh.quaternion = {1.0, 0.0, 0.0, 0.0};
  fake->publish(fresh);
  ASSERT_TRUE(waitFor([&] { return sawPosition(7.0); },
                       std::chrono::seconds(5)))
    << "a new source identity did not produce an odometry sample";

  // The same source identity again must not refresh the sample: the core
  // rejects a repeated source counter, because a DDS participant that keeps
  // replaying the same counter must not be able to keep a dead publisher
  // alive. The payload differs on purpose, so an accidental acceptance would
  // publish a second, distinguishable odometry sample.
  const auto before_duplicate = fake->callCount();
  SportStateSnapshot duplicate;
  duplicate.has_tick = true;
  duplicate.tick = fresh_tick;
  duplicate.position = {8.0, 8.0, 8.0};
  duplicate.quaternion = {1.0, 0.0, 0.0, 0.0};
  fake->publish(duplicate);

  // The repeated identity must not refresh the telemetry age either: the
  // state expires 1 s after the last accepted sample and the driver then
  // replaces the nonzero output with a zero, although the command source
  // keeps publishing fresh commands.
  ASSERT_TRUE(waitFor([&] { return sawZeroAfter(before_duplicate); },
                       std::chrono::seconds(5)))
    << "a repeated source identity kept the motion alive";
  EXPECT_NE(node_->driverStatus().detail.find("telemetry"), std::string::npos)
    << "unexpected watchdog reason: " << node_->driverStatus().detail;
  // The command source keeps publishing fresh commands, so the zero output
  // cannot come from a command expiry.
  EXPECT_EQ(node_->driverStatus().detail.find("expired"), std::string::npos)
    << "the watchdog reported a command expiry: "
    << node_->driverStatus().detail;

  // A newer identity resumes the publication: the identity rule is not a
  // latched fault. The odometry comes from a single publisher with ordered
  // delivery and the duplicate was rejected synchronously, so the newer
  // sample can only be observed after a sample of the rejected duplicate
  // would have been observed.
  SportStateSnapshot newer;
  newer.has_tick = true;
  newer.tick = fresh_tick + 1;
  newer.position = {9.0, 9.0, 9.0};
  newer.quaternion = {1.0, 0.0, 0.0, 0.0};
  fake->publish(newer);
  ASSERT_TRUE(waitFor([&] { return sawPosition(9.0); },
                       std::chrono::seconds(5)))
    << "a newer source identity did not resume the odometry publication";

  // The rejected duplicate never reached the odometry.
  EXPECT_FALSE(sawPosition(8.0))
    << "a repeated source identity refreshed the odometry";
  const auto resumed = observations_->snapshot();
  EXPECT_NEAR(resumed.back().pose.pose.position.x, 9.0, 1e-6);
}

TEST_F(Go2DriverFixture, StaleTelemetryStopsTheMotionWhileTheCommandStaysValid) {
  auto fake = backend();
  ASSERT_TRUE(fake != nullptr);
  startTelemetry();
  ASSERT_TRUE(publishUntilWritten(0.25, std::chrono::seconds(5)));
  // The command source keeps a valid command alive: the command TTL is 5 s
  // and the telemetry timeout is 1 s, so a zero output inside this window
  // can only come from the telemetry expiry.
  startCommandSource(0.25);

  const auto command_count = fake->callCount();
  telemetry_->stop();
  ASSERT_TRUE(waitFor(
    [&] { return sawZeroAfter(command_count); },
    std::chrono::seconds(3)))
    << "the driver kept a nonzero output after the telemetry expired";
  // The watchdog reason must name the missing telemetry, not an expired
  // command.
  EXPECT_NE(node_->driverStatus().detail.find("telemetry"),
            std::string::npos)
    << "unexpected watchdog reason: " << node_->driverStatus().detail;
  // The command source is still publishing: the zero output is not a
  // command expiry.
  const auto after = fake->callCount();
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_GT(fake->callCount(), after)
    << "the command source stopped publishing";
}

TEST_F(Go2DriverFixture, IllegalTelemetryStopsTheMotionAndDoesNotLatch) {
  auto fake = backend();
  ASSERT_TRUE(fake != nullptr);
  startTelemetry();
  ASSERT_TRUE(publishUntilWritten(0.25, std::chrono::seconds(5)));

  // The background source is stopped before the illegal sample: otherwise
  // the next regular sample would legitimately restore the motion and the
  // stop under test would not be observable at all.
  telemetry_->stop();
  ASSERT_TRUE(waitQuiet())
    << "the odometry kept being published after the telemetry stopped";

  // An illegal sample (an all-zero quaternion is not a unit quaternion)
  // is rejected by the core: the sample is reset and the motion stops. The
  // core does not latch a permanent fault for an illegal sample, so a
  // legal sample restores the motion.
  const auto before = fake->callCount();
  SportStateSnapshot illegal;
  illegal.has_tick = true;
  illegal.tick = 9999;
  illegal.quaternion = {0.0, 0.0, 0.0, 0.0};
  fake->publish(illegal);

  ASSERT_TRUE(waitFor([&] { return sawZeroAfter(before); },
                       std::chrono::seconds(3)))
    << "the driver kept moving after an illegal telemetry sample";
  // An illegal sample is not an unrecoverable fault: the driver may recover
  // on the next legal sample.
  EXPECT_TRUE(node_->driverStatus().fault.empty())
    << "an illegal telemetry sample latched a permanent fault: "
    << node_->driverStatus().fault;

  // A fresh command must not bypass the stop: the telemetry is not fresh, so
  // the command is refused and no nonzero output is written.
  const auto after_stop = fake->callCount();
  publishCommand(0.25);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_FALSE(sawNonzeroAfter(after_stop))
    << "a fresh command bypassed the stop caused by the illegal sample";

  // A legal sample restores the motion. The marker makes sure that a *new*
  // write is observed: the write from before the illegal sample must not
  // satisfy this assertion.
  const auto before_legal = fake->callCount();
  SportStateSnapshot legal;
  legal.has_tick = true;
  legal.tick = 10000;
  legal.position = {1.0, 2.0, 3.0};
  legal.quaternion = {1.0, 0.0, 0.0, 0.0};
  fake->publish(legal);
  ASSERT_TRUE(publishUntilWritten(0.25, std::chrono::seconds(5),
                                  before_legal))
    << "a legal sample did not restore the motion";
}

TEST_F(Go2DriverFixture, SdkWriteFaultStopsTheMotion) {
  auto fake = backend();
  ASSERT_TRUE(fake != nullptr);
  startTelemetry();
  ASSERT_TRUE(publishUntilWritten(0.25, std::chrono::seconds(5)));

  // A failing SDK write latches a fault: the driver must not keep a
  // nonzero output.
  fake->setFailMoves(true);
  const auto before = fake->callCount();
  publishCommand(0.25);
  ASSERT_TRUE(waitFor([&] { return sawZeroAfter(before); },
                       std::chrono::seconds(5)));
  EXPECT_FALSE(node_->driverStatus().fault.empty())
    << "a failed SDK write did not latch a fault";
}

TEST_F(Go2DriverFixture, ShutdownWaitsForAnInFlightTelemetryCallback) {
  auto fake = backend();
  ASSERT_TRUE(fake != nullptr);
  startTelemetry();
  ASSERT_TRUE(publishUntilWritten(0.25, std::chrono::seconds(5)));

  // The background source is stopped, so the only delivery that can be held
  // at the barrier is the one the test injects.
  telemetry_->stop();

  // Hold a telemetry callback in flight on a barrier. The shutdown must
  // wait for it: the callback must never reach a released core.
  fake->blockDeliveries();
  SportStateSnapshot held;
  held.has_tick = true;
  held.tick = 777;
  held.quaternion = {1.0, 0.0, 0.0, 0.0};
  std::thread delivery([&] { fake->publish(held); });

  // The delivery must really be held at the barrier before the shutdown
  // starts: a delivery that is refused by stop() would leave the shutdown
  // with nothing to wait for.
  ASSERT_TRUE(waitFor([&] { return fake->blockedDeliveries() > 0; },
                       std::chrono::seconds(5)))
    << "the telemetry delivery never reached the barrier";

  std::atomic<bool> closed{false};
  std::thread closer([&] {
    node_->close();
    closed = true;
  });

  // The callback is still blocked, so close() must not have returned.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  EXPECT_FALSE(closed.load())
    << "close() returned while a telemetry callback was still in flight";

  fake->releaseDeliveries();
  delivery.join();
  ASSERT_TRUE(waitFor([&] { return closed.load(); },
                       std::chrono::seconds(10)));
  closer.join();
  EXPECT_TRUE(fake->stopped())
    << "the telemetry source was not stopped before the core was released";
  const auto calls = fake->calls();
  ASSERT_FALSE(calls.empty());
  EXPECT_DOUBLE_EQ(calls.back()[0], 0.0)
    << "the last output was not a zero";
}

TEST_F(Go2DriverFixture, ACommandThatArrivesAfterItsDeadlineIsRefused) {
  auto fake = backend();
  ASSERT_TRUE(fake != nullptr);
  startTelemetry();
  ASSERT_TRUE(publishUntilWritten(0.25, std::chrono::seconds(5)));

  // The interval of the command was already consumed on the way: it was
  // issued 200 ms ago and expires 100 ms ago, so it arrives after its
  // deadline. The driver must refuse it instead of running the motion.
  // The refused command uses its own velocity, so it cannot be confused
  // with the motion that the driver still holds.
  const auto before = fake->callCount();
  const std::int64_t now = runtime::driver_deadline::monotonicNowNs();
  publishCommandWith(0.75, now - 200000000LL, now - 100000000LL, false,
                     domain_);

  ASSERT_TRUE(waitFor(
    [&] {
      const auto detail = node_->driverStatus().detail;
      return detail.find("deadline") != std::string::npos;
    },
    std::chrono::seconds(3)))
    << "the driver did not report the expired deadline: "
    << node_->driverStatus().detail;
  EXPECT_FALSE(sawValueAfter(before, 0.75))
    << "the driver wrote a command that had already expired";
}

TEST_F(Go2DriverFixture, ACommandWithMostOfItsIntervalConsumedNeverExtendsIt) {
  auto fake = backend();
  ASSERT_TRUE(fake != nullptr);
  startTelemetry();

  // The readiness barrier: the driver must have written its initial zero
  // output and the telemetry must be fresh. Without it, a missing nonzero
  // write could not be told apart from an unready driver.
  ASSERT_TRUE(waitFor(
    [&] {
      const auto status = node_->driverStatus();
      return status.initialized && !status.stop_pending;
    },
    std::chrono::seconds(5)))
    << "the driver never became ready";

  ensureCommandPublisher();
  ASSERT_GT(cmd_pub_->get_subscription_count(), 0u);

  // 80 ms of the 100 ms interval are already consumed. The transit is
  // simulated by the issue time and the deadline, not by a sleep: the
  // command was issued 80 ms ago and expires 20 ms from now.
  const std::int64_t anchor = runtime::driver_deadline::monotonicNowNs();
  const std::int64_t issued = anchor - 80000000LL;
  const std::int64_t deadline = anchor + 20000000LL;
  const auto before = fake->callCount();
  publishCommandWith(0.25, issued, deadline, false, domain_);

  // Prove this packet was processed, even if scheduling consumed its remainder.
  ASSERT_TRUE(waitFor([&] {
    const auto status = node_->driverStatus();
    return status.last_command_issued_mono_ns == issued ||
           status.detail.find("deadline has already passed") != std::string::npos;
  }, std::chrono::seconds(3)));

  // Wait until the delivered deadline has passed and the watchdog ran
  // again.
  while (runtime::driver_deadline::monotonicNowNs() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  const auto calls = fake->calls();
  const auto stamps = fake->callStamps();
  ASSERT_FALSE(calls.empty());
  // The driver never extends the delivered deadline: every nonzero write
  // happened strictly before it.
  for (std::size_t i = before; i < calls.size(); ++i) {
    if (std::abs(calls[i][0]) > 1e-9) {
      EXPECT_LT(stamps[i], deadline)
        << "the driver wrote a nonzero output at or after the delivered "
           "deadline";
    }
  }
  // The last output is a zero: the motion did not survive the deadline. A
  // scheduling delay may have let the remaining 20 ms elapse before the
  // watchdog ran, in which case the driver failed closed and never wrote a
  // nonzero output at all. Both outcomes are allowed, and neither of them
  // extends the deadline.
  EXPECT_DOUBLE_EQ(calls.back()[0], 0.0)
    << "the driver kept a nonzero output after the delivered deadline";
}

class InvalidGo2Domain : public Go2DriverFixture,
                         public ::testing::WithParamInterface<std::string> {};

TEST_P(InvalidGo2Domain, ACommandOfAnotherClockDomainIsRefused) {
  auto fake = backend();
  ASSERT_TRUE(fake != nullptr);
  startTelemetry();
  ASSERT_TRUE(publishUntilWritten(0.25, std::chrono::seconds(5)));

  const auto before = fake->callCount();
  // A domain of another boot instance or time namespace: the issue time and
  // the deadline of that command belong to a different clock and cannot be
  // compared with the local ones. The refused commands use their own
  // velocity, so they cannot be confused with the motion that the driver
  // still holds.
  publishCommandWith(0.75, 0, 0, false, GetParam());

  ASSERT_TRUE(waitFor(
    [&] {
      const auto detail = node_->driverStatus().detail;
      return detail.find("domain") != std::string::npos;
    },
    std::chrono::seconds(3)))
    << "the driver did not report the mismatched clock domain: "
    << node_->driverStatus().detail;
  EXPECT_FALSE(sawValueAfter(before, 0.75))
    << "the driver wrote a command of a foreign clock domain";
}

INSTANTIATE_TEST_SUITE_P(
  InvalidDomains, InvalidGo2Domain,
  ::testing::Values(std::string("v1:00000000-0000-0000-0000-000000000000:1:2"),
                    std::string("")));

TEST_F(Go2DriverFixture, AnExplicitStopRefusesAnOlderIssuedMotionCommand) {
  auto fake = backend();
  ASSERT_TRUE(fake != nullptr);
  startTelemetry();
  ASSERT_TRUE(publishUntilWritten(0.25, std::chrono::seconds(5)));

  // The explicit stop: six exact zeros, a current issue time and no
  // deadline.
  const auto before_stop = fake->callCount();
  const std::int64_t stop_issue = publishStop();

  // The driver stops: the next output is a zero.
  ASSERT_TRUE(waitFor([&] { return sawZeroAfter(before_stop); },
                       std::chrono::seconds(3)))
    << "the driver did not stop on the explicit stop command";

  // A motion command that was issued before the stop must not revive the
  // motion: the barrier of the stop is higher than its issue time. The
  // replayed command uses its own velocity, so it cannot be confused with
  // the motion that the driver held before the stop.
  const auto after_stop = fake->callCount();
  publishCommandWith(0.75, stop_issue - 1000000LL,
                     stop_issue + 4000000000LL, false, domain_);

  ASSERT_TRUE(waitFor(
    [&] {
      const auto detail = node_->driverStatus().detail;
      return detail.find("issue time") != std::string::npos;
    },
    std::chrono::seconds(3)))
    << "the driver did not report the backward issue time: "
    << node_->driverStatus().detail;
  EXPECT_FALSE(sawValueAfter(after_stop, 0.75))
    << "an old motion command revived the motion after the explicit stop";
}

}  // namespace
}  // namespace chaos::go2
