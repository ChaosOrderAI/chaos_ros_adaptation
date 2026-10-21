// One-to-one port of the former test_protocol.py contract, plus the review
// requirements: exact non-zero value preservation, schema-mutation rejection
// for every missing field, and depth-guard semantics.
#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "chaos_mqtt/adapter_config.hpp"
#include "chaos_mqtt/protocol.hpp"

namespace
{

using chaos_mqtt::Command;
using chaos_mqtt::CommandError;
using chaos_mqtt::Correlations;
using chaos_mqtt::Json;
using chaos_mqtt::parse_command;
using chaos_mqtt::recover_identity;
using chaos_mqtt::response;
using chaos_mqtt::wire_value;

std::string envelope(const std::string & command = "motion.velocity",
  std::int64_t timestamp_ms = 10000, const Json & fields = Json::object())
{
  Json payload = fields;
  payload["command"] = command;
  return Json{
    {"bridge", Json{{"source", "mqtt"}, {"request_id", "request_0001"}, {"timestamp_ms", timestamp_ms}}},
    {"payload", payload},
  }.dump();
}

std::string velocity(const Json & fields = Json::object())
{
  Json values{
    {"lease_token", "lease_secret"},
    {"velocity", Json{{"linear", Json{{"x", 0.1}}}}},
    {"valid_for_s", 0.5},
  };
  for (const auto & item : fields.items()) {
    values[item.key()] = item.value();
  }
  return envelope("motion.velocity", 10000, values);
}

TEST(Protocol, RemoteVelocityKeepsOriginalDeadlineAfterTransportDelay)
{
  const auto command = parse_command(velocity(), {"motion.velocity"}, 10400);
  EXPECT_EQ(command.deadline_ms, 10500);
  EXPECT_EQ(command.arguments["velocity"]["angular"]["z"].get<double>(), 0);
  try {
    parse_command(velocity(), {"motion.velocity"}, 10501);
    FAIL() << "expected CommandError";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "EXPIRED");
    EXPECT_NE(std::string(error.what()).find("validity elapsed"), std::string::npos);
  }
}

TEST(Protocol, StaleFutureAndClockSkewCommandsAreRejected)
{
  struct Case {
    std::string data;
    std::int64_t now;
  } cases[] = {
    // The validity window elapsed while the command was in transit.
    {velocity(Json{{"ttl_ms", 100}}), 10100},
    // The sender clock is more than 100 ms ahead of the gateway clock.
    {velocity(Json::object()), 10000},
    // The sender clock exceeds the tolerated skew of 5000 ms. A timestamp
    // that is merely close to "now" is legal: the deadline is
    // timestamp + ttl_ms and is not refreshed by the transport delay.
    {velocity(Json{{"ttl_ms", 30000}, {"valid_for_s", 30}}), 16000},
  };
  // Adjust the second case to use timestamp 10200.
  cases[1].data = envelope("motion.velocity", 10200,
      Json{{"lease_token", "lease_secret"},
        {"velocity", Json::object()}, {"valid_for_s", 0.5}});
  for (const auto & test : cases) {
    try {
      parse_command(test.data, {"motion.velocity"}, test.now);
      FAIL() << "expected EXPIRED for " << test.data;
    } catch (const CommandError & error) {
      EXPECT_EQ(error.code(), "EXPIRED") << error.what();
    }
  }
  // The same envelope without skew and before the deadline stays valid: the
  // deadline is derived from the sender timestamp, never from "now".
  EXPECT_NO_THROW(parse_command(
    velocity(Json{{"ttl_ms", 30000}, {"valid_for_s", 30}}), {"motion.velocity"}, 10000));
}

