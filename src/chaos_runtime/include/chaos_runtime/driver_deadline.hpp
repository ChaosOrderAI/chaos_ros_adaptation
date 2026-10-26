// Go2 driver deadline facilities: the Linux clock-domain identity and the
// checked, conservative conversions that produce the immutable
// CLOCK_MONOTONIC deadline D of one admitted motion command.
//
// ROS-free and header-only on purpose. The runtime bridge, the adapter and
// the supervised driver must all agree on one named clock (Linux
// CLOCK_MONOTONIC) and on one clock-domain identity, so the helper is shared
// by every participant instead of being re-implemented per process.
//
// Contract summary:
//   * D is an absolute CLOCK_MONOTONIC nanosecond value captured once at
//     runtime admission. Transport delay, queueing, a runtime pause, a
//     retransmission and a later lease renewal consume that same interval;
//     none of them extends D.
//   * Every conversion is checked and conservative. A duration never rounds
//     up and never extends the original interval by a rounding artifact, an
//     interval shorter than one nanosecond cannot arm motion, and an
//     addition that would overflow int64 is refused instead of wrapped.
//   * The pure helpers take every clock sample from the caller and never read
//     a clock themselves. Only monotonicNowNs() and localClockDomainId()
//     touch the system.
//   * This is local-output safety bookkeeping. It never confirms that a
//     physical robot stopped.
//
// Threading: every function is stateless and reentrant. localClockDomainId()
// reads /proc and is meant to be called once at startup and cached by the
// caller; it mutates no shared state.
#pragma once

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

