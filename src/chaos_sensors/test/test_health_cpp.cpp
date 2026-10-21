// C++ contract tests for chaos::sensors::HealthTracker, ported from
// test/test_health.py.
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "chaos_sensors/health.hpp"

namespace {

using chaos::sensors::HealthTracker;

struct Clocks {
  double monotonic_s = 100.0;
  std::int64_t ros_ns = 1'700'000'000'000'000'000;

  HealthTracker tracker(double timeout = 2.0) {
    return HealthTracker("camera_frame", timeout,
                         [this] { return ros_ns; },
                         [this] { return monotonic_s; });
  }

  void observe(HealthTracker & tracker, std::int64_t offset_ns = 0,
               const std::string & frame = "camera_frame") {
    const std::int64_t stamp = ros_ns + offset_ns;
    tracker.observe(stamp / 1'000'000'000, stamp % 1'000'000'000, frame);
  }
};

TEST(HealthTest, NoMessagesIsStaleAndFirstFreshMessageRecovers) {
  Clocks clocks;
  HealthTracker tracker = clocks.tracker();
  EXPECT_EQ(tracker.snapshot().level, 3);
  EXPECT_EQ(tracker.snapshot().values.at("receipt"), "no data");
  clocks.observe(tracker);
  EXPECT_EQ(tracker.snapshot().level, 0);
  EXPECT_EQ(tracker.snapshot().values.at("received_count"), "1");
}

TEST(HealthTest, ReceiptTimeoutUsesMonotonicEvenWhenRosClockPaused) {
  Clocks clocks;
  HealthTracker tracker = clocks.tracker();
  clocks.observe(tracker);
  clocks.monotonic_s += 2.1;
  const auto health = tracker.snapshot();
  EXPECT_EQ(health.level, 3);
  EXPECT_EQ(health.values.at("receipt"), "timeout");
  EXPECT_EQ(health.values.at("source"), "ok");
}

TEST(HealthTest, RecentReceiptDoesNotHideOldMeasurement) {
  Clocks clocks;
  HealthTracker tracker = clocks.tracker();
  clocks.observe(tracker, -3'000'000'000);
  const auto health = tracker.snapshot();
  EXPECT_EQ(health.level, 2);
  EXPECT_EQ(health.values.at("receipt"), "ok");
  EXPECT_EQ(health.values.at("source"), "source timestamp expired");
}

TEST(HealthTest, SourceAgeBoundaries) {
  for (const auto & [offset, expected] :
       {std::pair<std::int64_t, int>{100'000'000, 0},
        {100'000'001, 2}, {-2'000'000'000, 0}, {-2'000'000'001, 2}}) {
    Clocks clocks;
    HealthTracker tracker = clocks.tracker();
    clocks.observe(tracker, offset);
    EXPECT_EQ(tracker.snapshot().level, expected);
  }
}

TEST(HealthTest, ZeroAndInvalidTimestamps) {
  struct Case {
    std::int64_t sec;
    std::int64_t nanosec;
    const char * problem;
  };
  for (const Case & c : {Case{0, 0, "zero source timestamp"},
                         Case{-1, 0, "invalid source timestamp"},
                         Case{10, 1'000'000'000, "invalid source timestamp"}}) {
    Clocks clocks;
    HealthTracker tracker = clocks.tracker();
    tracker.observe(c.sec, c.nanosec, "camera_frame");
    EXPECT_EQ(tracker.snapshot().level, 2);
    EXPECT_EQ(tracker.snapshot().values.at("source"), c.problem);
    EXPECT_EQ(tracker.snapshot().values.at("invalid_stamp_count"), "1");
  }
}

TEST(HealthTest, RepeatedStampIsBadBeforeAgeTimeoutAndRecovers) {
  Clocks clocks;
  HealthTracker tracker = clocks.tracker();
  clocks.observe(tracker);
  clocks.observe(tracker);
  EXPECT_EQ(tracker.snapshot().values.at("source"),
            "source timestamp did not advance");
  EXPECT_EQ(tracker.snapshot().level, 2);
  clocks.ros_ns += 100'000'000;
  clocks.observe(tracker);
  EXPECT_EQ(tracker.snapshot().level, 0);
  EXPECT_EQ(tracker.snapshot().values.at("nonforward_stamp_count"), "1");
}

TEST(HealthTest, SensorClockRestartRecoversAfterOneAdvancingSample) {
  Clocks clocks;
  HealthTracker tracker = clocks.tracker();
  clocks.observe(tracker);
  clocks.ros_ns -= 1'000'000'000;
  clocks.observe(tracker);
  EXPECT_EQ(tracker.snapshot().level, 2);
  clocks.ros_ns += 100'000'000;
  clocks.observe(tracker);
  EXPECT_EQ(tracker.snapshot().level, 0);
}

TEST(HealthTest, FrameMismatchHasIndependentDiagnosticAndRecovers) {
  Clocks clocks;
  HealthTracker tracker = clocks.tracker();
  clocks.observe(tracker, 0, "other_frame");
  const auto health = tracker.snapshot();
  EXPECT_EQ(health.level, 2);
  EXPECT_EQ(health.values.at("source"), "ok");
  EXPECT_EQ(health.values.at("frame"), "mismatch");
  clocks.ros_ns += 100'000'000;
  clocks.observe(tracker);
  EXPECT_EQ(tracker.snapshot().level, 0);
  EXPECT_EQ(tracker.snapshot().values.at("frame_mismatch_count"), "1");
}

TEST(HealthTest, InvalidTimeoutIsRejected) {
  for (const double timeout :
       {0.0, -1.0, 0.000999, 300.000001,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()}) {
    Clocks clocks;
    EXPECT_THROW(clocks.tracker(timeout), std::invalid_argument);
  }
}

// Stamps outside the ROS int32-second range are invalid source timestamps,
// never overflowed arithmetic (reviewer UBSan finding).
TEST(HealthTest, OutOfRangeStampIsInvalidSource) {
  Clocks clocks;
  HealthTracker tracker = clocks.tracker();
  tracker.observe(std::numeric_limits<std::int64_t>::max(), 0,
                  "camera_frame");
  const auto health = tracker.snapshot();
  EXPECT_EQ(health.level, 2);
  EXPECT_EQ(health.values.at("source"), "invalid source timestamp");
  EXPECT_EQ(health.values.at("invalid_stamp_count"), "1");
  EXPECT_EQ(tracker.receivedCount(), 1);
}

}  // namespace