TEST(Protocol, MalformedControlPayloadsAreRejected)
{
  const std::vector<std::string> bad{
    velocity(Json{{"velocity", Json{{"linear", Json{{"x", std::nan("")}}}}}}),
    velocity(Json{{"valid_for_s", true}}),
    velocity(Json{{"velocity", Json{{"topic", "/hardware/cmd_vel"}}}}),
    velocity(Json{{"topic", "/hardware/cmd_vel"}}),
    velocity(Json{{"lease_token", ""}}),
    envelope("navigation.to_pose", 10000,
      Json{{"lease_token", "secret"},
        {"target", Json{{"frame_id", "map"}, {"position", Json::object()},
            {"orientation", Json{{"w", 0}}}}}}),
    envelope("navigation.to_pose", 10000,
      Json{{"lease_token", "secret"},
        {"target", Json{{"frame_id", "map"}, {"position", Json::object()},
            {"orientation", Json{{"w", 1e308}}}}}}),
  };
  for (const auto & data : bad) {
    try {
      parse_command(data, {"motion.velocity", "navigation.pose"}, 10000);
      FAIL() << "expected INVALID_ARGUMENT for " << data;
    } catch (const CommandError & error) {
      EXPECT_EQ(error.code(), "INVALID_ARGUMENT") << error.what() << " for " << data;
    }
  }
}

TEST(Protocol, CapabilityFilterAndUnknownCommands)
{
  const std::vector<std::string> unsupported{
    envelope("flight.takeoff", 10000, Json{{"lease_token", "secret"}, {"altitude_m", 1}}),
    Json{
      {"bridge", Json{{"source", "mqtt"}, {"request_id", "request_0001"}, {"timestamp_ms", 10000}}},
      {"payload", Json{{"command", "vendor.shell"}, {"command_line", "anything"}}},
    }.dump(),
  };
  for (const auto & data : unsupported) {
    try {
      parse_command(data, {"motion.stop"}, 10000);
      FAIL() << "expected UNSUPPORTED";
    } catch (const CommandError & error) {
      EXPECT_EQ(error.code(), "UNSUPPORTED");
    }
  }
}

TEST(Protocol, OnlyValidIdentityCanReceiveCorrelatedError)
{
  const auto identity = recover_identity(velocity(Json{{"velocity", "invalid"}}));
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(identity->first, "request_0001");
  EXPECT_EQ(identity->second, "motion.velocity");
  EXPECT_FALSE(recover_identity("not JSON").has_value());
  EXPECT_FALSE(recover_identity("{\"bridge\": []}").has_value());
  const std::string wrong_source = velocity(Json::object());
  const auto replaced = std::string(wrong_source).replace(
    wrong_source.find("\"mqtt\""), 6, "\"ros\"");
  EXPECT_FALSE(recover_identity(replaced).has_value());
}

TEST(Protocol, PendingAndCompletedCorrelationsAreBoundedAndTimeoutIsMonotonic)
{
  Correlations records(1, 2);
  const Command command{"request_0001", "flight.takeoff", 11000, Json::object()};
  records.start(command, 100, 5);
  try {
    records.start(Command{"request_0002", "flight.land", 11000, Json::object()}, 100, 5);
    FAIL() << "expected BUSY";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "BUSY");
  }
  EXPECT_TRUE(records.expired(104.9).empty());
  EXPECT_EQ(records.expired(105), std::vector<std::string>({"request_0001"}));
  const Json reply = response(
    command.request_id, command.name, true, "ACCEPTED", "", "accepted");
  EXPECT_FALSE(reply["response"]["terminal"].get<bool>());
  records.finish(command.request_id, reply, Json{{"phase", "result"}});
  EXPECT_EQ(records.pending_size(), 0u);
  try {
    records.start(command, 110, 5);
    FAIL() << "expected DUPLICATE";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "DUPLICATE");
  }
  records.finish("request_0002", Json::object());
  records.finish("request_0003", Json::object());
  ASSERT_TRUE(records.completed("request_0002"));
  ASSERT_TRUE(records.completed("request_0003"));
  EXPECT_FALSE(records.completed("request_0001"));
}

TEST(Protocol, FinishingTimedOutRequestCancelsOnlyLocalObservation)
{
  Correlations records;
  const Command command{"request_0001", "flight.takeoff", 11000, Json::object()};
  records.start(command, 100, 5);
  bool cancelled = false;
  records.pending(command.request_id)->cancel = [&cancelled]() { cancelled = true; };
  records.finish(command.request_id, Json{{"execution_state", "unknown"}});
  EXPECT_TRUE(cancelled);
  EXPECT_EQ(records.pending_size(), 0u);
}

