#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "chaos_runtime/humanoid/driver_core.hpp"
#include "chaos_runtime/humanoid/process_guard.hpp"
#include "chaos_runtime/humanoid/telemetry_gate.hpp"

namespace chaos::humanoid {
namespace {

constexpr char kDomain[] = "v1:0123abcd-89ef-4abc-8def-0123456789ab:1:2";
constexpr std::int64_t kMs = 1000000;
const std::array<double, 3> kZero{0.0, 0.0, 0.0};
const std::array<double, 3> kMove{0.2, 0.0, 0.1};

struct Clock {
  double steady_s = 10.0;
  std::int64_t ros_ns = 100000000000LL;
  std::int64_t mono_ns = 10000000000LL;
};

class FakeBackend : public HumanoidBackend {
public:
  int move(double vx, double vy, double wz) override {
    std::function<void()> hook;
    int result = 0;
    {
      std::lock_guard<std::mutex> guard(mu_);
      calls.push_back({vx, vy, wz});
      hook = on_move;
      if (fail_next) {
        --fail_next;
        result = -1;
      }
    }
    if (hook) {
      hook();
    }
    return result;
  }
  void stop() override {
    std::lock_guard<std::mutex> guard(mu_);
    ++stop_calls;
  }

  std::vector<std::array<double, 3>> calls;
  std::function<void()> on_move;
  int fail_next = 0;
  int stop_calls = 0;

private:
  std::mutex mu_;
};

struct Harness {
  Harness() : backend(std::make_shared<FakeBackend>()) {
    DriverConfig config;
    config.base_frame = "base_link";
    config.odom_frame = "odom";
    config.clock_domain_id = kDomain;
    config.limits.linear_mps = 1.0;
    config.limits.angular_rps = 1.0;
    config.limits.command_timeout_s = 0.5;
    config.limits.state_timeout_s = 1.0;
    core = std::make_unique<HumanoidDriverCore>(config, backend,
      [this] { return clock.steady_s; }, [this] { return clock.ros_ns; },
      [this] { return clock.mono_ns; });
  }

  Odometry odometry(std::int64_t source_ns = 0) const {
    const std::int64_t source = source_ns == 0 ? clock.ros_ns : source_ns;
    Odometry message;
    message.stamp = Stamp{source / 1000000000LL, source % 1000000000LL};
    message.frame_id = "odom";
    message.child_frame_id = "base_link";
    message.position = {1.0, 2.0, 0.0};
    message.twist = {0.1, 0.0, 0.0, 0.0, 0.0, 0.2};
    return message;
  }

  VelocityCommand command(std::array<double, 6> values = {0.2, 0.0, 0.0, 0.0, 0.0, 0.1},
                          std::int64_t issued_offset_ns = 0) const {
    VelocityCommand message;
    message.stamp = Stamp{clock.ros_ns / 1000000000LL,
                          clock.ros_ns % 1000000000LL};
    message.frame_id = "base_link";
    message.values = values;
    message.clock_domain_id = kDomain;
    message.issued_monotonic_ns = clock.mono_ns + issued_offset_ns;
    message.expires_monotonic_ns = message.issued_monotonic_ns + 100 * kMs;
    return message;
  }

  VelocityCommand stop() const {
    auto message = command({0.0, 0.0, 0.0, 0.0, 0.0, 0.0});
    message.stamp = Stamp{};
    message.expires_monotonic_ns = 0;
    message.stop = true;
    return message;
  }

  void ready() {
    ASSERT_TRUE(core->acceptOdometry(odometry()));
    ASSERT_TRUE(core->acceptMotionMode(true));
    ASSERT_TRUE(core->step().has_value());
    ASSERT_EQ(backend->calls.back(), kZero);
    ASSERT_TRUE(core->status().initialized);
  }

