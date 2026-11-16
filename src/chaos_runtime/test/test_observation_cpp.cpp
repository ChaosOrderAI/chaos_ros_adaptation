// ROS-free foundation tests for the runtime observation service.
//
// The suite drives the real production service (chaos::runtime::ObservationService)
// through its public API only. No ROS, no DDS, no coordinator and no
// synchronous replacement of the worker pool is involved: every assertion
// observes the real four worker implementation.
//
// HARNESS CONTRACT
//   * The fixture owns the service, the endpoints, the shared recording state,
//     every release gate and every auxiliary thread.
//   * The cleanup guard releases every gate, joins the auxiliary threads,
//     closes the endpoints, requests the stop and joins the service -- in
//     that order -- also when an ASSERT aborts the test.
//   * No assertion runs on a worker or a sink thread: the workers only record
//     synchronized results, the test thread asserts.
//   * Every wait is a bounded predicate wait. The steady clock deadlines are
//     generous deadlock guards, never hard real time promises.
//   * A call that may block forever (joinFor, close) runs on a fixture owned
//     helper thread. The test thread waits for a completion marker; a missed
//     deadline is recorded, every gate is released and only then the helper is
//     joined, so the test thread never blocks inside the call under test.
//
// Only the observation header is included: the staging chunk keeps the view
// definitions in observation_types.hpp, so including coordinator.hpp here would
// make CommandResultView and StateView ambiguous.
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "chaos_runtime/observation.hpp"

namespace {

using chaos::runtime::Admission;
using chaos::runtime::CommandResultView;
using chaos::runtime::ObservationChannel;
using chaos::runtime::ObservationEndpoint;
using chaos::runtime::ObservationService;
using chaos::runtime::ObservationServiceStats;
using chaos::runtime::ObservationStats;
using chaos::runtime::ObservationSinks;
using chaos::runtime::StateView;
using chaos::runtime::TraceEvent;
using chaos::runtime::observation_limits::kMaxAggregatePayloadBytes;
using chaos::runtime::observation_limits::kMaxEventStringBytes;
using chaos::runtime::observation_limits::kProducerSlots;
using chaos::runtime::observation_limits::kResultsQueueCapacity;
using chaos::runtime::observation_limits::kTraceQueueCapacity;
using chaos::runtime::observation_limits::kLogQueueCapacity;

using namespace std::chrono_literals;

// The public channel order is the internal channel order of the per channel
// counters and of the per channel busy flags.
std::size_t channelIndex(ObservationChannel channel) {
  return static_cast<std::size_t>(channel);
}

const char * channelName(ObservationChannel channel) {
  switch (channel) {
    case ObservationChannel::trace:
      return "trace";
    case ObservationChannel::results:
      return "results";
    case ObservationChannel::state:
      return "state";
    case ObservationChannel::log:
      return "log";
  }
  return "unknown";
}

const char * admissionName(Admission admission) {
  switch (admission) {
    case Admission::admitted:
      return "admitted";
    case Admission::coalesced:
      return "coalesced";
    case Admission::full:
      return "full";
    case Admission::contended:
      return "contended";
    case Admission::closed:
      return "closed";
    case Admission::oversize:
      return "oversize";
    case Admission::construction_failed:
      return "construction_failed";
  }
  return "unknown";
}

// A bounded predicate wait: the predicate is re-evaluated until it holds or
// the deadline expires. The short timed wait keeps the poll rate low; it is
// never a fixed sleep that hopes for the right moment.
template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::mutex mutex;
  std::condition_variable cv;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, 1ms);
  }
  return true;
}

// A one shot release gate. A waiter announces its entry and then blocks until
// the gate is released. The gate is one shot: a waiter that arrives after the
// release does not block again.
class Gate {
public:
  explicit Gate(std::string name) : name_(std::move(name)) {}

  void wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    entered_ = true;
    cv_.notify_all();
    cv_.wait(lock, [this] { return open_; });
  }

  void release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (open_) {
        return;
      }
      open_ = true;
    }
    cv_.notify_all();
  }

  bool entered() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entered_;
  }

  bool released() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
  }

  const std::string & name() const { return name_; }

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::string name_;
  bool entered_ = false;
  bool open_ = false;
};

// A mutex protected recording of the delivered payloads. Only the workers and
// the sink threads write into it; the test thread reads snapshots.
template <typename Value>
class Recorder {
public:
  void add(Value value) {
    std::lock_guard<std::mutex> lock(mutex_);
    values_.push_back(std::move(value));
  }

  std::vector<Value> snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return values_;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return values_.size();
  }

private:
  mutable std::mutex mutex_;
  std::vector<Value> values_;
};

using LogRecord = std::pair<std::string, std::string>;

// A trace sink that records the delivered events. The optional gate blocks the
// delivery before the recording, so the test can mutate the caller storage
// after the admission and before the delivery.
class RecordingTraceSink : public chaos::runtime::TraceSink {
public:
  RecordingTraceSink(std::shared_ptr<Recorder<TraceEvent>> recorder,
                     std::shared_ptr<Gate> gate)
      : recorder_(std::move(recorder)), gate_(std::move(gate)) {}

  void publish(const TraceEvent & event) override {
    if (gate_) {
      gate_->wait();
    }
    recorder_->add(event);
  }

private:
  std::shared_ptr<Recorder<TraceEvent>> recorder_;
  std::shared_ptr<Gate> gate_;
};

// One auxiliary call that runs on a fixture owned thread.
struct GuardedCall {
  // The result is read after the completion marker, so the mutex has to be
  // reachable from a const reader.
  mutable std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  bool value = false;
  // Read by the fixture only after joining the helper.
  bool unexpected_exception = false;
  std::string label;
  std::thread thread;

  bool waitFor(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex);
    return cv.wait_for(lock, timeout, [this] { return done; });
  }

  bool result() const {
    std::lock_guard<std::mutex> lock(mutex);
    return value;
  }

  void complete(bool result) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      value = result;
      done = true;
    }
    cv.notify_all();
  }
};

struct GuardedOutcome {
  // The call returned before the guard deadline expired.
  bool completed = false;
  // The value the call returned.
  bool value = false;
  // The call missed the guard deadline.
  bool timed_out = false;
};

// Payload builders. The sizes are derived from the real fields: the trace
// payload is frame + asset_id + request_id + command + stage + code.
TraceEvent makeTraceEvent(std::uint64_t sequence, std::int64_t stamp_ns) {
  TraceEvent event;
  event.stamp_ns = stamp_ns;
  event.monotonic_ns = stamp_ns + 1;
  event.frame = "base_link";
  event.asset_id = "dog";
  event.sequence = sequence;
  event.request_id = "req-" + std::to_string(sequence);
  event.command = "stop";
  event.stage = "dispatch";
  event.code = "OK";
  return event;
}

std::size_t traceEventPayloadBytes(const TraceEvent & event) {
  return event.frame.size() + event.asset_id.size() + event.request_id.size() +
         event.command.size() + event.stage.size() + event.code.size();
}

// A trace event whose owned string payload is exactly the per event bound.
TraceEvent makeMaximumTraceEvent(std::uint64_t sequence) {
  TraceEvent event;
  event.stamp_ns = static_cast<std::int64_t>(sequence);
  event.monotonic_ns = static_cast<std::int64_t>(sequence) + 1;
  event.asset_id = "dog";
  event.request_id = "req";
  event.command = "stop";
  event.stage = "dispatch";
  event.code = "OK";
  const std::size_t fixed = event.asset_id.size() + event.request_id.size() +
                            event.command.size() + event.stage.size() +
                            event.code.size();
  event.frame.assign(kMaxEventStringBytes - fixed, 'f');
  event.sequence = sequence;
  return event;
}

CommandResultView makeResult(std::uint64_t sequence) {
  CommandResultView view;
  view.stamp_ns = static_cast<std::int64_t>(1000 + sequence);
  view.request_id = "req-" + std::to_string(sequence);
  view.success = true;
  view.code = "OK";
  view.message = "completed";
  return view;
}

StateView makeState(std::int64_t stamp_ns) {
  StateView view;
  view.stamp_ns = stamp_ns;
  view.asset_id = "dog";
  view.model = "go2";
  view.connected = true;
  view.ready = true;
  view.pose_valid = true;
  view.operational_state = "ACTIVE";
  view.detail = "ok";
  view.snapshot.connected = true;
  view.snapshot.ready = true;
  view.snapshot.pose_valid = true;
  // The default detail of the snapshot is not empty: it is replaced here so
  // the state payload size stays explicit.
  view.snapshot.detail = "telemetry";
  return view;
}

