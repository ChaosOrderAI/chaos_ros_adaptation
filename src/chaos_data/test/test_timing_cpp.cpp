// C++ contract tests for chaos::data timing utilities, ported from
// test/test_timing.py.
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "chaos_data/timing.hpp"

namespace {

using chaos::data::ChronyProbe;
using chaos::data::ChronycResult;
using chaos::data::ClockGuard;
using chaos::data::TimeSample;
using chaos::data::parseTracking;

constexpr std::int64_t kWall = 1'700'000'000'000'000'000;
constexpr std::int64_t kMono = 1'000'000'000;
const std::string kTracking =
  "CB00710F,203.0.113.15,3,1699999999.0,0.001,-0.0001,0.0002,1.2,0.1,0.02,"
  "0.002,0.0005,64.0,Normal\n";

TimeSample sample(const std::string & output = kTracking,
                  std::int64_t tick = 0) {
  return parseTracking(output, kWall + tick, kMono + tick);
}

std::string changed(std::size_t index, const std::string & value) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  const std::string stripped = kTracking.substr(0, kTracking.size() - 1);
  while (true) {
    const std::size_t comma = stripped.find(',', start);
    fields.push_back(stripped.substr(
      start, comma == std::string::npos ? std::string::npos : comma - start));
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  fields[index] = value;
  std::string result;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i) {
      result += ",";
    }
    result += fields[i];
  }
  return result + "\n";
}

TEST(TimingTest, TrackingKeepsSignedCorrectionAndAbsoluteBound) {
  const TimeSample result = sample(changed(4, "-0.001"));
  EXPECT_EQ(result.status, "synced");
  ASSERT_TRUE(result.offset_s.has_value());
  EXPECT_DOUBLE_EQ(*result.offset_s, -0.001);
  EXPECT_EQ(result.offset_basis, "ntp_minus_system");
  ASSERT_TRUE(result.uncertainty_s.has_value());
  EXPECT_NEAR(*result.uncertainty_s, 0.0025, 1e-12);
  ASSERT_TRUE(result.reference_id.has_value());
  EXPECT_EQ(*result.reference_id, "CB00710F");
}

TEST(TimingTest, UnsyncedOrExcessUncertaintyNeverPasses) {
  struct Case {
    std::size_t index;
    std::string value;
    const char * reason;
  };
  for (const Case & c :
       {Case{0, "00000000", "no_reference"},
        Case{0, "7F7F0101", "local_reference_without_external_sync"},
        Case{2, "0", "invalid_stratum"},
        Case{2, "16", "invalid_stratum"},
        Case{3, "0", "no_reference"},
        Case{3, "1699999879", "reference_stale"},
        Case{3, "1700000001", "reference_in_future"},
        Case{4, "0.02", "uncertainty_exceeded"},
        Case{10, ".03", "uncertainty_exceeded"},
        Case{11, ".03", "uncertainty_exceeded"},
        Case{13, "Not synchronised", "leap_status_not_normal"},
        Case{13, "Insert second", "leap_status_not_normal"},
        Case{13, "Delete second", "leap_status_not_normal"}}) {
    const TimeSample result = sample(changed(c.index, c.value));
    EXPECT_EQ(result.status, "unsynced");
    EXPECT_EQ(result.reason, c.reason);
  }
}

TEST(TimingTest, MalformedFieldsCannotBeMistakenForGoodClock) {
  const std::vector<std::string> outputs{
    "", kTracking + kTracking, kTracking.substr(0, kTracking.size() - 10),
    std::string(4097, 'x'), changed(0, "garbage"), changed(2, "3.0"),
    changed(2, "-1"), changed(4, "NaN"), changed(11, "inf"),
    changed(9, "-1"), changed(10, "-1"), changed(11, "-1"),
    changed(3, "1e100"), changed(4, "1e100"), changed(6, "nan"),
  };
  for (const std::string & output : outputs) {
    EXPECT_EQ(sample(output).status, "unknown") << "output: " << output;
  }
}

