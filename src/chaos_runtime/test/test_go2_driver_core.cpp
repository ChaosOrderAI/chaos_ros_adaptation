// C++ contract tests for chaos::go2::Go2DriverCore, ported case-by-case from
// test/test_go2_driver.py and extended with the transport deadline rules.
// No robot, DDS participant, vendor SDK, or ROS graph is involved. The three
// ROS-wrapper pytest cases (driver_factory fixtures) remain Python-side
// until the wrapper migrates.
//
// The clock model of these tests has three independent readings:
//
//   * the seconds clock owns the telemetry age,
//   * the ROS clock owns the published sample stamps,
//   * the monotonic clock (int64 nanoseconds) owns every command deadline.
//
// The transport delivers an absolute CLOCK_MONOTONIC deadline with every
// command, so the tests inject the domain and the monotonic clock explicitly
// and never rely on the wall clock.
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
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
// The three values that reach the SDK: vx, vy and wz.
constexpr std::array<double, 3> kMotion{0.2, 0.0, 0.1};
// The same motion as the six transport values.
constexpr std::array<double, 6> kMotionTwist{0.2, 0.0, 0.0, 0.0, 0.0, 0.1};
// A second motion, used to tell two commands apart in the interleavings.
constexpr std::array<double, 3> kMotionB{0.3, 0.0, 0.2};
constexpr std::array<double, 6> kMotionBTwist{0.3, 0.0, 0.0, 0.0, 0.0, 0.2};

// The clock-domain identity that the transport and the driver share.
constexpr char kDomain[] = "v1:0123abcd-89ef-4abc-8def-0123456789ab:1:2";
constexpr char kOtherDomain[] = "v1:0123abcd-89ef-4abc-8def-0123456789ac:1:2";

constexpr std::int64_t kMs = 1'000'000;
// The deadline the transport gives to a command.
constexpr std::int64_t kTtlNs = 100 * kMs;
// The cap the driver enforces on the lifetime of an armed command.
constexpr double kCapS = 0.5;

Go2AssetConfig asset(const std::string & velocity_frame = "odom") {
  Go2AssetConfig config;
  config.base_frame = "go2_01/base_link";
  config.odom_frame = "go2_01/odom";
  config.velocity_frame = velocity_frame;
  config.clock_domain_id = kDomain;
  config.limits.linear_mps = 0.5;
  config.limits.angular_rps = 0.8;
  config.limits.command_timeout_s = kCapS;
  config.limits.state_timeout_s = 1.0;
  return config;
}

struct Clock {
  // The seconds clock owns the telemetry age.
  double mono = 10.0;
  // The ROS clock owns the published sample stamps.
  std::int64_t ros = 100'000'000'000;
  // The transport clock owns every deadline.
  std::int64_t mono_ns = 10'000'000'000;
  // A failing transport clock fails closed.
  bool fail_mono = false;
  // The number of monotonic readings and an optional hook that runs on every
  // reading. The hook lets a test interleave a concurrent command between
  // the selection of a sample and the final pre-SDK check.
  std::int64_t mono_reads = 0;
  std::function<void(std::int64_t reads)> mono_hook;
  // The number of seconds-clock readings and an optional hook that runs on
  // every reading. The hook lets a test fail exactly one reading of the
  // seconds clock, for example the final pre-SDK one.
  std::int64_t seconds_reads = 0;
  std::function<void(std::int64_t reads)> seconds_hook;

