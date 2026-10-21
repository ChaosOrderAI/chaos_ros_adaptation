// C++ contract tests for chaos::go2::Go2DriverCore, ported case-by-case from
// test/test_go2_driver.py. No robot, DDS participant, vendor SDK, or ROS
// graph is involved. The three ROS-wrapper pytest cases
// (driver_factory fixtures) remain Python-side until the wrapper migrates.
#include <array>
#include <cmath>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "chaos_runtime/go2_driver_core.hpp"

namespace {

using chaos::go2::Go2AssetConfig;
using chaos::go2::Go2DriverCore;
using chaos::go2::Move;
using chaos::go2::SportSample;
using chaos::go2::SportState;
using chaos::go2::Status;
using chaos::go2::Stamp;
using chaos::go2::VelocityCommand;

constexpr std::array<double, 3> kZero{0.0, 0.0, 0.0};

Go2AssetConfig asset(const std::string & velocity_frame = "odom") {
  Go2AssetConfig config;
  config.base_frame = "go2_01/base_link";
  config.odom_frame = "go2_01/odom";
  config.velocity_frame = velocity_frame;
  config.limits.linear_mps = 0.5;
  config.limits.angular_rps = 0.8;
  config.limits.command_timeout_s = 0.5;
  config.limits.state_timeout_s = 1.0;
  return config;
}

struct Clock {
  double mono = 10.0;
  std::int64_t ros = 100'000'000'000;
  void advance(double seconds) {
    mono += seconds;
    ros += static_cast<std::int64_t>(std::round(seconds * 1'000'000'000.0));
  }
};

SportState state(bool has_tick = false, std::uint32_t tick = 0,
                 bool has_stamp = true, std::int64_t stamp = 1,
                 std::int32_t error_code = 0,
                 const std::array<double, 3> & position = {1.0, 2.0, 0.3},
                 const std::array<double, 3> & velocity = {0.1, 0.2, 0.0},
                 double yaw_speed = 0.3,
                 const std::array<double, 4> & quaternion = {1.0, 0, 0, 0},
                 const std::array<double, 3> & gyroscope = {0.01, 0.02, 0.3}) {
  SportState message;
  message.has_tick = has_tick;
  message.tick = tick;
  message.has_stamp = has_stamp;
  message.stamp = Stamp{stamp, 0};
  message.error_code = error_code;
  message.position = position;
  message.velocity = velocity;
  message.yaw_speed = yaw_speed;
  message.quaternion = quaternion;
  message.gyroscope = gyroscope;
  return message;
}

VelocityCommand command(const Clock & clock,
                        const std::array<double, 6> & values =
                          {0.2, 0, 0, 0, 0, 0.1},
                        std::optional<std::int64_t> stamp = std::nullopt,
                        const std::string & frame = "go2_01/base_link") {
  const std::int64_t ns = stamp.value_or(clock.ros);
  VelocityCommand message;
  message.stamp = Stamp{ns / 1'000'000'000, ns % 1'000'000'000};
  message.frame_id = frame;
  message.values = values;
  return message;
}

// Anything returning nonzero or throwing is a failed SDK write; the bounded
// no-reply Move path is the only SDK entry the core may call.
struct Client {
  std::vector<std::array<double, 3>> calls;
  std::deque<int> failures;
  std::deque<std::string> exceptions;

  int operator()(double x, double y, double z) {
    calls.push_back({x, y, z});
    if (!exceptions.empty()) {
      std::string message = exceptions.front();
      exceptions.pop_front();
      throw std::runtime_error(message);
    }
    if (!failures.empty()) {
      const int result = failures.front();
      failures.pop_front();
      return result;
    }
    return 0;
  }
};

struct Harness {
  Clock clock;
  Client client;
  Go2DriverCore core;
  explicit Harness(const std::string & velocity_frame = "odom")
      : core(asset(velocity_frame),
             [this] { return clock.mono; },
             [this] { return clock.ros; }) {}
};

// ready(): fresh telemetry accepted, one zero written, driver initialized.
std::unique_ptr<Harness> ready() {
  auto harness = std::make_unique<Harness>();
  EXPECT_TRUE(harness->core.acceptState(state()));
  EXPECT_TRUE(harness->core.step(harness->client).has_value());
  EXPECT_EQ(harness->client.calls.size(), 1u);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  return harness;
}

TEST(Go2CoreTest, InitialUnknownStateAndFailedInitialZeroInhibitMotion) {
  Harness harness;
  EXPECT_FALSE(harness.core.acceptCommand(command(harness.clock)));
  EXPECT_FALSE(harness.core.step(harness.client).has_value());
  EXPECT_TRUE(harness.client.calls.empty());
  EXPECT_TRUE(harness.core.acceptState(state()));
  harness.core.requestStop();
  harness.client.failures = {1};
  EXPECT_FALSE(harness.core.step(harness.client).has_value());
  EXPECT_FALSE(harness.core.status().output_error.empty());
  EXPECT_TRUE(harness.core.status().stop_pending);
  EXPECT_TRUE(harness.core.status().fault.empty());
  EXPECT_FALSE(harness.core.acceptCommand(command(harness.clock)));
  EXPECT_TRUE(harness.core.step(harness.client).has_value());
  EXPECT_TRUE(harness.core.status().initialized);
  EXPECT_FALSE(harness.core.status().stop_pending);
  EXPECT_TRUE(harness.core.acceptCommand(command(harness.clock)));
}

TEST(Go2CoreTest, InitialDiscoveryRetriesNeverPublishReadyBeforeZeroSucceeds) {
  Harness harness;
  harness.client.failures = {1, 1};
  harness.core.acceptState(state());
  EXPECT_FALSE(harness.core.step(harness.client).has_value());
  EXPECT_FALSE(harness.core.step(harness.client).has_value());
  EXPECT_FALSE(harness.core.status().initialized);
  EXPECT_TRUE(harness.core.status().stop_pending);
  EXPECT_TRUE(harness.core.step(harness.client).has_value());
  EXPECT_TRUE(harness.core.status().initialized);
  EXPECT_TRUE(harness.core.status().fault.empty());
  ASSERT_EQ(harness.client.calls.size(), 3u);
  for (const auto & call : harness.client.calls) {
    EXPECT_EQ(call, kZero);
  }
}

TEST(Go2CoreTest, CommandWatchdogWorksWhenRuntimeStopsPublishing) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), (std::array<double, 3>{0.2, 0.0, 0.1}));
  harness->clock.advance(0.5);
  EXPECT_TRUE(harness->core.acceptState(state(false, 0, true, 2)));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  EXPECT_FALSE(harness->core.status().has_command);
  EXPECT_FALSE(harness->core.status().moving);
  harness->clock.advance(0.1);
  harness->core.acceptState(state(false, 0, true, 3));
  harness->core.step(harness->client);
  // New telemetry cannot revive an old command.
  EXPECT_EQ(harness->client.calls.back(), kZero);
}

