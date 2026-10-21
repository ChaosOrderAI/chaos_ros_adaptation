// C++ contract tests for chaos::control, ported case-by-case from
// test/test_control.py (the Python oracle stays in Git history).
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "chaos_runtime/control.hpp"

namespace {

using chaos::Rejected;
using chaos::control::CommandDeadline;
using chaos::control::ControlGuard;
using chaos::control::Limits;
using chaos::control::RosTime;
using chaos::control::validate_pose;

void expectRejectedStatic(const std::string & code,
                          const std::function<void()> & callback) {
  try {
    callback();
    FAIL() << "expected Rejected with code " << code;
  } catch (const Rejected & error) {
    EXPECT_EQ(error.code(), code);
  }
}

class FakeClock {
public:
  double value = 100.0;
  double operator()() const { return value; }
};

class ControlTest : public testing::Test {
protected:
  void SetUp() override {
    clock_ = std::make_shared<FakeClock>();
    guard_ = std::make_unique<ControlGuard>(
      Limits(), [this] { return clock_->value; }, [] { return std::string("secret"); });
  }

  void command(const std::string & token = "secret", double stamp_s = 200.0,
               double ros_now_s = 200.0, double valid_for_s = 0.5,
               const std::string & frame = "robot/base",
               const std::string & base_frame = "robot/base",
               const std::array<double, 6> & values =
                 {0.5, 0.0, 0.0, 0.0, 0.0, 0.0}) {
    guard_->velocity(token, stamp_s, ros_now_s, valid_for_s, frame,
                     base_frame, values);
  }

  void expectRejected(const std::string & code,
                      const std::function<void()> & callback) {
    try {
      callback();
      FAIL() << "expected Rejected with code " << code;
    } catch (const Rejected & error) {
      EXPECT_EQ(error.code(), code);
    }
  }