// A start barrier for the auxiliary calls: every participant waits for the
// release and then runs at the same time.
//
// The barrier is owned by the fixture: the cleanup releases every barrier
// before it joins the auxiliary threads, so a helper never waits on a
// barrier of a test body that already returned.
class StartBarrier {
public:
  void wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return open_; });
  }

  void open() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      open_ = true;
    }
    cv_.notify_all();
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool open_ = false;
};

// The shared state of the close/admission race.
//
// The state is heap owned and mutex protected: the auxiliary threads never
// touch a stack local of the test body, not even when an assertion aborts
// the test body before the race finished.
struct CloseAdmissionRace {
  std::shared_ptr<StartBarrier> barrier;
  std::shared_ptr<ObservationEndpoint> endpoint;
  mutable std::mutex mutex;
  Admission outcome = Admission::admitted;

  void setOutcome(Admission value) {
    std::lock_guard<std::mutex> lock(mutex);
    outcome = value;
  }

  Admission result() const {
    std::lock_guard<std::mutex> lock(mutex);
    return outcome;
  }
};

// The shared state of the two simultaneous process callers.
//
// Every acquired generation is registered with the fixture by the acquiring
// thread, so a generation is never lost when an assertion aborts the test
// body before the generations could be handed over.
struct ProcessRace {
  std::shared_ptr<StartBarrier> barrier;
  mutable std::mutex mutex;
  std::shared_ptr<ObservationService> first;
  std::shared_ptr<ObservationService> second;

  void setFirst(std::shared_ptr<ObservationService> generation) {
    std::lock_guard<std::mutex> lock(mutex);
    first = std::move(generation);
  }

  void setSecond(std::shared_ptr<ObservationService> generation) {
    std::lock_guard<std::mutex> lock(mutex);
    second = std::move(generation);
  }

  std::shared_ptr<ObservationService> firstGeneration() const {
    std::lock_guard<std::mutex> lock(mutex);
    return first;
  }

  std::shared_ptr<ObservationService> secondGeneration() const {
    std::lock_guard<std::mutex> lock(mutex);
    return second;
  }
};

// The shared fixture. It owns the service, the endpoints, the gates, the
// recording state and every auxiliary thread.
class ObservationTest : public ::testing::Test {
protected:
  void SetUp() override {
    std::shared_ptr<ObservationService> service = ObservationService::process();
    if (service->stats().stop_requested) {
      // A leftover stopping generation of an earlier (failed) test: retire it
      // before a fresh generation is acquired.
      service->requestStop();
      if (!service->joinFor(10s)) {
        ADD_FAILURE() << "a leftover observation generation could not be joined";
      }
      service = ObservationService::process();
    }
    service_ = std::move(service);
  }

  void TearDown() override {
    // The cleanup order is fixed: release every gate and every barrier, join
    // the auxiliary threads, close the endpoints, request the stop and join
    // the service.
    releaseAllGates();
    releaseAllBarriers();
    joinHelpers();
    closeEndpoints();
    if (service_) {
      service_->requestStop();
      joinService(*service_);
    }
    std::vector<std::shared_ptr<ObservationService>> generations;
    {
      std::lock_guard<std::mutex> lock(generations_mutex_);
      generations = extra_generations_;
    }
    for (auto & generation : generations) {
      generation->requestStop();
      joinService(*generation);
    }
  }

  const std::shared_ptr<ObservationService> & service() const { return service_; }

  std::shared_ptr<ObservationEndpoint> registerProducer(ObservationSinks sinks) {
    std::shared_ptr<ObservationEndpoint> endpoint =
      service_->registerProducer(std::move(sinks));
    endpoints_.push_back(endpoint);
    return endpoint;
  }

  // Releases the fixture ownership of an endpoint, so a local reference
  // becomes the last owner of the registration handle and its destruction
  // really runs.
  void releaseEndpointOwner(
    const std::shared_ptr<ObservationEndpoint> & endpoint) {
    endpoints_.erase(
      std::remove(endpoints_.begin(), endpoints_.end(), endpoint),
      endpoints_.end());
  }

  // The generations are registered by the auxiliary threads as well, so the
  // registration is guarded: a generation is never lost when an assertion
  // aborts the test body.
  void trackGeneration(std::shared_ptr<ObservationService> generation) {
    std::lock_guard<std::mutex> lock(generations_mutex_);
    extra_generations_.push_back(std::move(generation));
  }

  std::shared_ptr<Gate> makeGate(std::string name) {
    auto gate = std::make_shared<Gate>(std::move(name));
    gates_.push_back(gate);
    return gate;
  }

  // The barrier is owned by the fixture, so the cleanup releases it before
  // the auxiliary threads are joined.
  std::shared_ptr<StartBarrier> makeBarrier(std::string name) {
    auto barrier = std::make_shared<StartBarrier>();
    barriers_.push_back(barrier);
    return barrier;
  }

  void releaseAllGates() {
    for (auto & gate : gates_) {
      gate->release();
    }
  }

  void releaseAllBarriers() {
    for (auto & barrier : barriers_) {
      barrier->open();
    }
  }

  bool awaitGateEntry(const std::shared_ptr<Gate> & gate,
                      std::chrono::milliseconds timeout) {
    return waitFor([&gate] { return gate->entered(); }, timeout);
  }

  // Retries ONLY the contended outcome. Every other outcome (closed, oversize,
  // full, construction_failed, admitted, coalesced) is returned unchanged, so
  // the drop outcome under test is never retried away. The contended attempts
  // stay visible in the dropped_contended counter.
  //
  // The bound is a duration, not an attempt count: a fixed number of yields
  // lasts only some tens of microseconds, less than one scheduling delay of a
  // worker that holds the queue lock.
  Admission admitRetrying(
    const std::function<Admission()> & attempt,
    std::chrono::milliseconds budget = std::chrono::milliseconds(5000)) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    Admission outcome = attempt();
    while (outcome == Admission::contended &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::yield();
      outcome = attempt();
    }
    return outcome;
  }

  // Runs a call that may block on an auxiliary thread. The test thread waits
  // for the completion marker. A missed deadline is recorded in the outcome,
  // every gate is released and only then the helper is joined.
  GuardedOutcome runGuarded(std::string label, std::function<bool()> body,
                            std::chrono::milliseconds guard) {
    GuardedCall & call = spawnHelper(std::move(label), std::move(body));
    GuardedOutcome outcome;
    outcome.completed = call.waitFor(guard);
    if (!outcome.completed) {
      outcome.timed_out = true;
      releaseAllGates();
      if (call.thread.joinable()) {
        call.thread.join();
      }
      return outcome;
    }
    outcome.value = call.result();
    return outcome;
  }

  GuardedCall & spawnHelper(std::string label, std::function<bool()> body) {
    auto call = std::make_shared<GuardedCall>();
    call->label = std::move(label);
    helpers_.push_back(call);
    call->thread = std::thread([call, body] {
      bool value = false;
      try {
        value = body();
      } catch (...) {
        call->unexpected_exception = true;
      }
      call->complete(value);
    });
    return *call;
  }

  void joinHelpers() {
    for (auto & call : helpers_) {
      if (call->thread.joinable()) {
        call->thread.join();
      }
      EXPECT_FALSE(call->unexpected_exception) << call->label;
    }
  }

  void closeEndpoints() {
    for (auto & endpoint : endpoints_) {
      if (endpoint) {
        endpoint->close();
      }
    }
  }

  void joinService(ObservationService & target) {
    if (!target.joinFor(15s)) {
      ADD_FAILURE() << "the observation service did not join in the cleanup";
    }
  }

private:
  std::shared_ptr<ObservationService> service_;
  std::vector<std::shared_ptr<ObservationService>> extra_generations_;
  std::mutex generations_mutex_;
  std::vector<std::shared_ptr<ObservationEndpoint>> endpoints_;
  std::vector<std::shared_ptr<Gate>> gates_;
  std::vector<std::shared_ptr<StartBarrier>> barriers_;
  std::vector<std::shared_ptr<GuardedCall>> helpers_;
};

// A sink bundle where one channel blocks on a gate and every other channel
// records its deliveries.
ObservationSinks makeMixedSinks(
    ObservationChannel blocked, const std::shared_ptr<Gate> & gate,
    const std::shared_ptr<Recorder<TraceEvent>> & trace_recorder,
    const std::shared_ptr<Recorder<CommandResultView>> & result_recorder,
    const std::shared_ptr<Recorder<StateView>> & state_recorder,
    const std::shared_ptr<Recorder<LogRecord>> & log_recorder) {
  ObservationSinks sinks;
  sinks.trace = std::make_shared<RecordingTraceSink>(
    trace_recorder, blocked == ObservationChannel::trace ? gate : nullptr);
  sinks.results = [result_recorder, gate, blocked](const CommandResultView & view) {
    if (blocked == ObservationChannel::results) {
      gate->wait();
    }
    result_recorder->add(view);
  };
  sinks.state = [state_recorder, gate, blocked](const StateView & view) {
    if (blocked == ObservationChannel::state) {
      gate->wait();
    }
    state_recorder->add(view);
  };
  sinks.log = [log_recorder, gate, blocked](const std::string & level,
                                            const std::string & text) {
    if (blocked == ObservationChannel::log) {
      gate->wait();
    }
    log_recorder->add(LogRecord{level, text});
  };
  return sinks;
}

