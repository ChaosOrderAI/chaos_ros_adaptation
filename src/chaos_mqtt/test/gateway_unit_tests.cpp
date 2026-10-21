// Gateway pure-contract tests that do not start ROS: deadline conversion must
// match the wire contract across 2038 boundaries and current wall time.
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "chaos_mqtt/error.hpp"
#include "chaos_mqtt/gateway_node.hpp"

namespace
{

using chaos_mqtt::Command;
using chaos_mqtt::CommandError;
using chaos_mqtt::GatewayNode;

TEST(GatewayDeadline, RejectsZeroAndBeyondRosTimeRange)
{
  for (const double deadline_ms : {0.0, -1.0,
    static_cast<double>(std::int64_t{1} << 31) * 1000.0})
  {
    try {
      GatewayNode::deadline_time(deadline_ms);
      FAIL() << "expected CommandError for deadline_ms=" << deadline_ms;
    } catch (const CommandError & error) {
      EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
    }
  }
}

TEST(GatewayDeadline, PreservesCurrentUtcDeadlinesExactly)
{
  // Same fixed instant the old Python contract test used: 1700000000500 ms.
  const auto time = GatewayNode::deadline_time(1700000000500.0);
  EXPECT_EQ(time.sec, 1700000000);
  EXPECT_EQ(time.nanosec, 500000000u);
  // Sub-second deadlines round down (never granting extra validity).
  const auto rounded = GatewayNode::deadline_time(1700000000999.9);
  EXPECT_EQ(rounded.sec, 1700000000);
  EXPECT_EQ(rounded.nanosec, 999000000u);
}

TEST(GatewayDeadline, AcceptsUpToTheLastRepresentableSecondBefore2038)
{
  // 2^31 - 1 seconds is the largest valid ROS Time second.
  const auto boundary = GatewayNode::deadline_time(
    (static_cast<double>((std::int64_t{1} << 31) - 1) * 1000.0) + 999.0);
  EXPECT_EQ(boundary.sec, static_cast<std::int32_t>((std::int64_t{1} << 31) - 1));
  // One millisecond past the boundary must be rejected.
  try {
    GatewayNode::deadline_time(static_cast<double>(std::int64_t{1} << 31) * 1000.0 + 1.0);
    FAIL() << "expected rejection just past 2^31 seconds";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
  }
}

TEST(GatewayDeadline, RejectsNonFiniteDeadlines)
{
  for (const double deadline_ms :
    {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
  {
    try {
      GatewayNode::deadline_time(deadline_ms);
      FAIL() << "expected rejection of non-finite deadline";
    } catch (const CommandError & error) {
      EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
    }
  }
}

}  // namespace