  void advance(double seconds) {
    const std::int64_t delta =
      static_cast<std::int64_t>(std::round(seconds * 1'000'000'000.0));
    mono += seconds;
    ros += delta;
    mono_ns += delta;
  }

  // Moves only the transport clock: the ROS clock stays frozen.
  void advanceMonotonic(std::int64_t nanoseconds) { mono_ns += nanoseconds; }
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

// The common command builder. Every test derives its commands from here, so
// the deadline arithmetic exists exactly once.
//
// The issue time defaults to the current monotonic reading and the deadline
// to `ttl_ns` after it. A negative issue offset models a command that was
// issued in the past and is now in transit; a positive one models an
// implausible future issue time. The original source stamp defaults to the
// current ROS time and is never re-aged by the driver.
VelocityCommand command(const Clock & clock,
                        const std::array<double, 6> & values = kMotionTwist,
                        std::optional<std::int64_t> stamp = std::nullopt,
                        const std::string & frame = "go2_01/base_link",
                        std::int64_t issued_offset_ns = 0,
                        std::int64_t ttl_ns = kTtlNs,
                        const std::string & domain = kDomain) {
  const std::int64_t source_ns = stamp.value_or(clock.ros);
  const std::int64_t issued = clock.mono_ns + issued_offset_ns;
  VelocityCommand message;
  message.stamp = Stamp{source_ns / 1'000'000'000, source_ns % 1'000'000'000};
  message.frame_id = frame;
  message.values = values;
  message.clock_domain_id = domain;
  message.issued_monotonic_ns = issued;
  message.expires_monotonic_ns = issued + ttl_ns;
  message.stop = false;
  return message;
}

// An explicit stop: six exact zeros, a current issue time, no expiry and no
// source stamp.
VelocityCommand stopCommand(const Clock & clock,
                             std::int64_t issued_offset_ns = 0,
                             const std::string & domain = kDomain,
                             const std::string & frame = "go2_01/base_link") {
  VelocityCommand message;
  message.stamp = Stamp{0, 0};
  message.frame_id = frame;
  message.values = {0, 0, 0, 0, 0, 0};
  message.clock_domain_id = domain;
  message.issued_monotonic_ns = clock.mono_ns + issued_offset_ns;
  message.expires_monotonic_ns = 0;
  message.stop = true;
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
             [this] {
               ++clock.seconds_reads;
               if (clock.seconds_hook) {
                 clock.seconds_hook(clock.seconds_reads);
               }
               return clock.mono;
             },
             [this] { return clock.ros; },
             [this] {
               ++clock.mono_reads;
               if (clock.fail_mono) {
                 throw std::runtime_error("injected monotonic clock failure");
               }
               if (clock.mono_hook) {
                 clock.mono_hook(clock.mono_reads);
               }
               return clock.mono_ns;
             }) {}
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

// Every call of the driver is a zero: no motion ever reached the SDK.
void expectOnlyZeros(const Client & client) {
  for (const auto & call : client.calls) {
    EXPECT_EQ(call, kZero);
  }
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
  EXPECT_EQ(harness->client.calls.back(), kMotion);
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

// The transport gives every command a 100 ms deadline. A command that
// arrives 80 ms after its issue time still has 20 ms left and is admitted; a
// command that arrives 150 ms after its issue time is already expired.
TEST(Go2CoreTest, TheDeadlineAdmitsAnEightyMillisecondTransitAndRefusesAnExpiredArrival) {
  {
    auto harness = ready();
    const std::int64_t issued = harness->clock.mono_ns;
    harness->clock.advance(0.08);
    VelocityCommand message = command(harness->clock);
    message.issued_monotonic_ns = issued;
    message.expires_monotonic_ns = issued + kTtlNs;
    EXPECT_TRUE(harness->core.acceptCommand(message));
    harness->core.step(harness->client);
    EXPECT_EQ(harness->client.calls.back(), kMotion);
  }
  {
    auto harness = ready();
    const std::int64_t issued = harness->clock.mono_ns;
    harness->clock.advance(0.15);
    VelocityCommand message = command(harness->clock);
    message.issued_monotonic_ns = issued;
    message.expires_monotonic_ns = issued + kTtlNs;
    EXPECT_FALSE(harness->core.acceptCommand(message));
    harness->core.step(harness->client);
    expectOnlyZeros(harness->client);
  }
}

// The deadline is exclusive: at exactly the deadline the command is expired.
TEST(Go2CoreTest, ExactlyAtTheDeadlineTheCommandIsExpired) {
  {
    auto harness = ready();
    const std::int64_t issued = harness->clock.mono_ns;
    harness->clock.advance(0.1);
    VelocityCommand message = command(harness->clock);
    message.issued_monotonic_ns = issued;
    message.expires_monotonic_ns = issued + kTtlNs;
    EXPECT_FALSE(harness->core.acceptCommand(message));
    harness->core.step(harness->client);
    expectOnlyZeros(harness->client);
  }
  {
    auto harness = ready();
    EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
    harness->core.step(harness->client);
    ASSERT_EQ(harness->client.calls.back(), kMotion);
    harness->clock.advance(0.1);
    harness->core.step(harness->client);
    EXPECT_EQ(harness->client.calls.back(), kZero);
    EXPECT_FALSE(harness->core.status().has_command);
  }
}

// The ROS clock is independent of the deadline: a frozen or jumped ROS clock
// neither expires nor extends a command.
TEST(Go2CoreTest, AFrozenOrJumpedRosClockNeverChangesTheDeadline) {
  for (const std::int64_t correction : {-1'000'000'000, 1'000'000'000}) {
    auto harness = ready();
    EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
    harness->core.step(harness->client);
    ASSERT_EQ(harness->client.calls.back(), kMotion);
    harness->clock.ros += correction;
    harness->core.step(harness->client);
    // The monotonic deadline is untouched: the command stays armed.
    EXPECT_EQ(harness->client.calls.back(), kMotion);
    EXPECT_TRUE(harness->core.status().has_command);
    // The transport clock still expires it.
    harness->clock.advanceMonotonic(kTtlNs);
    harness->core.step(harness->client);
    EXPECT_EQ(harness->client.calls.back(), kZero);
  }
  {
    // A completely frozen ROS clock does not pause the watchdog.
    auto harness = ready();
    EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
    harness->core.step(harness->client);
    harness->clock.advanceMonotonic(kTtlNs);
    harness->core.step(harness->client);
    EXPECT_EQ(harness->client.calls.back(), kZero);
  }
}

// The original source stamp is kept and never aged a second time against the
// clock of the driver.
TEST(Go2CoreTest, TheSourceStampIsRetainedWithoutReAging) {
  auto harness = ready();
  const std::int64_t old_stamp = harness->clock.ros - 10'000'000'000;
  EXPECT_TRUE(harness->core.acceptCommand(
    command(harness->clock, kMotionTwist, old_stamp)));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kMotion);
  // A ROS clock jump does not invalidate the accepted command either.
  harness->clock.ros += 3'600'000'000'000LL;
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kMotion);
  EXPECT_TRUE(harness->core.status().has_command);
}

TEST(Go2CoreTest, DuplicateAndBackwardIssuedCommandsAreRefusedAndNeverExtendTheDeadline) {
  auto harness = ready();
  const VelocityCommand message = command(harness->clock);
  EXPECT_TRUE(harness->core.acceptCommand(message));
  harness->core.step(harness->client);
  const std::int64_t barrier = harness->core.status().last_command_issued_mono_ns;
  EXPECT_EQ(barrier, message.issued_monotonic_ns);
  // The same command again is refused and does not extend the deadline.
  EXPECT_FALSE(harness->core.acceptCommand(message));
  // A backward issue time is refused as well.
  VelocityCommand backward = message;
  backward.issued_monotonic_ns = message.issued_monotonic_ns - 1;
  backward.expires_monotonic_ns = backward.issued_monotonic_ns + kTtlNs;
  EXPECT_FALSE(harness->core.acceptCommand(backward));
  EXPECT_EQ(harness->core.status().last_command_issued_mono_ns, barrier);
  // The original deadline still expires the command.
  harness->clock.advance(0.1);
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  EXPECT_FALSE(harness->core.status().has_command);
}

// An issue time in the future is implausible: the driver never accepts a
// command that claims to be issued later than its own clock.
TEST(Go2CoreTest, AnImplausibleFutureIssueTimeIsRefused) {
  auto harness = ready();
  EXPECT_FALSE(harness->core.acceptCommand(
    command(harness->clock, kMotionTwist, std::nullopt, "go2_01/base_link",
            1 * kMs)));
  // A missing issue time is refused as well.
  VelocityCommand missing = command(harness->clock);
  missing.issued_monotonic_ns = 0;
  EXPECT_FALSE(harness->core.acceptCommand(missing));
  // A missing deadline is refused as well.
  VelocityCommand no_deadline = command(harness->clock);
  no_deadline.expires_monotonic_ns = 0;
  EXPECT_FALSE(harness->core.acceptCommand(no_deadline));
  // The refused packets advanced the barrier neither.
  EXPECT_EQ(harness->core.status().last_command_issued_mono_ns, 0);
  expectOnlyZeros(harness->client);
}

// The barrier of an explicit stop only ever rises.
TEST(Go2CoreTest, TheStopBarrierBlocksOlderMotionAndNeverLowersItself) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  const std::int64_t issued = harness->clock.mono_ns;

  harness->clock.advance(0.01);
  const VelocityCommand stop = stopCommand(harness->clock);
  EXPECT_TRUE(harness->core.acceptCommand(stop));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  const std::int64_t barrier = harness->core.status().last_command_issued_mono_ns;
  EXPECT_EQ(barrier, stop.issued_monotonic_ns);

  // A duplicate stop is idempotent and never lowers the barrier.
  EXPECT_TRUE(harness->core.acceptCommand(stop));
  EXPECT_EQ(harness->core.status().last_command_issued_mono_ns, barrier);
  // An older stop is safe as well and never lowers the barrier.
  EXPECT_TRUE(harness->core.acceptCommand(stopCommand(harness->clock, -5 * kMs)));
  EXPECT_EQ(harness->core.status().last_command_issued_mono_ns, barrier);

  // A motion command that was issued before the stop is refused.
  VelocityCommand replay = command(harness->clock);
  replay.issued_monotonic_ns = issued;
  replay.expires_monotonic_ns = issued + kTtlNs;
  EXPECT_FALSE(harness->core.acceptCommand(replay));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);