TEST(TimingTest, ProbeOnlyRunsReadOnlyTrackingCommand) {
  int calls = 0;
  const ChronyProbe probe(0.01, 120.0, 1.0,
                          [&](double) {
                            ++calls;
                            return ChronycResult{ChronycResult::Error::kNone, 0,
                                                 kTracking};
                          });
  const TimeSample result = probe.sample(kWall, kMono);
  EXPECT_EQ(result.status, "synced");
  EXPECT_EQ(calls, 1);
}

TEST(TimingTest, ProbeFailureRemainsUnknown) {
  struct Case {
    ChronycResult::Error error;
    const char * reason;
  };
  for (const Case & c :
       {Case{ChronycResult::Error::kNotFound, "chronyc_not_found"},
        Case{ChronycResult::Error::kTimeout, "chronyc_timeout"},
        Case{ChronycResult::Error::kUnavailable, "chronyc_unavailable"}}) {
    const ChronyProbe probe(0.01, 120.0, 1.0,
                            [&](double) {
                              return ChronycResult{c.error, 0, ""};
                            });
    const TimeSample result = probe.sample(kWall, kMono);
    EXPECT_EQ(result.status, "unknown");
    EXPECT_EQ(result.reason, c.reason);
    EXPECT_FALSE(result.uncertainty_s.has_value());
  }
}

TEST(TimingTest, NonzeroProbeExitCannotUseValidLookingStdout) {
  const ChronyProbe probe(0.01, 120.0, 1.0,
                          [&](double) {
                            return ChronycResult{ChronycResult::Error::kNone, 1,
                                                 kTracking};
                          });
  EXPECT_EQ(probe.sample(kWall, kMono).reason, "chronyc_failed");
}

// update() helper mirroring the pytest fixture: reading or sample() at
// (kWall + tick + offset, kMono + tick).
chaos::data::ClockDecision update(ClockGuard & guard, std::int64_t tick = 0,
                                  std::int64_t offset = 0,
                                  const TimeSample * reading = nullptr) {
  const TimeSample used = reading ? *reading : sample(kTracking, tick);
  return guard.update(used, kWall + tick + offset, kMono + tick);
}