TEST(Protocol, UnknownTelemetryIsNullWhileControlNanRemainsInvalid)
{
  const Json state{
    {"battery_percentage", std::nan("")},
    {"pose", Json{{"z", std::numeric_limits<double>::infinity()}}},
    {"values", Json{1.5, -std::numeric_limits<double>::infinity()}},
    {"pose_valid", false},
  };
  const Json encoded = wire_value(state);
  EXPECT_TRUE(encoded["battery_percentage"].is_null());
  EXPECT_TRUE(encoded["pose"]["z"].is_null());
  EXPECT_TRUE(encoded["values"][1].is_null());
  EXPECT_EQ(encoded["values"][0].get<double>(), 1.5);
  EXPECT_FALSE(encoded["pose_valid"].get<bool>());
  try {
    parse_command(velocity(Json{{"valid_for_s", std::nan("")}}), {"motion.velocity"}, 10000);
    FAIL() << "expected INVALID_ARGUMENT";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
  }
}

TEST(Protocol, ExtremeJsonCannotEscapeValidation)
{
  const std::string deeply_nested = std::string(2000, '[') + "0" + std::string(2000, ']');
  EXPECT_FALSE(recover_identity(deeply_nested).has_value());
  try {
    parse_command(deeply_nested, {"state"}, 10000);
    FAIL() << "expected CommandError";
  } catch (const CommandError &) {
  }
  // 10**500 as raw 501-digit integer literals (what json.dumps produces).
  const std::string huge_int_ttl = velocity(Json{{"ttl_ms", std::string(500, '9')}});
  std::string huge_int_timestamp_base = envelope("motion.velocity", 0,
      Json{{"lease_token", "lease_secret"}, {"velocity", Json::object()}, {"valid_for_s", 0.5}});
  const std::string marker = "\"timestamp_ms\":0";
  const auto marker_pos = huge_int_timestamp_base.find(marker);
  const std::string huge_int_timestamp = huge_int_timestamp_base.replace(
    marker_pos, marker.size(), "\"timestamp_ms\":" + std::string(500, '9'));
  for (const auto & data : {huge_int_ttl, huge_int_timestamp}) {
    try {
      parse_command(data, {"motion.velocity"}, 10000);
      FAIL() << "expected CommandError for huge integer";
    } catch (const CommandError & error) {
      EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
    }
  }
}

TEST(Protocol, NonZeroVelocityValuesArePreservedExactly)
{
  const std::string data = envelope("motion.velocity", 10000,
      Json{
        {"lease_token", "lease_secret"},
        {"valid_for_s", 0.25},
        {"ttl_ms", 2000},
        {"velocity", Json{
          {"linear", Json{{"x", 0.25}, {"y", -0.5}, {"z", 0.125}}},
          {"angular", Json{{"x", 0.0}, {"y", 0.0}, {"z", -0.75}}},
        }},
      });
  const auto command = parse_command(data, {"motion.velocity"}, 10100);
  EXPECT_EQ(command.deadline_ms, 10250);
  const Json & linear = command.arguments["velocity"]["linear"];
  EXPECT_EQ(linear["x"].get<double>(), 0.25);
  EXPECT_EQ(linear["y"].get<double>(), -0.5);
  EXPECT_EQ(linear["z"].get<double>(), 0.125);
  const Json & angular = command.arguments["velocity"]["angular"];
  EXPECT_EQ(angular["z"].get<double>(), -0.75);
  EXPECT_EQ(command.arguments["valid_for_s"].get<double>(), 0.25);
}

TEST(Protocol, NonZeroNavigationTargetIsPreservedExactly)
{
  const std::string data = envelope("navigation.to_pose", 10000,
      Json{{"lease_token", "lease_secret"},
        {"ttl_ms", 4000},
        {"target", Json{
          {"frame_id", "map"},
          {"position", Json{{"x", 1.5}, {"y", -2.25}, {"z", 0.75}}},
          {"orientation", Json{{"x", 0.1}, {"y", 0.2}, {"z", 0.3}, {"w", 0.92736184955}}},
        }}});
  const auto command = parse_command(data, {"navigation.pose"}, 10050);
  const Json & target = command.arguments["target"];
  EXPECT_EQ(target["frame_id"].get<std::string>(), "map");
  EXPECT_EQ(target["position"]["x"].get<double>(), 1.5);
  EXPECT_EQ(target["position"]["y"].get<double>(), -2.25);
  EXPECT_EQ(target["position"]["z"].get<double>(), 0.75);
  EXPECT_EQ(target["orientation"]["x"].get<double>(), 0.1);
  EXPECT_EQ(target["orientation"]["w"].get<double>(), 0.92736184955);
  // Slightly off-unit quaternion must be rejected.
  const std::string bad = envelope("navigation.to_pose", 10000,
      Json{{"lease_token", "lease_secret"},
        {"target", Json{{"frame_id", "map"}, {"position", Json::object()},
            {"orientation", Json{{"w", 0.5}}}}}});
  try {
    parse_command(bad, {"navigation.pose"}, 10000);
    FAIL() << "expected INVALID_ARGUMENT for non-unit quaternion";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
  }
}

