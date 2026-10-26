// Coordinator contract tests (ROS-free). Ported from the Python runtime
// failure contracts: lease lifecycle, velocity guard, action reservation,
// cancel/stop ordering, and observation-channel isolation.
//
// OBSERVATION CONTRACT
//   * The suite drives the real observation service
//     (chaos::runtime::ObservationService) through its public API. The
//     coordinator receives a real producer endpoint, the control events queue
//     up through the real non-blocking admissions and the four real workers
//     deliver them: no sink is frozen, no queue is inspected and no delivery
//     is faked.
//   * The fixture parks the four channel workers on a shared gate before the
//     coordinator exists. One bootstrap dummy event per channel is what enters
//     the four callbacks, so every worker is really inside its callback while
//     the control events are queued.
//   * A trace assertion is a checkpoint: the expected code plus the trace
//     sequence the coordinator had assigned at that moment. The delivered
//     event with the highest matching sequence inside the checkpoint window
//     must carry the expected code. The window has a lower bound too, so an
//     earlier event never satisfies a later assertion.
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "chaos_runtime/adapters/mock_adapter.hpp"
#include "chaos_runtime/coordinator.hpp"
#include "chaos_runtime/observation.hpp"
#include "chaos_runtime/trace.hpp"

namespace chaos::runtime {
namespace {

using std::chrono::milliseconds;

// The bounded waits of the fixture. They are deadlock guards, never hard real
// time promises.
constexpr milliseconds kGateBound{30000};
constexpr milliseconds kBootstrapTimeout{5000};
constexpr milliseconds kJoinTimeout{15000};
constexpr milliseconds kDrainTimeout{5000};

// Deterministic clock: the tests advance time explicitly. The values are
// atomic because the watchdog thread and the operation thread read them
// concurrently.
class FakeClock {
public:
  std::atomic<double> steady_s{100.0};
  std::atomic<std::int64_t> mono_ns{100'000'000'000LL};
  // The driver deadline clock (Linux CLOCK_MONOTONIC) is injected
  // independently: its epoch is never derived from the steady clock and
  // never from the ROS clock.
  std::atomic<std::int64_t> driver_mono_ns{100'000'000'000LL};
  std::atomic<std::int64_t> wall_ns{1'700'000'000'000'000'000LL};
  std::atomic<std::int64_t> ros_ns{1'700'000'000'000'000'000LL};

  void advance(double seconds) {
    steady_s.store(steady_s.load() + seconds);
    const std::int64_t delta = static_cast<std::int64_t>(seconds * 1e9);
    mono_ns.store(mono_ns.load() + delta);
    driver_mono_ns.store(driver_mono_ns.load() + delta);
    wall_ns.store(wall_ns.load() + delta);
    ros_ns.store(ros_ns.load() + delta);
  }

  // Advances the steady clock and the two monotonic test clocks while the
  // ROS clock stays frozen: the source age of a command must not change
  // while its monotonic interval is consumed.
  void advanceMonotonicOnly(double seconds) {
    steady_s.store(steady_s.load() + seconds);
    const std::int64_t delta = static_cast<std::int64_t>(seconds * 1e9);
    mono_ns.store(mono_ns.load() + delta);
    driver_mono_ns.store(driver_mono_ns.load() + delta);
  }
};

AssetConfig mockAsset() {
  AssetConfig asset;
  asset.id = "mock-ugv";
  asset.model = "mock-model";
  asset.kind = "ugv";
  asset.mode = "mock";
  asset.name_space = "/mock";
  asset.frames = {"base_link", "odom", "map"};
  asset.capabilities = {"state", "motion.velocity", "motion.stop",
                        "navigation.pose"};
  asset.plugin = "mock";
  asset.limits.linear_mps = 1.0;
  asset.limits.angular_rps = 1.0;
  asset.limits.command_timeout_s = 0.5;
  asset.limits.state_timeout_s = 2.0;
  asset.limits.lease_max_s = 30.0;
  asset.limits.action_timeout_s = 120.0;
  return asset;
}

// A bounded, one-shot rendezvous. The opener never blocks and the waiter
// never waits past its bound, so a failed expectation cannot hang the suite.
// The entry counter lets the fixture wait until every channel worker is
// really inside its callback.
class Gate {
public:
  void open() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      opened_ = true;
    }
    condition_.notify_all();
  }

  bool wait(milliseconds bound) {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, bound, [this] { return opened_; });
  }

  // Records that a waiter reached its bound instead of the explicit
  // release. The flag lives on the shared gate, so the fixture can prove
  // that the parked workers were released and never timed out.
  void recordTimeout() {
    std::lock_guard<std::mutex> lock(mutex_);
    timed_out_ = true;
  }

  bool timedOut() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return timed_out_;
  }

  // Announces that one waiter entered the gate.
  void enter() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ++entered_;
    }
    condition_.notify_all();
  }

  std::size_t entered() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entered_;
  }

  // Waits until at least `count` waiters entered the gate.
  bool waitForEntered(std::size_t count, milliseconds bound) {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, bound,
                               [this, count] { return entered_ >= count; });
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  bool opened_ = false;
  bool timed_out_ = false;
  std::size_t entered_ = 0;
};

// Parks the first invocation of one observation channel on the shared gate.
// The bootstrap entry returns without running the user callback: the dummy
// event never reaches the user code and never pollutes an assertion.
class GateGuard {
public:
  explicit GateGuard(std::shared_ptr<Gate> gate) : gate_(std::move(gate)) {}

  // Returns true when the caller is the bootstrap entry.
  bool block() {
    if (!first_.exchange(false)) {
      return false;
    }
    gate_->enter();
    // The bound is a deadlock guard, never a release: a waiter that reaches
    // it records the timeout on the shared gate, so the fixture rejects a
    // proof that the automatic timeout produced instead of the release.
    if (!gate_->wait(kGateBound)) {
      gate_->recordTimeout();
    }
    return true;
  }

private:
  std::shared_ptr<Gate> gate_;
  std::atomic<bool> first_{true};
};

// The trace recorder: it owns the mutex, the condition variable and the
// delivered snapshot. Its first invocation parks the trace worker on the
// shared gate, so the bootstrap entry is the event that blocks it.
class RecordingTraceSink : public TraceSink {
public:
  explicit RecordingTraceSink(std::shared_ptr<Gate> gate)
      : guard_(std::make_shared<GateGuard>(std::move(gate))) {}

  void publish(const TraceEvent & event) override {
    guard_->block();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      events_.push_back(event);
    }
    condition_.notify_all();
  }

  std::vector<TraceEvent> snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_.size();
  }

  // Waits until at least `count` events were recorded.
  bool waitForCount(std::size_t count, milliseconds bound) {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, bound,
                               [this, count] { return events_.size() >= count; });
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::vector<TraceEvent> events_;
  std::shared_ptr<GateGuard> guard_;
};

// The user owned observation callbacks of a test. A default callback is a
// consumed no-op: the channel is alive and its worker is parked on the
// shared gate, but the test does not observe that channel.
struct UserSinks {
  std::function<void(const CommandResultView &)> results =
    [](const CommandResultView &) {};
  std::function<void(const StateView &)> state =
    [](const StateView &) {};
  std::function<void(const std::string &, const std::string &)> log =
    [](const std::string &, const std::string &) {};
};

// Wraps a user callback so that its first invocation parks on the shared
// gate. The bootstrap entry returns without calling the user callback.
template <typename Signature, typename Callback>
std::function<Signature> makeGuardedCallback(std::shared_ptr<Gate> gate,
                                             Callback callback) {
  auto guard = std::make_shared<GateGuard>(std::move(gate));
  return [guard, callback](auto &&... args) {
    if (guard->block()) {
      return;
    }
    callback(std::forward<decltype(args)>(args)...);
  };
}

// One recorded trace expectation: the expected code and the trace sequence
// the coordinator had assigned when the expectation was recorded.
struct TraceExpectation {
  std::string command;
  std::string code;
  std::uint64_t checkpoint = 0;
  std::string test;
};

std::string currentTestName() {
  const auto * info = ::testing::UnitTest::GetInstance()->current_test_info();
  if (info == nullptr) {
    return std::string();
  }
  return std::string(info->test_suite_name()) + "." + info->name();
}

// The observation rig of one test: the real service, the real producer
// endpoint, the recording trace sink, the shared gate and the recorded trace
// expectations.
//
// The coordinator is not owned here. The owner closes it before the rig is
// finished, so the shutdown traces of the coordinator are still admitted
// while the endpoint is open.
class ObservationRig {
public:
  explicit ObservationRig(const UserSinks & sinks = UserSinks()) {
    service_ = ObservationService::process();
    if (service_->stats().stop_requested) {
      // A leftover stopping generation of an earlier failed test: retire it
      // before the registration, otherwise the registration is refused.
      service_->requestStop();
      if (!service_->joinFor(kJoinTimeout)) {
        ADD_FAILURE() << "a leftover observation generation could not be joined";
      }
      service_ = ObservationService::process();
    }
    gate_ = std::make_shared<Gate>();
    traces_ = std::make_shared<RecordingTraceSink>(gate_);
    ObservationSinks bundle;
    bundle.trace = traces_;
    bundle.results = makeGuardedCallback<void(const CommandResultView &)>(
      gate_, sinks.results);
    bundle.state = makeGuardedCallback<void(const StateView &)>(
      gate_, sinks.state);
    bundle.log = makeGuardedCallback<void(const std::string &,
                                           const std::string &)>(
      gate_, sinks.log);
    endpoint_ = service_->registerProducer(std::move(bundle));
    bootstrap();
  }

  ~ObservationRig() { finish(); }