TEST(TimingTest, GuardRequiresDistinctConsecutiveMeasurements) {
  ClockGuard guard;
  EXPECT_FALSE(update(guard).allowed);
  EXPECT_FALSE(update(guard).allowed);
  EXPECT_EQ(guard.stableSamples(), 1);
  EXPECT_FALSE(update(guard, 1'000'000'000).allowed);
  EXPECT_TRUE(update(guard, 2'000'000'000).allowed);
  EXPECT_EQ(guard.stableSamples(), 3);
}

TEST(TimingTest, BadSyncResetsAdmissionAndOldGoodSampleCannotRecover) {
  ClockGuard guard(1);
  const TimeSample old = sample();
  EXPECT_TRUE(update(guard, 0, 0, &old).allowed);
  TimeSample bad = sample("", 1'000'000'000);
  bad.status = "unknown";
  bad.reason = "offline";
  EXPECT_FALSE(update(guard, 1'000'000'000, 0, &bad).allowed);
  EXPECT_FALSE(update(guard, 2'000'000'000, 0, &old).allowed);
  EXPECT_TRUE(update(guard, 3'000'000'000).allowed);
}

TEST(TimingTest, SampleExpiryUsesMonotonicTimeAndRequiresFreshRecovery) {
  ClockGuard guard(1, 0.1, 2.0);
  const TimeSample old = sample();
  EXPECT_TRUE(update(guard, 0, 0, &old).allowed);
  EXPECT_TRUE(update(guard, 2'000'000'000, 0, &old).allowed);
  EXPECT_FALSE(update(guard, 2'000'000'001, 0, &old).allowed);
  EXPECT_TRUE(update(guard, 3'000'000'000).allowed);
}

TEST(TimingTest, ClockStepsMakeNewEpochAndNeedFreshStableSamples) {
  for (const std::int64_t step : {-200'000'000, 200'000'000}) {
    ClockGuard guard(1);
    EXPECT_TRUE(update(guard).allowed);
    const auto jumped = update(guard, 1'000'000'000, step);
    EXPECT_FALSE(jumped.allowed);
    EXPECT_EQ(jumped.reason, "wall_clock_jump");
    EXPECT_EQ(jumped.epoch, 1);
    EXPECT_EQ(jumped.jump_ns, step);
    EXPECT_EQ(jumped.stable_samples, 0);
    const auto recovered = update(guard, 2'000'000'000, step);
    EXPECT_TRUE(recovered.allowed);
    EXPECT_EQ(recovered.epoch, 1);
  }
}

TEST(TimingTest, PauseDoesNotForgetJumpBaselineOrReuseOldSample) {
  ClockGuard guard(1);
  EXPECT_TRUE(update(guard).allowed);
  guard.reset();
  EXPECT_FALSE(update(guard).allowed);
  EXPECT_TRUE(update(guard, 1'000'000'000).allowed);
  guard.reset();
  EXPECT_EQ(update(guard, 2'000'000'000, 300'000'000).epoch, 1);
}

TEST(TimingTest, ObserveModePreservesUnknownStatusAndEpoch) {
  ClockGuard guard(3, 0.1, 5.0, false);
  TimeSample unknown = sample();
  unknown.status = "unknown";
  unknown.reason = "missing";
  const auto decision = update(guard, 0, 0, &unknown);
  EXPECT_TRUE(decision.allowed);
  EXPECT_EQ(decision.reason, "missing");
  const auto jumped = update(guard, 1'000'000'000, -1'000'000'000);
  EXPECT_TRUE(jumped.allowed);
  EXPECT_EQ(jumped.epoch, 1);
  EXPECT_EQ(jumped.reason, "wall_clock_jump");
}

TEST(TimingTest, MonotonicReversalStartsEpoch) {
  ClockGuard guard(1);
  EXPECT_TRUE(update(guard).allowed);
  const auto decision = guard.update(sample(), kWall, kMono - 1);
  EXPECT_FALSE(decision.allowed);
  EXPECT_EQ(decision.epoch, 1);
  EXPECT_EQ(decision.reason, "monotonic_clock_reversed");
}

TEST(TimingTest, IncompleteSyncedSampleIsNotAccepted) {
  struct Case {
    bool has_uncertainty;
    double uncertainty;
    bool has_offset;
    double offset;
  };
  for (const Case & c :
       {Case{false, 0, true, 0.001}, Case{true, std::numeric_limits<double>::infinity(), true, 0.001},
        Case{true, -0.1, true, 0.001}, Case{true, 0.0025, true, std::numeric_limits<double>::quiet_NaN()},
        Case{true, 0.0025, false, 0}}) {
    ClockGuard guard(1);
    TimeSample incomplete = sample();
    if (c.has_uncertainty) {
      incomplete.uncertainty_s = c.uncertainty;
    } else {
      incomplete.uncertainty_s.reset();
    }
    if (c.has_offset) {
      incomplete.offset_s = c.offset;
    } else {
      incomplete.offset_s.reset();
    }
    EXPECT_FALSE(update(guard, 0, 0, &incomplete).allowed);
  }
}

TEST(TimingTest, InvalidGuardConfigurationRejected) {
  EXPECT_THROW(ClockGuard(0), std::invalid_argument);
  EXPECT_THROW(ClockGuard(3, std::numeric_limits<double>::quiet_NaN()),
               std::invalid_argument);
  EXPECT_THROW(ClockGuard(3, 0.1, 0), std::invalid_argument);
}

}  // namespace
