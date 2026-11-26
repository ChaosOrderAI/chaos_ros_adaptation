#include "chaos_data/timing.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/wait.h>

#include "chaos_core/clock.hpp"

namespace chaos::data {

namespace {
constexpr std::int64_t kNanoseconds = 1'000'000'000;
constexpr std::size_t kMaxTrackingBytes = 4096;
constexpr std::size_t kMaxChronycOutput = 4096;

double positive(double value, const char * name) {
  if (!std::isfinite(value) || value <= 0) {
    throw std::invalid_argument(
      std::string(name) + " must be finite and positive");
  }
  return value;
}

bool isHex8(const std::string & value) {
  if (value.size() != 8) {
    return false;
  }
  for (char ch : value) {
    const bool ok = (ch >= '0' && ch <= '9') ||
                    (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
    if (!ok) {
      return false;
    }
  }
  return true;
}

bool isDigits(const std::string & value, int max_digits) {
  if (value.empty() || static_cast<int>(value.size()) > max_digits) {
    return false;
  }
  for (char ch : value) {
    if (ch < '0' || ch > '9') {
      return false;
    }
  }
  return true;
}

// Python float(str)-equivalent: the whole field must be consumed.
bool parseDouble(const std::string & text, double & out) {
  if (text.empty()) {
    return false;
  }
  try {
    std::size_t used = 0;
    out = std::stod(text, &used);
    return used == text.size();
  } catch (const std::exception &) {
    return false;
  }
}

std::string trim(const std::string & value) {
  const std::size_t first = value.find_first_not_of(" \t\n\r");
  if (first == std::string::npos) {
    return "";
  }
  const std::size_t last = value.find_last_not_of(" \t\n\r");
  return value.substr(first, last - first + 1);
}

bool validUtf8(const std::string & value) {
  std::size_t i = 0;
  while (i < value.size()) {
    const unsigned char lead = value[i];
    int length = 0;
    if (lead < 0x80) {
      length = 1;
    } else if ((lead >> 5) == 0x6) {
      length = 2;
    } else if ((lead >> 4) == 0xE) {
      length = 3;
    } else if ((lead >> 3) == 0x1E) {
      length = 4;
    } else {
      return false;
    }
    if (i + length > value.size()) {
      return false;
    }
    for (int j = 1; j < length; ++j) {
      if ((static_cast<unsigned char>(value[i + j]) >> 6) != 0x2) {
        return false;
      }
    }
    i += length;
  }
  return true;
}
}  // namespace

TimeSample parseTracking(const std::string & output, std::int64_t wall_ns,
                         std::int64_t monotonic_ns,
                         double max_uncertainty_s,
                         double max_reference_age_s) {
  positive(max_uncertainty_s, "max_uncertainty_s");
  positive(max_reference_age_s, "max_reference_age_s");

  TimeSample result;
  result.sample_utc_ns = wall_ns;
  result.sample_monotonic_ns = monotonic_ns;

  auto malformed = [&result]() {
    result.status = "unknown";
    result.reason = "malformed_tracking";
    return result;
  };

  if (output.size() > kMaxTrackingBytes) {
    return malformed();
  }
  // chronyc -c emits exactly one comma-separated record. Quoting is never
  // expected; a quote byte means the output is not chrony 4.x tracking CSV.
  if (output.find('"') != std::string::npos ||
      output.find('\'') != std::string::npos) {
    return malformed();
  }
  const std::string stripped = trim(output);
  if (stripped.empty()) {
    return malformed();
  }
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (true) {
    const std::size_t comma = stripped.find(',', start);
    fields.push_back(trim(stripped.substr(
      start, comma == std::string::npos ? std::string::npos : comma - start)));
    if (comma == std::string::npos) {
      break;
    }
    start = comma + 1;
  }
  if (fields.size() != 14) {
    return malformed();
  }
  if (!isHex8(fields[0])) {
    return malformed();
  }
  if (!isDigits(fields[2], 2)) {
    return malformed();
  }
  double values[10];
  for (int i = 0; i < 10; ++i) {
    if (!parseDouble(fields[3 + i], values[i]) ||
        !std::isfinite(values[i])) {
      return malformed();
    }
  }
  const double reference = values[0];
  const double offset = values[1];
  const double rms = values[3];
  const double skew = values[6];
  const double delay = values[7];
  const double dispersion = values[8];
  const double interval = values[9];
  if (std::min({reference, rms, skew, delay, dispersion, interval}) < 0) {
    return malformed();
  }
  // ROS builtin_interfaces/Time uses signed 32-bit seconds. This also bounds
  // integer conversion of malformed but finite values.
  if (reference >= 2147483648.0 || std::fabs(offset) >= 2147483648.0) {
    return malformed();
  }
  const double uncertainty = std::fabs(offset) + dispersion + delay / 2.0;
  if (!std::isfinite(uncertainty)) {
    return malformed();
  }

  result.offset_s = offset;
  result.uncertainty_s = uncertainty;
  // std::lrint uses the default rounding mode (round-half-to-even), matching
  // Python's round() for these values.
  result.reference_utc_ns =
    static_cast<std::int64_t>(std::lrint(reference * kNanoseconds));
  result.stratum = std::stoi(fields[2]);
  std::string reference_id = fields[0];
  for (char & ch : reference_id) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  result.reference_id = reference_id;
  result.reference_name = fields[1];
  result.root_delay_s = delay;
  result.root_dispersion_s = dispersion;
  result.leap_status = fields[13];

  if (*result.reference_id == "00000000" || reference == 0) {
    result.status = "unsynced";
    result.reason = "no_reference";
    return result;
  }
  if (*result.reference_id == "7F7F0101") {
    result.status = "unsynced";
    result.reason = "local_reference_without_external_sync";
    return result;
  }
  if (*result.stratum < 1 || *result.stratum > 15) {
    result.status = "unsynced";
    result.reason = "invalid_stratum";
    return result;
  }
  if (*result.leap_status != "Normal") {
    result.status = "unsynced";
    result.reason = "leap_status_not_normal";
    return result;
  }
  // A far-future reference is also suspect, including after a wall-clock
  // step.
  const double reference_age_s =
    static_cast<double>(wall_ns - *result.reference_utc_ns) / kNanoseconds;
  if (reference_age_s < -max_uncertainty_s) {
    result.status = "unsynced";
    result.reason = "reference_in_future";
    return result;
  }
  if (reference_age_s > max_reference_age_s) {
    result.status = "unsynced";
    result.reason = "reference_stale";
    return result;
  }
  if (uncertainty > max_uncertainty_s) {
    result.status = "unsynced";
    result.reason = "uncertainty_exceeded";
    return result;
  }
  result.status = "synced";
  result.reason = "within_host_time_bound";
  return result;
}

ChronycResult runChronycTracking(double timeout_s) {
  ChronycResult result;
  int stdout_pipe[2];
  if (pipe(stdout_pipe) != 0) {
    result.error = ChronycResult::Error::kUnavailable;
    return result;
  }
  const pid_t child = fork();
  if (child < 0) {
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    result.error = ChronycResult::Error::kUnavailable;
    return result;
  }
  if (child == 0) {
    dup2(stdout_pipe[1], STDOUT_FILENO);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    execlp("chronyc", "chronyc", "-c", "-n", "tracking",
           static_cast<char *>(nullptr));
    _exit(127);
  }
  close(stdout_pipe[1]);
  std::string output;
  char buffer[512];
  bool timed_out = false;
  // Absolute deadline: like subprocess.run(timeout=), the bound covers the
  // whole invocation — reading stdout, waiting for EOF, and waiting for
  // process exit after EOF — not each read. Output is capped; every path
  // closes the pipe and reaps the child, killing it when the deadline is
  // exceeded.
  const auto deadline = std::chrono::steady_clock::now() +
    std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(timeout_s));
  auto remaining_ms = [&deadline]() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - std::chrono::steady_clock::now()).count();
  };
  auto reap_blocking = [child]() {
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return status;
  };
  auto kill_and_reap = [child, &reap_blocking]() {
    kill(child, SIGKILL);
    reap_blocking();
  };
  while (true) {
    const auto remaining = remaining_ms();
    if (remaining <= 0) {
      timed_out = true;
      break;
    }
    struct pollfd descriptor{stdout_pipe[0], POLLIN, 0};
    const int ready = poll(&descriptor, 1, static_cast<int>(remaining));
    if (ready == 0) {
      timed_out = true;
      break;
    }
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      close(stdout_pipe[0]);
      kill_and_reap();
      result.error = ChronycResult::Error::kUnavailable;
      return result;
    }
    const ssize_t count = read(stdout_pipe[0], buffer, sizeof(buffer));
    if (count == 0) {
      break;
    }
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      close(stdout_pipe[0]);
      kill_and_reap();
      result.error = ChronycResult::Error::kUnavailable;
      return result;
    }
    output.append(buffer, static_cast<std::size_t>(count));
    if (output.size() > kMaxChronycOutput) {
      // Oversized output is unusable (parseTracking rejects >4096 bytes).
      timed_out = true;
      break;
    }
  }
  close(stdout_pipe[0]);
  if (timed_out) {
    kill_and_reap();
    result.error = ChronycResult::Error::kTimeout;
    return result;
  }
  // stdout reached EOF; the process may still be running. Wait for exit
  // within the remaining deadline instead of blocking in waitpid forever.
  int status = 0;
  while (true) {
    const pid_t reaped = waitpid(child, &status, WNOHANG);
    if (reaped == child) {
      break;
    }
    if (reaped < 0 && errno != EINTR) {
      result.error = ChronycResult::Error::kUnavailable;
      return result;
    }
    const auto remaining = remaining_ms();
    if (remaining <= 0) {
      kill_and_reap();
      result.error = ChronycResult::Error::kTimeout;
      return result;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(
      std::min<std::int64_t>(remaining, 5)));
  }
  if (!validUtf8(output)) {
    result.error = ChronycResult::Error::kUnavailable;
    return result;
  }
  if (WIFEXITED(status)) {
    if (WEXITSTATUS(status) == 127) {
      result.error = ChronycResult::Error::kNotFound;
      return result;
    }
    result.returncode = WEXITSTATUS(status);
  } else {
    result.returncode = -1;
  }
  result.output = std::move(output);
  return result;
}