  // Records the expected trace code together with the trace sequence the
  // coordinator had assigned at this moment.
  void expectTraceCode(const std::string & command, const std::string & code) {
    TraceExpectation expectation;
    expectation.command = command;
    expectation.code = code;
    expectation.checkpoint = endpoint_->stats().last_assigned_trace_sequence;
    expectation.test = currentTestName();
    expectations_.push_back(std::move(expectation));
  }

  // Enables the expected construction loss of one channel. The ordinary rig
  // rejects every construction drop: only a case that injects a broken
  // clock enables the channels where the loss is the expected outcome.
  void allowConstructionLoss(ObservationChannel channel) {
    expected_construction_loss[static_cast<std::size_t>(channel)] = true;
  }

  // Enables the expected sink exception of one channel. The ordinary rig
  // rejects every sink exception: only a case with a throwing sink enables
  // the channels where the exception is the expected outcome.
  void allowSinkExceptions(ObservationChannel channel) {
    expected_sink_exceptions[static_cast<std::size_t>(channel)] = true;
  }

  // The trace sequence and the drop counters of one moment. A case that
  // injects a fault captures it before the fault: every loss of the fault
  // is strictly after the captured sequence.
  struct FaultCheckpoint {
    std::uint64_t last_assigned_trace_sequence = 0;
    std::array<std::uint64_t, kObservationChannelCount> dropped_construction{};
    std::array<std::uint64_t, kObservationChannelCount> dropped_contended{};
  };

  // Captures the checkpoint of the present moment and keeps it for the
  // final verification.
  FaultCheckpoint captureCheckpoint() {
    const ObservationStats stats = endpoint_->stats();
    FaultCheckpoint checkpoint;
    checkpoint.last_assigned_trace_sequence = stats.last_assigned_trace_sequence;
    for (std::size_t index = 0; index < kObservationChannelCount; ++index) {
      checkpoint.dropped_construction[index] =
        stats.channels[index].dropped_construction;
      checkpoint.dropped_contended[index] =
        stats.channels[index].dropped_contended;
    }
    fault_checkpoint_ = checkpoint;
    return checkpoint;
  }

  // The highest trace sequence the endpoint has seen so far.
  std::uint64_t lastAssignedTraceSequence() const {
    return endpoint_->stats().last_assigned_trace_sequence;
  }

  // Releases the parked workers of a special case. The ordinary rig keeps
  // them parked until the teardown, so the queued events of a test are only
  // delivered when the endpoint is closed.
  //
  // The counters are read before the release: the state before the release
  // is strict, the state after it may show the contention of the running
  // workers.
  void releaseWorkers() {
    if (released_) {
      return;
    }
    released_ = true;
    const ObservationStats stats = endpoint_->stats();
    for (std::size_t index = 0; index < kObservationChannelCount; ++index) {
      const ChannelStats & channel = stats.channels[index];
      // The parked workers hold no queue lock: the bootstrap is the only
      // source of contention and the case lost no event at all.
      EXPECT_EQ(channel.dropped_contended, bootstrap_contended_[index]);
      EXPECT_EQ(channel.dropped_full, 0U);
      EXPECT_EQ(channel.dropped_closed, 0U);
      EXPECT_EQ(channel.dropped_oversize, 0U);
      if (!expected_construction_loss[index]) {
        EXPECT_EQ(channel.dropped_construction, 0U);
      }
      // A parked worker never reached a sink: no exception happened yet.
      EXPECT_EQ(channel.sink_exceptions, 0U);
    }
    gate_->open();
  }

