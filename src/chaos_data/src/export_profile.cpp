// The export profile: parsing, strict schema validation and the exact
// rational frame rate.
//
// The profile is the only description of what is exported. Nothing is
// guessed: a feature that is not written down is not exported, and a task
// boundary is never inferred from the data.
//
// The YAML is parsed by the shared configuration engine of the workspace, so
// the type of a scalar is the one of that engine: a plain scalar resolves
// with the YAML 1.1 / PyYAML implicit typing (a boolean, a null, a decimal,
// hexadecimal, octal, binary or sexagesimal integer, a float), a quoted
// scalar always stays a string, a duplicate mapping key is refused and an
// integer has to fit into a signed 64 bit integer.
#include "chaos_data/export.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>

#include <boost/multiprecision/cpp_int.hpp>
#include <chaos_assets/yaml.hpp>

namespace chaos::data {
namespace {

using chaos::assets::Json;
using boost::multiprecision::cpp_int;

const std::regex & namePattern()
{
  static const std::regex pattern("^[A-Za-z][A-Za-z0-9_.-]{0,127}$");
  return pattern;
}

const std::regex & fieldPattern()
{
  static const std::regex pattern(
    "^[A-Za-z][A-Za-z0-9_]*(\\.(?:[A-Za-z][A-Za-z0-9_]*|[0-9]+))*$");
  return pattern;
}

const std::regex & topicPattern()
{
  static const std::regex pattern("^(/[A-Za-z_][A-Za-z0-9_]*)+$");
  return pattern;
}

// A member of a mapping, or null when it is not there.
const Json * member(const Json & value, const std::string & key)
{
  if (!value.is_object()) {
    return nullptr;
  }
  const auto found = value.find(key);
  return found == value.end() ? nullptr : &found.value();
}

// The known fields of a mapping, and the fields that have to be present.
void checkKeys(const Json & value, const std::vector<std::string> & allowed,
               const std::vector<std::string> & required, const std::string & label)
{
  if (!value.is_object()) {
    throw std::runtime_error(label + ": expected a mapping");
  }
  for (auto entry = value.begin(); entry != value.end(); ++entry) {
    if (std::find(allowed.begin(), allowed.end(), entry.key()) == allowed.end()) {
      throw std::runtime_error(label + ": unknown field '" + entry.key() + "'");
    }
  }
  for (const auto & key : required) {
    if (!value.contains(key)) {
      throw std::runtime_error(label + ": missing field '" + key + "'");
    }
  }
}

std::string text(const Json & value, const std::string & label)
{
  if (!value.is_string()) {
    throw std::runtime_error(label + ": must be text");
  }
  const std::string & result = value.get_ref<const std::string &>();
  if (result.empty() ||
      result.find_first_not_of(" \t\r\n") == std::string::npos) {
    throw std::runtime_error(label + ": must not be empty");
  }
  if (result.size() > 4096) {
    throw std::runtime_error(label + ": must not exceed 4096 characters");
  }
  if (result.find('\0') != std::string::npos) {
    throw std::runtime_error(label + ": must not contain a null character");
  }
  return result;
}

std::int64_t integer(const Json & value, const std::string & label,
                     std::int64_t minimum = 1,
                     std::int64_t maximum = 9223372036854775807LL)
{
  // A boolean is not an integer. The loader keeps the integers inside the
  // signed 64 bit range, so an unsigned value is a value that does not fit.
  if (value.is_boolean() || !value.is_number_integer()) {
    throw std::runtime_error(label + ": must be an integer in [" +
                             std::to_string(minimum) + ", " +
                             std::to_string(maximum) + "]");
  }
  std::int64_t result = 0;
  if (value.is_number_unsigned()) {
    const std::uint64_t raw = value.get<std::uint64_t>();
    if (raw > static_cast<std::uint64_t>(maximum)) {
      throw std::runtime_error(label + ": must be an integer in [" +
                               std::to_string(minimum) + ", " +
                               std::to_string(maximum) + "]");
    }
    result = static_cast<std::int64_t>(raw);
  } else {
    result = value.get<std::int64_t>();
  }
  if (result < minimum || result > maximum) {
    throw std::runtime_error(label + ": must be an integer in [" +
                             std::to_string(minimum) + ", " +
                             std::to_string(maximum) + "]");
  }
  return result;
}

// A number that is finite and inside (0, maximum].
//
// A boolean is not a number, and an integer is a number: the profile writes
// "fps: 10" and "max_age_ms: 500" as integers.
double positive(const Json & value, const std::string & label,
                double maximum = 60000.0)
{
  if (value.is_boolean() || !value.is_number()) {
    throw std::runtime_error(label + ": must be finite and in (0, " +
                             std::to_string(static_cast<long long>(maximum)) +
                             "]");
  }
  const double number = value.get<double>();
  if (!std::isfinite(number) || !(number > 0.0) || number > maximum) {
    throw std::runtime_error(label + ": must be finite and in (0, " +
                             std::to_string(static_cast<long long>(maximum)) +
                             "]");
  }
  return number;
}

std::string fieldName(const Json & value, const std::string & label)
{
  const std::string path = text(value, label);
  if (!std::regex_match(path, fieldPattern()) ||
      path.find("__") != std::string::npos) {
    throw std::runtime_error(label + ": invalid ROS field path: " + path);
  }
  return path;
}

// The age limit of a feature, in nanoseconds.
//
// The rounding is the one of the original implementation: the product of
// the milliseconds and a million is rounded to the nearest integer, and a
// half goes to the even neighbour. A rounding that goes away from zero
// would move the threshold of a feature by one nanosecond.
std::int64_t ageLimitNs(double max_age_ms)
{
  const double product = max_age_ms * 1000000.0;
  const double lower = std::floor(product);
  const double fraction = product - lower;
  double rounded = lower;
  if (fraction > 0.5) {
    rounded = lower + 1.0;
  } else if (fraction == 0.5) {
    // A tie goes to the even neighbour.
    rounded = (std::fmod(lower, 2.0) == 0.0) ? lower : lower + 1.0;
  }
  if (rounded > 9.0e18) {
    throw std::runtime_error("max_age_ms: is out of range");
  }
  return static_cast<std::int64_t>(rounded);
}

// An exact rational number.
struct Rational {
  cpp_int numerator = 1;
  cpp_int denominator = 1;
};

// The greatest common divisor of two positive integers.
cpp_int greatestCommonDivisor(cpp_int left, cpp_int right)
{
  while (right != 0) {
    const cpp_int rest = left % right;
    left = right;
    right = rest;
  }
  return left;
}

// The exact rational value of a decimal number.
//
// The frame rate decides the length of a grid step, so it is kept as a
// fraction of integers and the grid is computed with integer arithmetic: a
// double would accumulate a rounding error over the steps of a long
// episode. The arithmetic uses big integers, so a frame rate like 1e-300 is
// exact and does not overflow.
Rational decimalToRational(const std::string & text_value,
                            const std::string & label)
{
  Rational result;
  std::size_t index = 0;
  bool negative = false;
  if (index < text_value.size() &&
      (text_value[index] == '+' || text_value[index] == '-')) {
    negative = text_value[index] == '-';
    ++index;
  }
  std::string digits;
  std::int64_t fraction_digits = 0;
  bool seen_point = false;
  std::int64_t exponent = 0;
  bool has_exponent = false;
  for (; index < text_value.size(); ++index) {
    const char character = text_value[index];
    if (character == '.') {
      seen_point = true;
      continue;
    }
    if (character == 'e' || character == 'E') {
      has_exponent = true;
      ++index;
      break;
    }
    if (character < '0' || character > '9') {
      throw std::runtime_error(label + ": is not a decimal number");
    }
    digits.push_back(character);
    if (seen_point) {
      ++fraction_digits;
    }
  }
  if (has_exponent) {
    bool exponent_negative = false;
    if (index < text_value.size() &&
        (text_value[index] == '+' || text_value[index] == '-')) {
      exponent_negative = text_value[index] == '-';
      ++index;
    }
    std::int64_t value = 0;
    for (; index < text_value.size(); ++index) {
      if (text_value[index] < '0' || text_value[index] > '9') {
        throw std::runtime_error(label + ": is not a decimal number");
      }
      value = value * 10 + (text_value[index] - '0');
      if (value > 1000000) {
        throw std::runtime_error(label + ": the exponent is out of range");
      }
    }
    exponent = exponent_negative ? -value : value;
  }
  if (digits.empty()) {
    throw std::runtime_error(label + ": is not a decimal number");
  }
  cpp_int numerator = 0;
  for (const char character : digits) {
    numerator *= 10;
    numerator += character - '0';
  }
  cpp_int denominator = 1;
  const std::int64_t scale = exponent - fraction_digits;
  if (scale > 0) {
    for (std::int64_t step = 0; step < scale; ++step) {
      numerator *= 10;
    }
  } else {
    for (std::int64_t step = 0; step < -scale; ++step) {
      denominator *= 10;
    }
  }
  if (negative) {
    numerator = -numerator;
  }
  // The fraction is reduced, so the denominator stays as small as possible.
  cpp_int divisor = greatestCommonDivisor(
    numerator < 0 ? -numerator : numerator, denominator);
  if (divisor > 1) {
    numerator /= divisor;
    denominator /= divisor;
  }
  result.numerator = numerator;
  result.denominator = denominator;
  return result;
}

// The shortest decimal representation of a double that reads back as the
// same value. The profile was written with a decimal number, so this is the
// number the author meant.
std::string shortestDecimal(double value)
{
  char buffer[64];
  const auto result =
    std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}

// The exact rational value of a number of the profile.
//
// An integer is exact already. A double is read through its shortest
// decimal representation, which is the value the profile was written with.
Rational numberToRational(const Json & value, const std::string & label)
{
  if (value.is_number_integer() && !value.is_boolean()) {
    Rational result;
    if (value.is_number_unsigned()) {
      const std::uint64_t raw = value.get<std::uint64_t>();
      if (raw > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error(label + ": is out of range");
      }
      result.numerator = static_cast<std::int64_t>(raw);
    } else {
      result.numerator = value.get<std::int64_t>();
    }
    result.denominator = 1;
    return result;
  }
  return decimalToRational(shortestDecimal(value.get<double>()), label);
}

void checkVector(const Json & value, const std::string & label)
{
  const Json * fields = member(value, "fields");
  const Json * units = member(value, "units");
  if (fields == nullptr || !fields->is_array() || fields->empty() ||
      fields->size() > 256) {
    throw std::runtime_error(label + ": fields must list 1 to 256 field paths");
  }
  if (units == nullptr || !units->is_array() ||
      units->size() != fields->size()) {
    throw std::runtime_error(label + ": units must match the field count");
  }
  std::vector<std::string> seen;
  for (std::size_t index = 0; index < fields->size(); ++index) {
    const std::string path =
      fieldName(fields->at(index), label + ".fields[" +
        std::to_string(index) + "]");
    if (std::find(seen.begin(), seen.end(), path) != seen.end()) {
      throw std::runtime_error(label + ": fields must be unique");
    }
    seen.push_back(path);
    text(units->at(index), label + ".units[" + std::to_string(index) + "]");
  }
}

}  // namespace

Profile loadProfile(const std::string & profile_path)
{
  std::error_code error;
  const std::filesystem::path resolved =
    std::filesystem::weakly_canonical(
      std::filesystem::absolute(std::filesystem::path(profile_path), error),
      error);
  if (error || !std::filesystem::is_regular_file(resolved, error)) {
    throw std::runtime_error("export profile not found: " + profile_path);
  }
  const std::uintmax_t size = std::filesystem::file_size(resolved, error);
  if (error || size > kMaxProfileBytes) {
    throw std::runtime_error("export profile must not exceed 1 MiB");
  }
  std::ifstream input(resolved, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read the export profile: " + profile_path);
  }
  std::stringstream buffer;
  buffer << input.rdbuf();
  const std::string raw = buffer.str();

  // The shared engine resolves the scalar types with its own strict resolver
  // and refuses a duplicate mapping key.
  Json root;
  try {
    root = chaos::assets::load_yaml_string(raw, "export profile");
  } catch (const std::exception & error) {
    throw std::runtime_error(
      std::string("cannot parse the export profile: ") + error.what());
  }
  if (!root.is_object()) {
    throw std::runtime_error("profile: expected a mapping");
  }
  checkKeys(root,
            {"schema_version", "fps", "allow_unverified_time", "features",
             "action", "episodes"},
            {"schema_version", "fps", "features", "action", "episodes"},
            "profile");

  Profile profile;
  profile.path = resolved;
  const Json * schema = member(root, "schema_version");
  if (!schema->is_number_integer() || schema->is_boolean() ||
      schema->get<std::int64_t>() != 1) {
    throw std::runtime_error("profile.schema_version: must be 1");
  }
  profile.schema_version = 1;

  const Json * fps = member(root, "fps");
  profile.fps = positive(*fps, "profile.fps", 240.0);
  const Rational rate = numberToRational(*fps, "profile.fps");
  profile.fps_numerator = rate.numerator;
  profile.fps_denominator = rate.denominator;
  if (const Json * allowed = member(root, "allow_unverified_time")) {
    if (!allowed->is_boolean()) {
      throw std::runtime_error("profile.allow_unverified_time: must be boolean");
    }
    profile.allow_unverified_time = allowed->get<bool>();
  }

  const Json * features = member(root, "features");
  if (!features->is_object() || features->empty() || features->size() > 32) {
    throw std::runtime_error(
      "profile.features: must contain between 1 and 32 explicit mappings");
  }
  const std::vector<std::string> common = {
    "kind", "topic", "type", "stamp_field", "frame_field", "frame",
    "clock_domain", "validity_fields", "max_age_ms", "units"};
  for (auto feature_it = features->begin(); feature_it != features->end();
       ++feature_it) {
    const std::string & feature_name = feature_it.key();
    if (!std::regex_match(feature_name, namePattern())) {
      throw std::runtime_error("profile.features: invalid feature name: " +
                               feature_name);
    }
    const std::string label = "feature " + feature_name;
    const Json & feature = feature_it.value();
    std::vector<std::string> required = common;
    required.erase(std::remove(required.begin(), required.end(),
                                "validity_fields"),
                   required.end());
    std::vector<std::string> allowed = common;
    allowed.push_back("fields");
    checkKeys(feature, allowed, required, label);

    FeatureSpec spec;
    spec.name = feature_name;
    const Json * kind = member(feature, "kind");
    if (kind->is_string() &&
        (kind->get_ref<const std::string &>() == "vector" ||
         kind->get_ref<const std::string &>() == "image")) {
      spec.kind = kind->get_ref<const std::string &>();
    } else {
      throw std::runtime_error(label + ": kind must be vector or image");
    }
    const std::string topic = text(*member(feature, "topic"), label + ".topic");
    if (!std::regex_match(topic, topicPattern())) {
      throw std::runtime_error(label + ".topic: must be an absolute ROS topic");
    }
    spec.topic = topic;
    spec.type = text(*member(feature, "type"), label + ".type");
    spec.stamp_field =
      fieldName(*member(feature, "stamp_field"), label + ".stamp_field");
    spec.frame_field =
      fieldName(*member(feature, "frame_field"), label + ".frame_field");
    spec.frame = text(*member(feature, "frame"), label + ".frame");
    const Json * clock = member(feature, "clock_domain");
    if (!clock->is_string() || clock->get_ref<const std::string &>() !=
          "system_utc") {
      throw std::runtime_error(
        label + ".clock_domain: only an explicitly established system_utc "
        "clock is supported");
    }
    spec.clock_domain = clock->get_ref<const std::string &>();
    spec.max_age_ms =
      positive(*member(feature, "max_age_ms"), label + ".max_age_ms");
    spec.max_age_ns = ageLimitNs(spec.max_age_ms);
    if (const Json * valid = member(feature, "validity_fields")) {
      if (!valid->is_array() || valid->size() > 32) {
        throw std::runtime_error(
          label + ".validity_fields: must list at most 32 boolean ROS fields");
      }
      for (auto item_it = valid->begin(); item_it != valid->end(); ++item_it) {
        const Json & item = *item_it;
        spec.validity_fields.push_back(
          fieldName(item, label + ".validity_fields"));
      }
    }
    if (spec.kind == "vector") {
      checkVector(feature, label);
      const Json * fields_value = member(feature, "fields");
      const Json * units_value = member(feature, "units");
      for (auto item_it = fields_value->begin(); item_it != fields_value->end();
           ++item_it) {
        const Json & item = *item_it;
        spec.fields.push_back(item.get_ref<const std::string &>());
      }
      for (auto item_it = units_value->begin(); item_it != units_value->end();
           ++item_it) {
        const Json & item = *item_it;
        spec.units.push_back(item.get_ref<const std::string &>());
      }
    } else {
      if (spec.type != "sensor_msgs/msg/Image" ||
          member(feature, "fields") != nullptr) {
        throw std::runtime_error(
          label + ": an image feature requires sensor_msgs/msg/Image and no "
          "fields");
      }
      spec.image_units = text(*member(feature, "units"), label + ".units");
    }
    if (spec.type == "chaos_interfaces/msg/RobotState") {
      if (spec.stamp_field != "pose.header.stamp" ||
          spec.frame_field != "pose.header.frame_id") {
        throw std::runtime_error(
          label + ": a RobotState observation requires pose.header source time "
          "and frame");
      }
      for (const auto & needed : {"pose_valid", "connected"}) {
        if (std::find(spec.validity_fields.begin(), spec.validity_fields.end(),
                      needed) == spec.validity_fields.end()) {
          throw std::runtime_error(
            label + ": a RobotState observation must require pose_valid and "
            "connected");
        }
      }
      for (const auto & path : spec.fields) {
        if (path.rfind("pose.pose.", 0) != 0) {
          throw std::runtime_error(
            label + ": a RobotState observation supports only pose.pose fields "
            "with their source stamp");
        }
      }
    }
    profile.features.push_back(spec);
  }

  const Json * action = member(root, "action");
  checkKeys(*action,
            {"topic", "fields", "units", "frame", "space", "clock_domain",
             "max_age_ms"},
            {"topic", "fields", "units", "frame", "space", "clock_domain",
             "max_age_ms"},
            "action");
  {
    const std::string topic = text(*member(*action, "topic"), "action.topic");
    if (!std::regex_match(topic, topicPattern())) {
      throw std::runtime_error("action.topic: must be an absolute ROS topic");
    }
    profile.action.topic = topic;
    const Json * clock = member(*action, "clock_domain");
    if (!clock->is_string() || clock->get_ref<const std::string &>() !=
          "system_utc") {
      throw std::runtime_error("action.clock_domain: must be system_utc");
    }
    profile.action.clock_domain = clock->get_ref<const std::string &>();
    profile.action.space = text(*member(*action, "space"), "action.space");
    profile.action.frame = text(*member(*action, "frame"), "action.frame");
    profile.action.max_age_ms =
      positive(*member(*action, "max_age_ms"), "action.max_age_ms");
    profile.action.max_age_ns = ageLimitNs(profile.action.max_age_ms);
    checkVector(*action, "action");
    const Json * fields = member(*action, "fields");
    const Json * units = member(*action, "units");
    for (auto item_it = fields->begin(); item_it != fields->end(); ++item_it) {
      const Json & item = *item_it;
      profile.action.fields.push_back(item.get_ref<const std::string &>());
    }
    for (auto item_it = units->begin(); item_it != units->end(); ++item_it) {
      const Json & item = *item_it;
      profile.action.units.push_back(item.get_ref<const std::string &>());
    }
    const std::vector<std::string> supported = {
      "velocity.linear.x", "velocity.linear.y", "velocity.linear.z",
      "velocity.angular.x", "velocity.angular.y", "velocity.angular.z"};
    for (const auto & field : profile.action.fields) {
      if (std::find(supported.begin(), supported.end(), field) == supported.end()) {
        throw std::runtime_error(
          "action.fields: must select ControlTrace velocity components");
      }
    }
  }

  // One topic has one type, and the control trace is not an observation.
  std::map<std::string, std::string> topic_types;
  for (const auto & feature : profile.features) {
    const auto found = topic_types.find(feature.topic);
    if (found != topic_types.end() && found->second != feature.type) {
      throw std::runtime_error(
        "profile.features: one topic cannot have conflicting feature types");
    }
    topic_types[feature.topic] = feature.type;
  }
  if (topic_types.find(profile.action.topic) != topic_types.end()) {
    throw std::runtime_error(
      "profile.action: the control trace must not also be an observation input");
  }

  const Json * episodes = member(root, "episodes");
  if (!episodes->is_array() || episodes->empty() || episodes->size() > 1024) {
    throw std::runtime_error(
      "profile.episodes: must contain between 1 and 1024 task intervals");
  }
  std::vector<std::string> names;
  for (auto item_it = episodes->begin(); item_it != episodes->end(); ++item_it) {
    const Json & item = *item_it;
    checkKeys(item,
              {"id", "session", "segment", "start_utc_ns", "end_utc_ns", "task",
               "outcome"},
              {"id", "session", "segment", "start_utc_ns", "end_utc_ns", "task",
               "outcome"},
              "episode");
    EpisodeSpec episode;
    const Json * id = member(item, "id");
    if (!id->is_string() || !std::regex_match(
          id->get_ref<const std::string &>(), namePattern()) ||
        std::find(names.begin(), names.end(),
                  id->get_ref<const std::string &>()) != names.end()) {
      throw std::runtime_error("episode.id: must be safe and unique");
    }
    episode.id = id->get_ref<const std::string &>();
    names.push_back(episode.id);
    episode.segment =
      static_cast<int>(integer(*member(item, "segment"), "episode.segment"));
    episode.start_utc_ns =
      integer(*member(item, "start_utc_ns"), "episode.start_utc_ns", 0);
    episode.end_utc_ns =
      integer(*member(item, "end_utc_ns"), "episode.end_utc_ns", 0);
    if (!(episode.start_utc_ns < episode.end_utc_ns) ||
        episode.end_utc_ns - episode.start_utc_ns > 86400LL * 1000000000LL) {
      throw std::runtime_error(
        "episode: the interval must be positive and at most 24 hours");
    }
    // The number of grid steps of the episode, computed exactly.
    const cpp_int steps = cpp_int(episode.end_utc_ns - episode.start_utc_ns) *
      profile.fps_numerator;
    const cpp_int limit = cpp_int(kMaxEpisodeSteps) * profile.fps_denominator *
      1000000000LL;
    if (steps > limit) {
      throw std::runtime_error(
        "episode: exceeds the " + std::to_string(kMaxEpisodeSteps) +
        " grid step limit");
    }
    episode.task = text(*member(item, "task"), "episode.task");
    const Json * outcome = member(item, "outcome");
    if (!outcome->is_string()) {
      throw std::runtime_error(
        "episode.outcome: must be success, failure, aborted, or unknown");
    }
    const std::string value = outcome->get_ref<const std::string &>();
    if (value != "success" && value != "failure" && value != "aborted" &&
        value != "unknown") {
      throw std::runtime_error(
        "episode.outcome: must be success, failure, aborted, or unknown");
    }
    episode.outcome = value;

    // The session is relative to the directory of the profile.
    const std::string session =
      text(*member(item, "session"), "episode.session");
    std::filesystem::path session_path(session);
    if (session_path.is_relative()) {
      session_path = resolved.parent_path() / session_path;
    }
    const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(session_path, error);
    if (error || !std::filesystem::is_directory(canonical, error)) {
      throw std::runtime_error("episode.session: not found: " + session);
    }
    episode.session = canonical;
    profile.episodes.push_back(episode);
  }

  // The profile is published with the resolved session paths, so the export
  // is reproducible from the document alone.
  profile.raw = root;
  if (!profile.raw.contains("allow_unverified_time")) {
    profile.raw["allow_unverified_time"] = profile.allow_unverified_time;
  }
  for (auto & item : profile.raw["episodes"]) {
    for (const auto & episode : profile.episodes) {
      if (episode.id == item["id"].get<std::string>()) {
        item["session"] = episode.session.string();
      }
    }
  }
  return profile;
}

}  // namespace chaos::data