TEST(Go2CoreTest, SourceAgeConsumesDeadlineAndDuplicateCannotExtendIt) {
  auto harness = ready();
  const VelocityCommand message =
    command(harness->clock, {0.2, 0, 0, 0, 0, 0.1},
            harness->clock.ros - 400'000'000);
  EXPECT_TRUE(harness->core.acceptCommand(message));
  harness->core.step(harness->client);
  harness->clock.advance(0.05);
  EXPECT_FALSE(harness->core.acceptCommand(message));
  harness->clock.advance(0.051);
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
}

TEST(Go2CoreTest, TelemetryLossStopsEvenWithFreshIncomingCommand) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  harness->clock.advance(0.9);
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  harness->clock.advance(0.101);
  EXPECT_FALSE(harness->core.step(harness->client).has_value());
  EXPECT_EQ(harness->client.calls.back(), kZero);
  EXPECT_FALSE(harness->core.acceptCommand(command(harness->clock)));
}

TEST(Go2CoreTest, RosClockPauseDoesNotPauseWatchdog) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  harness->clock.mono += 0.501;
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
}

TEST(Go2CoreTest, ReplayedCommandWithFrozenRosClockCannotRefreshOrRestart) {
  auto harness = ready();
  const VelocityCommand message = command(harness->clock);
  EXPECT_TRUE(harness->core.acceptCommand(message));
  harness->core.step(harness->client);
  for (int index = 1; index <= 7; ++index) {
    harness->clock.mono += 0.1;
    EXPECT_TRUE(harness->core.acceptState(state(false, 0, true, index + 1)));
    EXPECT_FALSE(harness->core.acceptCommand(message));
    harness->core.step(harness->client);
    if (index >= 6) {
      EXPECT_EQ(harness->client.calls.back(), kZero);
      EXPECT_FALSE(harness->core.status().has_command);
    }
  }
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock, {0, 0, 0, 0, 0, 0})));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
}