  // Waits until every listed channel reported a sink exception, at most the
  // bound. The wait polls the counters of the live endpoint: it never
  // sleeps to create an order.
  bool waitForSinkExceptions(std::initializer_list<ObservationChannel> channels,
                              milliseconds bound) {
    const auto deadline = std::chrono::steady_clock::now() + bound;
    for (;;) {
      const ObservationStats stats = endpoint_->stats();
      bool ready = true;
      for (const ObservationChannel channel : channels) {
        if (stats.channels[static_cast<std::size_t>(channel)].sink_exceptions ==
            0U) {
          ready = false;
        }
      }
      if (ready) {
        return true;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  // Closes the endpoint, releases the four parked workers, stops the service
  // and joins it. The coordinator must already be closed: its last traces are
  // admitted while the endpoint is still open.
  void finish() {
    if (finished_) {
      return;
    }
    finished_ = true;
    endpoint_->close();
    gate_->open();
    service_->requestStop();
    joined_ = service_->joinFor(kJoinTimeout);
    EXPECT_TRUE(joined_) << "the observation service did not join";
    verify();
  }

  std::shared_ptr<ObservationEndpoint> endpoint() const { return endpoint_; }
  std::shared_ptr<RecordingTraceSink> traces() const { return traces_; }
  ObservationServiceStats serviceStats() const { return service_->stats(); }

private:
  // Retries ONLY the contended outcome: every other outcome is returned
  // unchanged, so a dropped event is never retried away. Returns the number
  // of contended attempts.
  std::uint64_t admit(const std::function<Admission()> & attempt,
                      Admission & outcome) {
    const auto deadline =
      std::chrono::steady_clock::now() + kBootstrapTimeout;
    std::uint64_t contended = 0;
    for (;;) {
      outcome = attempt();
      if (outcome != Admission::contended) {
        return contended;
      }
      ++contended;
      if (std::chrono::steady_clock::now() >= deadline) {
        return contended;
      }
      std::this_thread::yield();
    }
  }

  // Sends one dummy event per channel. The trace dummy carries the sequence 0
  // sentinel: it is never a real trace. The four admissions are sequential and
  // bounded, and only the contended outcome is retried.
  void bootstrap() {
    TraceEvent dummy;
    dummy.frame = "base_link";
    dummy.asset_id = "mock-ugv";
    dummy.sequence = 0;
    dummy.command = "bootstrap";
    dummy.stage = "bootstrap";
    dummy.code = "BOOTSTRAP";

    Admission outcome = Admission::admitted;
    bootstrap_contended_[static_cast<std::size_t>(ObservationChannel::trace)] =
      admit([&] { return endpoint_->trace(dummy); }, outcome);
    EXPECT_EQ(outcome, Admission::admitted);

    CommandResultView result;
    result.code = "BOOTSTRAP";
    bootstrap_contended_[static_cast<std::size_t>(ObservationChannel::results)] =
      admit([&] { return endpoint_->result(result); }, outcome);
    EXPECT_EQ(outcome, Admission::admitted);

    StateView state;
    state.asset_id = "mock-ugv";
    state.model = "mock-model";
    bootstrap_contended_[static_cast<std::size_t>(ObservationChannel::state)] =
      admit([&] { return endpoint_->state(state); }, outcome);
    EXPECT_EQ(outcome, Admission::admitted);

    bootstrap_contended_[static_cast<std::size_t>(ObservationChannel::log)] =
      admit([&] { return endpoint_->log("bootstrap", "bootstrap"); }, outcome);
    EXPECT_EQ(outcome, Admission::admitted);

    // Every channel worker is now inside its callback and waits for the gate.
    EXPECT_TRUE(gate_->waitForEntered(4, kBootstrapTimeout))
        << "the four observation workers did not enter their callbacks";
  }

  // Checks the delivered trace snapshot against the recorded checkpoints and
  // the drop counters of the four channels.
  void verify() {
    if (!joined_) {
      return;
    }
    // The parked workers were released explicitly: the automatic bound never
    // stood in for the release.
    EXPECT_FALSE(gate_->timedOut())
        << "a parked observation worker was released by the timeout bound "
           "instead of the explicit release";
    const ObservationStats stats = endpoint_->stats();
    for (std::size_t index = 0; index < kObservationChannelCount; ++index) {
      const ChannelStats & channel = stats.channels[index];
      if (released_) {
        // The released workers really run: a later admission may lose the
        // queue lock race again. The counters before the release were
        // checked strictly when the workers were released.
        EXPECT_GE(channel.dropped_contended, bootstrap_contended_[index]);
      } else {
        // The bootstrap may lose the race for the queue lock; it retries
        // only the contended outcome. The coordinator phase loses no event
        // at all.
        EXPECT_EQ(channel.dropped_contended, bootstrap_contended_[index]);
      }
      EXPECT_EQ(channel.dropped_full, 0U);
      EXPECT_EQ(channel.dropped_closed, 0U);
      EXPECT_EQ(channel.dropped_oversize, 0U);
      if (!expected_construction_loss[index]) {
        // A construction drop is a real loss of the case: only the case that
        // injected the fault enables the channel where the loss is expected.
        EXPECT_EQ(channel.dropped_construction, 0U);
      }
      if (expected_sink_exceptions[index]) {
        EXPECT_GT(channel.sink_exceptions, 0U);
      } else {
        EXPECT_EQ(channel.sink_exceptions, 0U);
      }
      // The state channel coalesces: a replaced sample is not a loss.
    }
    const std::size_t trace_index =
      static_cast<std::size_t>(ObservationChannel::trace);
    const ChannelStats & trace = stats.channels[trace_index];
    // Every admitted event was drained by the workers.
    EXPECT_EQ(stats.queued_items, 0U);
    EXPECT_EQ(stats.in_flight_items, 0U);
    // Every event the trace worker really delivered is in the snapshot.
    EXPECT_TRUE(traces_->waitForCount(
      static_cast<std::size_t>(trace.admitted), kDrainTimeout));
    const std::vector<TraceEvent> events = traces_->snapshot();
    std::uint64_t delivered = 0;
    for (const TraceEvent & event : events) {
      if (event.sequence > 0) {
        ++delivered;
      }
    }
    // Every assigned trace sequence was either delivered or counted as a
    // loss: no event is lost silently. The contended drop of the bootstrap
    // dummy carries the sequence 0 sentinel and never moved the watermark.
    const std::uint64_t lost =
      trace.dropped_contended - bootstrap_contended_[trace_index] +
      trace.dropped_full + trace.dropped_closed + trace.dropped_oversize;
    EXPECT_EQ(delivered + trace.dropped_construction + lost,
              stats.last_assigned_trace_sequence);
    // Every admitted trace event was delivered.
    EXPECT_EQ(trace.admitted, trace.delivered);
    verifyCheckpoint(events, stats);
    verifyExpectations(events);
  }

  // The losses of an injected fault start at the checkpoint: every sequence
  // assigned after it was either delivered or counted as a loss, so the
  // loss is strictly after the captured sequence.
  void verifyCheckpoint(const std::vector<TraceEvent> & events,
                        const ObservationStats & stats) {
    if (!fault_checkpoint_.has_value()) {
      return;
    }
    const FaultCheckpoint & checkpoint = *fault_checkpoint_;
    const std::size_t trace_index =
      static_cast<std::size_t>(ObservationChannel::trace);
    const ChannelStats & trace = stats.channels[trace_index];
    std::uint64_t delivered_after = 0;
    for (const TraceEvent & event : events) {
      if (event.sequence > checkpoint.last_assigned_trace_sequence) {
        ++delivered_after;
      }
    }
    const std::uint64_t assigned_after =
      stats.last_assigned_trace_sequence -
      checkpoint.last_assigned_trace_sequence;
    const std::uint64_t construction_after =
      trace.dropped_construction - checkpoint.dropped_construction[trace_index];
    const std::uint64_t contended_after =
      trace.dropped_contended - checkpoint.dropped_contended[trace_index];
    EXPECT_EQ(assigned_after,
              delivered_after + construction_after + contended_after);
  }

  // For every checkpoint: the delivered event of the command with the
  // highest sequence inside the checkpoint window must carry the expected
  // code. The window is bounded from below by the previous checkpoint of
  // the SAME command, so an earlier event never satisfies a later
  // expectation and the event of another command never discards a valid
  // trace.
  void verifyExpectations(const std::vector<TraceEvent> & events) {
    std::map<std::string, std::uint64_t> previous_checkpoint;
    for (const TraceExpectation & expectation : expectations_) {
      const auto known = previous_checkpoint.find(expectation.command);
      const std::uint64_t lower_bound =
        known == previous_checkpoint.end() ? 0U : known->second;
      const TraceEvent * match = nullptr;
      for (const TraceEvent & event : events) {
        // The bootstrap event carries the sequence 0 sentinel: it is never a
        // real trace and never satisfies an expectation.
        if (event.sequence == 0) {
          continue;
        }
        if (event.command != expectation.command) {
          continue;
        }
        if (event.sequence > expectation.checkpoint) {
          continue;
        }
        if (event.sequence <= lower_bound) {
          continue;
        }
        if (match == nullptr || event.sequence > match->sequence) {
          match = &event;
        }
      }
      EXPECT_TRUE(match != nullptr)
          << expectation.test << ": no delivered trace for command "
          << expectation.command << " in the window (" << lower_bound << ", "
          << expectation.checkpoint << "]";
      if (match != nullptr) {
        EXPECT_EQ(match->code, expectation.code)
            << expectation.test << ": command " << expectation.command
            << " at sequence " << match->sequence;
      }
      previous_checkpoint[expectation.command] = expectation.checkpoint;
    }
  }

  std::shared_ptr<ObservationService> service_;
  std::shared_ptr<Gate> gate_;
  std::shared_ptr<RecordingTraceSink> traces_;
  std::shared_ptr<ObservationEndpoint> endpoint_;
  std::vector<TraceExpectation> expectations_;
  std::array<std::uint64_t, kObservationChannelCount> bootstrap_contended_{};
  // The per case expectations of the rig. The ordinary rig rejects every
  // construction loss and every sink exception.
  std::array<bool, kObservationChannelCount> expected_construction_loss{};
  std::array<bool, kObservationChannelCount> expected_sink_exceptions{};
  std::optional<FaultCheckpoint> fault_checkpoint_;
  bool released_ = false;
  bool finished_ = false;
  bool joined_ = false;
};

// Goal context that records the terminal transition.
class FakeGoalContext : public GoalContext {
public:
  bool cancel_requested = false;
  bool active = true;
  int successes = 0;
  int cancellations = 0;
  int aborts = 0;
  OperationResult last;
  std::atomic<int> feedbacks{0};

  bool isCancelRequested() override { return cancel_requested; }
  bool isActive() override { return active; }
  void publishFeedback(double, const AdapterSnapshot &) override {
    ++feedbacks;
  }
  void succeed(const OperationResult & outcome) override {
    ++successes;
    last = outcome;
  }
  void canceled(const OperationResult & outcome) override {
    ++cancellations;
    last = outcome;
  }
  void abort(const OperationResult & outcome) override {
    ++aborts;
    last = outcome;
  }
};

// Records velocity samples issued to the adapter and can fail controlled
// stop output, without changing the mock telemetry semantics.
class RecordingAdapter : public MockAdapter {
public:
  explicit RecordingAdapter(AssetConfig asset) : MockAdapter(std::move(asset)) {}
  std::vector<std::array<double, 6>> samples;
  int controlled_stop_calls = 0;
  bool fail_controlled_stop = false;

  void setVelocity(const std::array<double, 6> & values) override {
    samples.push_back(values);
    MockAdapter::setVelocity(values);
  }
  void controlledStop() override {
    ++controlled_stop_calls;
    if (fail_controlled_stop) {
      throw std::runtime_error("controlled stop output failed");
    }
    MockAdapter::stop();
  }
};

// Records the deadline-carrying dispatch of the coordinator: the original
// integer source stamp and the immutable absolute deadline D. The adapter
// can inject a deterministic validation delay and can refuse or fail the
// dispatch, without changing the mock telemetry semantics.
class DeadlineCapturingAdapter : public MockAdapter {
public:
  struct Dispatch {
    std::array<double, 6> values{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::int64_t source_stamp_ns = 0;
    std::int64_t deadline_ns = 0;
  };

  explicit DeadlineCapturingAdapter(AssetConfig asset)
      : MockAdapter(std::move(asset)) {}

  std::vector<Dispatch> dispatches;
  int stop_calls = 0;
  // Invoked by validateVelocity: the deterministic validation delay of the
  // command. The hook runs under the control lock.
  std::function<void()> on_validate;
  bool reject_dispatch = false;
  bool throw_dispatch = false;

  void validateVelocity(const std::array<double, 6> & values) override {
    if (on_validate) {
      on_validate();
    }
    MockAdapter::validateVelocity(values);
  }

  void setVelocityUntil(const std::array<double, 6> & values,
                        std::int64_t source_stamp_ns,
                        std::int64_t expires_mono_ns) override {
    dispatches.push_back(Dispatch{values, source_stamp_ns, expires_mono_ns});
    if (throw_dispatch) {
      throw std::runtime_error("deadline dispatch failed");
    }
    if (reject_dispatch) {
      throw chaos::Rejected("STALE_COMMAND", "driver deadline has passed");
    }
    MockAdapter::setVelocity(values);
  }

  void stop() override {
    ++stop_calls;
    MockAdapter::stop();
  }

  bool hasNonzeroDispatch() const {
    for (const Dispatch & dispatch : dispatches) {
      for (double value : dispatch.values) {
        if (value != 0.0) {
          return true;
        }
      }
    }
    return false;
  }
};

struct Harness {
  std::shared_ptr<FakeClock> clock = std::make_shared<FakeClock>();
  // The real observation service, the real producer endpoint, the recording
  // trace sink and the trace expectations of this test. The rig is declared
  // before the coordinator, so the coordinator always holds a live endpoint.
  ObservationRig rig;
  // Optional hook invoked on every injected wall-clock read. It lets a
  // test move the clock at a well defined point of a command, for example
  // at the final deadline check of a stop command.
  std::shared_ptr<std::function<void()>> wall_hook;
  // Optional replacements for the injected clocks. They let a test inject a
  // clock failure or a specific sample at a well defined point of a command
  // without touching the production code paths. They are declared before
  // the coordinator, so they outlive it: the coordinator reads them while
  // it shuts down.
  std::shared_ptr<std::function<std::int64_t()>> driver_mono_override;
  std::shared_ptr<std::function<std::int64_t()>> ros_override;
  std::shared_ptr<std::function<double()>> steady_override;
  std::unique_ptr<ControlCoordinator> coordinator;

  explicit Harness(bool uav = false,
                   std::unique_ptr<Adapter> adapter = nullptr,
                   UserSinks sinks = UserSinks())
      : rig(sinks) {
    AssetConfig asset = mockAsset();
    if (uav) {
      asset.kind = "uav";
      asset.capabilities.insert("flight.takeoff");
      asset.capabilities.insert("flight.land");
    }
    Clocks clocks;
    clocks.steady_s = [this] {
      if (steady_override) {
        return (*steady_override)();
      }
      return clock->steady_s.load();
    };
    clocks.mono_ns = [this] { return clock->mono_ns.load(); };
    clocks.wall_ns = [this] {
      const std::int64_t value = clock->wall_ns.load();
      if (wall_hook) {
        (*wall_hook)();
      }
      return value;
    };
    clocks.ros_ns = [this] {
      if (ros_override) {
        return (*ros_override)();
      }
      return clock->ros_ns.load();
    };
    // The driver deadline clock is injected independently of the steady
    // clock and of the ROS clock.
    clocks.driver_mono_ns = [this] {
      if (driver_mono_override) {
        return (*driver_mono_override)();
      }
      return clock->driver_mono_ns.load();
    };
    if (!adapter) {
      adapter = std::make_unique<MockAdapter>(asset);
    }
    coordinator = std::make_unique<ControlCoordinator>(
      asset, std::move(adapter), clocks, rig.endpoint());
  }

  ~Harness() { finish(); }

  // Closes the coordinator, finalizes the adapter and then releases the
  // parked workers and joins the observation service. The order is the
  // production order: the coordinator closes, the adapter is finalized and
  // the coordinator is destroyed while the endpoint is still open, so its
  // last traces are admitted. It is idempotent: the destructor calls it
  // once and a test may call it earlier to observe the asynchronous
  // counters.
  void finish() {
    if (finished_) {
      return;
    }
    finished_ = true;
    if (coordinator) {
      coordinator->close();
      coordinator->finalize();
      coordinator.reset();
    }
    rig.finish();
  }

  // Records the expected trace code and the trace sequence the coordinator
  // had assigned at this moment.
  void expectTraceCode(const std::string & command, const std::string & code) {
    rig.expectTraceCode(command, code);
  }

private:
  bool finished_ = false;
};

LeaseRequestView leaseRequest(const std::string & operation,
                              const std::string & owner,
                              const std::string & token = "") {
  LeaseRequestView view;
  view.operation = operation;
  view.owner = owner;
  view.token = token;
  view.duration_s = 5.0;
  return view;
}

// A velocity command with an explicit source stamp, so the original integer
// sec/nsec pair is never lost in the test.
VelocityRequestView velocityRequest(const std::string & token, double ttl,
                                     std::int64_t source_ns) {
  VelocityRequestView view;
  view.stamp_sec = source_ns / 1'000'000'000;
  view.stamp_nanosec = source_ns % 1'000'000'000;
  view.frame_id = "base_link";
  view.lease_token = token;
  view.valid_for_s = ttl;
  view.velocity = {0.5, 0.0, 0.0, 0.0, 0.0, 0.0};
  return view;
}

// The ordered clock reads of the commands under test. The driver monotonic
// clock and the ROS clock are logged by name with the sample they returned,
// so the capture order of a command and the anchor it captured are
// observable from the outside.
class ClockReadLog {
public:
  void record(const char * clock, std::int64_t sample) {
    std::lock_guard<std::mutex> lock(mutex_);
    reads_.push_back(Read{clock, sample});
    condition_.notify_all();
  }

  // Waits until at least `count` reads were recorded, at most `bound`.
  bool waitFor(std::size_t count, milliseconds bound) {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, bound,
                               [this, count] { return reads_.size() >= count; });
  }

  std::string at(std::size_t index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return index < reads_.size() ? reads_[index].clock : std::string();
  }

  std::int64_t sampleAt(std::size_t index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return index < reads_.size() ? reads_[index].sample : 0;
  }

private:
  struct Read {
    std::string clock;
    std::int64_t sample = 0;
  };

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::vector<Read> reads_;
};

// Releases the gate and joins the workers on every exit path: a failed
// expectation never leaves a thread blocked on the control lock.
class WorkerGroup {
public:
  explicit WorkerGroup(std::shared_ptr<Gate> release)
      : release_(std::move(release)) {}

  ~WorkerGroup() {
    release_->open();
    joinAll();
  }

  void add(std::thread worker) { workers_.push_back(std::move(worker)); }

  void joinAll() {
    for (std::thread & worker : workers_) {
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

private:
  std::shared_ptr<Gate> release_;
  std::vector<std::thread> workers_;
};

// The mock adapter only advances when the watchdog updates it, exactly like
// the production runtime. The test drives the same 20 Hz watchdog path, so
// the operation loop, the lease guard and the stop retry are all exercised.
OperationResult runWithWatchdog(Harness & harness, OperationKind kind,
                                 const void * token, FakeGoalContext & ctx) {
  std::atomic<bool> done{false};
  OperationResult result;
  std::thread worker([&] {
    result = harness.coordinator->runOperation(kind, token, token, ctx);
    done = true;
  });
  const auto deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(20);
  while (!done.load() && std::chrono::steady_clock::now() < deadline) {
    harness.clock->advance(0.05);
    harness.coordinator->tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  worker.join();
  return result;
}

TEST(CoordinatorLease, AcquireIssuesTokenAndReleaseRevokes) {
  Harness harness;
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  EXPECT_TRUE(acquired.success);
  EXPECT_FALSE(acquired.token.empty());
  EXPECT_GT(acquired.remaining_s, 0.0);

  const LeaseResponseView released =
    harness.coordinator->lease(leaseRequest("release", "op-1", acquired.token));
  EXPECT_TRUE(released.success);
  EXPECT_TRUE(released.token.empty());

  // The lease is gone: a new command without a token is rejected.
  const StopRequestView stop;
  const StopResponseView stopped = harness.coordinator->stop(stop);
  EXPECT_FALSE(stopped.success);
  EXPECT_EQ(stopped.code, "NO_CONTROL");
}

TEST(CoordinatorLease, WrongTokenIsRejected) {
  Harness harness;
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);
  const LeaseResponseView renew =
    harness.coordinator->lease(leaseRequest("renew", "op-1", "wrong-token"));
  EXPECT_FALSE(renew.success);
  EXPECT_EQ(renew.code, "NO_CONTROL");
}

TEST(CoordinatorLease, ExpiredLeaseStopsMotion) {
  Harness harness;
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  VelocityRequestView velocity;
  velocity.frame_id = "base_link";
  velocity.lease_token = acquired.token;
  velocity.valid_for_s = 0.2;
  velocity.velocity = {0.5, 0.0, 0.0, 0.0, 0.0, 0.0};
  harness.coordinator->velocity(velocity);

  harness.clock->advance(6.0);
  harness.coordinator->tick();
  // The lease expired: the motion must be inhibited.
  StopRequestView expired_stop;
  expired_stop.lease_token = acquired.token;
  const StopResponseView stopped = harness.coordinator->stop(expired_stop);
  EXPECT_FALSE(stopped.success);
}

TEST(CoordinatorVelocity, RejectsWrongFrameAndNonFiniteValues) {
  auto adapter = std::make_unique<RecordingAdapter>(mockAsset());
  RecordingAdapter *recording = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  // The source stamp and the TTL are valid, so the frame check and the
  // component check are the ones that reject the two commands.
  const std::int64_t stamp_ns = harness.clock->ros_ns.load();
  VelocityRequestView bad_frame;
  bad_frame.stamp_sec = stamp_ns / 1'000'000'000;
  bad_frame.stamp_nanosec = stamp_ns % 1'000'000'000;
  bad_frame.frame_id = "wrong_frame";
  bad_frame.lease_token = acquired.token;
  bad_frame.valid_for_s = 0.5;
  bad_frame.velocity = {0.1, 0.0, 0.0, 0.0, 0.0, 0.0};
  harness.coordinator->velocity(bad_frame);
  harness.expectTraceCode("motion.velocity", "INVALID_FRAME");

  VelocityRequestView nan_velocity;
  nan_velocity.stamp_sec = stamp_ns / 1'000'000'000;
  nan_velocity.stamp_nanosec = stamp_ns % 1'000'000'000;
  nan_velocity.frame_id = "base_link";
  nan_velocity.lease_token = acquired.token;
  nan_velocity.valid_for_s = 0.5;
  nan_velocity.velocity = {std::nan(""), 0.0, 0.0, 0.0, 0.0, 0.0};
  harness.coordinator->velocity(nan_velocity);
  harness.expectTraceCode("motion.velocity", "INVALID_ARGUMENT");

  // Neither invalid command armed a velocity sample.
  harness.clock->advance(0.1);
  harness.coordinator->tick();
  EXPECT_TRUE(recording->samples.empty());
}

TEST(CoordinatorAction, GoalReachesSuccessTerminal) {
  Harness harness;
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  GoalRequestView goal;
  goal.lease_token = acquired.token;
  goal.target_frame = "map";
  goal.position = {0.5, 0.0, 0.0};
  int token = 0;
  EXPECT_EQ(harness.coordinator->goal(OperationKind::kNavigatePose, goal,
                                      &token),
            ControlCoordinator::GoalDecision::kAccept);
  // The action server delivers the accepted handle before execution.
  harness.coordinator->accepted(&token, &token);
  FakeGoalContext ctx;
  const OperationResult result =
    runWithWatchdog(harness, OperationKind::kNavigatePose, &token, ctx);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(ctx.successes, 1);
  EXPECT_EQ(ctx.aborts, 0);
}

TEST(CoordinatorAction, CancelProducesCanceledTerminal) {
  Harness harness;
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  GoalRequestView goal;
  goal.lease_token = acquired.token;
  goal.target_frame = "map";
  goal.position = {50.0, 0.0, 0.0};  // far away: the goal keeps running
  int token = 0;
  ASSERT_EQ(harness.coordinator->goal(OperationKind::kNavigatePose, goal,
                                      &token),
            ControlCoordinator::GoalDecision::kAccept);
  harness.coordinator->accepted(&token, &token);
  FakeGoalContext ctx;
  ctx.cancel_requested = true;
  const OperationResult result =
    runWithWatchdog(harness, OperationKind::kNavigatePose, &token, ctx);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.code, "CANCELED");
  EXPECT_EQ(ctx.cancellations, 1);
  EXPECT_EQ(ctx.aborts, 0);
}

TEST(CoordinatorAction, ExplicitStopInterruptsRunningGoal) {
  Harness harness;
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  GoalRequestView goal;
  goal.lease_token = acquired.token;
  goal.target_frame = "map";
  goal.position = {50.0, 0.0, 0.0};
  int token = 0;
  ASSERT_EQ(harness.coordinator->goal(OperationKind::kNavigatePose, goal,
                                      &token),
            ControlCoordinator::GoalDecision::kAccept);
  harness.coordinator->accepted(&token, &token);

  // An explicit stop arrives while the goal is executing.
  StopRequestView stop;
  stop.lease_token = acquired.token;
  stop.request_id = "stop-1";
  const StopResponseView stopped = harness.coordinator->stop(stop);
  EXPECT_TRUE(stopped.success);

  FakeGoalContext ctx;
  const OperationResult result =
    runWithWatchdog(harness, OperationKind::kNavigatePose, &token, ctx);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.code, "STOPPED");
  EXPECT_EQ(ctx.aborts, 1);
}

TEST(CoordinatorAction, LostReservationStillPublishesTerminalResult) {
  Harness harness;
  FakeGoalContext ctx;
  // No reservation exists: the client must still receive a typed result.
  const OperationResult result = harness.coordinator->runOperation(
    OperationKind::kNavigatePose, nullptr, nullptr, ctx);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.code, "INTERNAL_ERROR");
  EXPECT_EQ(ctx.aborts, 1);
}

TEST(CoordinatorAction, RejectedGoalDoesNotLeakTerminal) {
  Harness harness;
  GoalRequestView goal;
  goal.target_frame = "map";
  goal.position = {1.0, 0.0, 0.0};
  int token = 0;
  // No lease: the goal is rejected.
  EXPECT_EQ(harness.coordinator->goal(OperationKind::kNavigatePose, goal,
                                      &token),
            ControlCoordinator::GoalDecision::kReject);
}

TEST(CoordinatorObservation, FailingSinksDoNotBreakTheWatchdog) {
  // The throwing channels are owned by the test: the counters are heap state,
  // so the worker threads never touch a stack local of the test body.
  auto result_calls = std::make_shared<std::atomic<int>>(0);
  auto state_calls = std::make_shared<std::atomic<int>>(0);
  auto log_calls = std::make_shared<std::atomic<int>>(0);
  UserSinks sinks;
  sinks.results = [result_calls](const CommandResultView &) {
    result_calls->fetch_add(1);
    throw std::runtime_error("result channel down");
  };
  sinks.state = [state_calls](const StateView &) {
    state_calls->fetch_add(1);
    throw std::runtime_error("state channel down");
  };
  sinks.log = [log_calls](const std::string &, const std::string &) {
    log_calls->fetch_add(1);
    throw std::runtime_error("log channel down");
  };
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter), sinks);
  // The throwing sinks are the expected outcome of this case: the ordinary
  // rig rejects every sink exception.
  harness.rig.allowSinkExceptions(ObservationChannel::results);
  harness.rig.allowSinkExceptions(ObservationChannel::state);
  harness.rig.allowSinkExceptions(ObservationChannel::log);

  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  EXPECT_TRUE(acquired.success);
  // The watchdog must keep running with every observation channel down.
  for (int i = 0; i < 5; ++i) {
    harness.clock->advance(0.05);
    harness.coordinator->tick();
  }