  std::shared_ptr<FakeClock> clock_;
  std::unique_ptr<ControlGuard> guard_;
};

TEST_F(ControlTest, LeaseContentionRenewalAndExactExpiry) {
  auto acquired = guard_->lease("acquire", "operator", "", 5);
  EXPECT_EQ(acquired.first, "secret");
  EXPECT_DOUBLE_EQ(acquired.second, 5.0);
  expectRejected("BUSY", [this] { guard_->lease("acquire", "other"); });
  expectRejected("NO_CONTROL",
                 [this] { guard_->lease("renew", "other", "secret", 5); });
  clock_->value += 4;
  guard_->lease("renew", "operator", "secret", 5);
  EXPECT_DOUBLE_EQ(guard_->remaining(), 5.0);
  clock_->value += 5;
  expectRejected("NO_CONTROL", [this] { guard_->require("secret"); });
  auto reacquired = guard_->lease("acquire", "other", "", 3);
  EXPECT_DOUBLE_EQ(reacquired.second, 3.0);
}

TEST_F(ControlTest, MalformedTokensRejectedWithoutExceptionLeak) {
  guard_->lease("acquire", "operator");
  for (const std::string & token :
       std::vector<std::string>{"", "wrong", "\xF0\x9F\x94\x92"}) {
    expectRejected("NO_CONTROL", [&] { guard_->require(token); });
  }
}

TEST_F(ControlTest, InvalidLeaseDuration) {
  for (double duration : {0.0, -1.0, std::nan(""), std::numeric_limits<double>::infinity()}) {
    expectRejected("INVALID_ARGUMENT", [&] {
      guard_->lease("acquire", "operator", "", duration);
    });
  }
}

TEST_F(ControlTest, BoundedLeaseAndOwnerRelease) {
  expectRejected("OUT_OF_RANGE",
                 [this] { guard_->lease("acquire", "operator", "", 31); });
  guard_->lease("acquire", "operator");
  expectRejected("NO_CONTROL",
                 [this] { guard_->lease("release", "other", "secret", 0); });
  guard_->lease("release", "operator", "secret", 0);
  EXPECT_DOUBLE_EQ(guard_->remaining(), 0.0);
}

TEST_F(ControlTest, IdleWatchdogAndTransitConsumesTtl) {
  EXPECT_FALSE(guard_->velocityTimedOut());
  guard_->lease("acquire", "operator");
  command("secret", 200.0, 200.3);
  EXPECT_NEAR(guard_->velocityExpires(), 100.2, 1e-9);
  clock_->value += 0.19;
  EXPECT_FALSE(guard_->velocityTimedOut());
  clock_->value += 0.02;
  EXPECT_TRUE(guard_->velocityTimedOut());
}

TEST_F(ControlTest, MonotonicWatchdogIndependentOfSourceClock) {
  guard_->lease("acquire", "operator");
  command();
  guard_->lease("renew", "operator", "secret", 20);
  clock_->value += 0.5;
  // Renew never extends a velocity sample.
  EXPECT_TRUE(guard_->velocityTimedOut());
}

TEST_F(ControlTest, VelocityRejections) {
  guard_->lease("acquire", "operator");
  struct Case {
    const char * code;
    double stamp_s = 200.0;
    double ros_now_s = 200.0;
    double valid_for_s = 0.5;
    std::string frame = "robot/base";
    std::array<double, 6> values{0.5, 0.0, 0.0, 0.0, 0.0, 0.0};
  };
  const std::vector<Case> cases{
    {"INVALID_FRAME", 200.0, 200.0, 0.5, "map", {0.5, 0, 0, 0, 0, 0}},
    {"INVALID_ARGUMENT", 200.0, 200.0, 0.5, "robot/base", {std::nan(""), 0, 0, 0, 0, 0}},
    {"OUT_OF_RANGE", 200.0, 200.0, 0.5, "robot/base", {0.8, 0.8, 0, 0, 0, 0}},
    {"OUT_OF_RANGE", 200.0, 200.0, 0.5, "robot/base", {0, 0, 0, 0, 0, 1.01}},
    {"OUT_OF_RANGE", 200.0, 200.0, 0.6, "robot/base", {0.5, 0, 0, 0, 0, 0}},
    {"INVALID_ARGUMENT", 200.0, 200.0, -1.0, "robot/base", {0.5, 0, 0, 0, 0, 0}},
    {"STALE_COMMAND", 200.0, 200.5, 0.5, "robot/base", {0.5, 0, 0, 0, 0, 0}},
    {"STALE_COMMAND", 201.0, 200.0, 0.5, "robot/base", {0.5, 0, 0, 0, 0, 0}},
    {"STALE_COMMAND", 0.0, 200.0, 0.5, "robot/base", {0.5, 0, 0, 0, 0, 0}},
    {"STALE_COMMAND", std::nan(""), 200.0, 0.5, "robot/base", {0.5, 0, 0, 0, 0, 0}},
  };
  for (const Case & c : cases) {
    expectRejected(c.code, [&] {
      guard_->velocity("secret", c.stamp_s, c.ros_now_s, c.valid_for_s,
                       c.frame, "robot/base", c.values);
    });
    EXPECT_DOUBLE_EQ(guard_->velocityExpires(), 0.0);
  }
}

TEST_F(ControlTest, InvalidPacketCannotExtendValidCommand) {
  guard_->lease("acquire", "operator");
  command();
  const double deadline = guard_->velocityExpires();
  clock_->value += 0.2;
  expectRejected("INVALID_FRAME", [this] { command("secret", 200.0, 200.0, 0.5, "wrong"); });
  EXPECT_DOUBLE_EQ(guard_->velocityExpires(), deadline);
}

TEST_F(ControlTest, PoseContract) {
  EXPECT_NO_THROW(validate_pose("map", "map", {1, 2, 0}, {0, 0, 0, 1}, "ugv", 20));
  expectRejected("INVALID_ARGUMENT", [] {
    validate_pose("map", "map", {1, 2, 0}, {0, 0, 0, 0}, "ugv", 20);
  });
  expectRejected("UNSUPPORTED", [] {
    validate_pose("map", "map", {1, 2, 1}, {0, 0, 0, 1}, "ugv", 20);
  });
  expectRejected("OUT_OF_RANGE", [] {
    validate_pose("map", "map", {1, 2, 21}, {0, 0, 0, 1}, "uav", 20);
  });
}

// Backward wall-clock change must not extend the captured TTL.
TEST(DeadlineTest, BackwardWallClockChangeDoesNotExtendTtl) {
  std::int64_t wall = 100'000'000'000;
  std::int64_t monotonic = 5'000'000'000;
  CommandDeadline deadline =
    CommandDeadline::capture(RosTime{100, 10'000'000}, wall, monotonic);
  EXPECT_NO_THROW(deadline.require(wall, monotonic));
  wall -= 1'000'000'000;
  monotonic += 10'000'000;
  EXPECT_THROW(deadline.require(wall, monotonic), Rejected);
  try {
    deadline.require(wall, monotonic);
    FAIL() << "expected Rejected";
  } catch (const Rejected & error) {
    EXPECT_EQ(error.code(), "STALE_COMMAND");
  }
}

TEST(DeadlineTest, InvalidDeadlineIsRejected) {
  for (const RosTime stamp : {RosTime{-1, 0}, RosTime{0, 1'000'000'000},
                              RosTime{1, -1}}) {
    const CommandDeadline deadline = CommandDeadline::capture(stamp, 0, 0);
    try {
      deadline.require(0, 0);
      FAIL() << "expected Rejected";
    } catch (const Rejected & error) {
      EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
    }
  }
}

TEST(DeadlineTest, ZeroDeadlinePreservesDirectRosCompatibility) {
  EXPECT_NO_THROW(
    CommandDeadline::capture(RosTime{0, 0}, 0, 0).require(0, 0));
}

// Stamps outside the ROS int32-second range must be rejected instead of
// overflowing int64 arithmetic (reviewer UBSan finding).
TEST(DeadlineTest, OutOfRangeStampsAreRejectedWithoutOverflow) {
  for (const RosTime stamp : {RosTime{std::numeric_limits<std::int64_t>::max(), 0},
                              RosTime{4294967296LL, 0},
                              RosTime{2147483648LL, 0},
                              RosTime{0, -1},
                              RosTime{0, 1'000'000'000}}) {
    const CommandDeadline deadline = CommandDeadline::capture(stamp, 0, 0);
    EXPECT_FALSE(deadline.valid());
    try {
      deadline.require(0, 0);
      FAIL() << "expected Rejected";
    } catch (const Rejected & error) {
      EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
    }
  }
  // The largest in-range stamp is accepted and cannot overflow the
  // monotonic bound.
  const CommandDeadline edge =
    CommandDeadline::capture(RosTime{2147483647LL, 999999999LL},
                             0, 0);
  EXPECT_TRUE(edge.valid());
  EXPECT_NO_THROW(edge.require(0, 0));
}

TEST(ControlGuardTest, OwnerUnicodeSemantics) {
  ControlGuard guard(Limits{}, [] { return 100.0; },
                     [] { return std::string("secret"); });
  // 128 Unicode characters (384 bytes) are within the Python limit.
  std::string wide;
  for (int i = 0; i < 128; ++i) {
    wide += "\xE4\xB8\xAD";  // U+4E2D, one CJK code point per 3 bytes
  }
  EXPECT_NO_THROW(guard.lease("acquire", wide));
  guard.lease("release", wide, "secret", 0);
  // Whitespace-only owners are rejected, including Unicode whitespace
  // (Python str.strip() semantics).
  expectRejectedStatic("INVALID_ARGUMENT", [&] {
    guard.lease("acquire", "\xE3\x80\x80");  // U+3000 ideographic space
  });
  // Malformed UTF-8 is not a valid owner.
  expectRejectedStatic("INVALID_ARGUMENT", [&] {
    guard.lease("acquire", std::string("\xff\xfe", 2));
  });
  // Overlong encodings and surrogate code points are rejected, not
  // silently re-decoded.
  expectRejectedStatic("INVALID_ARGUMENT", [&] {
    guard.lease("acquire", std::string("\xc0\xaf", 2));  // overlong '/'
  });
  expectRejectedStatic("INVALID_ARGUMENT", [&] {
    guard.lease("acquire", std::string("\xed\xa0\x80", 3));  // U+D800 surrogate
  });
  // 129 characters exceed the limit even when they fit in fewer bytes.
  expectRejectedStatic("INVALID_ARGUMENT", [&] {
    guard.lease("acquire", std::string(129, 'a'));
  });
}

TEST(ControlGuardTest, TokenGenerationFailureNeverArmsLease) {
  ControlGuard guard(Limits{}, [] { return 100.0; },
                     []() -> std::string {
                       throw std::runtime_error("csprng unavailable");
                     });
  EXPECT_THROW(guard.lease("acquire", "operator"), std::runtime_error);
  // No lease was armed and no empty token was accepted.
  EXPECT_FALSE(guard.leaseActive());
  EXPECT_DOUBLE_EQ(guard.remaining(), 0.0);
  EXPECT_THROW(guard.require(""), Rejected);
}

}  // namespace