ChronyProbe::ChronyProbe(double max_uncertainty_s,
                         double max_reference_age_s, double timeout_s,
                         ChronycRunner runner)
    : max_uncertainty_s_(positive(max_uncertainty_s, "max_uncertainty_s")),
      max_reference_age_s_(
        positive(max_reference_age_s, "max_reference_age_s")),
      timeout_s_(positive(timeout_s, "timeout_s")),
      runner_(runner ? std::move(runner) : runChronycTracking) {}

namespace {
// Shared conversion; each overload supplies its own observation timestamps.
TimeSample finishSample(const ChronycResult & completed, std::int64_t wall_ns,
                        std::int64_t monotonic_ns, double max_uncertainty_s,
                        double max_reference_age_s) {
  std::string reason;
  if (completed.error == ChronycResult::Error::kNotFound) {
    reason = "chronyc_not_found";
  } else if (completed.error == ChronycResult::Error::kTimeout) {
    reason = "chronyc_timeout";
  } else if (completed.error == ChronycResult::Error::kUnavailable) {
    reason = "chronyc_unavailable";
  } else if (completed.returncode != 0) {
    reason = "chronyc_failed";
  }
  if (!reason.empty()) {
    TimeSample sample;
    sample.status = "unknown";
    sample.reason = reason;
    sample.sample_utc_ns = wall_ns;
    sample.sample_monotonic_ns = monotonic_ns;
    return sample;
  }
  return parseTracking(completed.output, wall_ns, monotonic_ns, max_uncertainty_s,
                       max_reference_age_s);
}
}  // namespace