  // A velocity command publishes a result: the throwing result sink has to
  // receive it and the decision has to stay unchanged.
  const std::int64_t stamp_ns = harness.clock->ros_ns.load();
  VelocityRequestView velocity;
  velocity.stamp_sec = stamp_ns / 1'000'000'000;
  velocity.stamp_nanosec = stamp_ns % 1'000'000'000;
  velocity.frame_id = "base_link";
  velocity.lease_token = acquired.token;
  velocity.valid_for_s = 0.5;
  velocity.velocity = {0.5, 0.0, 0.0, 0.0, 0.0, 0.0};
  harness.coordinator->velocity(velocity);
  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");
  ASSERT_EQ(capturing->dispatches.size(), 1u);

  // A rejected goal publishes a log line: the throwing log sink has to
  // receive it and the goal has to stay rejected.
  GoalRequestView goal;
  goal.lease_token = "not-the-lease-token";
  goal.target_frame = "map";
  goal.position = {0.5, 0.0, 0.0};
  int goal_token = 0;
  EXPECT_EQ(harness.coordinator->goal(OperationKind::kNavigatePose, goal,
                                      &goal_token),
            ControlCoordinator::GoalDecision::kReject);

  // The queued result, state and log line are released now: the throwing
  // sinks are reached while the coordinator is still alive.
  harness.rig.releaseWorkers();