  // A command issued after the stop may resume the motion.
  harness->clock.advance(0.01);
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kMotion);
}

// An explicit stop needs six exact zeros, the local domain, the base frame
// and a current issue time. It does not need fresh telemetry.
TEST(Go2CoreTest, AStopRequiresExactZerosTheLocalDomainAndAValidIssueTime) {
  auto harness = ready();
  VelocityCommand not_zero = stopCommand(harness->clock);
  not_zero.values = {0, 0, 0, 0, 0, 1e-12};
  EXPECT_FALSE(harness->core.acceptCommand(not_zero));
  EXPECT_FALSE(harness->core.acceptCommand(
    stopCommand(harness->clock, 0, kOtherDomain)));
  EXPECT_FALSE(harness->core.acceptCommand(stopCommand(harness->clock, 1 * kMs)));
  VelocityCommand missing_issue = stopCommand(harness->clock);
  missing_issue.issued_monotonic_ns = 0;
  EXPECT_FALSE(harness->core.acceptCommand(missing_issue));
  EXPECT_FALSE(harness->core.acceptCommand(
    stopCommand(harness->clock, 0, kDomain, "map")));
  // No refused stop advanced the barrier.
  EXPECT_EQ(harness->core.status().last_command_issued_mono_ns, 0);
  // A valid stop is accepted even when the telemetry went stale.
  harness->clock.advance(2.0);
  EXPECT_TRUE(harness->core.acceptCommand(stopCommand(harness->clock)));
  EXPECT_GT(harness->core.status().last_command_issued_mono_ns, 0);
}