  Clock clock;
  std::shared_ptr<FakeBackend> backend;
  std::unique_ptr<HumanoidDriverCore> core;
};

TEST(HumanoidCore, MotionNeedsModeAndSuccessfulInitialZero) {
  Harness h;
  ASSERT_TRUE(h.core->acceptOdometry(h.odometry()));
  EXPECT_FALSE(h.core->acceptCommand(h.command()));
  h.core->step();
  EXPECT_TRUE(h.backend->calls.empty());
  ASSERT_TRUE(h.core->acceptMotionMode(false));
  h.core->step();
  EXPECT_TRUE(h.backend->calls.empty());
  ASSERT_TRUE(h.core->acceptMotionMode(true));
  h.backend->fail_next = 1;
  h.core->step();
  EXPECT_TRUE(h.core->status().fault.empty());
  EXPECT_FALSE(h.core->acceptCommand(h.command()));
  h.core->step();
  ASSERT_TRUE(h.core->status().initialized);
  EXPECT_EQ(h.backend->calls.back(), kZero);
  // There is deliberately no backend mode-transition method to invoke.
  EXPECT_TRUE(h.core->acceptCommand(h.command()));
}

TEST(HumanoidCore, DeadlineExpiryStopReplayAndRecovery) {
  Harness h;
  h.ready();
  const auto first = h.command();
  ASSERT_TRUE(h.core->acceptCommand(first));
  h.core->step();
  ASSERT_EQ(h.backend->calls.back(), kMove);

  h.clock.mono_ns = first.expires_monotonic_ns;
  h.core->step();
  EXPECT_EQ(h.backend->calls.back(), kZero);
  EXPECT_FALSE(h.core->status().has_command);
  EXPECT_FALSE(h.core->acceptCommand(first));  // no expired/replayed revival

  h.core->step();  // finish the zero barrier caused by the rejected replay
  ++h.clock.mono_ns;
  auto second = h.command();
  second.expires_monotonic_ns = second.issued_monotonic_ns + 100 * kMs;
  ASSERT_TRUE(h.core->acceptCommand(second));
  h.core->step();
  EXPECT_EQ(h.backend->calls.back(), kMove);

  ++h.clock.mono_ns;
  const auto stop = h.stop();
  ASSERT_TRUE(h.core->acceptCommand(stop));
  h.core->step();
  EXPECT_EQ(h.backend->calls.back(), kZero);
  EXPECT_FALSE(h.core->acceptCommand(second));
  h.core->step();
  ++h.clock.mono_ns;
  auto third = h.command();
  third.expires_monotonic_ns = third.issued_monotonic_ns + 100 * kMs;
  EXPECT_TRUE(h.core->acceptCommand(third));
}

TEST(HumanoidCore, NonwalkingAndStaleModeStopWithoutLatching) {
  Harness h;
  h.ready();
  ASSERT_TRUE(h.core->acceptCommand(h.command()));
  h.core->step();
  EXPECT_TRUE(h.core->acceptMotionMode(false));
  h.core->step();
  EXPECT_EQ(h.backend->calls.back(), kZero);
  EXPECT_FALSE(h.core->acceptCommand(h.command()));
  ASSERT_TRUE(h.core->acceptMotionMode(true));
  h.core->step();  // successful zero re-arms admission
  ++h.clock.mono_ns;
  EXPECT_TRUE(h.core->acceptCommand(h.command()));

  h.clock.steady_s += 1.1;
  h.core->step();
  EXPECT_EQ(h.backend->calls.back(), kZero);
  EXPECT_FALSE(h.core->status().has_command);
  EXPECT_TRUE(h.core->status().fault.empty());
}

TEST(HumanoidCore, SourceStampFramesAndNumbersAreValidated) {
  Harness h;
  auto valid = h.odometry();
  ASSERT_TRUE(h.core->acceptOdometry(valid));
  EXPECT_FALSE(h.core->acceptOdometry(valid));  // equal stamp cannot refresh
  auto backward = h.odometry(h.clock.ros_ns - 1);
  EXPECT_FALSE(h.core->acceptOdometry(backward));
  auto wrong_frame = h.odometry(h.clock.ros_ns + 1);
  wrong_frame.child_frame_id = "other";
  EXPECT_FALSE(h.core->acceptOdometry(wrong_frame));
  EXPECT_FALSE(h.core->status().has_odometry);
  auto nan = h.odometry(h.clock.ros_ns + 2);
  nan.position[0] = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(h.core->acceptOdometry(nan));
  auto future = h.odometry(h.clock.ros_ns + 200 * kMs);
  EXPECT_FALSE(h.core->acceptOdometry(future));
  auto stale = h.odometry(h.clock.ros_ns - 1001 * kMs);
  EXPECT_FALSE(h.core->acceptOdometry(stale));
  auto newer = h.odometry(h.clock.ros_ns + 3);
  EXPECT_TRUE(h.core->acceptOdometry(newer));
  EXPECT_EQ(h.core->sample()->source_stamp_ns, h.clock.ros_ns + 3);
}

TEST(HumanoidCore, OutputTelemetryRequiresReadyModeAndZeroHandshake) {
  Harness h;
  ASSERT_TRUE(h.core->acceptOdometry(h.odometry()));
  ASSERT_TRUE(h.core->acceptMotionMode(true));
  h.backend->fail_next = 1;
  EXPECT_FALSE(h.core->step().has_value());
  EXPECT_TRUE(h.core->step().has_value());
  h.clock.ros_ns += kMs;
  ASSERT_TRUE(h.core->acceptOdometry(h.odometry()));
  ASSERT_TRUE(h.core->acceptMotionMode(false));
  EXPECT_FALSE(h.core->step().has_value());
  ASSERT_TRUE(h.core->acceptMotionMode(true));
  EXPECT_TRUE(h.core->step().has_value());
}

TEST(HumanoidCore, SourceFrameNamesMapToAssetNamesWithoutChangingPose) {
  Clock clock;
  auto backend = std::make_shared<FakeBackend>();
  DriverConfig config;
  config.base_frame = "robot_002/base_link";
  config.odom_frame = "robot_002/odom";
  config.source_base_frame = "body";
  config.source_odom_frame = "vendor_odom";
  config.clock_domain_id = kDomain;
  HumanoidDriverCore core(config, backend, [&] { return clock.steady_s; },
    [&] { return clock.ros_ns; }, [&] { return clock.mono_ns; });
  Odometry data;
  data.stamp = {100, 0};
  data.frame_id = "vendor_odom";
  data.child_frame_id = "body";
  data.position = {1.0, 2.0, 3.0};
  ASSERT_TRUE(core.acceptOdometry(data));
  ASSERT_TRUE(core.acceptMotionMode(true));
  auto sample = core.step();
  ASSERT_TRUE(sample.has_value());
  EXPECT_EQ(sample->position, data.position);
  EXPECT_EQ(core.baseFrame(), "robot_002/base_link");
  EXPECT_EQ(core.odomFrame(), "robot_002/odom");
}

TEST(HumanoidCore, DestructionQuiescesTelemetryWithoutExplicitClose) {
  auto backend = std::make_shared<FakeBackend>();
  {
    HumanoidDriverCore core(DriverConfig{}, backend);
  }
  EXPECT_EQ(backend->stop_calls, 1);
  EXPECT_TRUE(backend->calls.empty());
}

TEST(HumanoidCore, UnsupportedAxesAndSpeedAreRejectedBeforeOutput) {
  Harness h;
  h.ready();
  EXPECT_FALSE(h.core->acceptCommand(h.command({0.0, 0.0, 0.1, 0.0, 0.0, 0.0})));
  h.core->step();
  EXPECT_FALSE(h.core->acceptCommand(h.command({0.0, 0.0, 0.0, 0.1, 0.0, 0.0})));
  h.core->step();
  EXPECT_FALSE(h.core->acceptCommand(h.command({2.0, 0.0, 0.0, 0.0, 0.0, 0.0})));
  for (const auto & call : h.backend->calls) {
    EXPECT_EQ(call, kZero);
  }
}

TEST(HumanoidCore, BackendFailureAfterHandshakeLatches) {
  Harness h;
  h.ready();
  h.backend->fail_next = 1;
  ASSERT_TRUE(h.core->acceptCommand(h.command()));
  h.core->step();
  EXPECT_FALSE(h.core->status().fault.empty());
  EXPECT_FALSE(h.core->acceptCommand(h.command()));
  h.core->step();
  EXPECT_EQ(h.backend->calls.back(), kZero);
  EXPECT_FALSE(h.core->status().fault.empty());
}

TEST(HumanoidCore, BackendExceptionsRetryInitialZeroAndLatchAfterHandshake) {
  for (const bool unknown : {false, true}) {
    SCOPED_TRACE(unknown ? "unknown exception" : "standard exception");
    Harness h;
    const auto throw_on_move = [unknown] {
      if (unknown) throw 7;
      throw std::runtime_error("transport failed");
    };
    h.backend->on_move = throw_on_move;
    ASSERT_TRUE(h.core->acceptOdometry(h.odometry()));
    ASSERT_TRUE(h.core->acceptMotionMode(true));
    EXPECT_NO_THROW(h.core->step());
    EXPECT_FALSE(h.core->status().initialized);
    EXPECT_TRUE(h.core->status().fault.empty());
    EXPECT_TRUE(h.core->status().stop_pending);

    h.backend->on_move = {};
    ASSERT_TRUE(h.core->step().has_value());
    ASSERT_TRUE(h.core->acceptCommand(h.command()));
    h.backend->on_move = throw_on_move;
    EXPECT_NO_THROW(h.core->step());
    EXPECT_FALSE(h.core->status().fault.empty());
    EXPECT_FALSE(h.core->status().has_command);

    h.backend->on_move = {};
    h.core->step();
    EXPECT_EQ(h.backend->calls.back(), kZero);
    ++h.clock.mono_ns;
    EXPECT_FALSE(h.core->acceptCommand(h.command()));
  }
}

TEST(HumanoidCore, BackendCallRunsOutsideTelemetryCacheLock) {
  Harness h;
  h.ready();
  h.backend->on_move = [&] { EXPECT_TRUE(h.core->acceptMotionMode(true)); };
  ASSERT_TRUE(h.core->acceptCommand(h.command()));
  h.core->step();
  EXPECT_EQ(h.backend->calls.back(), kMove);
  h.backend->on_move = {};
}

TEST(HumanoidCore, ConcurrentStepCannotOverwriteShutdownZero) {
  Harness h;
  h.ready();
  ASSERT_TRUE(h.core->acceptCommand(h.command()));
  std::atomic<bool> run{true};
  std::thread ticker([&] {
    while (run.load()) {
      h.core->step();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  h.core->close();
  run.store(false);
  ticker.join();
  h.core->step();
  ASSERT_FALSE(h.backend->calls.empty());
  EXPECT_EQ(h.backend->calls.back(), kZero);
  EXPECT_EQ(h.backend->stop_calls, 1);
  EXPECT_FALSE(h.core->status().has_command);
}

TEST(HumanoidCore, CloseStopsOwnedOutputThenTelemetryAndIsIdempotent) {
  Harness unowned;
  unowned.core->close();
  EXPECT_TRUE(unowned.backend->calls.empty());
  EXPECT_EQ(unowned.backend->stop_calls, 1);

  Harness h;
  h.ready();
  ASSERT_TRUE(h.core->acceptCommand(h.command()));
  h.core->step();
  h.core->close();
  const auto count = h.backend->calls.size();
  EXPECT_EQ(h.backend->calls.back(), kZero);
  EXPECT_EQ(h.backend->stop_calls, 1);
  h.core->close();
  EXPECT_EQ(h.backend->calls.size(), count);
  EXPECT_EQ(h.backend->stop_calls, 1);
  EXPECT_FALSE(h.core->acceptOdometry(h.odometry(h.clock.ros_ns + 1)));
}

TEST(HumanoidTelemetry, ShutdownDrainsDeliveryAndRejectsRetainedSdkCallbacks) {
  using namespace std::chrono_literals;
  std::promise<void> entered, release, closing;
  auto release_future = release.get_future().share();
  std::atomic<int> calls{0};
  auto gate = std::make_shared<TelemetryGate>(
    [&](const Odometry &) {
      entered.set_value();
      release_future.wait();
      ++calls;
    }, [&](bool) { ++calls; });
  // Model an SDK executor retaining both callbacks beyond CloseChannel().
  auto queued_odometry = [gate] { gate->odometry(Odometry{}); };
  auto queued_mode = [gate] { gate->mode(true); };
  std::thread worker(queued_odometry);
  entered.get_future().wait();
  auto stopped = std::async(std::launch::async, [&] {
    closing.set_value();
    gate->close();
  });
  closing.get_future().wait();
  EXPECT_EQ(stopped.wait_for(20ms), std::future_status::timeout);
  release.set_value();
  worker.join();
  stopped.get();
  gate->close();  // idempotent
  gate.reset();
  queued_odometry();
  queued_mode();
  EXPECT_EQ(calls.load(), 1);
}

TEST(HumanoidProcessGuard, DiscoveryIsLocalOnlyWhenExplicitlyConfined) {
  EXPECT_TRUE(localhostDiscovery(nullptr, "LOCALHOST"));
  EXPECT_TRUE(localhostDiscovery("1", "SUBNET"));
  EXPECT_TRUE(localhostDiscovery("1", nullptr));
  EXPECT_FALSE(localhostDiscovery("0", "LOCALHOST"));
  EXPECT_FALSE(localhostDiscovery(nullptr, "SUBNET"));
  EXPECT_FALSE(localhostDiscovery(nullptr, nullptr));
  EXPECT_FALSE(localhostDiscovery(nullptr, "localhost"));
}

TEST(HumanoidProcessGuard, EmergencyZeroRunsOnlyForAnUnreleasedSession) {
  EXPECT_FALSE(emergencyZeroRequired(parseVendorSession("")));
  EXPECT_FALSE(emergencyZeroRequired(parseVendorSession("nope")));
  const std::string session = encodeVendorSession(7, "eth0");
  ASSERT_FALSE(session.empty());
  const auto armed = parseVendorSession(session);
  EXPECT_TRUE(armed.active);
  EXPECT_FALSE(armed.released);
  EXPECT_EQ(armed.domain, 7);
  EXPECT_EQ(armed.interface_name, "eth0");
  EXPECT_TRUE(emergencyZeroRequired(armed));
  auto released = session + vendorSessionReleaseByte();
  EXPECT_FALSE(emergencyZeroRequired(parseVendorSession(released)));
  released.push_back('x');
  EXPECT_FALSE(parseVendorSession(released).active);
  EXPECT_TRUE(encodeVendorSession(-1, "eth0").empty());
  EXPECT_TRUE(encodeVendorSession(233, "eth0").empty());
  EXPECT_TRUE(encodeVendorSession(1, "").empty());
}

TEST(HumanoidTelemetry, CallbackExceptionDoesNotPreventShutdown) {
  TelemetryGate gate([](const Odometry &) { throw std::runtime_error("callback failed"); },
                     [](bool) { throw std::runtime_error("callback failed"); });
  EXPECT_THROW(gate.odometry(Odometry{}), std::runtime_error);
  EXPECT_THROW(gate.mode(true), std::runtime_error);
  gate.close();
  EXPECT_NO_THROW(gate.odometry(Odometry{}));
  EXPECT_NO_THROW(gate.mode(true));
}

}  // namespace
}  // namespace chaos::humanoid