  // The exceptions are counted on the channels that really threw.
  EXPECT_TRUE(harness.rig.waitForSinkExceptions(
                {ObservationChannel::results, ObservationChannel::state,
                 ObservationChannel::log},
                kDrainTimeout))
      << "the throwing sinks were not reached while the coordinator was alive";
  EXPECT_GT(result_calls->load(), 0);
  EXPECT_GT(state_calls->load(), 0);
  EXPECT_GT(log_calls->load(), 0);

  // The control path continues after the exceptions: the velocity sample
  // expires and the watchdog stops the output. The stop is the proof of the
  // progress: it is not an observation channel, so it never depends on a
  // delivery that the running workers may contend for.
  harness.clock->advance(1.0);
  harness.coordinator->tick();
  EXPECT_EQ(capturing->stop_calls, 1);
  for (double value : capturing->snapshot().velocity) {
    EXPECT_DOUBLE_EQ(value, 0.0);
  }
  // The observation failures are not a control fault.
  EXPECT_TRUE(harness.coordinator->fault().empty())
      << harness.coordinator->fault();

  // The channels are drained and joined: the asynchronous counters are only
  // meaningful after the join.
  harness.finish();
  EXPECT_GT(result_calls->load(), 0);
  EXPECT_GT(state_calls->load(), 0);
  EXPECT_GT(log_calls->load(), 0);
  const ObservationServiceStats stats = harness.rig.serviceStats();
  EXPECT_GT(stats.channels[static_cast<std::size_t>(ObservationChannel::results)]
              .sink_exceptions,
            0U);
  EXPECT_GT(stats.channels[static_cast<std::size_t>(ObservationChannel::state)]
              .sink_exceptions,
            0U);
  EXPECT_GT(stats.channels[static_cast<std::size_t>(ObservationChannel::log)]
              .sink_exceptions,
            0U);
}

TEST(CoordinatorObservation, FaultIsReturnedAsAValueSnapshot) {
  Harness harness;
  EXPECT_TRUE(harness.coordinator->fault().empty());
}

// An accepted stop revokes the current lease before halt output: commands
// replayed afterwards with the old token are rejected and no nonzero
// velocity reaches the adapter, while a fresh acquire starts a new session.
TEST(CoordinatorStop, StopRevokesLeaseAndReplayedCommandsAreRejected) {
  auto adapter = std::make_unique<RecordingAdapter>(mockAsset());
  RecordingAdapter *recording = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  const std::int64_t t0 = harness.clock->ros_ns.load();
  VelocityRequestView velocity;
  velocity.stamp_sec = t0 / 1'000'000'000;
  velocity.stamp_nanosec = t0 % 1'000'000'000;
  velocity.frame_id = "base_link";
  velocity.lease_token = acquired.token;
  velocity.valid_for_s = 0.5;
  velocity.velocity = {0.5, 0.0, 0.0, 0.0, 0.0, 0.0};
  harness.coordinator->velocity(velocity);
  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");
  ASSERT_EQ(recording->samples.size(), 1u);

  // t=50 ms: accepted stop.
  harness.clock->advance(0.05);
  StopRequestView stop;
  stop.lease_token = acquired.token;
  stop.request_id = "stop-1";
  const StopResponseView stopped = harness.coordinator->stop(stop);
  EXPECT_TRUE(stopped.success) << stopped.message;
  EXPECT_EQ(stopped.code, "OK");

  // t=60 ms: replayed velocity with the revoked token is rejected with
  // NO_CONTROL and never reaches the adapter.
  harness.clock->advance(0.01);
  velocity.stamp_sec = harness.clock->ros_ns.load() / 1'000'000'000;
  velocity.stamp_nanosec = harness.clock->ros_ns.load() % 1'000'000'000;
  harness.coordinator->velocity(velocity);
  harness.expectTraceCode("motion.velocity", "NO_CONTROL");
  EXPECT_EQ(recording->samples.size(), 1u);
  const AdapterSnapshot snapshot = recording->snapshot();
  for (double value : snapshot.velocity) {
    EXPECT_DOUBLE_EQ(value, 0.0);
  }

  // Renew and action with the revoked token are rejected too.
  const LeaseResponseView renew =
    harness.coordinator->lease(leaseRequest("renew", "op-1", acquired.token));
  EXPECT_FALSE(renew.success);
  EXPECT_EQ(renew.code, "NO_CONTROL");
  GoalRequestView goal;
  goal.lease_token = acquired.token;
  goal.target_frame = "map";
  goal.position = {0.5, 0.0, 0.0};
  int goal_token = 0;
  EXPECT_EQ(harness.coordinator->goal(OperationKind::kNavigatePose, goal,
                                      &goal_token),
            ControlCoordinator::GoalDecision::kReject);

  // A new acquire restores control.
  const LeaseResponseView reacquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-2"));
  ASSERT_TRUE(reacquired.success);
  velocity.lease_token = reacquired.token;
  velocity.stamp_sec = harness.clock->ros_ns.load() / 1'000'000'000;
  velocity.stamp_nanosec = harness.clock->ros_ns.load() % 1'000'000'000;
  harness.coordinator->velocity(velocity);
  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");
  EXPECT_EQ(recording->samples.size(), 2u);
}