namespace chaos::runtime::driver_deadline {

namespace detail {

constexpr std::int64_t kInt64Max = INT64_MAX;
constexpr std::int64_t kNsPerSecond = 1000000000LL;

// The exact nanosecond numerator of floorDurationNs() needs at most 83
// bits, so it is formed in a 128-bit unsigned integer. The supported
// Linux toolchains (GCC and Clang) provide it.
#if !defined(__SIZEOF_INT128__)
#error "chaos_runtime/driver_deadline.hpp requires a 128-bit integer type"
#endif
using WideUnsigned = unsigned __int128;

// The exact decomposition used by floorDurationNs() relies on the
// IEEE-754 binary64 layout of `double`.
static_assert(std::numeric_limits<double>::is_iec559,
              "double must be an IEEE-754 binary64 format");
static_assert(std::numeric_limits<double>::radix == 2,
              "double must use a radix of 2");
static_assert(std::numeric_limits<double>::digits == 53,
              "double must carry a 53-bit significand");

// A source stamp may sit at most this far in the future of the local ROS
// clock. A stamp that is further ahead is implausible (clock jump, replay or
// forgery) and is refused instead of being granted a full TTL.
constexpr std::int64_t kMaxSourceFutureNs = 100000000LL;  // 100 ms

// Canonical boot identity shape: lower-case hex in 8-4-4-4-12 groups.
inline bool isCanonicalBootUuid(const std::string & text) {
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

// Closes the descriptor exactly once, on every exit path.
class FileDescriptorGuard {
public:
  explicit FileDescriptorGuard(int descriptor) : descriptor_(descriptor) {}
  ~FileDescriptorGuard() {
    if (descriptor_ >= 0) {
      ::close(descriptor_);
    }
  }
  FileDescriptorGuard(const FileDescriptorGuard &) = delete;
  FileDescriptorGuard & operator=(const FileDescriptorGuard &) = delete;

private:
  int descriptor_;
};

}  // namespace detail

// Actual local CLOCK_MONOTONIC reading in nanoseconds.
//
// The call is explicitly clock_gettime(CLOCK_MONOTONIC): ROS time and
// std::steady_clock are not used, because the deadline is only meaningful
// against the same named clock on every participant of the transport. There
// is no fallback path: a failure throws, so the caller fails closed instead
// of silently switching to another clock.
inline std::int64_t monotonicNowNs() {
  struct timespec reading {};
  if (::clock_gettime(CLOCK_MONOTONIC, &reading) != 0) {
    throw std::runtime_error(std::string("clock_gettime(CLOCK_MONOTONIC) failed: ") +
                             std::strerror(errno));
  }
  if (reading.tv_sec < 0 || reading.tv_nsec < 0 ||
      reading.tv_nsec >= detail::kNsPerSecond) {
    throw std::runtime_error(
      "clock_gettime(CLOCK_MONOTONIC) returned an invalid timespec");
  }
  // Checked conversion: seconds * 1e9 + nanoseconds must fit in int64.
  if (reading.tv_sec > (detail::kInt64Max - reading.tv_nsec) / detail::kNsPerSecond) {
    throw std::runtime_error(
      "CLOCK_MONOTONIC reading does not fit into int64 nanoseconds");
  }
  return static_cast<std::int64_t>(reading.tv_sec) * detail::kNsPerSecond +
         static_cast<std::int64_t>(reading.tv_nsec);
}

// Serializes a validated clock-domain identity.
//
// The identity names the boot instance and the Linux time namespace that the
// deadline belongs to. The boot part is the canonical lower-case
// /proc/sys/kernel/random/boot_id, the namespace part is the device/inode
// pair of the opened /proc/self/ns/time entry. Two participants may compare
// deadlines only when both parts match.
//
// This is a clock-compatibility check. It is not authentication and it does
// not protect against an arbitrary publisher.
//
// Throws std::invalid_argument for a malformed boot identity or a zero
// namespace inode.
inline std::string clockDomainIdFromParts(const std::string & canonical_boot_uuid,
                                          std::uint64_t ns_dev,
                                          std::uint64_t ns_ino) {
  if (!detail::isCanonicalBootUuid(canonical_boot_uuid)) {
    throw std::invalid_argument(
      "boot identity is not a canonical lower-case 8-4-4-4-12 hex uuid");
  }
  if (ns_ino == 0U) {
    throw std::invalid_argument("time namespace inode must be non-zero");
  }
  return "v1:" + canonical_boot_uuid + ":" + std::to_string(ns_dev) + ":" +
         std::to_string(ns_ino);
}

// Local clock-domain identity of this process.
//
// Reads the boot identity from /proc/sys/kernel/random/boot_id with a bounded
// read and removes a single trailing newline, then opens /proc/self/ns/time
// and takes the identity of the opened namespace inode with fstat. The magic
// symlink is followed by open(2) itself, so the device/inode pair describes
// the real time namespace and never a guessed or cached path.
//
// The caller is expected to call this once at startup and cache the result.
// Every failure throws; there is no degraded identity. A runtime and a driver
// that disagree on this value must refuse to arm motion.
inline std::string localClockDomainId() {
  constexpr std::size_t kMaxBootIdBytes = 128U;

  std::string boot_identity;
  {
    const int descriptor =
      ::open("/proc/sys/kernel/random/boot_id", O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
      throw std::runtime_error(
        std::string("cannot open /proc/sys/kernel/random/boot_id: ") +
        std::strerror(errno));
    }
    detail::FileDescriptorGuard guard(descriptor);

    char buffer[kMaxBootIdBytes];
    std::size_t total = 0U;
    while (total < kMaxBootIdBytes) {
      const ssize_t count =
        ::read(descriptor, buffer + total, kMaxBootIdBytes - total);
      if (count < 0) {
        if (errno == EINTR) {
          continue;
        }
        throw std::runtime_error(
          std::string("cannot read /proc/sys/kernel/random/boot_id: ") +
          std::strerror(errno));
      }
      if (count == 0) {
        break;
      }
      total += static_cast<std::size_t>(count);
    }
    if (total == 0U) {
      throw std::runtime_error("/proc/sys/kernel/random/boot_id is empty");
    }
    boot_identity.assign(buffer, total);
  }
  // Exactly one trailing newline is removed; anything else stays and makes
  // the shape validation fail.
  if (!boot_identity.empty() && boot_identity.back() == '\n') {
    boot_identity.pop_back();
  }
  if (!detail::isCanonicalBootUuid(boot_identity)) {
    throw std::runtime_error(
      "/proc/sys/kernel/random/boot_id is not a canonical boot identity");
  }

  std::uint64_t ns_dev = 0U;
  std::uint64_t ns_ino = 0U;
  {
    const int descriptor = ::open("/proc/self/ns/time", O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
      throw std::runtime_error(
        std::string("cannot open /proc/self/ns/time: ") + std::strerror(errno));
    }
    detail::FileDescriptorGuard guard(descriptor);

    struct stat identity {};
    if (::fstat(descriptor, &identity) != 0) {
      throw std::runtime_error(std::string("cannot stat /proc/self/ns/time: ") +
                               std::strerror(errno));
    }
    if (identity.st_ino == 0) {
      throw std::runtime_error("/proc/self/ns/time reported a zero inode");
    }
    if (S_ISDIR(identity.st_mode)) {
      throw std::runtime_error("/proc/self/ns/time resolved to a directory");
    }
    ns_dev = static_cast<std::uint64_t>(identity.st_dev);
    ns_ino = static_cast<std::uint64_t>(identity.st_ino);
  }

  return clockDomainIdFromParts(boot_identity, ns_dev, ns_ino);
}

// Converts a positive duration in seconds into whole nanoseconds, rounding
// down.
//
// The result is the floor of the EXACT rational value seconds * 1e9, where
// `seconds` denotes the real number that the binary64 input represents. The
// product is therefore NOT formed in floating point: a floating-point
// product rounds to nearest and can therefore report one nanosecond MORE
// than the true duration. The safety requirement is the opposite: a
// conversion may only shorten an interval, it must never extend the
// original TTL or the driver cap by a rounding artifact.
//
// The binary64 value is decomposed exactly as
//
//   seconds = (mantissa / 2^53) * 2^exponent,   mantissa in [2^52, 2^53)
//
// with an integral mantissa, so
//
//   seconds * 1e9 = (mantissa * 1e9) / 2^(53 - exponent)
//
// and the floor is the right shift of that integer numerator. The numerator
// needs at most 83 bits, so it is formed in a 128-bit unsigned integer and
// the shift is exact. Subnormal inputs use the same decomposition: frexp
// normalizes the fraction into [0.5, 1) and keeps an exact exponent, so
// the recovered significand stays integral. No epsilon, no ULP nudge and
// no rounding of any kind is applied: an exactly representable duration
// keeps its exact nanosecond value.
//
// Returns nullopt for a non-finite or non-positive duration, for a duration
// below one nanosecond (motion cannot be armed with less than one
// nanosecond) and for a duration whose nanosecond value does not fit in
// int64. The bounds are checked before the cast.
inline std::optional<std::int64_t> floorDurationNs(double seconds) noexcept {
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    return std::nullopt;
  }