TEST(Go2CoreTest, StopDoesNotAllowSameCommandStampToRestartMotion) {
  auto harness = ready();
  const VelocityCommand message = command(harness->clock);
  EXPECT_TRUE(harness->core.acceptCommand(message));
  harness->core.step(harness->client);
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock, {0, 0, 0, 0, 0, 0})));
  harness->core.step(harness->client);
  EXPECT_FALSE(harness->core.acceptCommand(message));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
}

TEST(Go2CoreTest, StopStampBlocksDelayedPreStopMotionAndOldZeroCannotLowerBarrier) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  harness->clock.advance(0.1);
  const VelocityCommand delayed = command(harness->clock);
  const VelocityCommand old_zero = command(harness->clock, {0, 0, 0, 0, 0, 0});
  harness->clock.advance(0.1);
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock, {0, 0, 0, 0, 0, 0})));
  const std::int64_t barrier = harness->clock.ros;
  harness->core.step(harness->client);
  EXPECT_TRUE(harness->core.acceptCommand(old_zero));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->core.status().last_command_stamp_ns, barrier);
  EXPECT_FALSE(harness->core.acceptCommand(delayed));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  harness->clock.advance(0.1);
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  EXPECT_NE(harness->client.calls.back(), kZero);
}

TEST(Go2CoreTest, RosClockCorrectionCannotExtendActiveCommand) {
  for (const std::int64_t correction : {-1'000'000'000, 1'000'000'000}) {
    auto harness = ready();
    EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
    harness->core.step(harness->client);
    harness->clock.ros += correction;
    harness->core.step(harness->client);
    EXPECT_EQ(harness->client.calls.back(), kZero);
  }
}

TEST(Go2CoreTest, UnsupportedAxesNonfiniteAndNormLimitsRejectAndStop) {
  const std::vector<std::array<double, 6>> cases{
    {0, 0, 0.01, 0, 0, 0}, {0, 0, 0, 0.01, 0, 0}, {0, 0, 0, 0, 0.01, 0},
    {0.4, 0.4, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0.81},
    {std::nan(""), 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, std::numeric_limits<double>::infinity()},
  };
  for (const auto & values : cases) {
    auto harness = ready();
    EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
    harness->core.step(harness->client);
    EXPECT_FALSE(harness->core.acceptCommand(command(harness->clock, values)));
    harness->core.step(harness->client);
    EXPECT_EQ(harness->client.calls.back(), kZero);
  }
}