// The accepted-stop point must not depend on the lease still being alive.
// A lease that expires between the token check and the revocation must
// still produce halt output and leave the token invalid: the revocation
// never re-reads the clock and never re-validates the lease.
TEST(CoordinatorStop, StopRevokesEvenWhenTheLeaseExpiresAtTheFinalCheck) {
  auto adapter = std::make_unique<RecordingAdapter>(mockAsset());
  RecordingAdapter *recording = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  // A velocity sample is accepted, so the stop has to zero the output.
  const std::int64_t t0 = harness.clock->ros_ns.load();
  VelocityRequestView velocity;
  velocity.stamp_sec = t0 / 1'000'000'000;
  velocity.stamp_nanosec = t0 % 1'000'000'000;
  velocity.frame_id = "base_link";
  velocity.lease_token = acquired.token;
  velocity.valid_for_s = 0.5;
  velocity.velocity = {0.5, 0.0, 0.0, 0.0, 0.0, 0.0};
  harness.coordinator->velocity(velocity);
  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");
  ASSERT_EQ(recording->samples.size(), 1u);

  // The clock jumps to the lease boundary at the final deadline check of
  // the stop command (read 1: deadline capture, read 2: first check, read
  // 3: final check). The token was already validated at that point, so the
  // lease expires in the window between the token check and the
  // revocation.
  auto wall_reads = std::make_shared<std::atomic<int>>(0);
  auto tripped = std::make_shared<std::atomic<bool>>(false);
  harness.wall_hook = std::make_shared<std::function<void()>>([&] {
    if (wall_reads->fetch_add(1) + 1 >= 3 && !tripped->exchange(true)) {
      harness.clock->advance(5.0);  // lease boundary
    }
  });

  StopRequestView stop;
  stop.lease_token = acquired.token;
  stop.request_id = "stop-race";
  const StopResponseView stopped = harness.coordinator->stop(stop);
  // The stop is accepted: the halt output is issued even though the lease
  // expired between the token check and the revocation.
  EXPECT_TRUE(stopped.success) << stopped.message;
  EXPECT_EQ(stopped.code, "OK");
  EXPECT_EQ(recording->controlled_stop_calls, 1);

  // Zero output: the accepted velocity never survives the stop.
  const AdapterSnapshot snapshot = recording->snapshot();
  for (double value : snapshot.velocity) {
    EXPECT_DOUBLE_EQ(value, 0.0);
  }
  EXPECT_EQ(recording->samples.size(), 1u);

  // The lease is invalid regardless of the elapsed lease time.
  const LeaseResponseView renew =
    harness.coordinator->lease(leaseRequest("renew", "op-1", acquired.token));
  EXPECT_FALSE(renew.success);
  EXPECT_EQ(renew.code, "NO_CONTROL");

  // A replayed velocity with the revoked token is rejected and never
  // reaches the adapter.
  velocity.stamp_sec = harness.clock->ros_ns.load() / 1'000'000'000;
  velocity.stamp_nanosec = harness.clock->ros_ns.load() % 1'000'000'000;
  harness.coordinator->velocity(velocity);
  harness.expectTraceCode("motion.velocity", "NO_CONTROL");
  EXPECT_EQ(recording->samples.size(), 1u);
}

// STOP_FAILED still leaves the token invalid: the lease is revoked before
// halt output, while the stop latch keeps retrying in the background.
TEST(CoordinatorStop, StopFailedStillRevokesAndRetriesContinue) {
  auto adapter = std::make_unique<RecordingAdapter>(mockAsset());
  adapter->fail_controlled_stop = true;
  RecordingAdapter *recording = adapter.get();
  Harness harness(false, std::move(adapter));

  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  StopRequestView stop;
  stop.lease_token = acquired.token;
  stop.request_id = "stop-fails";
  const StopResponseView stopped = harness.coordinator->stop(stop);
  EXPECT_FALSE(stopped.success);
  EXPECT_EQ(stopped.code, "STOP_FAILED");
  EXPECT_EQ(recording->controlled_stop_calls, 1);

  // The token is invalid even though stop output failed.
  const LeaseResponseView renew =
    harness.coordinator->lease(leaseRequest("renew", "op-1", acquired.token));
  EXPECT_FALSE(renew.success);
  EXPECT_EQ(renew.code, "NO_CONTROL");

  // The latched stop keeps retrying on the watchdog backoff.
  harness.clock->advance(0.15);
  harness.coordinator->tick();
  EXPECT_EQ(recording->controlled_stop_calls, 2);
}

// Only a fully validated stop revokes: an invalid token or an expired
// deadline leaves the valid lease untouched.
TEST(CoordinatorStop, InvalidTokenOrExpiredStopPreservesValidLease) {
  Harness harness;
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  // Wrong token: rejected without halting or revoking.
  StopRequestView wrong_token;
  wrong_token.lease_token = "not-the-lease-token";
  wrong_token.request_id = "stop-wrong-token";
  const StopResponseView wrong = harness.coordinator->stop(wrong_token);
  EXPECT_FALSE(wrong.success);
  EXPECT_EQ(wrong.code, "NO_CONTROL");
  harness.expectTraceCode("motion.stop", "NO_CONTROL");

  // Expired deadline: rejected as stale, lease still intact.
  StopRequestView expired;
  expired.lease_token = acquired.token;
  expired.request_id = "stop-expired";
  expired.expires_at = control::RosTime{1'699'999'999LL, 0};
  const StopResponseView stale = harness.coordinator->stop(expired);
  EXPECT_FALSE(stale.success);
  EXPECT_EQ(stale.code, "STALE_COMMAND");

  // The original token still renews and drives motion.
  const LeaseResponseView renew =
    harness.coordinator->lease(leaseRequest("renew", "op-1", acquired.token));
  EXPECT_TRUE(renew.success) << renew.message;
  const std::int64_t now = harness.clock->ros_ns.load();
  VelocityRequestView velocity;
  velocity.stamp_sec = now / 1'000'000'000;
  velocity.stamp_nanosec = now % 1'000'000'000;
  velocity.frame_id = "base_link";
  velocity.lease_token = acquired.token;
  velocity.valid_for_s = 0.5;
  velocity.velocity = {0.5, 0.0, 0.0, 0.0, 0.0, 0.0};
  harness.coordinator->velocity(velocity);
  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");
}

// Source stamps must strictly increase per lease: equal and backward stamps
// are stale even when fresh, and renew never rewinds the sequence.
TEST(CoordinatorVelocity, SourceStampMustStrictlyIncreasePerLease) {
  Harness harness;
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  const std::int64_t base = harness.clock->ros_ns.load();
  ASSERT_EQ(base % 1'000'000'000, 0);
  VelocityRequestView velocity;
  velocity.frame_id = "base_link";
  velocity.lease_token = acquired.token;
  velocity.valid_for_s = 0.5;
  velocity.velocity = {0.5, 0.0, 0.0, 0.0, 0.0, 0.0};

  velocity.stamp_sec = base / 1'000'000'000;
  velocity.stamp_nanosec = 0;
  harness.coordinator->velocity(velocity);
  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");

  // Equal stamp: a replay, rejected via the watermark even though fresh.
  harness.coordinator->velocity(velocity);
  harness.expectTraceCode("motion.velocity", "STALE_COMMAND");

  // One nanosecond forward from the accepted stamp is ordered and accepted.
  velocity.stamp_nanosec = 1;
  harness.coordinator->velocity(velocity);
  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");

  // One nanosecond backward is stale via the watermark as well.
  velocity.stamp_nanosec = 0;
  harness.coordinator->velocity(velocity);
  harness.expectTraceCode("motion.velocity", "STALE_COMMAND");

  // Renew extends the lease but keeps the watermark: the last accepted
  // stamp is now a replay.
  const LeaseResponseView renew =
    harness.coordinator->lease(leaseRequest("renew", "op-1", acquired.token));
  ASSERT_TRUE(renew.success);
  velocity.stamp_nanosec = 1;
  harness.coordinator->velocity(velocity);
  harness.expectTraceCode("motion.velocity", "STALE_COMMAND");
}

TEST(CoordinatorDeadline, CaptureReadsMonotonicBeforeWall) {
  // The deadline must be captured from the monotonic reading taken first:
  // preemption between the two reads may shorten the interval but must
  // never extend it.
  auto order = std::make_shared<std::vector<std::string>>();
  auto mono_ns = std::make_shared<std::int64_t>(0);
  auto wall_ns = std::make_shared<std::int64_t>(0);
  Clocks clocks;
  clocks.steady_s = [] { return 0.0; };
  clocks.mono_ns = [order, mono_ns] {
    order->push_back("mono");
    return *mono_ns;
  };
  clocks.wall_ns = [order, wall_ns] {
    order->push_back("wall");
    return *wall_ns;
  };
  AssetConfig asset = mockAsset();
  // The coordinator owns an observation endpoint of the real service: the rig
  // registers the producer, parks the four workers and owns the bounded
  // teardown of this local coordinator.
  ObservationRig rig;
  ControlCoordinator coordinator(asset, std::make_unique<MockAdapter>(asset),
                                 clocks, rig.endpoint());
  control::RosTime stamp;
  stamp.sec = 1700;
  const control::CommandDeadline deadline =
    control::CommandDeadline::capture(stamp, *wall_ns, *mono_ns);
  EXPECT_TRUE(deadline.valid());
}

// --- Deadline coverage ----------------------------------------------------

// The driver monotonic clock is read first and the ROS clock second, and
// both are read before the control lock is taken. The adapter check runs
// under the lock, so it marks the point where the lock is already held.
//
// The first command blocks inside that validation, so it holds the control
// lock while the second command runs. The second command takes its clock
// samples before the lock and keeps them: the ROS clock jump that happens
// while the first command is blocked never refreshes the source age of the
// second one.
TEST(CoordinatorDeadline, VelocityCapturesDriverMonoThenRosBeforeTheLock) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  // The first command is one nanosecond older than the base sample, the
  // second one carries the base sample itself: the watermark of the lease
  // admits both of them.
  const std::int64_t base_ns = harness.clock->ros_ns.load();
  const std::int64_t first_source_ns = base_ns - 1;

  auto first_entered = std::make_shared<Gate>();
  auto release = std::make_shared<Gate>();
  auto reads = std::make_shared<ClockReadLog>();
  auto second_started = std::make_shared<std::atomic<bool>>(false);
  auto validations = std::make_shared<std::atomic<int>>(0);
  auto second_completed = std::make_shared<std::atomic<bool>>(false);

  // The clock overrides are installed before the workers start. Only the
  // reads of the second worker are logged: the first worker is blocked in
  // the validation and reads no clock until it is released.
  harness.driver_mono_override =
    std::make_shared<std::function<std::int64_t()>>(
      [reads, second_started, &harness] {
        const std::int64_t sample = harness.clock->driver_mono_ns.load();
        if (second_started->load()) {
          reads->record("driver_mono", sample);
        }
        return sample;
      });
  harness.ros_override =
    std::make_shared<std::function<std::int64_t()>>(
      [reads, second_started, &harness] {
        const std::int64_t sample = harness.clock->ros_ns.load();
        if (second_started->load()) {
          reads->record("ros", sample);
        }
        return sample;
      });

  // Only the first validation blocks: it runs under the control lock, so
  // the second worker waits for that lock.
  capturing->on_validate = [first_entered, release, validations] {
    if (validations->fetch_add(1) == 0) {
      first_entered->open();
      EXPECT_TRUE(release->wait(milliseconds(2000)))
        << "holder gate timed out before explicit release";
    }
  };

  // The worker group is declared last: on a failed expectation it releases
  // the blocked worker and joins both workers before the captured locals
  // are destroyed.
  WorkerGroup workers(release);

  workers.add(std::thread([&] {
    harness.coordinator->velocity(
      velocityRequest(acquired.token, 0.5, first_source_ns));
  }));
  ASSERT_TRUE(first_entered->wait(milliseconds(2000)));

  // The second worker starts while the first one holds the control lock.
  second_started->store(true);
  workers.add(std::thread([&] {
    harness.coordinator->velocity(
      velocityRequest(acquired.token, 0.1, base_ns));
    second_completed->store(true);
  }));

  // The second worker took both samples before it waited for the lock.
  ASSERT_TRUE(reads->waitFor(2, milliseconds(2000)));
  EXPECT_EQ(reads->at(0), "driver_mono");
  EXPECT_EQ(reads->at(1), "ros");
  // It cannot be complete: the first worker still holds the control lock.
  EXPECT_FALSE(second_completed->load());

  // The monotonic clocks advance and the ROS clock jumps while the first
  // worker is still blocked.
  harness.clock->advanceMonotonicOnly(0.08);
  harness.clock->ros_ns.store(harness.clock->ros_ns.load() + 300'000'000LL);

  release->open();
  workers.joinAll();

  // Both commands were dispatched: the second one was admitted with the
  // ROS sample it captured before the lock, the later ROS jump did not age
  // it out.
  ASSERT_EQ(capturing->dispatches.size(), 2u);
  EXPECT_EQ(capturing->dispatches[0].source_stamp_ns, first_source_ns);
  EXPECT_EQ(capturing->dispatches[1].source_stamp_ns, base_ns);
  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");

  // The dispatched deadline is anchored at the sample the second worker
  // captured before the lock: the 80 ms it waited for the control lock are
  // consumed from the original 100 ms interval, they never extend it.
  const DeadlineCapturingAdapter::Dispatch & second = capturing->dispatches[1];
  EXPECT_EQ(second.deadline_ns, reads->sampleAt(0) + 100'000'000LL);
  EXPECT_EQ(second.deadline_ns - harness.clock->driver_mono_ns.load(),
            20'000'000LL);
}

// The source age is measured against the ROS sample captured before the
// lock. A refreshed sample would age the command out and reject it.
TEST(CoordinatorDeadline, VelocityGuardUsesTheCapturedRosSample) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  const std::int64_t source_ns = harness.clock->ros_ns.load();
  // The ROS clock jumps past the TTL while the command waits for the lock.
  capturing->on_validate = [&harness] {
    harness.clock->ros_ns.store(harness.clock->ros_ns.load() + 300'000'000LL);
  };

  harness.coordinator->velocity(velocityRequest(acquired.token, 0.1, source_ns));

  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");
  ASSERT_EQ(capturing->dispatches.size(), 1u);
  EXPECT_EQ(capturing->dispatches.front().source_stamp_ns, source_ns);
}

