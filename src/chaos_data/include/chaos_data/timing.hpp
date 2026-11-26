// Read-only host time quality and monotonic recording admission checks.
//
// Chrony tracking describes the host clock, not a camera/IMU's hardware
// clock. The error bound assumes that chrony's upstream reference itself is
// correct. No function here adjusts the clock or changes the time service
// configuration.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace chaos::data {

struct TimeSample {
  std::string status;
  std::string reason;
  std::int64_t sample_utc_ns = 0;
  std::int64_t sample_monotonic_ns = 0;
  std::string source = "chrony";
  std::optional<double> offset_s;
  std::optional<double> uncertainty_s;
  std::optional<std::int64_t> reference_utc_ns;
  std::optional<int> stratum;
  std::optional<std::string> reference_id;
  std::optional<std::string> reference_name;
  std::optional<double> root_delay_s;
  std::optional<double> root_dispersion_s;
  std::optional<std::string> leap_status;
  std::string offset_basis = "ntp_minus_system";
};

// Parse chrony 4.x `chronyc -c tracking` output (14 CSV fields).
// System time's CSV sign is NTP minus system; the bound is
// abs(system time) + root dispersion + root delay / 2 (chronyc manual).
TimeSample parseTracking(const std::string & output, std::int64_t wall_ns,
                         std::int64_t monotonic_ns,
                         double max_uncertainty_s = 0.01,
                         double max_reference_age_s = 120.0);

// Outcome of one read-only `chronyc -c -n tracking` invocation.
struct ChronycResult {
  enum class Error { kNone, kNotFound, kTimeout, kUnavailable } error =
    Error::kNone;
  int returncode = 0;
  std::string output;
};

using ChronycRunner =
  std::function<ChronycResult(double timeout_s)>;

// Runs `chronyc -c -n tracking` read-only with a bounded wait.
ChronycResult runChronycTracking(double timeout_s);

class ChronyProbe {
public:
  explicit ChronyProbe(double max_uncertainty_s = 0.01,
                       double max_reference_age_s = 120.0,
                       double timeout_s = 1.0,
                       ChronycRunner runner = nullptr);

  // Samples with explicit observation stamps (tests) or live clocks. The
  // completion time is marked, not the command start: no probe may be
  // freshly accepted simply because a subprocess has been blocked.
  TimeSample sample(std::int64_t wall_ns, std::int64_t monotonic_ns) const;
  TimeSample sample() const;

private:
  double max_uncertainty_s_;
  double max_reference_age_s_;
  double timeout_s_;
  ChronycRunner runner_;
};

struct ClockDecision {
  bool allowed = false;
  std::string reason;
  int epoch = 0;
  std::int64_t jump_ns = 0;
  int stable_samples = 0;
  TimeSample time_sample;
};

// Gate recording on fresh, distinct samples and detect host clock steps.
// strict=false records despite poor/unknown synchronisation, with the
// failure reason preserved. Epoch changes remain visible in either mode.
// Call update with actual wall and monotonic clocks, never ROS /clock.
class ClockGuard {
public:
  explicit ClockGuard(int required_samples = 3,
                      double jump_threshold_s = 0.1,
                      double max_sample_age_s = 5.0, bool strict = true);

  // Require fresh measurements after pause; preserve the step baseline.
  void reset();
  ClockDecision update(const TimeSample & sample, std::int64_t wall_ns,
                       std::int64_t monotonic_ns);

  int epoch() const { return epoch_; }
  int stableSamples() const { return stable_samples_; }

private:
  int required_samples_;
  std::int64_t jump_threshold_ns_;
  std::int64_t max_sample_age_ns_;
  bool strict_;
  int epoch_ = 0;
  int stable_samples_ = 0;
  bool has_last_wall_ns_ = false;
  std::int64_t last_wall_ns_ = 0;
  bool has_last_monotonic_ns_ = false;
  std::int64_t last_monotonic_ns_ = 0;
  bool has_last_sample_ns_ = false;
  std::int64_t last_sample_ns_ = 0;
  bool has_invalid_before_ns_ = false;
  std::int64_t invalid_before_ns_ = 0;
};

}  // namespace chaos::data
