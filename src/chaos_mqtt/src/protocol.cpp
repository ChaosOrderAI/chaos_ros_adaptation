#include "chaos_mqtt/protocol.hpp"

#include <cmath>
#include <regex>
#include <vector>

namespace chaos_mqtt
{
namespace
{

constexpr std::size_t kMaxPayloadBytes = 16384;
constexpr std::size_t kMaxJsonDepth = 100;

const std::regex kRequestIdPattern("[A-Za-z0-9_-]{8,64}");
const std::regex kNameSegmentPattern("[A-Za-z_][A-Za-z0-9_]*");

// Reject pathological nesting before the recursive parser runs; the wire
// schema is at most ~8 levels deep. Tracks current nesting depth, so many
// shallow siblings are fine while deeply nested containers are rejected.
// Mismatched closers are left for the parser's stricter validation.
std::size_t json_depth_guard(const std::string & data)
{
  std::size_t depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (char c : data) {
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    switch (c) {
      case '"':
        in_string = true;
        break;
      case '[':
      case '{':
        ++depth;
        if (depth > kMaxJsonDepth) {
          throw CommandError("INVALID_ARGUMENT", "JSON exceeds maximum nesting depth");
        }
        break;
      case ']':
      case '}':
        if (depth > 0) {
          --depth;
        }
        break;
      default:
        break;
    }
  }
  return depth;
}

Json parse_json(const std::string & data)
{
  json_depth_guard(data);
  try {
    return Json::parse(data);
  } catch (const std::exception &) {
    throw CommandError("INVALID_ARGUMENT", "invalid JSON");
  }
}

Json parse_json_permissive(const std::string & data)
{
  json_depth_guard(data);
  return Json::parse(data);
}

// Stable reference into an object, or a shared null for missing keys and
// non-objects. Callers must only read from the result. This deliberately
// mirrors Python's dict.get(): a missing key is observably null downstream,
// so explicit JSON null and absent keys share validation outcomes.
const Json & member(const Json & object, const char * key)
{
  static const Json kNull = nullptr;
  if (!object.is_object()) {
    return kNull;
  }
  const auto it = object.find(key);
  return it == object.end() ? kNull : *it;
}

// Copying accessor for keys with a schema default; an explicit JSON null is
// kept (and later rejected by number()) exactly like Python's payload.get().
Json member_or(const Json & object, const char * key, const Json & fallback)
{
  if (!object.is_object()) {
    return fallback;
  }
  const auto it = object.find(key);
  return it == object.end() ? fallback : Json(*it);
}

double number(const Json & value, const char * field_name, double minimum, double maximum)
{
  if (!value.is_number() || !std::isfinite(value.get<double>())) {
    throw CommandError("INVALID_ARGUMENT", std::string(field_name) + " must be finite numeric");
  }
  const double result = value.get<double>();
  if (result < minimum || result > maximum) {
    throw CommandError("INVALID_ARGUMENT", std::string(field_name) + " is out of range");
  }
  return result;
}

std::size_t utf8_code_point_count(const std::string & value)
{
  std::size_t count = 0;
  for (unsigned char c : value) {
    if ((c & 0xC0) != 0x80) {
      ++count;
    }
  }
  return count;
}

// Python measured len(str) in code points; keep the same 256 limit.
std::string nonempty(const Json & value, const char * field_name)
{
  if (!value.is_string()) {
    throw CommandError(
      "INVALID_ARGUMENT", std::string(field_name) + " must be a nonempty string (max 256)");
  }
  const std::string & text = value.get_ref<const std::string &>();
  if (text.empty() || utf8_code_point_count(text) > 256) {
    throw CommandError(
      "INVALID_ARGUMENT", std::string(field_name) + " must be a nonempty string (max 256)");
  }
  return text;
}

const Json & fields(const Json & value, const std::vector<const char *> & allowed,
  const char * field_name)
{
  if (!value.is_object()) {
    throw CommandError("INVALID_ARGUMENT", std::string(field_name) + " contains unsupported fields");
  }
  for (const auto & item : value.items()) {
    bool known = false;
    for (const char * key : allowed) {
      if (item.key() == key) {
        known = true;
        break;
      }
    }
    if (!known) {
      throw CommandError("INVALID_ARGUMENT", std::string(field_name) + " contains unsupported fields");
    }
  }
  return value;
}

Json vector(const Json & value, const std::vector<const char *> & axes,
  const char * field_name, const Json & defaults = nullptr)
{
  fields(value, axes, field_name);
  Json result = Json::object();
  for (const char * axis : axes) {
    Json axis_value = 0.0;
    if (defaults.is_object() && defaults.contains(axis)) {
      axis_value = defaults.at(axis);
    }
    const Json & raw = member(value, axis);
    if (!raw.is_null() || value.contains(axis)) {
      axis_value = raw;
    }
    result[axis] = number(axis_value, (std::string(field_name) + "." + axis).c_str(),
        -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity());
  }
  return result;
}

bool has_capability(const std::vector<std::string> & capabilities, const std::string & name)
{
  for (const auto & capability : capabilities) {
    if (capability == name) {
      return true;
    }
  }
  return false;
}

// Positive Unix milliseconds within ROS Time range, with unsigned inputs
// range-checked before any narrowing so out-of-range values become a stable
// INVALID_ARGUMENT instead of an implementation-defined conversion.
std::int64_t parse_timestamp_ms(const Json & value)
{
  constexpr std::int64_t kMaxTimestampMs = (std::int64_t{1} << 31) * 1000;
  if (value.is_number_unsigned()) {
    const auto unsigned_value = value.get<std::uint64_t>();
    if (unsigned_value == 0 || unsigned_value >= static_cast<std::uint64_t>(kMaxTimestampMs)) {
      throw CommandError(
        "INVALID_ARGUMENT", "timestamp_ms must be positive Unix milliseconds within ROS Time range");
    }
    return static_cast<std::int64_t>(unsigned_value);
  }
  if (!value.is_number_integer()) {
    throw CommandError(
      "INVALID_ARGUMENT", "timestamp_ms must be positive Unix milliseconds within ROS Time range");
  }
  const auto signed_value = value.get<std::int64_t>();
  if (signed_value <= 0 || signed_value >= kMaxTimestampMs) {
    throw CommandError(
      "INVALID_ARGUMENT", "timestamp_ms must be positive Unix milliseconds within ROS Time range");
  }
  return signed_value;
}

}  // namespace

const std::unordered_map<std::string, std::optional<std::string>> kCommandCapabilities{
  {"system.describe", "state"},
  {"control.acquire", std::nullopt},
  {"control.renew", std::nullopt},
  {"control.release", std::nullopt},
  {"motion.stop", "motion.stop"},
  {"motion.velocity", "motion.velocity"},
  {"navigation.to_pose", "navigation.pose"},
  {"flight.takeoff", "flight.takeoff"},
  {"flight.land", "flight.land"},
};

Json wire_value(const Json & value)
{
  if (value.is_number_float()) {
    const double number_value = value.get<double>();
    if (!std::isfinite(number_value)) {
      return nullptr;
    }
    return value;
  }
  if (value.is_object()) {
    Json result = Json::object();
    for (const auto & item : value.items()) {
      result[item.key()] = wire_value(item.value());
    }
    return result;
  }
  if (value.is_array()) {
    Json result = Json::array();
    for (const auto & item : value) {
      result.push_back(wire_value(item));
    }
    return result;
  }
  return value;
}

Command parse_command(
  const std::string & data,
  const std::vector<std::string> & capabilities,
  std::int64_t now_ms,
  std::int64_t maximum_clock_skew_ms)
{
  if (data.size() > kMaxPayloadBytes) {
    throw CommandError("INVALID_ARGUMENT", "command payload exceeds 16384 bytes");
  }
  const Json parsed = parse_json(data);
  const Json & envelope = fields(parsed, {"bridge", "payload"}, "envelope");
  const Json & bridge = fields(member(envelope, "bridge"), {"source", "request_id", "timestamp_ms"},
      "bridge");
  const Json & source = member(bridge, "source");
  if (!source.is_string() || source.get<std::string>() != "mqtt") {
    throw CommandError("INVALID_ARGUMENT", "bridge.source must be mqtt");
  }
  const Json & request_id_json = member(bridge, "request_id");
  if (!request_id_json.is_string() ||
    !std::regex_match(request_id_json.get_ref<const std::string &>(), kRequestIdPattern))
  {
    throw CommandError("INVALID_ARGUMENT", "invalid bridge.request_id");
  }
  const std::string request_id = request_id_json.get<std::string>();
  const std::int64_t timestamp = parse_timestamp_ms(member(bridge, "timestamp_ms"));
  const Json & payload = member(envelope, "payload");
  if (!payload.is_object()) {
    throw CommandError("INVALID_ARGUMENT", "payload must be an object");
  }
  const Json & name_json = member(payload, "command");
  if (!name_json.is_string() ||
    kCommandCapabilities.find(name_json.get<std::string>()) == kCommandCapabilities.end())
  {
    throw CommandError("UNSUPPORTED", "unknown command");
  }
  const std::string name = name_json.get<std::string>();
  const auto & capability = kCommandCapabilities.at(name);
  if (capability.has_value() && !has_capability(capabilities, *capability)) {
    throw CommandError("UNSUPPORTED", "asset does not expose " + *capability);
  }
  static const Json kDefaultTtlMs = 1000;
  const double ttl_ms = number(member_or(payload, "ttl_ms", kDefaultTtlMs),
      "ttl_ms", 1, 30000);
  double deadline_ms = static_cast<double>(timestamp) + ttl_ms;
  if (std::llabs(now_ms - timestamp) > maximum_clock_skew_ms ||
    static_cast<double>(now_ms) >= deadline_ms)
  {
    throw CommandError("EXPIRED", "command expired or sender clock exceeds allowed skew");
  }
  // Future timestamps must not grant a control command extra validity.
  if (timestamp > now_ms + 100) {
    throw CommandError("EXPIRED", "command timestamp is more than 100 ms in the future");
  }

  Json arguments = Json::object();
  std::vector<const char *> allowed{"command", "ttl_ms"};
  if (name.rfind("control.", 0) == 0) {
    allowed.push_back("owner");
    allowed.push_back("token");
    allowed.push_back("duration_s");
    arguments["owner"] = nonempty(member(payload, "owner"), "owner");
    const Json & token = member(payload, "token");
    if (name != "control.acquire") {
      arguments["token"] = nonempty(token, "token");
    } else {
      if (!token.is_string() || !token.get_ref<const std::string &>().empty()) {
        throw CommandError("INVALID_ARGUMENT", "acquire must omit token");
      }
      arguments["token"] = "";
    }
    static const Json kDefaultDurationS = 5.0;
    arguments["duration_s"] = number(member_or(payload, "duration_s", kDefaultDurationS),
        "duration_s", 0.001, 3600);
  } else if (name != "system.describe") {
    allowed.push_back("lease_token");
    arguments["lease_token"] = nonempty(member(payload, "lease_token"), "lease_token");
  }
  if (name == "motion.velocity") {
    allowed.push_back("velocity");
    allowed.push_back("valid_for_s");
    arguments["valid_for_s"] = number(member(payload, "valid_for_s"),
        "valid_for_s", 0.001, 30);
    deadline_ms = std::min(deadline_ms, static_cast<double>(timestamp) +
        arguments["valid_for_s"].get<double>() * 1000.0);
    if (deadline_ms <= static_cast<double>(now_ms)) {
      throw CommandError("EXPIRED", "velocity command validity elapsed during transport");
    }
    const Json & velocity_json = fields(
      member(payload, "velocity"), {"linear", "angular"}, "velocity");
    Json velocity = Json::object();
    for (const char * kind : {"linear", "angular"}) {
      const std::string axis_name = std::string("velocity.") + kind;
      velocity[kind] = vector(member(velocity_json, kind), {"x", "y", "z"}, axis_name.c_str());
    }
    arguments["velocity"] = std::move(velocity);
  } else if (name == "navigation.to_pose") {
    allowed.push_back("target");
    const Json & target = fields(
      member(payload, "target"), {"frame_id", "position", "orientation"}, "target");
    Json target_json = Json::object();
    target_json["frame_id"] = nonempty(member(target, "frame_id"), "target.frame_id");
    target_json["position"] = vector(member(target, "position"), {"x", "y", "z"}, "target.position");
    static const Json kOrientationDefault = Json{{"w", 1.0}};
    target_json["orientation"] = vector(member(target, "orientation"), {"x", "y", "z", "w"},
        "target.orientation", kOrientationDefault);
    double norm_sq = 0.0;
    for (const char * axis : {"x", "y", "z", "w"}) {
      const double component = target_json["orientation"][axis].get<double>();
      norm_sq += component * component;
    }
    const double norm = std::sqrt(norm_sq);
    if (!std::isfinite(norm) || norm < std::sqrt(0.99) || norm > std::sqrt(1.01)) {
      throw CommandError("INVALID_ARGUMENT", "orientation must be a unit quaternion");
    }
    arguments["target"] = std::move(target_json);
  } else if (name == "flight.takeoff" || name == "flight.land") {
    allowed.push_back("altitude_m");
    static const Json kDefaultAltitude = 0.0;
    arguments["altitude_m"] = number(member_or(payload, "altitude_m", kDefaultAltitude),
        "altitude_m", 0, 10000);
  }
  fields(payload, allowed, "payload");
  return Command{request_id, name, deadline_ms, std::move(arguments)};
}

std::optional<std::pair<std::string, std::string>> recover_identity(const std::string & data)
{
  try {
    if (data.size() > kMaxPayloadBytes) {
      return std::nullopt;
    }
    const Json value = parse_json_permissive(data);
    const Json & bridge = member(value, "bridge");
    const Json & request_id_json = member(bridge, "request_id");
    if (!request_id_json.is_string() ||
      member(bridge, "source").get<std::string>() != "mqtt" ||
      !std::regex_match(request_id_json.get_ref<const std::string &>(), kRequestIdPattern))
    {
      return std::nullopt;
    }
    std::string name;
    const Json & payload = member(value, "payload");
    const Json & command = member(payload, "command");
    if (command.is_string()) {
      name = command.get<std::string>();
    }
    return std::make_pair(request_id_json.get<std::string>(), name);
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

Json response(
  const std::string & request_id,
  const std::string & command,
  bool success,
  std::string code,
  std::string message,
  const std::string & phase,
  const Json & extra)
{
  Json body = Json{
    {"ok", success},
    {"code", std::move(code)},
    {"message", std::move(message)},
    {"phase", phase},
    {"terminal", phase != "accepted"},
  };
  if (extra.is_object()) {
    for (const auto & item : extra.items()) {
      body[item.key()] = item.value();
    }
  }
  return Json{
    {"source", "mqtt"},
    {"request_id", request_id},
    {"command", command},
    {"response", std::move(body)},
  };
}

void Correlations::start(const Command & command, double now, double timeout_s)
{
  if (pending_.count(command.request_id) > 0 || is_completed(command.request_id)) {
    throw CommandError("DUPLICATE", "request already tracked");
  }
  if (pending_.size() >= max_pending_) {
    throw CommandError("BUSY", "maximum pending requests reached");
  }
  pending_.emplace(command.request_id, Pending{command, now + timeout_s, false, std::nullopt, {}});
}

void Correlations::finish(const std::string & request_id, const Json & reply, const Json & event)
{
  const auto it = pending_.find(request_id);
  if (it != pending_.end()) {
    if (it->second.cancel) {
      it->second.cancel();
    }
    pending_.erase(it);
  }
  auto existing = completed_.find(request_id);
  if (existing == completed_.end()) {
    if (completed_.size() >= max_completed_) {
      completed_.erase(completed_order_.front());
      completed_order_.pop_front();
    }
    completed_order_.push_back(request_id);
  }
  completed_[request_id] = std::make_pair(reply, event);
}

std::vector<std::string> Correlations::expired(double now) const
{
  std::vector<std::string> result;
  for (const auto & item : pending_) {
    if (now >= item.second.expires_at) {
      result.push_back(item.first);
    }
  }
  return result;
}

Pending * Correlations::pending(const std::string & request_id)
{
  const auto it = pending_.find(request_id);
  return it == pending_.end() ? nullptr : &it->second;
}

const Pending * Correlations::pending(const std::string & request_id) const
{
  const auto it = pending_.find(request_id);
  return it == pending_.end() ? nullptr : &it->second;
}

bool Correlations::is_completed(const std::string & request_id) const
{
  return completed_.count(request_id) > 0;
}

const std::pair<Json, Json> * Correlations::completed(const std::string & request_id) const
{
  const auto it = completed_.find(request_id);
  return it == completed_.end() ? nullptr : &it->second;
}

}  // namespace chaos_mqtt