// The dispatched deadline is the ORIGINAL interval, anchored at the sample
// taken before the lock. The validation delay consumes the interval, it
// never extends it.
TEST(CoordinatorDeadline, OriginalTtlBoundsTheDispatchedDeadline) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  // The validation consumes 80 ms of the 100 ms interval. The ROS clock
  // stays frozen, so the source age does not change.
  capturing->on_validate = [&harness] {
    harness.clock->advanceMonotonicOnly(0.08);
  };
  const std::int64_t origin_anchor = harness.clock->driver_mono_ns.load();
  const std::int64_t source_ns = harness.clock->ros_ns.load();

  harness.coordinator->velocity(velocityRequest(acquired.token, 0.1, source_ns));

  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");
  ASSERT_EQ(capturing->dispatches.size(), 1u);
  const DeadlineCapturingAdapter::Dispatch & dispatch =
    capturing->dispatches.front();
  EXPECT_EQ(dispatch.source_stamp_ns, source_ns);
  const std::int64_t granted = dispatch.deadline_ns - origin_anchor;
  EXPECT_LE(granted, 100'000'000LL);
  EXPECT_EQ(granted, 100'000'000LL);
}

// A validation delay that consumes the whole interval produces no new
// nonzero output, and the output that is still armed is stopped.
TEST(CoordinatorDeadline, ElapsedOriginalTtlEmitsNoMotionAndStopsTheOutput) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  // The first command arms the output.
  harness.coordinator->velocity(
    velocityRequest(acquired.token, 0.5, harness.clock->ros_ns.load()));
  ASSERT_EQ(capturing->dispatches.size(), 1u);
  ASSERT_TRUE(capturing->hasNonzeroDispatch());
  ASSERT_DOUBLE_EQ(capturing->snapshot().velocity[0], 0.5);

  // The validation now consumes 120 ms of a 100 ms interval.
  capturing->on_validate = [&harness] {
    harness.clock->advanceMonotonicOnly(0.12);
  };
  // A fresh source stamp: the watermark of the first command stands.
  harness.clock->ros_ns.store(harness.clock->ros_ns.load() + 10'000'000LL);
  const std::int64_t source_ns = harness.clock->ros_ns.load();

  harness.coordinator->velocity(velocityRequest(acquired.token, 0.1, source_ns));

  // No new nonzero output: the original interval is already consumed.
  EXPECT_EQ(capturing->dispatches.size(), 1u);
  harness.expectTraceCode("motion.velocity", "DRIVER_ERROR");
  // The output that was still armed is stopped.
  EXPECT_EQ(capturing->stop_calls, 1);
  for (double value : capturing->snapshot().velocity) {
    EXPECT_DOUBLE_EQ(value, 0.0);
  }
}

// A short remaining lease dominates a longer TTL, and a later renewal never
// mutates the deadline that was already recorded.
TEST(CoordinatorDeadline, RemainingLeaseDominatesLongerTtl) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter));
  LeaseRequestView short_lease = leaseRequest("acquire", "op-1");
  short_lease.duration_s = 0.04;
  const LeaseResponseView acquired = harness.coordinator->lease(short_lease);
  ASSERT_TRUE(acquired.success);

  const std::int64_t origin_anchor = harness.clock->driver_mono_ns.load();
  harness.coordinator->velocity(
    velocityRequest(acquired.token, 0.5, harness.clock->ros_ns.load()));

  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");
  ASSERT_EQ(capturing->dispatches.size(), 1u);
  const std::int64_t granted =
    capturing->dispatches.front().deadline_ns - origin_anchor;
  // The remaining lease (40 ms) dominates the 500 ms TTL, and the
  // conservative flooring never rounds it up.
  EXPECT_GT(granted, 0LL);
  EXPECT_LE(granted, 40'000'000LL);
  EXPECT_LT(granted, 500'000'000LL);

  // The renewal extends the lease, never the deadline of an issued command.
  const std::int64_t recorded = capturing->dispatches.front().deadline_ns;
  const LeaseResponseView renewed =
    harness.coordinator->lease(leaseRequest("renew", "op-1", acquired.token));
  ASSERT_TRUE(renewed.success) << renewed.message;
  EXPECT_EQ(capturing->dispatches.front().deadline_ns, recorded);
  EXPECT_EQ(capturing->dispatches.size(), 1u);
}

// A wrong token stays NO_CONTROL and never reaches the transport.
TEST(CoordinatorDeadline, WrongTokenStaysNoControl) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  const std::int64_t source_ns = harness.clock->ros_ns.load();
  harness.coordinator->velocity(
    velocityRequest("not-the-lease-token", 0.1, source_ns));

  harness.expectTraceCode("motion.velocity", "NO_CONTROL");
  EXPECT_TRUE(capturing->dispatches.empty());
  // The rejected command advanced no sample: the same source stamp is still
  // accepted with the right token.
  harness.coordinator->velocity(velocityRequest(acquired.token, 0.1, source_ns));
  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");
  ASSERT_EQ(capturing->dispatches.size(), 1u);
}

// An invalid TTL keeps the error codes of the guard and never dispatches.
TEST(CoordinatorDeadline, InvalidTtlKeepsTheGuardErrorCodes) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  const std::int64_t source_ns = harness.clock->ros_ns.load();
  for (double ttl : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                     std::numeric_limits<double>::infinity()}) {
    harness.coordinator->velocity(velocityRequest(acquired.token, ttl, source_ns));
    harness.expectTraceCode("motion.velocity", "INVALID_ARGUMENT");
  }
  for (double ttl : {0.6, 5.0}) {
    harness.coordinator->velocity(velocityRequest(acquired.token, ttl, source_ns));
    harness.expectTraceCode("motion.velocity", "OUT_OF_RANGE");
  }
  EXPECT_TRUE(capturing->dispatches.empty());
  // No rejected input advanced a usable sample.
  harness.coordinator->velocity(velocityRequest(acquired.token, 0.1, source_ns));
  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");
  ASSERT_EQ(capturing->dispatches.size(), 1u);
}