TEST(Go2CoreTest, WrongFrameOrMissingStaleFutureStampNeverMoves) {
  struct Case {
    std::string frame = "go2_01/base_link";
    std::optional<std::int64_t> stamp;
  };
  const std::vector<Case> cases{
    {"map", std::nullopt}, {"", std::nullopt},
    {"go2_01/base_link", 0}, {"go2_01/base_link", 99'500'000'000},
    {"go2_01/base_link", 100'000'000'001},
  };
  for (const Case & c : cases) {
    auto harness = ready();
    EXPECT_FALSE(harness->core.acceptCommand(
      command(harness->clock, {0.2, 0, 0, 0, 0, 0.1}, c.stamp, c.frame)));
    harness->core.step(harness->client);
    for (const auto & call : harness->client.calls) {
      EXPECT_EQ(call, kZero);
    }
  }
}

TEST(Go2CoreTest, DuplicateAndBackwardSportStampsDoNotRefreshState) {
  auto harness = ready();
  const double received = harness->core.sample()->received;
  for (int index = 0; index < 5; ++index) {
    harness->clock.advance(0.25);
    EXPECT_FALSE(harness->core.acceptState(
      state(false, 0, true, index % 2 ? 0 : 1)));
    EXPECT_DOUBLE_EQ(harness->core.sample()->received, received);
    EXPECT_FALSE(harness->core.step(harness->client).has_value());
  }
  EXPECT_FALSE(harness->core.acceptCommand(command(harness->clock)));
  EXPECT_TRUE(harness->core.acceptState(state(false, 0, true, 2)));
}

TEST(Go2CoreTest, Uint32TickWrapAcceptedButDuplicatesAndOldTicksIgnored) {
  Harness harness;
  EXPECT_TRUE(harness.core.acceptState(state(true, 0xFFFFFFFE)));
  const SportSample first = *harness.core.sample();
  harness.clock.advance(0.01);
  EXPECT_FALSE(harness.core.acceptState(state(true, 0xFFFFFFFE)));
  EXPECT_DOUBLE_EQ(harness.core.sample()->received, first.received);
  EXPECT_TRUE(harness.core.acceptState(state(true, 1)));
  EXPECT_FALSE(harness.core.acceptState(state(true, 0xFFFFFFFF)));
  ASSERT_TRUE(harness.core.status().source_identity.has_value());
  EXPECT_EQ(harness.core.status().source_identity->kind,
            chaos::go2::SourceIdentity::Kind::kTick);
  EXPECT_EQ(harness.core.status().source_identity->value, 1);
}

TEST(Go2CoreTest, BadTelemetryInvalidatesMotionAndDoesNotPublish) {
  const std::vector<SportState> cases{
    state(false, 0, true, 2, 0, {std::nan(""), 0, 0}),
    state(false, 0, true, 2, 0, {1, 2, 0.3}, {0, std::numeric_limits<double>::infinity(), 0}),
    state(false, 0, true, 2, 0, {1, 2, 0.3}, {0.1, 0.2, 0}, 0.3, {0, 0, 0, 0}),
    state(false, 0, true, 2, 0, {1, 2, 0.3}, {0.1, 0.2, 0}, 0.3, {1.1, 0, 0, 0}),
    state(false, 0, true, 2, 0, {1, 2, 0.3}, {0.1, 0.2, 0}, 0.3, {1, 0, 0, 0}, {0, std::nan(""), 0}),
    state(false, 0, true, 2, 0, {1, 2, 0.3}, {0.1, 0.2, 0}, std::nan("")),
    state(false, 0, true, 2, 123),
    [] {
      SportState message = state(false, 0, true, 2);
      message.stamp = Stamp{1, 1'000'000'000};
      return message;
    }(),
  };
  for (const SportState & bad : cases) {
    auto harness = ready();
    EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
    harness->core.step(harness->client);
    EXPECT_FALSE(harness->core.acceptState(bad));
    EXPECT_FALSE(harness->core.step(harness->client).has_value());
    EXPECT_EQ(harness->client.calls.back(), kZero);
    EXPECT_FALSE(harness->core.acceptCommand(command(harness->clock)));
  }
}

TEST(Go2CoreTest, WriteFailureLatchesFaultAndRetriesZeroWithoutResuming) {
  // Python also exercises None and False as failure payloads; those are
  // Python type-hygiene variants (bool/None crossing the SDK boundary). The
  // C++ boundary is typed int, so nonzero code and exception cover them.
  for (const char * failure : {"1", "exception"}) {
    auto harness = ready();
    EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
    if (failure[0] == '1') {
      harness->client.failures = {1, 1};
    } else {
      harness->client.exceptions = {"injected send failure", ""};
      harness->client.failures = {1};
    }
    EXPECT_FALSE(harness->core.step(harness->client).has_value());
    ASSERT_GE(harness->client.calls.size(), 2u);
    EXPECT_EQ(harness->client.calls[harness->client.calls.size() - 2],
              (std::array<double, 3>{0.2, 0.0, 0.1}));
    EXPECT_EQ(harness->client.calls.back(), kZero);
    EXPECT_TRUE(harness->core.status().stop_pending);
    EXPECT_FALSE(harness->core.status().fault.empty());
    harness->client.failures = {1};
    harness->client.exceptions.clear();
    harness->core.step(harness->client);
    EXPECT_TRUE(harness->core.status().stop_pending);
    harness->core.step(harness->client);
    EXPECT_FALSE(harness->core.status().stop_pending);
    EXPECT_FALSE(harness->core.status().fault.empty());
    EXPECT_FALSE(harness->core.acceptCommand(command(harness->clock)));
    EXPECT_TRUE(harness->core.acceptState(state(false, 0, true, 2)));
    // Telemetry must not advertise ready while the fault is latched.
    EXPECT_FALSE(harness->core.step(harness->client).has_value());
  }
}

TEST(Go2CoreTest, ZeroCommandIsAllowedAfterFaultAndUsesOnlyMove) {
  // Latch an output fault through a real failed SDK write (the previous
  // Python test poked core.fault directly; the C++ core exposes no mutable
  // fault handle).
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->client.failures = {1, 1};
  harness->core.step(harness->client);
  ASSERT_FALSE(harness->core.status().fault.empty());
  // The immediate zero retry after the failed nonzero write also failed, so
  // settle the pending stop first.
  harness->client.failures.clear();
  harness->core.step(harness->client);
  ASSERT_FALSE(harness->core.status().stop_pending);
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock, {0, 0, 0, 0, 0, 0})));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  // The fault latch survives the successful zero output and still inhibits
  // motion.
  EXPECT_FALSE(harness->core.status().fault.empty());
  EXPECT_FALSE(harness->core.acceptCommand(command(harness->clock)));
}