// A refused packet never advances the barrier.
TEST(Go2CoreTest, AnInvalidPacketDoesNotPoisonTheBarrier) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  const std::int64_t barrier = harness->core.status().last_command_issued_mono_ns;

  VelocityCommand wrong_domain = command(harness->clock);
  wrong_domain.clock_domain_id = kOtherDomain;
  EXPECT_FALSE(harness->core.acceptCommand(wrong_domain));
  EXPECT_EQ(harness->core.status().last_command_issued_mono_ns, barrier);

  VelocityCommand wrong_frame = command(harness->clock);
  wrong_frame.frame_id = "map";
  EXPECT_FALSE(harness->core.acceptCommand(wrong_frame));
  EXPECT_EQ(harness->core.status().last_command_issued_mono_ns, barrier);

  // A refused packet requests a local zero, so the driver settles it before
  // it accepts motion again.
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  EXPECT_FALSE(harness->core.status().stop_pending);

  // A valid command with a later issue time is still admitted.
  harness->clock.advance(0.01);
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  EXPECT_GT(harness->core.status().last_command_issued_mono_ns, barrier);
}

// A clock domain that does not match admits no motion at all.
TEST(Go2CoreTest, ADomainMismatchOrAMissingDomainAdmitsNoMotion) {
  {
    auto harness = ready();
    EXPECT_FALSE(harness->core.acceptCommand(
      command(harness->clock, kMotionTwist, std::nullopt, "go2_01/base_link", 0,
              kTtlNs, kOtherDomain)));
    harness->core.step(harness->client);
    expectOnlyZeros(harness->client);
    EXPECT_FALSE(harness->core.status().has_command);
  }
  {
    // A driver without a local domain admits nothing, not even a command that
    // carries the same empty domain.
    Go2AssetConfig config = asset();
    config.clock_domain_id.clear();
    Go2DriverCore core(config, [] { return 10.0; },
                       [] { return 100'000'000'000LL; },
                       [] { return 10'000'000'000LL; });
    VelocityCommand message;
    message.stamp = Stamp{100, 0};
    message.frame_id = "go2_01/base_link";
    message.values = kMotionTwist;
    message.clock_domain_id.clear();
    message.issued_monotonic_ns = 10'000'000'000LL;
    message.expires_monotonic_ns = 10'100'000'000LL;
    EXPECT_FALSE(core.acceptCommand(message));
    EXPECT_FALSE(core.status().has_command);
  }
}

// A failing transport clock fails closed: no motion is admitted, the cached
// command is disarmed and the next step writes a zero.
TEST(Go2CoreTest, OneShotAdmissionSecondsFailureCannotBeHiddenByRecovery) {
  auto harness = ready();
  const auto failed_read = harness->clock.seconds_reads + 1;
  harness->clock.seconds_hook = [failed_read](std::int64_t read) {
    if (read == failed_read) throw std::runtime_error("one-shot clock failure");
  };
  EXPECT_FALSE(harness->core.acceptCommand(command(harness->clock)));
  EXPECT_FALSE(harness->core.status().has_command);
  EXPECT_TRUE(harness->core.status().stop_pending);
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
}

TEST(Go2CoreTest, AClockFailureAdmitsNoMotionAndRequestsALocalZero) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  ASSERT_EQ(harness->client.calls.back(), kMotion);

  harness->clock.fail_mono = true;
  const std::size_t before = harness->client.calls.size();
  EXPECT_FALSE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  EXPECT_FALSE(harness->core.status().has_command);
  EXPECT_FALSE(harness->core.status().moving);
  // Every output after the clock failure is a zero.
  for (std::size_t index = before; index < harness->client.calls.size(); ++index) {
    EXPECT_EQ(harness->client.calls[index], kZero);
  }
}

// The armed lifetime of a command is bounded by the configured cap and every
// arithmetic step is overflow safe.
TEST(Go2CoreTest, TheCommandCapAndInt64OverflowAreRefused) {
  {
    auto harness = ready();
    // A deadline beyond the configured cap is refused.
    EXPECT_FALSE(harness->core.acceptCommand(
      command(harness->clock, kMotionTwist, std::nullopt, "go2_01/base_link", 0,
              600 * kMs)));
    // The largest possible deadline is refused without overflowing.
    VelocityCommand huge = command(harness->clock);
    huge.expires_monotonic_ns = std::numeric_limits<std::int64_t>::max();
    EXPECT_FALSE(harness->core.acceptCommand(huge));
    harness->core.step(harness->client);
    expectOnlyZeros(harness->client);
    EXPECT_FALSE(harness->core.status().has_command);
  }
  {
    // A cap that is not a usable interval refuses every command.
    Go2AssetConfig config = asset();
    config.limits.command_timeout_s = 1e19;
    Go2DriverCore core(config, [] { return 10.0; },
                       [] { return 100'000'000'000LL; },
                       [] { return 10'000'000'000LL; });
    VelocityCommand message;
    message.stamp = Stamp{100, 0};
    message.frame_id = "go2_01/base_link";
    message.values = kMotionTwist;
    message.clock_domain_id = kDomain;
    message.issued_monotonic_ns = 10'000'000'000LL;
    message.expires_monotonic_ns = 10'100'000'000LL;
    EXPECT_FALSE(core.acceptCommand(message));
  }
}

// The final pre-SDK check is bound to the selected sample. A newer cached
// command with a longer lifetime must not extend the selected interval.
TEST(Go2CoreTest, TheSelectedSampleExpiresEvenWhenANewerCommandIsCached) {
  auto harness = ready();
  const std::int64_t issued = harness->clock.mono_ns;
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  ASSERT_EQ(harness->client.calls.back(), kMotion);

  // The second monotonic reading of the next step is the final pre-SDK
  // check. The newer command is cached right there, and the selected
  // deadline passes in the same instant.
  bool injected = false;
  harness->clock.mono_reads = 0;
  harness->clock.mono_hook = [&](std::int64_t reads) {
    if (injected || reads != 2) {
      return;
    }
    injected = true;
    harness->clock.mono_ns = issued + 200 * kMs;
    VelocityCommand newer = command(harness->clock);
    newer.issued_monotonic_ns = issued + 200 * kMs;
    newer.expires_monotonic_ns = issued + 300 * kMs;
    EXPECT_TRUE(harness->core.acceptCommand(newer));
  };
  harness->core.step(harness->client);
  // The expired selection never reached the SDK: a zero was written instead.
  EXPECT_EQ(harness->client.calls.back(), kZero);
  EXPECT_FALSE(harness->core.status().moving);
  // The newer command is still armed and is written by the next step.
  EXPECT_TRUE(harness->core.status().has_command);
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kMotion);
}

// The check after the SDK call is bound to the selected sample as well: a
// newer command that arrives during the call must not excuse an already
// expired selection. The expired motion is zeroed in the same iteration and
// the newer command is written by a later iteration only.
TEST(Go2CoreTest, AnExpiredSelectionIsZeroedWhenANewerCommandArrivesDuringTheSdkCall) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  // The SDK call advances the transport clock past the deadline of the
  // selected command and installs a newer command with a longer lifetime.
  const Move delayed = [&](double x, double y, double z) {
    const int result = harness->client(x, y, z);
    if (x != 0.0 || y != 0.0 || z != 0.0) {
      harness->clock.advanceMonotonic(150 * kMs);
      VelocityCommand newer = command(harness->clock, kMotionBTwist);
      newer.issued_monotonic_ns = harness->clock.mono_ns;
      newer.expires_monotonic_ns = harness->clock.mono_ns + 400 * kMs;
      EXPECT_TRUE(harness->core.acceptCommand(newer));
    }
    return result;
  };
  const std::size_t before = harness->client.calls.size();
  harness->core.step(delayed);
  // The expired selection reached the SDK, and its zero followed immediately.
  ASSERT_EQ(harness->client.calls.size(), before + 2);
  EXPECT_EQ(harness->client.calls[before], kMotion);
  EXPECT_EQ(harness->client.calls[before + 1], kZero);
  EXPECT_FALSE(harness->core.status().moving);
  // The newer command survives, but it is written by a later iteration only.
  EXPECT_TRUE(harness->core.status().has_command);
  harness->core.step(harness->client);
  ASSERT_EQ(harness->client.calls.size(), before + 3);
  EXPECT_EQ(harness->client.calls[before + 2], kMotionB);
}

// A seconds-clock failure that is observed at the final pre-SDK check cannot
// permit a nonzero write. The driver writes a zero and drops the cached
// motion; the fault is not latched, so fresh input may recover.
TEST(Go2CoreTest, ASecondsClockFailureAtTheFinalCheckAdmitsNoMotion) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  ASSERT_EQ(harness->client.calls.back(), kMotion);

  // The seconds clock fails during the final pre-SDK reading of the next
  // step and recovers immediately afterwards.
  harness->clock.seconds_reads = 0;
  harness->clock.seconds_hook = [](std::int64_t reads) {
    if (reads != 2) {
      return;
    }
    throw std::runtime_error("injected seconds clock failure");
  };
  const std::size_t before = harness->client.calls.size();
  harness->core.step(harness->client);
  harness->clock.seconds_hook = nullptr;
  // No nonzero reached the SDK, a zero was attempted and the cached motion
  // is gone.
  ASSERT_EQ(harness->client.calls.size(), before + 1);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  EXPECT_FALSE(harness->core.status().has_command);
  EXPECT_FALSE(harness->core.status().moving);
}

// A transport-clock failure at the final pre-SDK check invalidates every
// cached motion, also the replacement that arrived during the same reading.
// The replacement must not resume without a fresh post-failure input.
TEST(Go2CoreTest, AClockFailureDisarmsAReplacementThatArrivedDuringTheFinalCheck) {
  auto harness = ready();
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  ASSERT_EQ(harness->client.calls.back(), kMotion);

  // The final pre-SDK reading of the next step installs a valid newer
  // command and then fails once. Every later reading recovers.
  bool injected = false;
  std::int64_t valid_mono_ns = harness->clock.mono_ns;
  harness->clock.mono_reads = 0;
  harness->clock.mono_hook = [&](std::int64_t reads) {
    if (injected) {
      // Every later reading of the transport clock recovers.
      harness->clock.mono_ns = valid_mono_ns;
      return;
    }
    if (reads != 2) {
      return;
    }
    injected = true;
    harness->clock.advanceMonotonic(10 * kMs);
    valid_mono_ns = harness->clock.mono_ns;
    VelocityCommand newer = command(harness->clock, kMotionBTwist);
    newer.issued_monotonic_ns = valid_mono_ns;
    newer.expires_monotonic_ns = valid_mono_ns + 200 * kMs;
    EXPECT_TRUE(harness->core.acceptCommand(newer));
    // This single outer reading is invalid.
    harness->clock.mono_ns = 0;
  };
  const std::size_t before = harness->client.calls.size();
  harness->core.step(harness->client);
  harness->clock.mono_hook = nullptr;
  // A zero was written and every cached motion is gone.
  ASSERT_EQ(harness->client.calls.size(), before + 1);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  EXPECT_FALSE(harness->core.status().has_command);
  // The next recovered tick cannot emit the replacement on its own.
  harness->core.step(harness->client);
  for (std::size_t index = before; index < harness->client.calls.size(); ++index) {
    EXPECT_EQ(harness->client.calls[index], kZero);
  }
  EXPECT_FALSE(harness->core.status().has_command);
}

TEST(Go2CoreTest, FinalTelemetryObservationCannotLeaveInvalidOutputActive) {
  // All earlier reads and the SDK call succeed. The fourth clock sample is
  // the final telemetry observation; it must enforce newly observed failure
  // or expiry before returning, with no cache lock held across the zero.
  for (const bool clock_failure : {false, true}) {
    auto harness = ready();
    ASSERT_TRUE(harness->core.acceptCommand(command(harness->clock)));
    harness->client.calls.clear();
    const auto fourth_read = harness->clock.seconds_reads + 4;
    harness->clock.seconds_hook = [&, fourth_read](auto count) {
      if (count != fourth_read) return;
      if (clock_failure) throw std::runtime_error("final clock failure");
      harness->clock.advanceMonotonic(200 * kMs);
    };
    EXPECT_FALSE(harness->core.step(harness->client).has_value());
    ASSERT_EQ(harness->client.calls.size(), 2u);
    EXPECT_EQ(harness->client.calls[0], kMotion);
    EXPECT_EQ(harness->client.calls[1], kZero);
    EXPECT_FALSE(harness->core.status().has_command);
    harness->clock.seconds_hook = {};
  }
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

TEST(Go2CoreTest, ReplayedCommandWithFrozenRosClockCannotRefreshOrRestart) {
  auto harness = ready();
  const VelocityCommand message = command(harness->clock);
  EXPECT_TRUE(harness->core.acceptCommand(message));
  harness->core.step(harness->client);
  for (int index = 1; index <= 7; ++index) {
    harness->clock.advanceMonotonic(20 * kMs);
    EXPECT_TRUE(harness->core.acceptState(state(false, 0, true, index + 1)));
    EXPECT_FALSE(harness->core.acceptCommand(message));
    harness->core.step(harness->client);
    if (index >= 5) {
      EXPECT_EQ(harness->client.calls.back(), kZero);
      EXPECT_FALSE(harness->core.status().has_command);
    }
  }
  EXPECT_TRUE(harness->core.acceptCommand(stopCommand(harness->clock)));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
}

TEST(Go2CoreTest, StopDoesNotAllowAnOlderCommandToRestartMotion) {
  auto harness = ready();
  const VelocityCommand message = command(harness->clock);
  EXPECT_TRUE(harness->core.acceptCommand(message));
  harness->core.step(harness->client);
  EXPECT_TRUE(harness->core.acceptCommand(stopCommand(harness->clock)));
  harness->core.step(harness->client);
  EXPECT_FALSE(harness->core.acceptCommand(message));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
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

// A zero command with stop == false is ordinary motion: it needs the regular
// deadline admission and it is not an implicit stop.
TEST(Go2CoreTest, AZeroCommandWithoutStopUsesTheOrdinaryDeadlineAdmission) {
  auto harness = ready();
  const std::array<double, 6> zero{0, 0, 0, 0, 0, 0};
  // A zero without a deadline is refused.
  VelocityCommand without_deadline = command(harness->clock, zero);
  without_deadline.expires_monotonic_ns = 0;
  EXPECT_FALSE(harness->core.acceptCommand(without_deadline));
  // A zero with an expired deadline is refused.
  EXPECT_FALSE(harness->core.acceptCommand(
    command(harness->clock, zero, std::nullopt, "go2_01/base_link", -200 * kMs)));
  // A refused packet requests a local zero: the driver settles it first.
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  // A valid motion command is admitted.
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock)));
  harness->core.step(harness->client);
  ASSERT_EQ(harness->client.calls.back(), kMotion);
  // A zero with a later issue time is admitted as ordinary motion and
  // replaces the armed motion.
  harness->clock.advance(0.001);
  EXPECT_TRUE(harness->core.acceptCommand(command(harness->clock, zero)));
  harness->core.step(harness->client);
  EXPECT_EQ(harness->client.calls.back(), kZero);
  EXPECT_FALSE(harness->core.status().moving);
}

TEST(Go2CoreTest, WrongFrameOrMissingOrMalformedStampNeverMoves) {
  struct Case {
    std::string frame = "go2_01/base_link";
    std::optional<std::int64_t> stamp;
  };
  const std::vector<Case> cases{
    {"map", std::nullopt}, {"", std::nullopt},
    {"go2_01/base_link", 0}, {"go2_01/base_link", -1'000'000'000},
  };
  for (const Case & c : cases) {
    auto harness = ready();
    EXPECT_FALSE(harness->core.acceptCommand(
      command(harness->clock, kMotionTwist, c.stamp, c.frame)));
    harness->core.step(harness->client);
    expectOnlyZeros(harness->client);
  }
  {
    // A malformed nanosecond field is refused as well.
    auto harness = ready();
    VelocityCommand message = command(harness->clock);
    message.stamp = Stamp{1, 1'000'000'000};
    EXPECT_FALSE(harness->core.acceptCommand(message));
    harness->core.step(harness->client);
    expectOnlyZeros(harness->client);
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
    EXPECT_EQ(harness->client.calls[harness->client.calls.size() - 2], kMotion);
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

// The explicit stop stays available while a fault is latched, and the stop
// never clears the fault.
TEST(Go2CoreTest, StopCommandIsAllowedAfterFaultAndUsesOnlyMove) {
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
  // The explicit stop is accepted even while the fault is latched.
  EXPECT_TRUE(harness->core.acceptCommand(stopCommand(harness->clock)));
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
      harness->clock.advanceMonotonic(200 * kMs);
    }
    return result;
  };
  harness->core.step(delayed);
  ASSERT_GE(harness->client.calls.size(), 2u);
  EXPECT_EQ(harness->client.calls[harness->client.calls.size() - 2], kMotion);
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
    VelocityCommand message = command(harness->clock);
    message.stamp = Stamp{bad_sec, 0};
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

// The watermark of the driver is the issue time of the transport, never the
// original source stamp.
TEST(Go2CoreTest, TheWatermarkIsTheIssuedIdentityAndNotTheSourceStamp) {
  auto harness = ready();
  const std::int64_t source_stamp = harness->clock.ros - 5'000'000'000;
  const VelocityCommand message =
    command(harness->clock, kMotionTwist, source_stamp);
  EXPECT_TRUE(harness->core.acceptCommand(message));
  EXPECT_EQ(harness->core.status().last_command_issued_mono_ns,
            message.issued_monotonic_ns);
  EXPECT_NE(harness->core.status().last_command_issued_mono_ns, source_stamp);
}

}  // namespace