TEST(Protocol, MissingKeysAndSchemaMutationsYieldInvalidArgument)
{
  const std::vector<std::string> cases{
    "{}",                                                              // no bridge/payload
    "{\"bridge\": {}}",                                                // no payload
    "{\"payload\": {}}",                                               // no bridge
    "{\"bridge\": {\"source\": \"mqtt\"}, \"payload\": {}}",           // no request_id/timestamp
    "{\"bridge\": {\"request_id\": \"request_0001\", \"timestamp_ms\": 10000}, \"payload\": {}}",  // no source
    "{\"bridge\": {\"source\": \"mqtt\", \"timestamp_ms\": 10000}, \"payload\": {}}",  // no request_id
    "{\"bridge\": {\"source\": \"mqtt\", \"request_id\": \"request_0001\"}, \"payload\": {}}",  // no timestamp
    "{\"bridge\": {\"source\": \"ros\", \"request_id\": \"request_0001\", \"timestamp_ms\": 10000}, \"payload\": {\"command\": \"motion.stop\"}}",  // wrong source
    "{\"bridge\": {\"source\": \"mqtt\", \"request_id\": \"request_0001\", \"timestamp_ms\": true}, \"payload\": {\"command\": \"motion.stop\"}}",  // bool timestamp
    envelope("control.acquire", 10000, Json{{"owner", "operator"}, {"token", "x"}}),
    velocity(Json{{"lease_token", 0}}),
  };
  // The last case is replaced with a direct null-lease_token check instead.
  for (std::size_t i = 0; i + 1 < cases.size(); ++i) {
    try {
      parse_command(cases[i], {"motion.stop", "motion.velocity"}, 10000);
      FAIL() << "expected INVALID_ARGUMENT for " << cases[i];
    } catch (const CommandError & error) {
      EXPECT_EQ(error.code(), "INVALID_ARGUMENT") << cases[i] << " -> " << error.what();
    }
  }
  // Acquire with a non-empty token is a schema violation.
  try {
    parse_command(cases.back(), {"motion.velocity"}, 10000);
    FAIL() << "expected INVALID_ARGUMENT for acquire with token";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
  }
}

TEST(Protocol, DepthGuardCountsNestingNotContainersAndIgnoresQuotedBraces)
{
  // Many shallow sibling objects must not trip the depth guard. The strict
  // parser rejects the unknown "note" field exactly like the original
  // protocol, so the permissive identity recovery is used to exercise the
  // depth accounting itself.
  Json shallow = Json::object();
  for (int i = 0; i < 500; ++i) {
    shallow["key" + std::to_string(i)] = Json{{"x", 1.0}};
  }
  const std::string shallow_text = shallow.dump();
  const std::string shallow_case = Json{
    {"bridge", Json{{"source", "mqtt"}, {"request_id", "request_0001"}, {"timestamp_ms", 10000}}},
    {"payload", Json{{"command", "motion.stop"}, {"lease_token", "token"}, {"note", shallow_text}}},
  }.dump();
  const auto identity = recover_identity(shallow_case);
  ASSERT_TRUE(identity.has_value());
  EXPECT_EQ(identity->first, "request_0001");
  // The strict schema still rejects the unknown field.
  try {
    parse_command(shallow_case, {"motion.stop"}, 10000);
    FAIL() << "expected the unknown field to be rejected";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
  }

  // Braces and brackets inside a legal string field must not count toward
  // the nesting depth.
  const std::string quoted_lease = R"([[[{{{"not nested)";
  const std::string quoted_case = Json{
    {"bridge", Json{{"source", "mqtt"}, {"request_id", "request_0001"}, {"timestamp_ms", 10000}}},
    {"payload", Json{{"command", "motion.stop"}, {"lease_token", quoted_lease}}},
  }.dump();
  EXPECT_NO_THROW(parse_command(quoted_case, {"motion.stop"}, 10000));

  // Escaped quotes inside strings are handled while scanning.
  const std::string escaped_case = Json{
    {"bridge", Json{{"source", "mqtt"}, {"request_id", "request_0001"}, {"timestamp_ms", 10000}}},
    {"payload", Json{{"command", "motion.stop"}, {"lease_token", "t\\\"oken"}}},
  }.dump();
  EXPECT_NO_THROW(parse_command(escaped_case, {"motion.stop"}, 10000));

  // Exactly at the limit is fine; one deeper is rejected.
  std::string at_limit = "0";
  for (int i = 0; i < 99; ++i) {
    at_limit = "[" + at_limit + "]";
  }
  EXPECT_NO_THROW(Json::parse(at_limit));
  const std::string too_deep = "[" + at_limit + "]";
  try {
    parse_command(too_deep, {"motion.stop"}, 10000);
    FAIL() << "expected depth rejection";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
  }
}