TimeSample ChronyProbe::sample(std::int64_t wall_ns,
                               std::int64_t monotonic_ns) const {
  return finishSample(runner_(timeout_s_), wall_ns, monotonic_ns,
                      max_uncertainty_s_, max_reference_age_s_);
}

TimeSample ChronyProbe::sample() const {
  // Run the read-only query first, then mark the completion time: a probe
  // blocked for a long subprocess wait must not be freshly accepted.
  const ChronycResult completed = runner_(timeout_s_);
  const std::int64_t wall_ns = chaos::systemNs();
  const std::int64_t monotonic_ns = chaos::monotonicNs();
  return finishSample(completed, wall_ns, monotonic_ns,
                      max_uncertainty_s_, max_reference_age_s_);
}

ClockGuard::ClockGuard(int required_samples, double jump_threshold_s,
                       double max_sample_age_s, bool strict)
    : required_samples_(required_samples),
      jump_threshold_ns_(static_cast<std::int64_t>(
        std::lrint(positive(jump_threshold_s, "jump_threshold_s") *
                   kNanoseconds))),
      max_sample_age_ns_(static_cast<std::int64_t>(
        std::lrint(positive(max_sample_age_s, "max_sample_age_s") *
                   kNanoseconds))),
      strict_(strict) {
  if (required_samples_ < 1) {
    throw std::invalid_argument("required_samples must be a positive integer");
  }
}

