// Behavior, error codes, and validation order are the compatibility
// contract carried over from the retired Python implementation.
#include "chaos_runtime/control.hpp"

#include <cerrno>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <sys/random.h>

#include "chaos_core/clock.hpp"
#include "chaos_core/time.hpp"

namespace chaos::control {

namespace {
constexpr std::int64_t kNanoseconds = 1'000'000'000;

// Marker for an unusable ROS-now reading. The age check treats it as an
// impossibly old reading, so a bogus clock value can never extend the life
// of a command and never reaches an undefined subtraction.
constexpr std::int64_t kInvalidNowNs = std::numeric_limits<std::int64_t>::min();

// Scales a seconds-valued timestamp into integer nanoseconds without
// overflowing. The range is checked BEFORE the integer conversion, so an
// unrepresentable value never reaches the conversion (undefined behaviour).
// Returns false when the value is not finite or does not fit in int64.
bool scaledNanoseconds(double seconds, std::int64_t & out_ns) {
  if (!std::isfinite(seconds)) {
    return false;
  }
  // The scaling happens in a wider type: the double product of a huge
  // finite value would already have lost the integer precision.
  const long double scaled =
    static_cast<long double>(seconds) * 1'000'000'000.0L;
  if (!std::isfinite(scaled)) {
    return false;
  }
  // llround semantics (half away from zero) without the undefined
  // conversion of an out-of-range value.
  const long double rounded = std::round(scaled);
  // 2^63 is exactly representable in every binary floating format used
  // here, so the bound is exact. The negated comparison also rejects NaN.
  constexpr long double kInt64Bound = 9223372036854775808.0L;
  if (!(rounded > -kInt64Bound && rounded < kInt64Bound)) {
    return false;
  }
  out_ns = static_cast<std::int64_t>(rounded);
  return true;
}

// 32 random bytes encoded as unpadded base64url, matching
// secrets.token_urlsafe(32) (43 characters). Generation failure throws;
// a lease is never armed with a predictable or empty token.
std::string default_token() {
  unsigned char bytes[32];
  std::size_t offset = 0;
  while (offset < sizeof(bytes)) {
    const ssize_t count =
      getrandom(bytes + offset, sizeof(bytes) - offset, 0);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("control token generation failed");
    }
    offset += static_cast<std::size_t>(count);
  }
  static constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string token;
  token.reserve(43);
  std::size_t i = 0;
  for (; i + 3 <= sizeof(bytes); i += 3) {
    const unsigned value =
      (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
    token += kAlphabet[(value >> 18) & 0x3f];
    token += kAlphabet[(value >> 12) & 0x3f];
    token += kAlphabet[(value >> 6) & 0x3f];
    token += kAlphabet[value & 0x3f];
  }
  const unsigned value = (bytes[i] << 16) | (bytes[i + 1] << 8);
  token += kAlphabet[(value >> 18) & 0x3f];
  token += kAlphabet[(value >> 12) & 0x3f];
  token += kAlphabet[(value >> 6) & 0x3f];
  return token;
}

bool is_ascii(const std::string & value) {
  for (unsigned char ch : value) {
    if (ch >= 0x80) {
      return false;
    }
  }
  return true;
}

// Whitespace matched against Python's str.strip() behavior for the ranges
// relevant to owner identifiers. Note: U+FEFF (BOM) is NOT Python
// whitespace and must not be stripped; U+001C..U+001F are.
bool is_unicode_whitespace(std::uint32_t code_point) {
  if (code_point == ' ' || (code_point >= 0x09 && code_point <= 0x0d)) {
    return true;
  }
  if (code_point >= 0x1c && code_point <= 0x1f) {
    return true;
  }
  if (code_point < 0x85) {
    return false;
  }
  return code_point == 0x85 || code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 ||
         code_point == 0x202f || code_point == 0x205f ||
         code_point == 0x3000;
}

// Decodes one UTF-8 sequence, rejecting overlong encodings, surrogate code
// points, and values above U+10FFFF. Malformed input is rejected at the
// owner-validation boundary rather than being silently re-interpreted.
bool decode_utf8(const std::string & value, std::size_t & position,
                 std::uint32_t & code_point) {
  const auto byte = [&](std::size_t i) {
    return static_cast<unsigned char>(value[i]);
  };
  const unsigned char lead = byte(position);
  if (lead < 0x80) {
    code_point = lead;
    ++position;
    return true;
  }
  int length = 0;
  std::uint32_t code = 0;
  std::uint32_t minimum = 0;
  if ((lead >> 5) == 0x6) {
    if (lead < 0xc2) {
      return false;  // overlong 2-byte sequence
    }
    length = 2;
    code = lead & 0x1f;
    minimum = 0x80;
  } else if ((lead >> 4) == 0xe) {
    length = 3;
    code = lead & 0x0f;
    minimum = 0x800;
  } else if ((lead >> 3) == 0x1e) {
    if (lead > 0xf4) {
      return false;  // above U+10FFFF
    }
    length = 4;
    code = lead & 0x07;
    minimum = 0x10000;
  } else {
    return false;
  }
  if (position + static_cast<std::size_t>(length) > value.size()) {
    return false;
  }
  for (int j = 1; j < length; ++j) {
    const unsigned char next = byte(position + j);
    if ((next >> 6) != 0x2) {
      return false;
    }
    code = (code << 6) | (next & 0x3f);
  }
  if (code < minimum || code > 0x10ffff ||
      (code >= 0xd800 && code <= 0xdfff)) {
    return false;
  }
  position += static_cast<std::size_t>(length);
  code_point = code;
  return true;
}

// Mirrors Python's `not owner.strip()`: owner is valid when it contains at
// least one non-whitespace character (Unicode whitespace) and at most 128
// Unicode characters (not bytes).
bool valid_owner(const std::string & owner) {
  std::size_t position = 0;
  int characters = 0;
  bool has_non_whitespace = false;
  while (position < owner.size()) {
    std::uint32_t code_point = 0;
    if (!decode_utf8(owner, position, code_point)) {
      return false;
    }
    ++characters;
    if (characters > 128) {
      return false;
    }
    if (!is_unicode_whitespace(code_point)) {
      has_non_whitespace = true;
    }
  }
  return has_non_whitespace;
}
}  // namespace

double finite_positive(double value, const std::string & name) {
  if (!std::isfinite(value) || value <= 0) {
    throw Rejected("INVALID_ARGUMENT", name + " must be finite and positive");
  }
  return value;
}

Limits Limits::fromMap(const std::map<std::string, double> & values) {
  Limits limits;
  auto apply = [&values](const char * name, double & slot) {
    auto found = values.find(name);
    if (found != values.end()) {
      slot = finite_positive(found->second, name);
    }
  };
  apply("linear_mps", limits.linear_mps);
  apply("angular_rps", limits.angular_rps);
  apply("command_timeout_s", limits.command_timeout_s);
  apply("state_timeout_s", limits.state_timeout_s);
  apply("lease_max_s", limits.lease_max_s);
  apply("action_timeout_s", limits.action_timeout_s);
  apply("max_altitude_m", limits.max_altitude_m);
  return limits;
}

bool constant_time_equal(const std::string & a, const std::string & b) {
  // Volatile accumulator: the compiler cannot vectorize or short-circuit
  // the loop, and every byte position is always visited. The control flow
  // is independent of the secret contents; only the public length bound
  // (128-char owner / 43-char token envelope) influences iterations.
  volatile std::size_t difference = a.size() ^ b.size();
  const std::size_t longest = a.size() > b.size() ? a.size() : b.size();
  for (std::size_t i = 0; i < longest; ++i) {
    const unsigned char x = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
    const unsigned char y = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
    difference |= x ^ y;
  }
  return difference == 0;
}

CommandDeadline CommandDeadline::capture(const RosTime & stamp,
                                         std::int64_t wall_now_ns,
                                         std::int64_t monotonic_now_ns) {
  // Read monotonic first: preemption between the two clock reads may
  // shorten the allowed interval but must never extend it.
  std::int64_t unix_ns = 0;
  const bool valid = chaos::rosStampToNs(stamp.sec, stamp.nanosec, unix_ns);
  const std::int64_t remaining_ns = valid ? unix_ns - wall_now_ns : 0;
  return CommandDeadline(unix_ns, monotonic_now_ns + remaining_ns, valid);
}

CommandDeadline CommandDeadline::capture(const RosTime & stamp) {
  const std::int64_t monotonic_now_ns = chaos::monotonicNs();
  const std::int64_t wall_now_ns = chaos::systemNs();
  return capture(stamp, wall_now_ns, monotonic_now_ns);
}

void CommandDeadline::require(std::int64_t wall_now_ns,
                              std::int64_t monotonic_now_ns) const {
  if (!valid_) {
    throw Rejected("INVALID_ARGUMENT",
                   "expires_at must be a valid Unix UTC timestamp");
  }
  if (unix_ns_ != 0 &&
      (wall_now_ns >= unix_ns_ || monotonic_now_ns >= monotonic_ns_)) {
    throw Rejected("STALE_COMMAND", "Command expired before execution");
  }
}

void CommandDeadline::require() const {
  require(chaos::systemNs(), chaos::monotonicNs());
}

ControlGuard::ControlGuard(Limits limits_value,
                           std::function<double()> clock,
                           std::function<std::string()> token_factory)
    : limits_(limits_value),
      clock_(clock ? std::move(clock) : chaos::monotonicSeconds),
      token_factory_(token_factory ? std::move(token_factory) : default_token) {}

double ControlGuard::remaining() const {
  return token_.empty() ? 0.0 : std::max(0.0, expires_ - clock_());
}

bool ControlGuard::expire() {
  if (!token_.empty() && remaining() <= 0) {
    owner_.clear();
    token_.clear();
    return true;
  }
  return false;
}

void ControlGuard::require(const std::string & token_value) {
  expire();
  if (token_value.empty() || !is_ascii(token_value) || token_.empty() ||
      !constant_time_equal(token_value, token_)) {
    throw Rejected("NO_CONTROL", "A current control lease is required");
  }
}

std::pair<std::string, double> ControlGuard::lease(const std::string & operation,
                                                   const std::string & owner_value,
                                                   const std::string & token_value,
                                                   double duration_s) {
  expire();
  if (!valid_owner(owner_value)) {
    throw Rejected("INVALID_ARGUMENT",
                   "owner must be a nonempty string of at most 128 characters");
  }
  if (operation != "acquire" && operation != "renew" &&
      operation != "release") {
    throw Rejected("INVALID_ARGUMENT",
                   "operation must be acquire, renew or release");
  }
  double duration = duration_s;
  if (operation != "release") {
    duration = finite_positive(duration_s, "duration_s");
    if (duration > limits_.lease_max_s) {
      throw Rejected("OUT_OF_RANGE", "duration_s exceeds lease_max_s");
    }
  }
  if (operation == "acquire") {
    if (!token_.empty()) {
      throw Rejected("BUSY", "Control is already leased");
    }
    owner_ = owner_value;
    token_ = token_factory_();
    expires_ = clock_() + duration;
    // A new control session starts a fresh stamp sequence.
    stamp_watermark_ns_ = 0;
  } else {
    require(token_value);
    if (owner_ != owner_value) {
      throw Rejected("NO_CONTROL", "Lease owner does not match");
    }
    if (operation == "renew") {
      // Renew extends the lease but never the stamp watermark: replayed
      // source stamps stay stale for the whole lease lifetime.
      expires_ = clock_() + duration;
    } else {
      // A release ends the control session: the token, the armed velocity
      // sample and the stamp watermark are cleared.
      revoke();
    }
  }
  return {token_, remaining()};
}

void ControlGuard::revoke() {
  // Clear the whole control session without consulting the clock and
  // without re-validating the caller: the caller has already validated
  // the request, and a lease that expired in between must not survive.
  owner_.clear();
  token_.clear();
  expires_ = 0.0;
  velocity_expires_ = 0.0;
  stamp_watermark_ns_ = 0;
}

void ControlGuard::velocity(const std::string & token_value, double stamp_s,
                            double ros_now_s, double valid_for_s,
                            const std::string & frame,
                            const std::string & base_frame,
                            const std::array<double, 6> & values) {
  // The token is checked first: a bad token is NO_CONTROL even when the
  // source stamp is unusable. The TTL, the frame and the velocity
  // components are validated by velocityNs(), which rejects them before
  // the source stamp, so the error precedence is unchanged.
  require(token_value);
  // The stamps are scaled with a bound that is checked before the integer
  // conversion: a huge finite value, a NaN or an infinity never reaches
  // the conversion and never overflows. An unusable stamp is handed over
  // as an invalid marker and rejected by the typed validator, after the
  // TTL, the frame and the velocity components have been validated.
  std::int64_t stamp_ns = 0;
  if (!scaledNanoseconds(stamp_s, stamp_ns) || stamp_ns <= 0) {
    stamp_ns = 0;  // "no usable source timestamp"
  }
  std::int64_t now_ns = kInvalidNowNs;
  if (!scaledNanoseconds(ros_now_s, now_ns)) {
    now_ns = kInvalidNowNs;
  }
  velocityNs(token_value, stamp_ns, now_ns, valid_for_s, frame, base_frame,
             values);
}

void ControlGuard::velocityNs(const std::string & token_value,
                              std::int64_t stamp_ns, std::int64_t ros_now_ns,
                              double valid_for_s, const std::string & frame,
                              const std::string & base_frame,
                              const std::array<double, 6> & values) {
  require(token_value);
  const double ttl = finite_positive(valid_for_s, "valid_for_s");
  if (ttl > limits_.command_timeout_s) {
    throw Rejected("OUT_OF_RANGE", "valid_for_s exceeds command_timeout_s");
  }
  if (frame != base_frame) {
    throw Rejected("INVALID_FRAME", "Expected velocity frame " + base_frame);
  }
  for (double value : values) {
    if (!std::isfinite(value)) {
      throw Rejected("INVALID_ARGUMENT", "Velocity components must be finite");
    }
  }
  const double linear =
    std::hypot(values[0], std::hypot(values[1], values[2]));
  const double angular =
    std::hypot(values[3], std::hypot(values[4], values[5]));
  if (linear > limits_.linear_mps + 1e-9) {
    throw Rejected("OUT_OF_RANGE", "Linear speed exceeds asset limit");
  }
  if (angular > limits_.angular_rps + 1e-9) {
    throw Rejected("OUT_OF_RANGE", "Angular speed exceeds asset limit");
  }
  if (stamp_ns <= 0) {
    throw Rejected("STALE_COMMAND", "A positive source timestamp is required");
  }
  // The age is computed in a wider floating type: the int64 subtraction
  // itself can overflow (INT64_MIN - 1 is undefined behaviour), so the
  // operands are widened BEFORE the difference is taken. An out-of-domain
  // clock reading therefore yields an extreme age and is rejected as stale
  // instead of trapping.
  const long double delta_ns =
    static_cast<long double>(ros_now_ns) - static_cast<long double>(stamp_ns);
  const double age = static_cast<double>(delta_ns) / kNanoseconds;
  if (!std::isfinite(age) || age < -0.1 || age >= ttl) {
    throw Rejected("STALE_COMMAND",
                   "Command is expired or timestamp is in the future");
  }
  if (stamp_ns <= stamp_watermark_ns_) {
    // The watermark comparison stays integer: adjacent-1ns stamps remain
    // distinguishable even though the age above is computed in a wider
    // floating type.
    throw Rejected("STALE_COMMAND",
                   "Velocity source timestamp must increase per lease");
  }
  // Network transit consumes TTL. Slight future skew never extends beyond TTL.
  // Watermark and deadline commit only after every validation above passed:
  // a rejected packet neither extends the old TTL nor advances the watermark.
  velocity_expires_ = clock_() + std::min(ttl, ttl - age);
  stamp_watermark_ns_ = stamp_ns;
}

bool ControlGuard::velocityTimedOut() const {
  return velocity_expires_ != 0.0 && clock_() >= velocity_expires_;
}

void validate_pose(const std::string & frame,
                   const std::string & expected_frame,
                   const std::array<double, 3> & position,
                   const std::array<double, 4> & quaternion,
                   const std::string & kind, double max_altitude) {
  if (frame != expected_frame) {
    throw Rejected("INVALID_FRAME",
                   "Expected navigation frame " + expected_frame);
  }
  for (double value : position) {
    if (!std::isfinite(value)) {
      throw Rejected("INVALID_ARGUMENT", "Pose must be finite");
    }
  }
  for (double value : quaternion) {
    if (!std::isfinite(value)) {
      throw Rejected("INVALID_ARGUMENT", "Pose must be finite");
    }
  }
  const double norm = std::hypot(
    quaternion[0], std::hypot(quaternion[1],
                              std::hypot(quaternion[2], quaternion[3])));
  if (std::abs(norm - 1.0) > 0.01) {
    throw Rejected("INVALID_ARGUMENT",
                   "Orientation quaternion must be normalized");
  }
  // First adapters support planar yaw, never silently discard roll/pitch.
  if (std::abs(quaternion[0]) > 1e-6 || std::abs(quaternion[1]) > 1e-6) {
    throw Rejected("UNSUPPORTED", "Navigation orientation supports yaw only");
  }
  if ((kind == "ugv" || kind == "quadruped" || kind == "humanoid") &&
      std::abs(position[2]) > 1e-6) {
    throw Rejected("UNSUPPORTED", "Ground navigation requires z=0");
  }
  if (kind == "uav" && !(position[2] >= 0 && position[2] <= max_altitude)) {
    throw Rejected("OUT_OF_RANGE", "Navigation altitude exceeds limits");
  }
}

}  // namespace chaos::control
