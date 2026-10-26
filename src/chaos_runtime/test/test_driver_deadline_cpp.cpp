// Pure unit tests for chaos::runtime::driver_deadline.
//
// No ROS, no robot, no vendor SDK, no DDS participant and no privileged
// namespace mutation is involved. The clock-domain identity is exercised
// through the pure parts function; the real local identity is only used for
// a shape and stability smoke check, never for a hardcoded host value.
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "chaos_runtime/driver_deadline.hpp"

namespace {

using chaos::runtime::driver_deadline::boundedSteadyDeadlineNs;
using chaos::runtime::driver_deadline::clockDomainIdFromParts;
using chaos::runtime::driver_deadline::floorDurationNs;
using chaos::runtime::driver_deadline::localClockDomainId;
using chaos::runtime::driver_deadline::monotonicNowNs;
using chaos::runtime::driver_deadline::sourceDeadlineNs;

constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMs = 1000000LL;

// Independent shape check of a canonical lower-case 8-4-4-4-12 uuid, used to
// inspect the identity produced by the helper without repeating its code.
bool looksLikeCanonicalUuid(const std::string & text) {
  if (text.size() != 36U) {
    return false;
  }
  for (std::size_t index = 0; index < 36U; ++index) {
    const char character = text[index];
    if (index == 8U || index == 13U || index == 18U || index == 23U) {
      if (character != '-') {
        return false;
      }
      continue;
    }
    const bool lower_hex = (character >= '0' && character <= '9') ||
                           (character >= 'a' && character <= 'f');
    if (!lower_hex) {
      return false;
    }
  }
  return true;
}

// --- floorDurationNs ------------------------------------------------------

TEST(DriverDeadlineFloor, PositiveSecondsFloorToWholeNanoseconds) {
  EXPECT_EQ(floorDurationNs(0.5), std::optional<std::int64_t>(500000000LL));
  EXPECT_EQ(floorDurationNs(1.0), std::optional<std::int64_t>(1000000000LL));
  EXPECT_EQ(floorDurationNs(1.5), std::optional<std::int64_t>(1500000000LL));
  // The remainder is truncated, never rounded up. The binary64 value of
  // 2.999999999 s is 2.999999998999999917259629000909626484813213348388671875 s
  // and its exact nanosecond product is
  // 2999999998.999999917259629000909626484813213348388671875 ns, so the
  // floor is 2999999998 ns. The previous expectation of 2999999999 ns was
  // one nanosecond too generous: it granted more than the original
  // interval allows.
  EXPECT_EQ(floorDurationNs(2.999999999),
            std::optional<std::int64_t>(2999999998LL));
}

TEST(DriverDeadlineFloor, SubNanosecondDurationsCannotArmMotion) {
  EXPECT_FALSE(floorDurationNs(0.5e-9).has_value());
  EXPECT_FALSE(floorDurationNs(1e-10).has_value());
  EXPECT_FALSE(floorDurationNs(1e-300).has_value());
  // One nanosecond is the smallest armable interval.
  const auto one = floorDurationNs(1e-9);
  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(*one, 1LL);
}

TEST(DriverDeadlineFloor, NonFiniteAndNonPositiveDurationsAreRejected) {
  EXPECT_FALSE(floorDurationNs(std::numeric_limits<double>::quiet_NaN())
                   .has_value());
  EXPECT_FALSE(
    floorDurationNs(std::numeric_limits<double>::infinity()).has_value());
  EXPECT_FALSE(
    floorDurationNs(-std::numeric_limits<double>::infinity()).has_value());
  EXPECT_FALSE(floorDurationNs(0.0).has_value());
  EXPECT_FALSE(floorDurationNs(-0.0).has_value());
  EXPECT_FALSE(floorDurationNs(-1.0).has_value());
}

TEST(DriverDeadlineFloor, Int64BoundariesAreCheckedBeforeTheCast) {
  const auto largest = floorDurationNs(9223372036.0);
  ASSERT_TRUE(largest.has_value());
  EXPECT_EQ(*largest, 9223372036000000000LL);
  // One second more does not fit into int64 nanoseconds.
  EXPECT_FALSE(floorDurationNs(9223372037.0).has_value());
  EXPECT_FALSE(floorDurationNs(1e19).has_value());
  EXPECT_FALSE(floorDurationNs(1e300).has_value());
}

TEST(DriverDeadlineFloor, ExactBinary64NeverExtendsTheInterval) {
  // 0x1.f9148c5ca2cbdp-5 s is the binary64 value
  // 0.0616553059999999999996057... s. Its exact nanosecond product is
  // 61655305.9999999999996057... ns, so the floor is exactly 61655305 ns.
  // A rounded product (the floating-point product of the previous
  // implementation) reported 61655306 ns and therefore granted one
  // nanosecond more than the original interval allows.
  EXPECT_EQ(floorDurationNs(0x1.f9148c5ca2cbdp-5),
            std::optional<std::int64_t>(61655305LL));
}

TEST(DriverDeadlineFloor, SubNanosecondAndSubnormalInputsAreRejectedExactly) {
  // The binary64 just below 1e-9 s is one ULP smaller, so its exact
  // nanosecond product is below 1 ns and its floor is 0 ns.
  EXPECT_FALSE(floorDurationNs(std::nextafter(1e-9, 0.0)).has_value());
  // The binary64 nearest to 1e-9 s is slightly above 1e-9 s, so its exact
  // nanosecond product is slightly above 1 ns and its floor is 1 ns.
  const auto one = floorDurationNs(1e-9);
  ASSERT_TRUE(one.has_value());
  EXPECT_EQ(*one, 1LL);
  // The smallest positive subnormal is far below one nanosecond.
  EXPECT_FALSE(
    floorDurationNs(std::numeric_limits<double>::denorm_min()).has_value());
}

TEST(DriverDeadlineFloor, TheLargestInt64NanosecondValueIsAcceptedExactly) {
  // 0x1.12E0BE826D694p+33 s is the largest binary64 whose exact
  // nanosecond floor still fits into int64: 9223372036854774475 ns is
  // below INT64_MAX.
  const auto largest = floorDurationNs(0x1.12E0BE826D694p+33);
  ASSERT_TRUE(largest.has_value());
  EXPECT_EQ(*largest, 9223372036854774475LL);
  // The next binary64 above it needs 9223372036854776382 ns, which does
  // not fit into int64 and is refused.
  EXPECT_FALSE(floorDurationNs(0x1.12E0BE826D695p+33).has_value());
}

// --- sourceDeadlineNs -----------------------------------------------------

TEST(DriverDeadlineSource, OriginalTtlIsConsumedByTheTransitAge) {
  const std::int64_t anchor = 5000000000LL;
  const std::int64_t source = 1000000000LL;
  // A 100 ms original TTL received 80 ms later leaves 20 ms.
  const auto deadline = sourceDeadlineNs(anchor, source, source + 80 * kMs, 0.1);
  ASSERT_TRUE(deadline.has_value());
  EXPECT_EQ(*deadline, anchor + 20 * kMs);
  // Received 150 ms later, the original interval is already expired.
  EXPECT_FALSE(
    sourceDeadlineNs(anchor, source, source + 150 * kMs, 0.1).has_value());
}

TEST(DriverDeadlineSource, SourceStampAheadOfTheLocalClockIsBounded) {
  const std::int64_t anchor = 5000000000LL;
  const std::int64_t now = 1000000000LL;
  // Exactly 100 ms ahead is still accepted: no age has elapsed yet.
  const auto at_bound = sourceDeadlineNs(anchor, now + 100 * kMs, now, 0.1);
  ASSERT_TRUE(at_bound.has_value());
  EXPECT_EQ(*at_bound, anchor + 100 * kMs);
  // One nanosecond beyond the bound is implausible and is refused.
  EXPECT_FALSE(
    sourceDeadlineNs(anchor, now + 100 * kMs + 1, now, 0.1).has_value());
}

TEST(DriverDeadlineSource, InvalidInputsAndOverflowAreRejected) {
  EXPECT_FALSE(sourceDeadlineNs(0, 1000, 1000, 0.1).has_value());
  EXPECT_FALSE(sourceDeadlineNs(-1, 1000, 1000, 0.1).has_value());
  EXPECT_FALSE(sourceDeadlineNs(1000, 0, 1000, 0.1).has_value());
  EXPECT_FALSE(sourceDeadlineNs(1000, -5, 1000, 0.1).has_value());
  EXPECT_FALSE(sourceDeadlineNs(1000, 1000, -1, 0.1).has_value());
  EXPECT_FALSE(sourceDeadlineNs(1000, 1000, 1000, 0.0).has_value());
  EXPECT_FALSE(sourceDeadlineNs(1000, 1000, 1000, -0.5).has_value());
  EXPECT_FALSE(sourceDeadlineNs(1000, 1000, 1000,
                                std::numeric_limits<double>::quiet_NaN())
                   .has_value());
  // anchor + remaining would overflow int64.
  EXPECT_FALSE(sourceDeadlineNs(kInt64Max - 10, 1000, 1000, 1.0).has_value());
  // A value that still fits is accepted.
  const auto fits = sourceDeadlineNs(kInt64Max - 10000000000LL, 1000, 1000, 1.0);
  ASSERT_TRUE(fits.has_value());
  EXPECT_EQ(*fits, kInt64Max - 10000000000LL + 1000000000LL);
}

// --- clock domain identity ------------------------------------------------

TEST(DriverDeadlineClockDomain, IdentityIsStableAndSensitiveToEveryPart) {
  const std::string uuid = "0123abcd-89ef-4abc-8def-0123456789ab";
  const std::string identity = clockDomainIdFromParts(uuid, 1, 2);
  EXPECT_FALSE(identity.empty());
  // The same parts always produce the same identity.
  EXPECT_EQ(identity, clockDomainIdFromParts(uuid, 1, 2));
  // A different boot identity is a different clock domain.
  EXPECT_NE(identity,
            clockDomainIdFromParts("0123abcd-89ef-4abc-8def-0123456789ac", 1, 2));
  // A different time namespace is a different clock domain.
  EXPECT_NE(identity, clockDomainIdFromParts(uuid, 1, 3));
  EXPECT_NE(identity, clockDomainIdFromParts(uuid, 2, 2));
}

TEST(DriverDeadlineClockDomain, BootAndNamespaceIdentityAreIndependent) {
  const std::string boot_a = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
  const std::string boot_b = "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeef";
  EXPECT_EQ(clockDomainIdFromParts(boot_a, 7, 11),
            clockDomainIdFromParts(boot_a, 7, 11));
  EXPECT_NE(clockDomainIdFromParts(boot_a, 7, 11),
            clockDomainIdFromParts(boot_b, 7, 11));
  EXPECT_NE(clockDomainIdFromParts(boot_a, 7, 11),
            clockDomainIdFromParts(boot_a, 7, 12));
}

TEST(DriverDeadlineClockDomain, MalformedIdentityIsRefused) {
  const std::string uuid = "0123abcd-89ef-4abc-8def-0123456789ab";
  EXPECT_THROW(clockDomainIdFromParts("", 1, 2), std::invalid_argument);
  // Missing separators.
  EXPECT_THROW(clockDomainIdFromParts("0123abcd89ef4abc8def0123456789ab", 1, 2),
               std::invalid_argument);
  // Upper case is not canonical.
  EXPECT_THROW(clockDomainIdFromParts("0123ABCD-89EF-4ABC-8DEF-0123456789AB", 1, 2),
               std::invalid_argument);
  // Wrong group lengths.
  EXPECT_THROW(clockDomainIdFromParts("0123abc-89ef-4abc-8def-0123456789ab", 1, 2),
               std::invalid_argument);
  EXPECT_THROW(clockDomainIdFromParts("0123abcd-89ef-4abc-8def-0123456789a", 1, 2),
               std::invalid_argument);
  EXPECT_THROW(clockDomainIdFromParts("0123abcd-89ef-4abc-8def-0123456789abc", 1, 2),
               std::invalid_argument);
  // Non hexadecimal digit.
  EXPECT_THROW(clockDomainIdFromParts("0123abcd-89ef-4abc-8def-0123456789ag", 1, 2),
               std::invalid_argument);
  // A zero namespace inode names no namespace.
  EXPECT_THROW(clockDomainIdFromParts(uuid, 1, 0), std::invalid_argument);
}

TEST(DriverDeadlineClockDomain, LocalIdentityIsUsableAndStableOnThisHost) {
  std::string identity;
  ASSERT_NO_THROW(identity = localClockDomainId());
  EXPECT_FALSE(identity.empty());
  // Reading it twice yields the same identity: nothing is mutated.
  EXPECT_EQ(identity, localClockDomainId());
  // The identity embeds a canonical boot uuid; no host value is hardcoded.
  bool found_uuid = false;
  for (std::size_t offset = 0; offset + 36U <= identity.size(); ++offset) {
    if (looksLikeCanonicalUuid(identity.substr(offset, 36U))) {
      found_uuid = true;
      break;
    }
  }
  EXPECT_TRUE(found_uuid);
}

// --- monotonic clock ------------------------------------------------------

TEST(DriverDeadlineClock, MonotonicReadingIsPositiveAndNonDecreasing) {
  const std::int64_t first = monotonicNowNs();
  EXPECT_GT(first, 0);
  const std::int64_t second = monotonicNowNs();
  EXPECT_GE(second, first);
}

// --- boundedSteadyDeadlineNs ----------------------------------------------

TEST(DriverDeadlineSteady, ConversionIsConservativeAndNeverRoundsUp) {
  const std::int64_t anchor = 7000000000LL;
  const auto deadline = boundedSteadyDeadlineNs(anchor, 10.5, 10.0, 5.0);
  ASSERT_TRUE(deadline.has_value());
  const std::int64_t granted = *deadline - anchor;
  // The true interval is 0.5 s; the helper may only be more strict.
  EXPECT_LE(granted, 500000000LL);
  EXPECT_GE(granted, 499999999LL);
  EXPECT_EQ(*deadline, anchor + granted);
}

TEST(DriverDeadlineSteady, DriverCapBoundsTheGrantedInterval) {
  const std::int64_t anchor = 7000000000LL;
  const auto deadline = boundedSteadyDeadlineNs(anchor, 20.0, 10.0, 0.1);
  ASSERT_TRUE(deadline.has_value());
  EXPECT_EQ(*deadline - anchor, 100000000LL);
}

TEST(DriverDeadlineSteady, ExpiredAndInvalidIntervalsAreRejected) {
  const std::int64_t anchor = 7000000000LL;
  // The steady deadline has already passed.
  EXPECT_FALSE(boundedSteadyDeadlineNs(anchor, 10.0, 10.0, 1.0).has_value());
  EXPECT_FALSE(boundedSteadyDeadlineNs(anchor, 9.0, 10.0, 1.0).has_value());
  // An interval below one nanosecond cannot arm motion.
  EXPECT_FALSE(
    boundedSteadyDeadlineNs(anchor, 10.000000000001, 10.0, 1.0).has_value());
  // Invalid anchors, caps and non-finite readings.
  EXPECT_FALSE(boundedSteadyDeadlineNs(0, 10.5, 10.0, 1.0).has_value());
  EXPECT_FALSE(boundedSteadyDeadlineNs(-1, 10.5, 10.0, 1.0).has_value());
  EXPECT_FALSE(boundedSteadyDeadlineNs(anchor, 1.0, 0.0, 1.0).has_value());
  EXPECT_FALSE(boundedSteadyDeadlineNs(anchor, 1.0, -1.0, 1.0).has_value());
  EXPECT_FALSE(boundedSteadyDeadlineNs(anchor, -1.0, -2.0, 1.0).has_value());
  EXPECT_FALSE(boundedSteadyDeadlineNs(anchor, 10.5, 10.0, 0.0).has_value());
  EXPECT_FALSE(boundedSteadyDeadlineNs(anchor, 10.5, 10.0, -1.0).has_value());
  EXPECT_FALSE(boundedSteadyDeadlineNs(
                   anchor, std::numeric_limits<double>::quiet_NaN(), 10.0, 1.0)
                   .has_value());
  EXPECT_FALSE(boundedSteadyDeadlineNs(
                   anchor, 10.5, std::numeric_limits<double>::infinity(), 1.0)
                   .has_value());
  EXPECT_FALSE(boundedSteadyDeadlineNs(
                   anchor, 10.5, 10.0, std::numeric_limits<double>::quiet_NaN())
                   .has_value());
}

TEST(DriverDeadlineSteady, PreemptionBetweenTheReadsCannotExtendTheDeadline) {
  const std::int64_t anchor = 7000000000LL;
  const double deadline_s = 10.5;
  // The anchor is captured first, then the steady reading is taken.
  const auto without_preemption =
    boundedSteadyDeadlineNs(anchor, deadline_s, 10.0, 5.0);
  // The process was preempted after the anchor was captured and before the
  // steady reading was taken: the observed reading is later, so the granted
  // interval can only shrink.
  const auto preempted = boundedSteadyDeadlineNs(anchor, deadline_s, 10.25, 5.0);
  ASSERT_TRUE(without_preemption.has_value());
  ASSERT_TRUE(preempted.has_value());
  EXPECT_LE(*preempted, *without_preemption);
  EXPECT_LT(*preempted, *without_preemption);
}

TEST(DriverDeadlineSteady, NearInt64ArithmeticIsChecked) {
  // A one nanosecond interval still fits below the int64 limit.
  const auto fits = boundedSteadyDeadlineNs(kInt64Max - 10, 10.5, 10.0, 1e-9);
  ASSERT_TRUE(fits.has_value());
  EXPECT_EQ(*fits, kInt64Max - 9);
  // A slightly larger interval would overflow the addition and is refused.
  EXPECT_FALSE(boundedSteadyDeadlineNs(kInt64Max - 10, 10.5, 10.0, 1e-6)
                   .has_value());
}

}  // namespace
