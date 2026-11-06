// The command deadline is a Unix UTC time, and it bounds when a command may
// start. Truncating the TTL can only shorten it, but a positive TTL must
// never expire immediately, and the addition must never wrap into the past.
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include "chaos_sdk/sdk_core.hpp"

TEST(DeadlineTimespec, TruncatesDownWithoutExtending) {
  const auto deadline = chaos_sdk::deadline_timespec(0.5, 1'000'000'000);
  EXPECT_EQ(deadline.first, 1);
  EXPECT_EQ(deadline.second, 500'000'000u);

  // The fractional nanosecond is dropped, so the deadline is earlier, never
  // later, than the exact sum.
  const auto truncated = chaos_sdk::deadline_timespec(1.0 + 1.9e-9, 0);
  EXPECT_EQ(truncated.first, 1);
  EXPECT_EQ(truncated.second, 1u);
}

TEST(DeadlineTimespec, ASubNanosecondTtlDoesNotExpireImmediately) {
  const std::int64_t now = 5'000'000'000;
  const auto deadline = chaos_sdk::deadline_timespec(1e-12, now);
  const std::int64_t deadline_ns =
    static_cast<std::int64_t>(deadline.first) * 1'000'000'000 +
    static_cast<std::int64_t>(deadline.second);
  EXPECT_GT(deadline_ns, now);
  EXPECT_EQ(deadline_ns - now, 1);
}

TEST(DeadlineTimespec, AnOverflowingDeadlineIsRejected) {
  const auto limit = std::numeric_limits<std::int64_t>::max();
  EXPECT_THROW(chaos_sdk::deadline_timespec(1.0, limit), std::invalid_argument);
  EXPECT_THROW(
    chaos_sdk::deadline_timespec(1.0, limit - 999'999'999), std::invalid_argument);

  // The boundary is exact: the largest TTL that still fits is accepted, and
  // one second more is refused. The fields are checked separately, because
  // the seconds field is wider than 32 bits here and a reconstructed sum
  // would overflow.
  const std::int64_t now = limit - 2'000'000'000;
  const auto deadline = chaos_sdk::deadline_timespec(2.0, now);
  EXPECT_EQ(deadline.first, static_cast<std::int32_t>(limit / 1'000'000'000));
  EXPECT_EQ(deadline.second, static_cast<std::uint32_t>(limit % 1'000'000'000));
  EXPECT_THROW(chaos_sdk::deadline_timespec(3.0, now), std::invalid_argument);
}