TEST(Go2CoreTest, ExpiryDuringSdkWriteSendsZeroInTheSameIteration) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  const Move delayed = [&](double x, double y, double z) {
    const int result = harness->client(x, y, z);
    if (x != 0.0 || y != 0.0 || z != 0.0) {
      harness->clock.advance(0.501);
    }
    return result;
  };
  harness->core.step(delayed);
  ASSERT_GE(harness->client.calls.size(), 2u);
  EXPECT_EQ(harness->client.calls[harness->client.calls.size() - 2],
            (std::array<double, 3>{0.2, 0.0, 0.1}));
  EXPECT_EQ(harness->client.calls.back(), kZero);
}

TEST(Go2CoreTest, SdkIoDoesNotHoldCallbackCacheLock) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  std::atomic<bool> callback_completed{false};
  const Move move_with_callback = [&](double x, double y, double z) {
    std::thread worker([&] {
      harness->core.acceptState(state(false, 0, true, 2, 1));
      callback_completed.store(true);
    });
    worker.join();
    EXPECT_TRUE(callback_completed.load())
      << "SDK I/O held the DDS callback lock";
    return harness->client(x, y, z);
  };
  harness->core.step(move_with_callback);
  EXPECT_EQ(harness->client.calls.back(), kZero);
}

TEST(Go2CoreTest, WorldVelocityRotatesIntoChildFrameAndKeepsAngularVelocity) {
  Harness harness;
  const double half = std::sqrt(0.5);
  EXPECT_TRUE(harness.core.acceptState(state(
    false, 0, true, 1, 0, {1, 2, 0.3}, {1.0, 0.0, 0.0}, 0.3,
    {half, 0, 0, half}, {0.1, 0.2, 0.3})));
  const std::optional<SportSample> sample = harness.core.step(harness.client);
  ASSERT_TRUE(sample.has_value());
  EXPECT_NEAR(sample->quaternion[0], 0, 1e-12);
  EXPECT_NEAR(sample->quaternion[1], 0, 1e-12);
  EXPECT_NEAR(sample->quaternion[2], half, 1e-12);
  EXPECT_NEAR(sample->quaternion[3], half, 1e-12);
  EXPECT_NEAR(sample->linear[0], 0, 1e-12);
  EXPECT_NEAR(sample->linear[1], -1, 1e-12);
  EXPECT_NEAR(sample->linear[2], 0, 1e-12);
  EXPECT_NEAR(sample->angular[0], 0.1, 1e-12);
  EXPECT_NEAR(sample->angular[1], 0.2, 1e-12);
  EXPECT_NEAR(sample->angular[2], 0.3, 1e-12);
}