// --- case 1 -----------------------------------------------------------------

TEST_F(ObservationTest, HealthyLifecycleAndOwnedPayload) {
  auto trace_recorder = std::make_shared<Recorder<TraceEvent>>();
  auto result_recorder = std::make_shared<Recorder<CommandResultView>>();
  auto state_recorder = std::make_shared<Recorder<StateView>>();
  auto log_recorder = std::make_shared<Recorder<LogRecord>>();
  auto delivery_gate = makeGate("trace-delivery");

  ObservationSinks sinks;
  sinks.trace = std::make_shared<RecordingTraceSink>(trace_recorder, delivery_gate);
  sinks.results = [result_recorder](const CommandResultView & view) {
    result_recorder->add(view);
  };
  sinks.state = [state_recorder](const StateView & view) {
    state_recorder->add(view);
  };
  sinks.log = [log_recorder](const std::string & level, const std::string & text) {
    log_recorder->add(LogRecord{level, text});
  };
  auto endpoint = registerProducer(std::move(sinks));

  TraceEvent event = makeTraceEvent(7, 1110000000LL);
  CommandResultView result = makeResult(7);
  StateView state = makeState(3330000000LL);

  ASSERT_EQ(admitRetrying([&] { return endpoint->trace(event); }),
            Admission::admitted);
  ASSERT_EQ(admitRetrying([&] { return endpoint->result(result); }),
            Admission::admitted);
  ASSERT_EQ(admitRetrying([&] { return endpoint->state(state); }),
            Admission::admitted);
  ASSERT_EQ(admitRetrying([&] { return endpoint->log("INFO", "started"); }),
            Admission::admitted);

  // The caller storage is mutated and released after the admission: the
  // service owns its own copy of every payload.
  event.frame.assign(64, 'x');
  event.asset_id.assign(64, 'y');
  event.request_id.assign(64, 'z');
  event.command.assign(64, 'w');
  result.message.assign(64, 'm');
  result.request_id.assign(64, 'n');
  state.detail.assign(64, 'd');
  state.operational_state.assign(64, 'o');
  delivery_gate->release();

  ASSERT_TRUE(waitFor(
    [&] {
      return trace_recorder->size() == 1 && result_recorder->size() == 1 &&
             state_recorder->size() == 1 && log_recorder->size() == 1;
    },
    10s));

  const std::vector<TraceEvent> recorded_traces = trace_recorder->snapshot();
  ASSERT_EQ(recorded_traces.size(), 1U);
  EXPECT_EQ(recorded_traces[0].stamp_ns, 1110000000LL);
  EXPECT_EQ(recorded_traces[0].sequence, 7U);
  EXPECT_EQ(recorded_traces[0].frame, "base_link");
  EXPECT_EQ(recorded_traces[0].asset_id, "dog");
  EXPECT_EQ(recorded_traces[0].request_id, "req-7");
  EXPECT_EQ(recorded_traces[0].command, "stop");

  const std::vector<CommandResultView> recorded_results = result_recorder->snapshot();
  ASSERT_EQ(recorded_results.size(), 1U);
  EXPECT_EQ(recorded_results[0].stamp_ns, 1007LL);
  EXPECT_EQ(recorded_results[0].request_id, "req-7");
  EXPECT_EQ(recorded_results[0].message, "completed");
  EXPECT_TRUE(recorded_results[0].success);

  const std::vector<StateView> recorded_states = state_recorder->snapshot();
  ASSERT_EQ(recorded_states.size(), 1U);
  EXPECT_EQ(recorded_states[0].stamp_ns, 3330000000LL);
  EXPECT_EQ(recorded_states[0].asset_id, "dog");
  EXPECT_EQ(recorded_states[0].operational_state, "ACTIVE");
  EXPECT_EQ(recorded_states[0].detail, "ok");

  const std::vector<LogRecord> recorded_logs = log_recorder->snapshot();
  ASSERT_EQ(recorded_logs.size(), 1U);
  EXPECT_EQ(recorded_logs[0].first, "INFO");
  EXPECT_EQ(recorded_logs[0].second, "started");

  // A producer that registered only some of the sinks keeps the documented
  // no-op accounting: the disabled channels are admitted and counted as
  // delivered without invoking anything.
  auto partial_recorder = std::make_shared<Recorder<TraceEvent>>();
  ObservationSinks partial;
  partial.trace = std::make_shared<RecordingTraceSink>(partial_recorder, nullptr);
  auto partial_endpoint = registerProducer(std::move(partial));
  ASSERT_EQ(admitRetrying([&] { return partial_endpoint->result(result); }),
            Admission::admitted);

  // Close, stop and join: no queued, in flight or occupied state survives.
  endpoint->close();
  partial_endpoint->close();
  service()->requestStop();
  ASSERT_TRUE(service()->joinFor(10s));

  const ObservationServiceStats service_stats = service()->stats();
  EXPECT_EQ(service_stats.queued_items, 0U);
  EXPECT_EQ(service_stats.queued_bytes, 0U);
  EXPECT_EQ(service_stats.in_flight_items, 0U);
  EXPECT_EQ(service_stats.in_flight_bytes, 0U);
  EXPECT_EQ(service_stats.slots_used, 0U);
  EXPECT_TRUE(service_stats.joined);

  const ObservationStats producer_stats = endpoint->stats();
  EXPECT_EQ(producer_stats.queued_items, 0U);
  EXPECT_EQ(producer_stats.in_flight_items, 0U);
  EXPECT_TRUE(producer_stats.closed);

  // The disabled channels of the partial producer are accounted as consumed
  // no-ops: the attempt was admitted and consumed. A contended drop stays
  // visible in its own counter, so the attempt count is not a fixed number.
  const ObservationStats partial_stats = partial_endpoint->stats();
  const auto & results_stats =
    partial_stats.channels[channelIndex(ObservationChannel::results)];
  EXPECT_GE(results_stats.attempted, 1U);
  EXPECT_EQ(results_stats.admitted, 1U);
  EXPECT_EQ(results_stats.delivered, 1U);
  EXPECT_EQ(results_stats.dropped_full, 0U);
  EXPECT_EQ(results_stats.sink_exceptions, 0U);
  EXPECT_EQ(results_stats.attempted,
            results_stats.admitted + results_stats.dropped_full +
              results_stats.dropped_contended + results_stats.dropped_closed +
              results_stats.dropped_oversize + results_stats.dropped_construction);
  const auto & state_stats =
    partial_stats.channels[channelIndex(ObservationChannel::state)];
  EXPECT_EQ(state_stats.attempted, 0U);
  EXPECT_EQ(state_stats.delivered, 0U);

  // A later process() returns a fresh healthy generation.
  auto fresh = ObservationService::process();
  ASSERT_NE(fresh.get(), service().get());
  trackGeneration(fresh);

  auto fresh_recorder = std::make_shared<Recorder<TraceEvent>>();
  ObservationSinks fresh_sinks;
  fresh_sinks.trace = std::make_shared<RecordingTraceSink>(fresh_recorder, nullptr);
  auto fresh_endpoint = fresh->registerProducer(std::move(fresh_sinks));
  EXPECT_EQ(admitRetrying([&] { return fresh_endpoint->trace(makeTraceEvent(1, 1)); }),
            Admission::admitted);
  ASSERT_TRUE(waitFor([&] { return fresh_recorder->size() == 1U; }, 10s));

  fresh->requestStop();
  EXPECT_TRUE(fresh->joinFor(10s));
}

// --- cases 2 to 5 -----------------------------------------------------------

struct BlockedChannelParam {
  ObservationChannel channel;
  const char * name;
};

class ObservationBlockedChannelIsolation
  : public ObservationTest,
    public ::testing::WithParamInterface<BlockedChannelParam> {};