void ClockGuard::reset() {
  stable_samples_ = 0;
  if (has_last_monotonic_ns_) {
    invalid_before_ns_ = last_monotonic_ns_;
    has_invalid_before_ns_ = true;
  }
}

ClockDecision ClockGuard::update(const TimeSample & sample,
                                 std::int64_t wall_ns,
                                 std::int64_t monotonic_ns) {
  std::int64_t jump_ns = 0;
  std::string problem;
  if (has_last_wall_ns_) {
    const std::int64_t mono_delta = monotonic_ns - last_monotonic_ns_;
    jump_ns = wall_ns - last_wall_ns_ - mono_delta;
    if (mono_delta < 0) {
      problem = "monotonic_clock_reversed";
    } else if (std::llabs(jump_ns) > jump_threshold_ns_) {
      problem = "wall_clock_jump";
    }
  }
  last_wall_ns_ = wall_ns;
  has_last_wall_ns_ = true;
  last_monotonic_ns_ = monotonic_ns;
  has_last_monotonic_ns_ = true;
  if (!problem.empty()) {
    ++epoch_;
    stable_samples_ = 0;
    invalid_before_ns_ = monotonic_ns;
    has_invalid_before_ns_ = true;
    has_last_sample_ns_ = false;
  } else {
    const std::int64_t age_ns = monotonic_ns - sample.sample_monotonic_ns;
    if (age_ns < 0 || age_ns > max_sample_age_ns_) {
      problem = "time_sample_stale_or_future";
    } else if (has_invalid_before_ns_ &&
               sample.sample_monotonic_ns <= invalid_before_ns_) {
      problem = "time_sample_before_epoch_or_pause";
    } else if (sample.status != "synced") {
      problem = sample.reason.empty() ? "host_not_synced" : sample.reason;
    } else if (!sample.uncertainty_s.has_value() ||
               !std::isfinite(*sample.uncertainty_s) ||
               *sample.uncertainty_s < 0 || !sample.offset_s.has_value() ||
               !std::isfinite(*sample.offset_s)) {
      problem = "invalid_time_sample";
    } else if (has_last_sample_ns_ &&
               sample.sample_monotonic_ns < last_sample_ns_) {
      problem = "time_sample_out_of_order";
    } else {
      if (!has_last_sample_ns_ ||
          sample.sample_monotonic_ns != last_sample_ns_) {
        stable_samples_ =
          std::min(required_samples_, stable_samples_ + 1);
        last_sample_ns_ = sample.sample_monotonic_ns;
        has_last_sample_ns_ = true;
      }
      if (stable_samples_ < required_samples_) {
        problem = "waiting_for_stable_sync";
      }
    }
    if (!problem.empty() && problem != "waiting_for_stable_sync") {
      stable_samples_ = 0;
      // Do not let a previously seen good sample contribute again after a
      // stale/failed observation.
      invalid_before_ns_ = monotonic_ns;
      has_invalid_before_ns_ = true;
    }
  }
  ClockDecision decision;
  decision.allowed = !strict_ || problem.empty();
  decision.reason = problem.empty() ? "ready" : problem;
  decision.epoch = epoch_;
  decision.jump_ns = jump_ns;
  decision.stable_samples = stable_samples_;
  decision.time_sample = sample;
  return decision;
}

}  // namespace chaos::data
