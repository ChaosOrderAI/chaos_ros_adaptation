// Release microbenchmark for the migrated C++ domain hot paths.
//
// Workload retains the Python baseline's batch sizes and value magnitudes,
// with advancing source stamps for the per-lease replay guard: 31 x 20000 operations,
// 20000 warmup iterations, 43-character token. Percentiles are of batch
// means, not individual request latency. This is a pure domain benchmark:
// no ROS, no token generation or JSON parsing in the timed loop, and it
// must not be reported as an end-to-end ROS gain.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "chaos_sensors/health.hpp"
#include "chaos_runtime/control.hpp"

namespace {

constexpr int kRounds = 31;
constexpr int kBatch = 20000;
constexpr int kWarmup = 20000;

struct Result {
  std::vector<double> batch_means_ns;
  double median_ns = 0;
  double p95_ns = 0;
  double p99_ns = 0;
  double cpu_ns_per_op = 0;
  double checksum = 0;
};

// Prevents the optimizer from eliminating the measured work: the sink is
// volatile and the checksum is printed, so every call must produce its value.
volatile double g_sink;

std::int64_t processNs() {
  struct timespec ts {};
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
  return ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

template <typename Fn>
Result measure(Fn && fn) {
  for (int i = 0; i < kWarmup; ++i) {
    g_sink = fn(i);
  }
  std::vector<double> samples;
  samples.reserve(kRounds);
  double checksum = 0.0;
  const std::int64_t cpu0 = processNs();
  for (int round = 0; round < kRounds; ++round) {
    const auto start = std::chrono::steady_clock::now();
    double subtotal = 0.0;
    for (int j = 0; j < kBatch; ++j) {
      subtotal += fn(kWarmup + round * kBatch + j);
    }
    const auto stop = std::chrono::steady_clock::now();
    samples.push_back(
      std::chrono::duration<double, std::nano>(stop - start).count() / kBatch);
    checksum += subtotal;
  }
  const std::int64_t cpu1 = processNs();
  std::vector<double> ordered = samples;
  std::sort(ordered.begin(), ordered.end());
  Result result;
  result.batch_means_ns = std::move(samples);
  // Quantile indices use round-half-to-even, identical to Python's round(),
  // so percentiles are comparable with the baseline harness.
  result.median_ns = ordered[kRounds / 2];
  result.p95_ns = ordered[static_cast<std::size_t>(
    std::nearbyint(0.95 * (kRounds - 1)))];
  result.p99_ns = ordered[static_cast<std::size_t>(
    std::nearbyint(0.99 * (kRounds - 1)))];
  result.cpu_ns_per_op =
    static_cast<double>(cpu1 - cpu0) / (kRounds * kBatch);
  result.checksum = checksum;
  return result;
}

nlohmann::json toJson(const Result & result) {
  return nlohmann::json{
    {"iterations_per_batch", kBatch},
    {"batches", kRounds},
    {"warmup_iterations", kWarmup},
    {"batch_mean_ns_per_operation", result.batch_means_ns},
    {"median_batch_mean_ns", result.median_ns},
    {"p95_batch_mean_ns", result.p95_ns},
    {"p99_batch_mean_ns", result.p99_ns},
    {"cpu_ns_per_operation", result.cpu_ns_per_op},
    {"checksum", result.checksum},
  };
}

}  // namespace

int main() {
  const std::string token(43, 'T');
  chaos::control::ControlGuard guard(
    chaos::control::Limits(), [] { return 100.0; },
    [&token] { return token; });
  guard.lease("acquire", "benchmark", "", 30.0);
  chaos::sensors::HealthTracker tracker(
    "camera_frame", 2.0, [] { return 1'700'000'000'000'000'000LL; },
    [] { return 100.0; });

  const Result control_result = measure([&](int i) {
    const double stamp = 200.0 + i * 1e-6;
    guard.velocity(token, stamp, stamp + 0.1, 0.5, "robot/base", "robot/base",
                   {0.25 + (i % 10) * 0.01, 0.0, 0.0, 0.0, 0.0, 0.0});
    return guard.velocityExpires();
  });

  const Result health_result = measure([&](int i) {
    tracker.observe(1'700'000'000, i + 1, "camera_frame");
    return static_cast<double>(tracker.receivedCount());
  });

  const nlohmann::json output{
    {"implementation", "cpp_native"},
    {"measurement_note",
     "Percentiles of batch means, not individual request latency. Pure "
     "domain microbenchmark; no ROS, no token generation or JSON parsing in "
     "timed loop. Source stamps advance per operation under the current replay contract."},
    {"control_velocity_valid", toJson(control_result)},
    {"sensor_health_observe", toJson(health_result)},
  };
  std::cout << output.dump(2) << "\n";
  return 0;
}