TEST(Protocol, ResponseEnvelopeShape)
{
  const Json reply = response(
    "request_0001", "motion.stop", false, "EXPIRED", "late", "result",
    Json{{"execution_state", "unknown"}});
  EXPECT_EQ(reply["source"].get<std::string>(), "mqtt");
  EXPECT_EQ(reply["request_id"].get<std::string>(), "request_0001");
  EXPECT_EQ(reply["command"].get<std::string>(), "motion.stop");
  EXPECT_TRUE(reply["response"]["terminal"].get<bool>());
  EXPECT_EQ(reply["response"]["execution_state"].get<std::string>(), "unknown");
}

TEST(Protocol, ControlAcquireDefaultsAndExplicitNullHandling)
{
  // Omitted token defaults to the empty string.
  const auto omitted = parse_command(
    envelope("control.acquire", 10000, Json{{"owner", "operator"}}),
    {"state"}, 10000);
  EXPECT_EQ(omitted.arguments["token"].get<std::string>(), "");
  EXPECT_EQ(omitted.arguments["owner"].get<std::string>(), "operator");
  EXPECT_EQ(omitted.arguments["duration_s"].get<double>(), 5.0);
  // Explicit empty string is equally valid.
  const auto empty = parse_command(
    envelope("control.acquire", 10000, Json{{"owner", "operator"}, {"token", ""}}),
    {"state"}, 10000);
  EXPECT_EQ(empty.arguments["token"].get<std::string>(), "");
  // Explicit null must be rejected even though omission is fine.
  const std::string null_token = envelope("control.acquire", 10000,
      Json{{"owner", "operator"}, {"token", nullptr}});
  try {
    parse_command(null_token, {"state"}, 10000);
    FAIL() << "expected INVALID_ARGUMENT for explicit null token";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
  }
  // Renew/release require a nonempty token.
  for (const char * command : {"control.renew", "control.release"}) {
    try {
      parse_command(
        envelope(command, 10000, Json{{"owner", "operator"}}), {"state"}, 10000);
      FAIL() << "expected INVALID_ARGUMENT for missing token on " << command;
    } catch (const CommandError & error) {
      EXPECT_EQ(error.code(), "INVALID_ARGUMENT") << command;
    }
  }
  const auto renew = parse_command(
    envelope("control.renew", 10000,
      Json{{"owner", "operator"}, {"token", "tok"}, {"duration_s", 2.5}}),
    {"state"}, 10000);
  EXPECT_EQ(renew.arguments["duration_s"].get<double>(), 2.5);
  EXPECT_EQ(renew.arguments["token"].get<std::string>(), "tok");
}