std::string blockedChannelName(const ::testing::TestParamInfo<BlockedChannelParam> & info) {
  return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(
  Channels, ObservationBlockedChannelIsolation,
  ::testing::Values(BlockedChannelParam{ObservationChannel::trace, "trace"},
                     BlockedChannelParam{ObservationChannel::results, "results"},
                     BlockedChannelParam{ObservationChannel::state, "state"},
                     BlockedChannelParam{ObservationChannel::log, "log"}),
  blockedChannelName);

TEST_P(ObservationBlockedChannelIsolation, BlockedChannelIsolation) {
  const BlockedChannelParam param = GetParam();
  const ObservationChannel blocked = param.channel;
  const ObservationChannel other =
    blocked == ObservationChannel::trace ? ObservationChannel::results
                                         : ObservationChannel::trace;

  auto gate = makeGate(std::string("blocked-") + param.name);
  auto trace_recorder = std::make_shared<Recorder<TraceEvent>>();
  auto result_recorder = std::make_shared<Recorder<CommandResultView>>();
  auto state_recorder = std::make_shared<Recorder<StateView>>();
  auto log_recorder = std::make_shared<Recorder<LogRecord>>();

  auto endpoint = registerProducer(
    makeMixedSinks(blocked, gate, trace_recorder, result_recorder,
                    state_recorder, log_recorder));

  // The blocked channel enters its callback and stays there.
  Admission blocked_admission = Admission::admitted;
  switch (blocked) {
    case ObservationChannel::trace:
      blocked_admission = admitRetrying([&] { return endpoint->trace(makeTraceEvent(1, 1)); });
      break;
    case ObservationChannel::results:
      blocked_admission = admitRetrying([&] { return endpoint->result(makeResult(1)); });
      break;
    case ObservationChannel::state:
      blocked_admission = admitRetrying([&] { return endpoint->state(makeState(1000)); });
      break;
    case ObservationChannel::log:
      blocked_admission = admitRetrying([&] { return endpoint->log("INFO", "blocked"); });
      break;
  }
  ASSERT_EQ(blocked_admission, Admission::admitted)
    << "the blocked admission returned " << admissionName(blocked_admission);
  ASSERT_TRUE(awaitGateEntry(gate, 10s))
    << "the " << param.name << " callback never entered the gate";

  // Another channel keeps delivering while the first one is blocked. The
  // baseline is taken BEFORE the admission: a fast worker must not be able to
  // make the test wait for a second delivery that never happens.
  const std::size_t other_count_before =
    other == ObservationChannel::trace ? trace_recorder->size()
                                       : result_recorder->size();
  Admission other_admission = Admission::admitted;
  if (other == ObservationChannel::trace) {
    other_admission = admitRetrying([&] { return endpoint->trace(makeTraceEvent(2, 2)); });
  } else {
    other_admission = admitRetrying([&] { return endpoint->result(makeResult(2)); });
  }
  ASSERT_EQ(other_admission, Admission::admitted);
  ASSERT_TRUE(waitFor(
    [&] {
      return (other == ObservationChannel::trace ? trace_recorder->size()
                                                  : result_recorder->size()) >
             other_count_before;
    },
    10s));

  // The close and the stop complete while the callback is blocked.
  const GuardedOutcome close_outcome =
    runGuarded("endpoint close", [&] { endpoint->close(); return true; }, 10s);
  EXPECT_TRUE(close_outcome.completed)
    << "the endpoint close did not return while the callback was blocked";
  const GuardedOutcome stop_outcome =
    runGuarded("requestStop", [&] { service()->requestStop(); return true; }, 10s);
  EXPECT_TRUE(stop_outcome.completed)
    << "the requestStop did not return while the callback was blocked";

  // A short join cannot succeed: the blocked worker is still alive.
  const GuardedOutcome short_join =
    runGuarded("short joinFor",
               [&] { return service()->joinFor(50ms); }, 15s);
  ASSERT_TRUE(short_join.completed)
    << "the short joinFor did not return by its deadline";
  EXPECT_FALSE(short_join.value);
  EXPECT_FALSE(service()->joined());

  // The generation is retained while it is not joined.
  EXPECT_EQ(ObservationService::process().get(), service().get());

  // Releasing the gate lets the worker finish and a later join succeeds.
  gate->release();
  const GuardedOutcome late_join =
    runGuarded("late joinFor", [&] { return service()->joinFor(15s); }, 30s);
  ASSERT_TRUE(late_join.completed) << "the late joinFor did not return";
  EXPECT_TRUE(late_join.value);
  EXPECT_TRUE(service()->joined());
}

// --- case 6 -----------------------------------------------------------------

// The copy counter of a user callable. The switch is enabled only after the
// registration, so the production owns the callable without a further copy.
struct CopyCounter {
  std::atomic<int> copies{0};
  std::atomic<bool> armed{false};
};

// A callable that observes its own copies and can refuse them. The copy
// constructor throws when the switch is armed, the move constructor only
// counts: a move may happen inside a noexcept context.
class CountingCallable {
public:
  CountingCallable(std::shared_ptr<CopyCounter> counter,
                   std::function<void(const CommandResultView &)> action)
      : counter_(std::move(counter)), action_(std::move(action)) {}

  CountingCallable(const CountingCallable & other)
      : counter_(other.counter_), action_(other.action_) {
    count();
    refuse();
  }

  CountingCallable(CountingCallable && other) noexcept
      : counter_(std::move(other.counter_)), action_(std::move(other.action_)) {
    count();
  }

  CountingCallable & operator=(const CountingCallable &) = delete;
  CountingCallable & operator=(CountingCallable &&) = delete;

  void operator()(const CommandResultView & view) const { action_(view); }

private:
  void count() { counter_->copies.fetch_add(1); }

  void refuse() {
    if (counter_->armed.load()) {
      throw std::runtime_error("chaos_runtime test: the callable was copied");
    }
  }

  std::shared_ptr<CopyCounter> counter_;
  std::function<void(const CommandResultView &)> action_;
};

// Copies the callable once: the copy constructor runs.
void copyCallableOnce(const CountingCallable & source) {
  const CountingCallable copy = source;
  (void)copy;
}

// The shared state of the throwing callbacks. It outlives the exceptions.
struct ThrowState {
  std::atomic<int> trace_calls{0};
  std::atomic<int> result_calls{0};
  std::atomic<int> state_calls{0};
  std::atomic<int> log_calls{0};
};

class ThrowingTraceSink : public chaos::runtime::TraceSink {
public:
  explicit ThrowingTraceSink(std::shared_ptr<ThrowState> state)
      : state_(std::move(state)) {}

  void publish(const TraceEvent &) override {
    state_->trace_calls.fetch_add(1);
    throw std::runtime_error("chaos_runtime test: the trace sink failed");
  }

private:
  std::shared_ptr<ThrowState> state_;
};

TEST_F(ObservationTest, CallableOwnershipAndThrows) {
  // --- the registered callable is owned and never copied again ----------
  auto counter = std::make_shared<CopyCounter>();
  auto result_recorder = std::make_shared<Recorder<CommandResultView>>();
  ObservationSinks owned_sinks;
  owned_sinks.results = CountingCallable{
    counter, [result_recorder](const CommandResultView & view) {
      result_recorder->add(view);
    }};
  auto owned_endpoint = registerProducer(std::move(owned_sinks));

  const int copies_after_registration = counter->copies.load();
  {
    // The counter itself is proven by an explicit copy.
    CountingCallable probe(counter, [](const CommandResultView &) {});
    const CountingCallable copy = probe;
    EXPECT_EQ(counter->copies.load(), copies_after_registration + 1);
  }
  // The throw switch is proven by an explicit copy as well.
  counter->armed.store(true);
  EXPECT_THROW(
    {
      CountingCallable source(counter, [](const CommandResultView &) {});
      copyCallableOnce(source);
    },
    std::runtime_error);

  const int copies_before_delivery = counter->copies.load();
  ASSERT_EQ(admitRetrying([&] { return owned_endpoint->result(makeResult(1)); }),
            Admission::admitted);
  // The delivered counter is published after the sink returned: it is the
  // proof that the delivery really finished, so it is awaited before the
  // counters are read.
  ASSERT_TRUE(waitFor(
    [&] {
      return owned_endpoint->stats()
               .channels[channelIndex(ObservationChannel::results)]
               .delivered == 1U;
    },
    10s));
  EXPECT_EQ(result_recorder->size(), 1U);
  EXPECT_EQ(counter->copies.load(), copies_before_delivery)
    << "the delivery copied the owned callable again";
  EXPECT_EQ(owned_endpoint->stats()
              .channels[channelIndex(ObservationChannel::results)]
              .delivered,
            1U);

  // --- the throwing callbacks are isolated ------------------------------
  auto throw_state = std::make_shared<ThrowState>();
  ObservationSinks throwing_sinks;
  throwing_sinks.trace = std::make_shared<ThrowingTraceSink>(throw_state);
  throwing_sinks.results = [throw_state](const CommandResultView &) {
    throw_state->result_calls.fetch_add(1);
    throw std::runtime_error("chaos_runtime test: the result callback failed");
  };
  throwing_sinks.state = [throw_state](const StateView &) {
    throw_state->state_calls.fetch_add(1);
    throw std::runtime_error("chaos_runtime test: the state callback failed");
  };
  throwing_sinks.log = [throw_state](const std::string &, const std::string &) {
    throw_state->log_calls.fetch_add(1);
    throw std::runtime_error("chaos_runtime test: the log callback failed");
  };
  auto throwing_endpoint = registerProducer(std::move(throwing_sinks));

  ASSERT_EQ(admitRetrying([&] { return throwing_endpoint->trace(makeTraceEvent(1, 1)); }),
            Admission::admitted);
  ASSERT_EQ(admitRetrying([&] { return throwing_endpoint->result(makeResult(1)); }),
            Admission::admitted);
  ASSERT_EQ(admitRetrying([&] { return throwing_endpoint->state(makeState(1000)); }),
            Admission::admitted);
  ASSERT_EQ(admitRetrying([&] { return throwing_endpoint->log("ERROR", "failure"); }),
            Admission::admitted);

  // The sink exception counters are published after the failing callback
  // returned: they are awaited before the snapshot is taken.
  ASSERT_TRUE(waitFor(
    [&] {
      const ObservationStats stats = throwing_endpoint->stats();
      return stats.channels[channelIndex(ObservationChannel::trace)]
               .sink_exceptions == 1U &&
             stats.channels[channelIndex(ObservationChannel::results)]
               .sink_exceptions == 1U &&
             stats.channels[channelIndex(ObservationChannel::state)]
               .sink_exceptions == 1U &&
             stats.channels[channelIndex(ObservationChannel::log)]
               .sink_exceptions == 1U;
    },
    10s));

  const ObservationStats throwing_snapshot = throwing_endpoint->stats();
  for (const ObservationChannel channel :
       {ObservationChannel::trace, ObservationChannel::results,
        ObservationChannel::state, ObservationChannel::log}) {
    const auto & stats = throwing_snapshot.channels[channelIndex(channel)];
    EXPECT_GE(stats.attempted, 1U) << "channel " << channelName(channel);
    EXPECT_EQ(stats.admitted, 1U) << "channel " << channelName(channel);
    EXPECT_EQ(stats.delivered, 0U)
      << "a failing callback was counted as delivered on channel "
      << channelName(channel);
    EXPECT_EQ(stats.sink_exceptions, 1U) << "channel " << channelName(channel);
    EXPECT_EQ(stats.attempted,
              stats.admitted + stats.dropped_full + stats.dropped_contended +
                stats.dropped_closed + stats.dropped_oversize +
                stats.dropped_construction)
      << "channel " << channelName(channel);
  }

  // The failures are never reported through the log channel: the log callback
  // was invoked exactly once, by the admitted log event.
  EXPECT_EQ(throw_state->trace_calls.load(), 1);
  EXPECT_EQ(throw_state->result_calls.load(), 1);
  EXPECT_EQ(throw_state->state_calls.load(), 1);
  EXPECT_EQ(throw_state->log_calls.load(), 1)
    << "a failing callback was reported through the log channel";
}

// --- case 7 -----------------------------------------------------------------

TEST_F(ObservationTest, FifoCapacityAndTraceGap) {
  // --- the trace FIFO ----------------------------------------------------
  auto trace_gate = makeGate("trace-capacity");
  auto trace_recorder = std::make_shared<Recorder<TraceEvent>>();
  ObservationSinks trace_sinks;
  trace_sinks.trace = std::make_shared<RecordingTraceSink>(trace_recorder, trace_gate);
  auto trace_endpoint = registerProducer(std::move(trace_sinks));

  ASSERT_EQ(admitRetrying([&] { return trace_endpoint->trace(makeTraceEvent(100, 1000)); }),
            Admission::admitted);
  ASSERT_TRUE(awaitGateEntry(trace_gate, 10s));

  for (std::size_t index = 0; index < kTraceQueueCapacity; ++index) {
    const TraceEvent event =
      makeTraceEvent(200 + index, 2000 + static_cast<std::int64_t>(index));
    ASSERT_EQ(admitRetrying([&] { return trace_endpoint->trace(event); }),
              Admission::admitted)
      << "the trace FIFO was full before its documented capacity";
  }
  {
    const ObservationStats stats = trace_endpoint->stats();
    EXPECT_EQ(stats.queued_items, kTraceQueueCapacity);
    EXPECT_EQ(stats.channels[channelIndex(ObservationChannel::trace)].dropped_full,
              0U);
  }

  // The next attempt is full and the queued count stays bounded.
  EXPECT_EQ(admitRetrying([&] { return trace_endpoint->trace(makeTraceEvent(500, 3000)); }),
            Admission::full);
  {
    const ObservationStats stats = trace_endpoint->stats();
    EXPECT_EQ(stats.queued_items, kTraceQueueCapacity);
    EXPECT_EQ(stats.channels[channelIndex(ObservationChannel::trace)].dropped_full,
              1U);
  }

  // The blocked delivery finishes and the queue drains.
  trace_gate->release();
  ASSERT_TRUE(waitFor(
    [&] {
      const ObservationStats stats = trace_endpoint->stats();
      return stats.queued_items == 0U && stats.in_flight_items == 0U;
    },
    20s));

  // A known later sequence is admitted after the rejected one.
  ASSERT_EQ(admitRetrying([&] { return trace_endpoint->trace(makeTraceEvent(600, 4000)); }),
            Admission::admitted);
  // The delivered counter and the delivered sequence are published after the
  // sink returned: they are awaited before the snapshot is taken.
  ASSERT_TRUE(waitFor(
    [&] {
      const ObservationStats stats = trace_endpoint->stats();
      return stats.channels[channelIndex(ObservationChannel::trace)].delivered ==
               kTraceQueueCapacity + 2U &&
             stats.last_delivered_trace_sequence == 600U;
    },
    20s));
  ASSERT_TRUE(waitFor([&] { return trace_recorder->size() == kTraceQueueCapacity + 2U; },
                      20s));

  const std::vector<TraceEvent> recorded = trace_recorder->snapshot();
  ASSERT_EQ(recorded.size(), kTraceQueueCapacity + 2U);
  for (std::size_t index = 1; index < recorded.size(); ++index) {
    EXPECT_GT(recorded[index].sequence, recorded[index - 1].sequence)
      << "the trace FIFO lost its order";
  }
  EXPECT_EQ(recorded.front().sequence, 100U);
  EXPECT_EQ(recorded.back().sequence, 600U);
  EXPECT_EQ(recorded.front().stamp_ns, 1000LL);
  EXPECT_EQ(recorded.back().stamp_ns, 4000LL);
  EXPECT_EQ(recorded[recorded.size() - 2U].sequence, 455U);
  const std::size_t rejected_count =
    std::count_if(recorded.begin(), recorded.end(),
                  [](const TraceEvent & event) { return event.sequence == 500U; });
  EXPECT_EQ(rejected_count, 0U) << "the rejected sequence was delivered";

  // The accounting is exact after the quiescence.
  const ObservationStats trace_stats = trace_endpoint->stats();
  const auto & trace_channel = trace_stats.channels[channelIndex(ObservationChannel::trace)];
  EXPECT_EQ(trace_channel.attempted,
            trace_channel.admitted + trace_channel.dropped_full +
              trace_channel.dropped_contended + trace_channel.dropped_closed +
              trace_channel.dropped_oversize + trace_channel.dropped_construction);
  EXPECT_EQ(trace_channel.delivered, trace_channel.admitted);
  EXPECT_EQ(trace_stats.last_assigned_trace_sequence, 600U);
  EXPECT_EQ(trace_stats.last_delivered_trace_sequence, 600U);

  // --- the results FIFO --------------------------------------------------
  auto results_gate = makeGate("results-capacity");
  auto result_recorder = std::make_shared<Recorder<CommandResultView>>();
  ObservationSinks result_sinks;
  result_sinks.results = [result_recorder, results_gate](const CommandResultView & view) {
    results_gate->wait();
    result_recorder->add(view);
  };
  auto result_endpoint = registerProducer(std::move(result_sinks));

  ASSERT_EQ(admitRetrying([&] { return result_endpoint->result(makeResult(1)); }),
            Admission::admitted);
  ASSERT_TRUE(awaitGateEntry(results_gate, 10s));
  for (std::size_t index = 0; index < kResultsQueueCapacity; ++index) {
    ASSERT_EQ(admitRetrying([&] { return result_endpoint->result(makeResult(index + 2)); }),
              Admission::admitted);
  }
  EXPECT_EQ(result_endpoint->stats().queued_items, kResultsQueueCapacity);
  EXPECT_EQ(admitRetrying([&] { return result_endpoint->result(makeResult(9999)); }),
            Admission::full);
  EXPECT_EQ(result_endpoint->stats().queued_items, kResultsQueueCapacity);
  EXPECT_EQ(result_endpoint->stats().channels[channelIndex(ObservationChannel::results)]
              .dropped_full,
            1U);
  results_gate->release();
  // The delivered counter is published after the sink returned: it is awaited
  // before the snapshot is taken.
  ASSERT_TRUE(waitFor(
    [&] {
      const ObservationStats stats = result_endpoint->stats();
      return stats.queued_items == 0U && stats.in_flight_items == 0U &&
             stats.channels[channelIndex(ObservationChannel::results)].delivered ==
               kResultsQueueCapacity + 1U;
    },
    20s));
  const ObservationStats result_snapshot = result_endpoint->stats();
  const auto & result_channel =
    result_snapshot.channels[channelIndex(ObservationChannel::results)];
  EXPECT_EQ(result_channel.delivered, result_channel.admitted);
  EXPECT_EQ(result_channel.attempted,
            result_channel.admitted + result_channel.dropped_full +
              result_channel.dropped_contended + result_channel.dropped_closed +
              result_channel.dropped_oversize + result_channel.dropped_construction);

  // --- the log FIFO ------------------------------------------------------
  auto log_gate = makeGate("log-capacity");
  auto log_recorder = std::make_shared<Recorder<LogRecord>>();
  ObservationSinks log_sinks;
  log_sinks.log = [log_recorder, log_gate](const std::string & level,
                                            const std::string & text) {
    log_gate->wait();
    log_recorder->add(LogRecord{level, text});
  };
  auto log_endpoint = registerProducer(std::move(log_sinks));

  ASSERT_EQ(admitRetrying([&] { return log_endpoint->log("INFO", "first"); }),
            Admission::admitted);
  ASSERT_TRUE(awaitGateEntry(log_gate, 10s));
  for (std::size_t index = 0; index < kLogQueueCapacity; ++index) {
    ASSERT_EQ(admitRetrying([&] { return log_endpoint->log("INFO", "queued"); }),
              Admission::admitted);
  }
  EXPECT_EQ(log_endpoint->stats().queued_items, kLogQueueCapacity);
  EXPECT_EQ(admitRetrying([&] { return log_endpoint->log("INFO", "rejected"); }),
            Admission::full);
  EXPECT_EQ(log_endpoint->stats().queued_items, kLogQueueCapacity);
  EXPECT_EQ(log_endpoint->stats().channels[channelIndex(ObservationChannel::log)]
              .dropped_full,
            1U);
  log_gate->release();
  // The delivered counter is published after the sink returned: it is awaited
  // before the snapshot is taken.
  ASSERT_TRUE(waitFor(
    [&] {
      const ObservationStats stats = log_endpoint->stats();
      return stats.queued_items == 0U && stats.in_flight_items == 0U &&
             stats.channels[channelIndex(ObservationChannel::log)].delivered ==
               kLogQueueCapacity + 1U;
    },
    20s));
  const ObservationStats log_snapshot = log_endpoint->stats();
  const auto & log_channel =
    log_snapshot.channels[channelIndex(ObservationChannel::log)];
  EXPECT_EQ(log_channel.delivered, log_channel.admitted);
  EXPECT_EQ(log_channel.attempted,
            log_channel.admitted + log_channel.dropped_full +
              log_channel.dropped_contended + log_channel.dropped_closed +
              log_channel.dropped_oversize + log_channel.dropped_construction);
}

// --- case 8 -----------------------------------------------------------------

// A trace sink that only blocks: the payload bound case must not retain the
// maximum sized payloads.
class GateTraceSink : public chaos::runtime::TraceSink {
public:
  explicit GateTraceSink(std::shared_ptr<Gate> gate) : gate_(std::move(gate)) {}

  void publish(const TraceEvent &) override { gate_->wait(); }

private:
  std::shared_ptr<Gate> gate_;
};

TEST_F(ObservationTest, PayloadBounds) {
  // Eight producers with 128 maximum sized events each reach the global
  // 4 MiB bound without any single producer reaching its own FIFO capacity.
  constexpr std::size_t kProducers = 8U;
  constexpr std::size_t kEventsPerProducer = 128U;
  static_assert(kProducers * kEventsPerProducer * kMaxEventStringBytes ==
                    kMaxAggregatePayloadBytes,
                "the payload distribution must reach the global bound exactly");
  static_assert(kEventsPerProducer < kTraceQueueCapacity,
                "a producer must stay below its own FIFO capacity");

  auto gate = makeGate("payload-gate");
  std::vector<std::shared_ptr<ObservationEndpoint>> endpoints;
  for (std::size_t index = 0; index < kProducers; ++index) {
    ObservationSinks sinks;
    if (index == 0U) {
      sinks.trace = std::make_shared<GateTraceSink>(gate);
    }
    endpoints.push_back(registerProducer(std::move(sinks)));
  }

  // The first event is delivered and blocks the single trace worker.
  const TraceEvent first = makeMaximumTraceEvent(1U);
  ASSERT_EQ(traceEventPayloadBytes(first), kMaxEventStringBytes);
  ASSERT_EQ(admitRetrying([&] { return endpoints[0]->trace(first); }),
            Admission::admitted);
  ASSERT_TRUE(awaitGateEntry(gate, 10s));

  for (std::size_t producer = 0; producer < kProducers; ++producer) {
    const std::size_t count =
      producer == 0U ? kEventsPerProducer - 1U : kEventsPerProducer;
    for (std::size_t item = 0; item < count; ++item) {
      const TraceEvent event =
        makeMaximumTraceEvent(1000U + producer * 1000U + item);
      ASSERT_EQ(traceEventPayloadBytes(event), kMaxEventStringBytes);
      ASSERT_EQ(admitRetrying([&] { return endpoints[producer]->trace(event); }),
                Admission::admitted)
        << "a producer was full before the global bound was reached";
    }
  }

  const ObservationServiceStats filled = service()->stats();
  EXPECT_EQ(filled.queued_bytes + filled.in_flight_bytes, kMaxAggregatePayloadBytes);
  EXPECT_LE(filled.payload_high_water_bytes, kMaxAggregatePayloadBytes);
  for (const auto & endpoint : endpoints) {
    const ObservationStats stats = endpoint->stats();
    EXPECT_LT(stats.queued_items, kTraceQueueCapacity);
  }

  // The next event does not fit into the global bound.
  EXPECT_EQ(admitRetrying([&] { return endpoints[0]->trace(makeMaximumTraceEvent(9999U)); }),
            Admission::full);
  const ObservationServiceStats after_full = service()->stats();
  EXPECT_EQ(after_full.queued_bytes + after_full.in_flight_bytes,
            kMaxAggregatePayloadBytes);
  EXPECT_LE(after_full.payload_high_water_bytes, kMaxAggregatePayloadBytes);
  EXPECT_EQ(endpoints[0]->stats().channels[channelIndex(ObservationChannel::trace)]
              .dropped_full,
            1U);

  // A payload above the per event bound is rejected, also when the oversized
  // string is nested in the adapter snapshot of a state event.
  const ObservationServiceStats before_oversize = service()->stats();
  TraceEvent oversized = makeMaximumTraceEvent(2U);
  oversized.frame.assign(kMaxEventStringBytes + 1U, 'x');
  EXPECT_EQ(endpoints[0]->trace(oversized), Admission::oversize);
  EXPECT_EQ(endpoints[0]->stats().channels[channelIndex(ObservationChannel::trace)]
              .dropped_oversize,
            1U);

  StateView oversized_state = makeState(1000);
  oversized_state.snapshot.detail.assign(kMaxEventStringBytes + 1U, 'y');
  EXPECT_EQ(endpoints[0]->state(oversized_state), Admission::oversize);
  EXPECT_EQ(endpoints[0]->stats().channels[channelIndex(ObservationChannel::state)]
              .dropped_oversize,
            1U);

  // The rejections take no ownership: the queued state is unchanged.
  const ObservationServiceStats after_oversize = service()->stats();
  EXPECT_EQ(after_oversize.queued_bytes, before_oversize.queued_bytes);
  EXPECT_EQ(after_oversize.queued_items, before_oversize.queued_items);
  EXPECT_EQ(after_oversize.in_flight_bytes, before_oversize.in_flight_bytes);
  EXPECT_EQ(after_oversize.in_flight_items, before_oversize.in_flight_items);
  EXPECT_LE(after_oversize.payload_high_water_bytes, kMaxAggregatePayloadBytes);

  // The bound is released when the payloads are delivered.
  gate->release();
  ASSERT_TRUE(waitFor(
    [&] {
      const ObservationServiceStats stats = service()->stats();
      return stats.queued_items == 0U && stats.in_flight_items == 0U &&
             stats.queued_bytes == 0U && stats.in_flight_bytes == 0U;
    },
    30s));
}

// --- case 9 -----------------------------------------------------------------

TEST_F(ObservationTest, StateCoalescesLatest) {
  auto gate = makeGate("state-gate");
  auto recorder = std::make_shared<Recorder<StateView>>();
  ObservationSinks sinks;
  sinks.state = [recorder, gate](const StateView & view) {
    gate->wait();
    recorder->add(view);
  };
  auto endpoint = registerProducer(std::move(sinks));

  // The first state is delivered and blocks the state worker.
  ASSERT_EQ(admitRetrying([&] { return endpoint->state(makeState(1000)); }),
            Admission::admitted);
  ASSERT_TRUE(awaitGateEntry(gate, 10s));

  // The pending updates replace each other.
  ASSERT_EQ(admitRetrying([&] { return endpoint->state(makeState(2000)); }),
            Admission::admitted);
  ASSERT_EQ(admitRetrying([&] { return endpoint->state(makeState(3000)); }),
            Admission::coalesced);
  ASSERT_EQ(admitRetrying([&] { return endpoint->state(makeState(4000)); }),
            Admission::coalesced);

  // Exactly one state is pending while the first one is in flight.
  const ObservationStats pending = endpoint->stats();
  EXPECT_EQ(pending.queued_items, 1U);
  EXPECT_EQ(pending.in_flight_items, 1U);

  gate->release();
  // The delivered counter is published after the sink returned: it is awaited
  // before the snapshot is taken.
  ASSERT_TRUE(waitFor(
    [&] {
      const ObservationStats stats = endpoint->stats();
      return stats.queued_items == 0U && stats.in_flight_items == 0U &&
             stats.channels[channelIndex(ObservationChannel::state)].delivered == 2U;
    },
    10s));

  // The recording holds the in flight value and the latest pending value.
  const std::vector<StateView> recorded = recorder->snapshot();
  ASSERT_EQ(recorded.size(), 2U);
  EXPECT_EQ(recorded[0].stamp_ns, 1000LL);
  EXPECT_EQ(recorded[1].stamp_ns, 4000LL);

  const ObservationStats stats = endpoint->stats();
  const auto & state_channel = stats.channels[channelIndex(ObservationChannel::state)];
  // Four attempts: two admitted and two coalesced. A coalesced attempt counts
  // as admitted, so the admitted counter holds all four of them.
  EXPECT_GE(state_channel.attempted, 4U);
  EXPECT_EQ(state_channel.admitted, 4U);
  EXPECT_EQ(state_channel.coalesced, 2U);
  EXPECT_EQ(state_channel.delivered, 2U);
  EXPECT_EQ(state_channel.sink_exceptions, 0U);
  EXPECT_EQ(state_channel.attempted,
            state_channel.admitted + state_channel.dropped_full +
              state_channel.dropped_contended + state_channel.dropped_closed +
              state_channel.dropped_oversize + state_channel.dropped_construction);
}

// --- case 10 ----------------------------------------------------------------

// A callable whose final destruction waits on a gate. The switch is armed only
// after the registration, so the copies of the registration itself never block.
class BlockingDtorCallable {
public:
  BlockingDtorCallable(std::shared_ptr<Gate> gate,
                       std::shared_ptr<std::atomic<bool>> armed)
      : gate_(std::move(gate)), armed_(std::move(armed)) {}

  BlockingDtorCallable(const BlockingDtorCallable & other)
      : gate_(other.gate_), armed_(other.armed_) {}

  BlockingDtorCallable(BlockingDtorCallable && other) noexcept
      : gate_(std::move(other.gate_)), armed_(std::move(other.armed_)) {}

  void operator()(const CommandResultView &) const {}

  ~BlockingDtorCallable() {
    if (armed_ && armed_->load()) {
      gate_->wait();
    }
  }

private:
  std::shared_ptr<Gate> gate_;
  std::shared_ptr<std::atomic<bool>> armed_;
};

TEST_F(ObservationTest, RetirementAndSlotOwnership) {
  auto dtor_gate = makeGate("sink-destructor");
  auto armed = std::make_shared<std::atomic<bool>>(false);
  auto recorder = std::make_shared<Recorder<TraceEvent>>();

  // Every slot is taken. The first producer owns the callable whose final
  // destruction blocks; the other producers own a recording trace sink.
  std::vector<std::shared_ptr<ObservationEndpoint>> endpoints;
  for (std::size_t index = 0; index < kProducerSlots; ++index) {
    ObservationSinks sinks;
    if (index == 0U) {
      sinks.results = BlockingDtorCallable{dtor_gate, armed};
    } else {
      sinks.trace = std::make_shared<RecordingTraceSink>(recorder, nullptr);
    }
    endpoints.push_back(registerProducer(std::move(sinks)));
  }
  ASSERT_EQ(service()->stats().slots_used, kProducerSlots);

  // The external owners of the sink bundle are released: the service owns the
  // callable alone. The switch is armed now. The close runs on a fixture
  // owned helper: the retirement of the callable blocks the results worker,
  // so the close itself must never keep the test thread waiting.
  armed->store(true);
  const GuardedOutcome close_outcome =
    runGuarded("endpoint close", [&] { endpoints[0]->close(); return true; }, 10s);
  EXPECT_TRUE(close_outcome.completed)
    << "the endpoint close did not return while the retirement was blocked";
  ASSERT_TRUE(awaitGateEntry(dtor_gate, 15s))
    << "the retirement never reached the blocking destructor";

  // The slot stays occupied and the channel is busy with the retirement.
  const ObservationServiceStats during = service()->stats();
  EXPECT_EQ(during.slots_used, kProducerSlots);
  EXPECT_EQ(during.retiring_producers, 1U);
  EXPECT_TRUE(during.worker_busy[channelIndex(ObservationChannel::results)]);
  EXPECT_FALSE(service()->joined());

  // Another channel keeps working while the retirement is blocked.
  ASSERT_EQ(admitRetrying([&] { return endpoints[1]->trace(makeTraceEvent(1, 1)); }),
            Admission::admitted);
  ASSERT_TRUE(waitFor([&] { return recorder->size() == 1U; }, 10s));

  // A further registration is refused while every slot is taken.
  EXPECT_THROW(service()->registerProducer(ObservationSinks{}), std::runtime_error);

  // The destruction of the endpoint of the blocked producer returns while the
  // retirement is blocked. The fixture owner is released first, so the local
  // reference is the last owner and the destruction really runs. The
  // destruction runs on a fixture owned helper, so the test thread never
  // waits for the blocking destructor.
  releaseEndpointOwner(endpoints[0]);
  const GuardedOutcome destroy_outcome =
    runGuarded("endpoint destruction", [&] { endpoints[0].reset(); return true; }, 10s);
  EXPECT_TRUE(destroy_outcome.completed)
    << "the endpoint destruction did not return while the retirement was blocked";
  EXPECT_EQ(endpoints[0], nullptr);

  // Releasing the destructor lets the retirement finish and the slot returns.
  dtor_gate->release();
  ASSERT_TRUE(waitFor(
    [&] {
      const ObservationServiceStats stats = service()->stats();
      return stats.retiring_producers == 0U && stats.slots_used < kProducerSlots;
    },
    15s));

  // The replacement takes a free slot.
  auto replacement_recorder = std::make_shared<Recorder<TraceEvent>>();
  ObservationSinks replacement_sinks;
  replacement_sinks.trace =
    std::make_shared<RecordingTraceSink>(replacement_recorder, nullptr);
  auto replacement_endpoint = registerProducer(std::move(replacement_sinks));
  EXPECT_NE(replacement_endpoint, nullptr);

  // The old endpoint cannot publish into the replacement and cannot close it.
  // A closed endpoint of a retired registration is rejected before it ever
  // reaches the registration id validation of the reused slot.
  const auto old_endpoint = replacement_endpoint;
  old_endpoint->close();
  ASSERT_TRUE(waitFor(
    [&] { return service()->stats().slots_used == kProducerSlots - 1U; }, 10s));
  // The other fifteen slots remain occupied, so this reuses the same slot.
  replacement_sinks.trace =
    std::make_shared<RecordingTraceSink>(replacement_recorder, nullptr);
  replacement_endpoint = registerProducer(std::move(replacement_sinks));
  old_endpoint->close();
  EXPECT_EQ(old_endpoint->trace(makeTraceEvent(99, 99)), Admission::closed);
  EXPECT_FALSE(replacement_endpoint->stats().closed);
  EXPECT_EQ(admitRetrying([&] { return replacement_endpoint->trace(makeTraceEvent(100, 100)); }),
            Admission::admitted);
  ASSERT_TRUE(waitFor([&] { return replacement_recorder->size() == 1U; }, 10s));
  EXPECT_EQ(replacement_recorder->size(), 1U);
}

// --- case 11 ----------------------------------------------------------------

TEST_F(ObservationTest, ConcurrentLifecycle) {
  auto gate = makeGate("join-gate");
  auto recorder = std::make_shared<Recorder<TraceEvent>>();
  ObservationSinks sinks;
  sinks.trace = std::make_shared<RecordingTraceSink>(recorder, gate);
  auto endpoint = registerProducer(std::move(sinks));

  ASSERT_EQ(admitRetrying([&] { return endpoint->trace(makeTraceEvent(1, 1)); }),
            Admission::admitted);
  ASSERT_TRUE(awaitGateEntry(gate, 10s));

  // A synchronized close and admission race. The allowed outcome is a
  // pre-close admission that drains or a rejection. The race state is heap
  // owned: the auxiliary threads never touch a stack local of the test body,
  // not even when an assertion aborts the test body first.
  auto race = std::make_shared<CloseAdmissionRace>();
  race->barrier = makeBarrier("close-race");
  race->endpoint = endpoint;
  GuardedCall & closer = spawnHelper("close racer", [race] {
    race->barrier->wait();
    race->endpoint->close();
    return true;
  });
  GuardedCall & admitter = spawnHelper("admission racer", [race] {
    race->barrier->wait();
    race->setOutcome(race->endpoint->trace(makeTraceEvent(2, 2)));
    return true;
  });
  race->barrier->open();
  ASSERT_TRUE(closer.waitFor(10s));
  ASSERT_TRUE(admitter.waitFor(10s));
  Admission race_outcome = race->result();
  if (race_outcome == Admission::contended) {
    // A contended drop is legitimate: the retry observes the closed producer.
    race_outcome =
      admitRetrying([&] { return endpoint->trace(makeTraceEvent(2, 2)); });
  }
  EXPECT_TRUE(race_outcome == Admission::admitted ||
              race_outcome == Admission::closed)
    << "the close/admission race returned " << admissionName(race_outcome);

  // Two concurrent join callers while the sink is still blocked. The service
  // is captured by value: a helper never reads a stack local.
  service()->requestStop();
  const std::shared_ptr<ObservationService> service_ref = service();
  GuardedCall & joiner_a =
    spawnHelper("joiner A", [service_ref] { return service_ref->joinFor(200ms); });
  GuardedCall & joiner_b =
    spawnHelper("joiner B", [service_ref] { return service_ref->joinFor(200ms); });
  ASSERT_TRUE(joiner_a.waitFor(20s));
  ASSERT_TRUE(joiner_b.waitFor(20s));
  EXPECT_FALSE(joiner_a.result()) << "the first join caller did not time out";
  EXPECT_FALSE(joiner_b.result()) << "the second join caller did not time out";
  EXPECT_FALSE(service()->joined());

  // The generation is retained and a registration is refused.
  EXPECT_EQ(ObservationService::process().get(), service().get());
  EXPECT_THROW(service()->registerProducer(ObservationSinks{}), std::runtime_error);

  // After the release the concurrent join callers succeed.
  gate->release();
  GuardedCall & joiner_c =
    spawnHelper("joiner C", [service_ref] { return service_ref->joinFor(15s); });
  GuardedCall & joiner_d =
    spawnHelper("joiner D", [service_ref] { return service_ref->joinFor(15s); });
  ASSERT_TRUE(joiner_c.waitFor(30s));
  ASSERT_TRUE(joiner_d.waitFor(30s));
  EXPECT_TRUE(joiner_c.result());
  EXPECT_TRUE(joiner_d.result());
  EXPECT_TRUE(service()->joined());
  EXPECT_EQ(service()->stats().slots_used, 0U)
    << "a slot survived the successful join";
  // A pre-close admission drains, a rejection leaves nothing behind.
  EXPECT_EQ(recorder->size(), race_outcome == Admission::admitted ? 2U : 1U);

  // Two simultaneous process callers receive the same single new generation.
  // Every acquired generation is registered with the fixture by the acquiring
  // thread, so a generation is never lost when an assertion aborts the test
  // body before the generations could be handed over.
  auto process_race = std::make_shared<ProcessRace>();
  process_race->barrier = makeBarrier("process-race");
  GuardedCall & process_a = spawnHelper("process A", [process_race, this] {
    process_race->barrier->wait();
    const std::shared_ptr<ObservationService> generation =
      ObservationService::process();
    process_race->setFirst(generation);
    trackGeneration(generation);
    return true;
  });
  GuardedCall & process_b = spawnHelper("process B", [process_race, this] {
    process_race->barrier->wait();
    const std::shared_ptr<ObservationService> generation =
      ObservationService::process();
    process_race->setSecond(generation);
    trackGeneration(generation);
    return true;
  });
  process_race->barrier->open();
  ASSERT_TRUE(process_a.waitFor(20s));
  ASSERT_TRUE(process_b.waitFor(20s));
  const std::shared_ptr<ObservationService> first_generation =
    process_race->firstGeneration();
  const std::shared_ptr<ObservationService> second_generation =
    process_race->secondGeneration();
  ASSERT_NE(first_generation, nullptr);
  ASSERT_NE(second_generation, nullptr);
  EXPECT_EQ(first_generation.get(), second_generation.get());
  EXPECT_NE(first_generation.get(), service().get());

  // The new generation is healthy and is stopped and joined explicitly.
  auto new_recorder = std::make_shared<Recorder<TraceEvent>>();
  ObservationSinks new_sinks;
  new_sinks.trace = std::make_shared<RecordingTraceSink>(new_recorder, nullptr);
  auto new_endpoint = first_generation->registerProducer(std::move(new_sinks));
  EXPECT_EQ(admitRetrying([&] { return new_endpoint->trace(makeTraceEvent(1, 1)); }),
            Admission::admitted);
  ASSERT_TRUE(waitFor([&] { return new_recorder->size() == 1U; }, 10s));
  first_generation->requestStop();
  EXPECT_TRUE(first_generation->joinFor(15s));
}

// --- case 12 ----------------------------------------------------------------

// A thread local object whose destructor announces its entry and then waits
// for the release. It is initialized on the worker thread by the sink, so it
// is destroyed at the real thread exit.
struct ThreadLocalBlocker {
  std::shared_ptr<Gate> gate;
  ~ThreadLocalBlocker() {
    if (gate) {
      gate->wait();
    }
  }
};

void initializeThreadLocal(const std::shared_ptr<Gate> & gate) {
  thread_local ThreadLocalBlocker blocker{gate};
  (void)blocker;
}

class ThreadLocalTraceSink : public chaos::runtime::TraceSink {
public:
  explicit ThreadLocalTraceSink(std::shared_ptr<Gate> gate)
      : gate_(std::move(gate)) {}

  void publish(const TraceEvent &) override { initializeThreadLocal(gate_); }

private:
  std::shared_ptr<Gate> gate_;
};

TEST_F(ObservationTest, ThreadLocalExitDeadline) {
  auto gate = makeGate("thread-local-exit");
  ObservationSinks sinks;
  sinks.trace = std::make_shared<ThreadLocalTraceSink>(gate);
  auto endpoint = registerProducer(std::move(sinks));

  // The delivery initializes the thread local on the worker thread.
  ASSERT_EQ(admitRetrying([&] { return endpoint->trace(makeTraceEvent(1, 1)); }),
            Admission::admitted);
  ASSERT_TRUE(waitFor(
    [&] {
      return endpoint->stats().channels[channelIndex(ObservationChannel::trace)]
                 .delivered == 1U;
    },
    10s));

  service()->requestStop();

  // The worker exits and the thread local destructor blocks.
  ASSERT_TRUE(awaitGateEntry(gate, 20s))
    << "the thread local destructor never ran at the thread exit";

  // A short join must return false by its deadline: the thread exit is not
  // finished while the thread local cleanup is blocked.
  const GuardedOutcome short_join =
    runGuarded("short joinFor", [&] { return service()->joinFor(200ms); }, 20s);
  ASSERT_TRUE(short_join.completed)
    << "the short joinFor did not return by its deadline";
  EXPECT_FALSE(short_join.value)
    << "the join entered an unbounded wait while the thread local cleanup was "
       "blocked";
  EXPECT_FALSE(service()->joined());

  // The generation is retained while it is not joined.
  EXPECT_EQ(ObservationService::process().get(), service().get());

  // Releasing the thread local cleanup lets the join succeed.
  gate->release();
  const GuardedOutcome late_join =
    runGuarded("late joinFor", [&] { return service()->joinFor(15s); }, 30s);
  ASSERT_TRUE(late_join.completed) << "the late joinFor did not return";
  EXPECT_TRUE(late_join.value);
  EXPECT_TRUE(service()->joined());

  // Only now a fresh generation is allowed.
  auto fresh = ObservationService::process();
  ASSERT_NE(fresh.get(), service().get());
  trackGeneration(fresh);
  fresh->requestStop();
  EXPECT_TRUE(fresh->joinFor(15s));
}

}  // namespace