// A source interval that is already consumed is rejected before the guard
// sample is committed: no dispatch and no watermark advance.
TEST(CoordinatorDeadline, StaleSourceIsRejectedWithoutDispatch) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  // The source stamp is one second old: the age exceeds the TTL.
  const std::int64_t old_ns = harness.clock->ros_ns.load() - 1'000'000'000LL;
  harness.coordinator->velocity(velocityRequest(acquired.token, 0.1, old_ns));

  harness.expectTraceCode("motion.velocity", "STALE_COMMAND");
  EXPECT_TRUE(capturing->dispatches.empty());
  // The rejected command advanced no sample.
  harness.coordinator->velocity(
    velocityRequest(acquired.token, 0.1, harness.clock->ros_ns.load()));
  harness.expectTraceCode("motion.velocity", "RUNTIME_ISSUED");
  ASSERT_EQ(capturing->dispatches.size(), 1u);
}

// A one-shot NaN steady sample exactly at the velocity expiry commit. The
// steady reads of one velocity command are, in this order:
//   1. the lease expiry check of expireLocked()
//   2. the token check inside the guard's velocityNs()
//   3. the velocity expiry commit          <- the NaN lands here
//   4. the bridge sample, which recovers
// A NaN commit must never be hidden behind the finite lease expiry.
TEST(CoordinatorDeadline, NaNVelocityExpiryIsNotDispatchedAndHalts) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  auto armed = std::make_shared<std::atomic<bool>>(false);
  auto remaining = std::make_shared<std::atomic<int>>(0);
  harness.steady_override =
    std::make_shared<std::function<double()>>([&harness, armed, remaining] {
      if (armed->load()) {
        const int left = remaining->fetch_sub(1) - 1;
        if (left == 0) {
          return std::numeric_limits<double>::quiet_NaN();
        }
      }
      return harness.clock->steady_s.load();
    });
  // Two steady reads follow the validation: the token check of the guard
  // and the expiry commit itself. The NaN lands on the second one.
  capturing->on_validate = [armed, remaining] {
    armed->store(true);
    remaining->store(2);
  };

  harness.coordinator->velocity(
    velocityRequest(acquired.token, 0.1, harness.clock->ros_ns.load()));

  // No motion: the guard expiry is unusable.
  EXPECT_TRUE(capturing->dispatches.empty());
  harness.expectTraceCode("motion.velocity", "DRIVER_ERROR");
  // The NaN was seen at the velocity expiry commit, not at the bridge.
  EXPECT_NE(harness.coordinator->fault().find("Velocity expiry is unusable"),
            std::string::npos);
  // The motion is stopped instead of being left to the extended timeout.
  EXPECT_EQ(capturing->stop_calls, 1);
}

// The steady clock jumps by 200 ms per read: the bridge sample lands after
// the velocity expiry and no bound is left to grant.
TEST(CoordinatorDeadline, ExpiredBridgeBoundIsNotDispatchedAndHalts) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  harness.steady_override =
    std::make_shared<std::function<double()>>([&harness] {
      const double value = harness.clock->steady_s.load();
      harness.clock->steady_s.store(value + 0.2);
      return value;
    });

  harness.coordinator->velocity(
    velocityRequest(acquired.token, 0.1, harness.clock->ros_ns.load()));

  EXPECT_TRUE(capturing->dispatches.empty());
  harness.expectTraceCode("motion.velocity", "DRIVER_ERROR");
  // The bridge bound failed, not the velocity expiry.
  EXPECT_NE(harness.coordinator->fault().find("No usable motion deadline"),
            std::string::npos);
  EXPECT_EQ(capturing->stop_calls, 1);
}

// An adapter that refuses the dispatch after the guard admitted the
// command is a driver failure: the motion is stopped instead of being left
// to the just extended guard timeout.
TEST(CoordinatorDeadline, AdapterRejectionAfterAdmissionHalts) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  capturing->reject_dispatch = true;
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  harness.coordinator->velocity(
    velocityRequest(acquired.token, 0.1, harness.clock->ros_ns.load()));

  ASSERT_EQ(capturing->dispatches.size(), 1u);
  harness.expectTraceCode("motion.velocity", "DRIVER_ERROR");
  EXPECT_EQ(capturing->stop_calls, 1);
  EXPECT_FALSE(harness.coordinator->fault().empty());
  for (double value : capturing->snapshot().velocity) {
    EXPECT_DOUBLE_EQ(value, 0.0);
  }
}

// An adapter that throws after the guard admitted the command is a driver
// failure as well, and no old motion is left newly armed.
TEST(CoordinatorDeadline, AdapterExceptionAfterAdmissionHalts) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  capturing->throw_dispatch = true;
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  harness.coordinator->velocity(
    velocityRequest(acquired.token, 0.1, harness.clock->ros_ns.load()));

  ASSERT_EQ(capturing->dispatches.size(), 1u);
  harness.expectTraceCode("motion.velocity", "DRIVER_ERROR");
  EXPECT_EQ(capturing->stop_calls, 1);
  EXPECT_NE(harness.coordinator->fault().find("deadline dispatch failed"),
            std::string::npos);
  for (double value : capturing->snapshot().velocity) {
    EXPECT_DOUBLE_EQ(value, 0.0);
  }
}

// A ROS clock that fails on every read must neither escape the public
// method nor suppress the stop output: the command is not admitted and the
// output that is still armed is stopped.
TEST(CoordinatorDeadline, PersistentRosClockFailureDoesNotEscapeVelocity) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  // The first command arms the output.
  harness.coordinator->velocity(
    velocityRequest(acquired.token, 0.5, harness.clock->ros_ns.load()));
  ASSERT_EQ(capturing->dispatches.size(), 1u);
  ASSERT_TRUE(capturing->hasNonzeroDispatch());

  // The broken ROS clock loses the trace and the result of the command it
  // breaks. The loss is the expected outcome of this case, so the channels
  // are enabled explicitly: the ordinary rig rejects every construction
  // drop.
  harness.rig.allowConstructionLoss(ObservationChannel::trace);
  harness.rig.allowConstructionLoss(ObservationChannel::results);

  // The checkpoint is taken before the fault: the loss starts here and
  // nowhere else.
  const ObservationRig::FaultCheckpoint checkpoint =
    harness.rig.captureCheckpoint();
  EXPECT_EQ(checkpoint.last_assigned_trace_sequence,
            harness.rig.lastAssignedTraceSequence());
  EXPECT_EQ(checkpoint.dropped_construction[static_cast<std::size_t>(
                ObservationChannel::trace)],
            0U);
  EXPECT_EQ(checkpoint.dropped_construction[static_cast<std::size_t>(
                ObservationChannel::results)],
            0U);

  // The ROS clock now fails on every read.
  harness.ros_override =
    std::make_shared<std::function<std::int64_t()>>([]() -> std::int64_t {
      throw std::runtime_error("ros clock unavailable");
    });

  EXPECT_NO_THROW(harness.coordinator->velocity(
    velocityRequest(acquired.token, 0.1, harness.clock->ros_ns.load())));

  // The construction loss is real and it is strictly after the checkpoint.
  const ObservationStats after = harness.rig.endpoint()->stats();
  EXPECT_GT(after.channels[static_cast<std::size_t>(ObservationChannel::trace)]
              .dropped_construction,
            0U);
  EXPECT_GT(after.channels[static_cast<std::size_t>(ObservationChannel::results)]
              .dropped_construction,
            0U);
  EXPECT_GT(after.last_assigned_trace_sequence,
            checkpoint.last_assigned_trace_sequence);

  // No new nonzero output, and the armed output is stopped.
  EXPECT_EQ(capturing->dispatches.size(), 1u);
  EXPECT_EQ(capturing->stop_calls, 1);
  for (double value : capturing->snapshot().velocity) {
    EXPECT_DOUBLE_EQ(value, 0.0);
  }
}

// A driver monotonic clock that fails on every read is handled the same
// way: the failure never escapes the public method.
TEST(CoordinatorDeadline, PersistentDriverClockFailureDoesNotEscapeVelocity) {
  auto adapter = std::make_unique<DeadlineCapturingAdapter>(mockAsset());
  DeadlineCapturingAdapter *capturing = adapter.get();
  Harness harness(false, std::move(adapter));
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  // The first command arms the output.
  harness.coordinator->velocity(
    velocityRequest(acquired.token, 0.5, harness.clock->ros_ns.load()));
  ASSERT_EQ(capturing->dispatches.size(), 1u);
  ASSERT_TRUE(capturing->hasNonzeroDispatch());

  // The driver deadline clock now fails on every read.
  harness.driver_mono_override =
    std::make_shared<std::function<std::int64_t()>>([]() -> std::int64_t {
      throw std::runtime_error("driver monotonic clock unavailable");
    });

  EXPECT_NO_THROW(harness.coordinator->velocity(
    velocityRequest(acquired.token, 0.1, harness.clock->ros_ns.load())));

  // No new nonzero output, and the armed output is stopped.
  EXPECT_EQ(capturing->dispatches.size(), 1u);
  EXPECT_EQ(capturing->stop_calls, 1);
  for (double value : capturing->snapshot().velocity) {
    EXPECT_DOUBLE_EQ(value, 0.0);
  }
}

}  // namespace
}  // namespace chaos::runtime