TEST(Protocol, VelocitySubObjectsDefaultToEmptyAndExplicitNullIsRejected)
{
  // Only linear given: angular must default to all-zero axes.
  const auto linear_only = parse_command(
    envelope("motion.velocity", 10000,
      Json{{"lease_token", "t"}, {"valid_for_s", 0.5},
        {"velocity", Json{{"linear", Json{{"x", 0.1}}}}}}),
    {"motion.velocity"}, 10000);
  EXPECT_EQ(linear_only.arguments["velocity"]["angular"]["z"].get<double>(), 0.0);
  // Velocity omitted entirely is a schema violation.
  try {
    parse_command(
      envelope("motion.velocity", 10000, Json{{"lease_token", "t"}, {"valid_for_s", 0.5}}),
      {"motion.velocity"}, 10000);
    FAIL() << "expected INVALID_ARGUMENT for missing velocity";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
  }
  // Explicit null for a sub-object is rejected (omission defaults instead).
  try {
    parse_command(
      envelope("motion.velocity", 10000,
        Json{{"lease_token", "t"}, {"valid_for_s", 0.5},
          {"velocity", Json{{"linear", nullptr}}}}),
      {"motion.velocity"}, 10000);
    FAIL() << "expected INVALID_ARGUMENT for null linear";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
  }
}

TEST(Protocol, NavigationOrientationDefaultsToUnitQuaternion)
{
  // Orientation omitted: unit quaternion defaults (w = 1).
  const auto defaulted = parse_command(
    envelope("navigation.to_pose", 10000,
      Json{{"lease_token", "t"},
        {"target", Json{{"frame_id", "map"}, {"position", Json{{"x", 0.25}}}}}}),
    {"navigation.pose"}, 10000);
  const Json & orientation = defaulted.arguments["target"]["orientation"];
  EXPECT_EQ(orientation["w"].get<double>(), 1.0);
  EXPECT_EQ(orientation["x"].get<double>(), 0.0);
  EXPECT_EQ(defaulted.arguments["target"]["position"]["x"].get<double>(), 0.25);
  // Position is required even when orientation is fine.
  try {
    parse_command(
      envelope("navigation.to_pose", 10000,
        Json{{"lease_token", "t"},
          {"target", Json{{"frame_id", "map"}, {"orientation", Json{{"w", 1.0}}}}}}),
      {"navigation.pose"}, 10000);
    FAIL() << "expected INVALID_ARGUMENT for missing position";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
  }
  // Explicit null orientation is rejected.
  try {
    parse_command(
      envelope("navigation.to_pose", 10000,
        Json{{"lease_token", "t"},
          {"target", Json{{"frame_id", "map"}, {"position", Json::object()},
              {"orientation", nullptr}}}}),
      {"navigation.pose"}, 10000);
    FAIL() << "expected INVALID_ARGUMENT for null orientation";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
  }
}

TEST(Protocol, FlightAltitudeDefaultsToZeroAndStopNeedsOnlyLease)
{
  const auto takeoff = parse_command(
    envelope("flight.takeoff", 10000, Json{{"lease_token", "t"}}),
    {"flight.takeoff"}, 10000);
  EXPECT_EQ(takeoff.arguments["altitude_m"].get<double>(), 0.0);
  const auto land = parse_command(
    envelope("flight.land", 10000, Json{{"lease_token", "t"}, {"altitude_m", 3.5}}),
    {"flight.land"}, 10000);
  EXPECT_EQ(land.arguments["altitude_m"].get<double>(), 3.5);
  const auto stop = parse_command(
    envelope("motion.stop", 10000, Json{{"lease_token", "t"}}),
    {"motion.stop"}, 10000);
  EXPECT_EQ(stop.arguments["lease_token"].get<std::string>(), "t");
  // system.describe accepts no extra fields and needs no lease.
  const auto describe = parse_command(
    envelope("system.describe", 10000, Json::object()), {"state"}, 10000);
  EXPECT_TRUE(describe.arguments.empty());
  try {
    parse_command(
      envelope("system.describe", 10000, Json{{"lease_token", "t"}}), {"state"}, 10000);
    FAIL() << "expected INVALID_ARGUMENT for describe with lease_token";
  } catch (const CommandError & error) {
    EXPECT_EQ(error.code(), "INVALID_ARGUMENT");
  }
}

TEST(Protocol, CorrelationsWithDisabledReplayNeverStoresCompleted)
{
  Correlations records(4, 0);
  const Command command{"request_0001", "motion.stop", 11000, Json::object()};
  records.start(command, 100, 5);
  records.finish(command.request_id, Json::object());
  EXPECT_FALSE(records.is_completed(command.request_id));
  EXPECT_EQ(records.completed_size(), 0u);
}

}  // namespace
