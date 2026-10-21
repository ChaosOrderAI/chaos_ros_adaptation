// Port of chaos_runtime/control.py. Behavior, error codes, and validation
// order are the compatibility contract; keep them in sync with the Python
// oracle in Git history until the Python implementation is retired.
#include "chaos_runtime/control.hpp"

#include <cerrno>
#include <cmath>
#include <stdexcept>
#include <sys/random.h>

#include "chaos_core/clock.hpp"
#include "chaos_core/time.hpp"

namespace chaos::control {

namespace {
constexpr std::int64_t kNanoseconds = 1'000'000'000;

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

// Python str.strip() whitespace, restricted to the Unicode whitespace set
// that matters for owner identifiers (ASCII plus the common BMP spaces).
bool is_unicode_whitespace(std::uint32_t code_point) {
  if (code_point == ' ' || (code_point >= 0x09 && code_point <= 0x0d) ||
      code_point == 0x85) {
    return true;
  }
  if (code_point < 0xa0) {
    return false;
  }
  return code_point == 0xa0 || code_point == 0x1680 ||
         (code_point >= 0x2000 && code_point <= 0x200a) ||
         code_point == 0x2028 || code_point == 0x2029 ||
         code_point == 0x202f || code_point == 0x205f ||
         code_point == 0x3000 || code_point == 0xfeff;
}

// Decodes one UTF-8 sequence; returns false for malformed input, which is
// rejected at the owner-validation boundary.
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
  if ((lead >> 5) == 0x6) {
    length = 2;
    code = lead & 0x1f;
  } else if ((lead >> 4) == 0xe) {
    length = 3;
    code = lead & 0x0f;
  } else if ((lead >> 3) == 0x1e) {
    length = 4;
    code = lead & 0x07;
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
  } else {
    require(token_value);
    if (owner_ != owner_value) {
      throw Rejected("NO_CONTROL", "Lease owner does not match");
    }
    if (operation == "renew") {
      expires_ = clock_() + duration;
    } else {
      owner_.clear();
      token_.clear();
      expires_ = 0.0;
      velocity_expires_ = 0.0;
    }
  }
  return {token_, remaining()};
}

void ControlGuard::velocity(const std::string & token_value, double stamp_s,
                            double ros_now_s, double valid_for_s,
                            const std::string & frame,
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
  if (!std::isfinite(stamp_s) || stamp_s <= 0) {
    throw Rejected("STALE_COMMAND", "A positive source timestamp is required");
  }
  const double age = ros_now_s - stamp_s;
  if (!std::isfinite(age) || age < -0.1 || age >= ttl) {
    throw Rejected("STALE_COMMAND",
                   "Command is expired or timestamp is in the future");
  }
  // Network transit consumes TTL. Slight future skew never extends beyond TTL.
  velocity_expires_ = clock_() + std::min(ttl, ttl - age);
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
  if ((kind == "ugv" || kind == "quadruped") &&
      std::abs(position[2]) > 1e-6) {
    throw Rejected("UNSUPPORTED", "Ground navigation requires z=0");
  }
  if (kind == "uav" && !(position[2] >= 0 && position[2] <= max_altitude)) {
    throw Rejected("OUT_OF_RANGE", "Navigation altitude exceeds limits");
  }
}

}  // namespace chaos::control