  // Exact decomposition: seconds = fraction * 2^exponent with 0.5 <=
  // fraction < 1. The entry check above guarantees a finite positive
  // input, so the fraction is always in that half-open interval.
  int exponent = 0;
  const double fraction = std::frexp(seconds, &exponent);
  // An exponent of 35 or more means at least 2^34 seconds, which is
  // already beyond the int64 nanosecond range. Refuse it here, before any
  // shift is formed.
  if (exponent >= 35) {
    return std::nullopt;
  }

  // The 53-bit significand, recovered exactly: the scaled fraction is an
  // integer in [2^52, 2^53) for every finite positive binary64, subnormals
  // included, so the cast below is exact.
  const double scaled_mantissa = std::ldexp(fraction, 53);
  const std::uint64_t mantissa = static_cast<std::uint64_t>(scaled_mantissa);

  // exponent <= 34, so the shift is at least 19 and never negative.
  const int shift = 53 - exponent;
  if (shift >= 128) {
    // The numerator holds at most 83 bits, so such a shift cannot yield a
    // nonzero result.
    return std::nullopt;
  }
  const detail::WideUnsigned numerator =
    static_cast<detail::WideUnsigned>(mantissa) *
    static_cast<detail::WideUnsigned>(detail::kNsPerSecond);
  const detail::WideUnsigned floored = numerator >> shift;
  if (floored == 0U ||
      floored > static_cast<detail::WideUnsigned>(detail::kInt64Max)) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(floored);
}

// Deadline derived from the original source stamp and its original TTL.
//
// `anchor` is the local CLOCK_MONOTONIC sample taken at admission,
// `source` is the original integer source stamp in ROS nanoseconds,
// `ros_now` is the current ROS time in nanoseconds and `ttl` is the original
// validity interval in seconds. All clock samples are supplied by the
// caller; this helper never reads a clock.
//
// The original TTL is consumed by the time already spent since the source
// stamp. A source stamp ahead of the local ROS clock has no elapsed age yet,
// but it may not be more than 100 ms ahead: a further future stamp is
// implausible and is refused.
//
// Returns nullopt for invalid inputs, for an expired source interval and for
// an addition that would overflow int64.
inline std::optional<std::int64_t> sourceDeadlineNs(std::int64_t anchor,
                                                      std::int64_t source,
                                                      std::int64_t ros_now,
                                                      double ttl) noexcept {
  if (anchor <= 0 || source <= 0 || ros_now < 0) {
    return std::nullopt;
  }
  const std::optional<std::int64_t> ttl_ns = floorDurationNs(ttl);
  if (!ttl_ns.has_value()) {
    return std::nullopt;
  }
  // Both operands are non-negative, so the difference is exactly
  // representable and cannot overflow.
  const std::int64_t ahead = source - ros_now;
  if (ahead > detail::kMaxSourceFutureNs) {
    return std::nullopt;
  }
  const std::int64_t age = ros_now > source ? (ros_now - source) : 0;
  const std::int64_t remaining = *ttl_ns - age;
  if (remaining <= 0) {
    return std::nullopt;
  }
  if (anchor > detail::kInt64Max - remaining) {
    return std::nullopt;
  }
  return anchor + remaining;
}

// Deadline derived from the seconds-based steady guard.
//
// `anchor` is the CLOCK_MONOTONIC nanosecond sample taken at admission and
// it must have been captured BEFORE the steady reading below was taken.
// `deadline_s` is the absolute steady-clock deadline of the admitted
// interval, `observed_steady_s` is the steady reading observed while that
// deadline was computed and `cap_s` is the configured driver cap in seconds.
//
// Conservative allowance: the double deadline is moved two ULPs toward
// -infinity and the observed reading one ULP toward +infinity, so the
// rounding of the caller's deadline arithmetic and of this subtraction can
// only shorten the granted interval. The subtraction itself is done in long
// double, the scaled result is moved one ULP toward -infinity and then
// floored, so the result never exceeds the true remaining interval. The
// result is finally bounded by the floor of the driver cap.
//
// The ordering requirement is the safety property: because the anchor is
// read before the steady reading, a preemption between the two reads can
// only move the observed reading later, which shortens the granted interval.
// It can never extend the deadline.
//
// Returns nullopt for invalid inputs, for an expired interval, for an
// interval below one nanosecond and for an addition that would overflow
// int64.
inline std::optional<std::int64_t> boundedSteadyDeadlineNs(std::int64_t anchor,
                                                            double deadline_s,
                                                            double observed_steady_s,
                                                            double cap_s) noexcept {
  if (anchor <= 0) {
    return std::nullopt;
  }
  if (!std::isfinite(deadline_s) || !std::isfinite(observed_steady_s) ||
      !std::isfinite(cap_s)) {
    return std::nullopt;
  }
  if (deadline_s <= 0.0 || observed_steady_s <= 0.0 ||
      deadline_s <= observed_steady_s) {
    return std::nullopt;
  }
  if (cap_s <= 0.0) {
    return std::nullopt;
  }

  const double negative_infinity = -std::numeric_limits<double>::infinity();
  const double positive_infinity = std::numeric_limits<double>::infinity();
  const double deadline_down =
    std::nextafter(std::nextafter(deadline_s, negative_infinity),
                   negative_infinity);
  const double observed_up =
    std::nextafter(observed_steady_s, positive_infinity);
  if (!(deadline_down > observed_up)) {
    // The interval is at most one double ULP wide: it cannot be measured.
    return std::nullopt;
  }

  const long double remaining_s =
    static_cast<long double>(deadline_down) -
    static_cast<long double>(observed_up);
  long double remaining_ns =
    remaining_s * static_cast<long double>(detail::kNsPerSecond);
  if (!std::isfinite(remaining_ns)) {
    return std::nullopt;
  }
  remaining_ns = std::nextafter(
    remaining_ns, -std::numeric_limits<long double>::infinity());
  long double floored = std::floor(remaining_ns);
  if (floored < 1.0L) {
    return std::nullopt;
  }

  const std::optional<std::int64_t> cap_ns = floorDurationNs(cap_s);
  if (!cap_ns.has_value()) {
    return std::nullopt;
  }
  if (floored > static_cast<long double>(*cap_ns)) {
    floored = static_cast<long double>(*cap_ns);
  }
  if (floored < 1.0L || floored > static_cast<long double>(detail::kInt64Max)) {
    return std::nullopt;
  }
  const std::int64_t remaining = static_cast<std::int64_t>(floored);
  if (anchor > detail::kInt64Max - remaining) {
    return std::nullopt;
  }
  return anchor + remaining;
}

}  // namespace chaos::runtime::driver_deadline