TEST(Go2CoreTest, RotationIncludesRollAndBodyVelocityOptionIsNotRotated) {
  const double half = std::sqrt(0.5);
  const SportState message = state(false, 0, true, 1, 0, {1, 2, 0.3},
                                   {0, 1, 0}, 0, {half, half, 0, 0});
  {
    Harness harness;
    harness.core.acceptState(message);
    const std::optional<SportSample> sample = harness.core.step(harness.client);
    ASSERT_TRUE(sample.has_value());
    EXPECT_NEAR(sample->linear[0], 0, 1e-12);
    EXPECT_NEAR(sample->linear[1], 0, 1e-12);
    EXPECT_NEAR(sample->linear[2], -1, 1e-12);
  }
  {
    Harness harness("body");
    harness.core.acceptState(message);
    const std::optional<SportSample> sample = harness.core.step(harness.client);
    ASSERT_TRUE(sample.has_value());
    EXPECT_DOUBLE_EQ(sample->linear[0], 0);
    EXPECT_DOUBLE_EQ(sample->linear[1], 1);
    EXPECT_DOUBLE_EQ(sample->linear[2], 0);
  }
}

TEST(Go2CoreTest, TimerNeverRepublishesOrRestampsCachedTelemetry) {
  Harness harness;
  harness.clock.ros += 123;
  const std::int64_t received_stamp = harness.clock.ros;
  harness.core.acceptState(state());
  harness.clock.advance(0.25);
  const std::optional<SportSample> sample = harness.core.step(harness.client);
  ASSERT_TRUE(sample.has_value());
  EXPECT_EQ(sample->ros_stamp_ns, received_stamp);
  for (int i = 0; i < 5; ++i) {
    harness.clock.advance(0.01);
    EXPECT_FALSE(harness.core.step(harness.client).has_value());
  }
}

TEST(Go2CoreTest, ShutdownRetriesFailedZeroAndNeverAcceptsMotionAgain) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  harness->core.requestStop(true);
  harness->client.failures = {1};
  harness->core.step(harness->client);
  EXPECT_TRUE(harness->core.status().stop_pending);
  harness->core.step(harness->client);
  EXPECT_FALSE(harness->core.status().stop_pending);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  EXPECT_FALSE(harness->core.acceptCommand(command(harness->clock)));
  EXPECT_FALSE(harness->core.acceptState(state(false, 0, true, 2)));
}

// Stamps outside the ROS int32-second range must be rejected without
// overflowing (reviewer UBSan finding); motion is inhibited either way.
TEST(Go2CoreTest, OutOfRangeCommandAndTelemetryStampsNeverMove) {
  auto harness = ready();
  for (const std::int64_t bad_sec :
       std::vector<std::int64_t>{std::numeric_limits<std::int64_t>::max(),
                                 4294967296LL, 2147483648LL, -1LL}) {
    VelocityCommand message;
    message.stamp = Stamp{bad_sec, 0};
    message.frame_id = "go2_01/base_link";
    message.values = {0.2, 0, 0, 0, 0, 0.1};
    const std::size_t before = harness->client.calls.size();
    EXPECT_FALSE(harness->core.acceptCommand(message));
    harness->core.step(harness->client);
    ASSERT_EQ(harness->client.calls.size(), before + 1);
    EXPECT_EQ(harness->client.calls.back(), kZero);
  }
  SportState bad_telemetry = state(false, 0, true, 0);
  bad_telemetry.stamp = Stamp{std::numeric_limits<std::int64_t>::max(), 0};
  EXPECT_FALSE(harness->core.acceptState(bad_telemetry));
  EXPECT_FALSE(harness->core.step(harness->client).has_value());
}

// status()/sample() return locked copies: mutating the returned snapshot
// must not affect the core, and reads must be safe while callbacks run.
TEST(Go2CoreTest, StatusAndSampleAreLockedCopies) {
  Harness harness;
  EXPECT_TRUE(harness.core.acceptState(state()));
  Status snapshot = harness.core.status();
  snapshot.fault = "external mutation attempt";
  snapshot.stop_pending = false;
  EXPECT_TRUE(harness.core.status().stop_pending);
  EXPECT_TRUE(harness.core.status().fault.empty());
  const auto sample = harness.core.sample();
  ASSERT_TRUE(sample.has_value());
  EXPECT_DOUBLE_EQ(sample->received, 10.0);
  EXPECT_EQ(sample->ros_stamp_ns, 100'000'000'000LL);
}

}  // namespace
