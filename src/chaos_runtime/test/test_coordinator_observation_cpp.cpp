// Coordinator and observation contract: the real control coordinator bound to
// the real process wide observation service.
//
// The coordinator and the observation service are the production ones. Every
// observation travels through the production endpoint, the production per
// producer queues and the production channel workers of
// chaos::runtime::ObservationService. Only the adapter of the test is fake.
//
// WHAT IS PROVED
//   * A blocked channel worker never blocks the control path. An explicit
//     stop and a velocity TTL expiry both return and issue their stop output
//     while the worker of the selected channel sits in a user callback.
//   * A callback capture owns its own state. The capture survives the
//     destruction of the coordinator and of the endpoint, and a callback may
//     reenter the coordinator without deadlocking on the control mutex.
//   * The issued velocity trace carries the interval the driver will really
//     honour and the stamps of the issue time. The delivery never re-stamps
//     the event and never re-computes the interval.
//
// HARNESS CONTRACT
//   * The fixture owns the service, the endpoints, the coordinators, the
//     adapters, the gates and every recording buffer.
//   * The cleanup order is fixed and runs on every exit path, also when an
//     ASSERT aborts the test body: the gates are opened first, the
//     coordinators are destroyed and the endpoints are closed next, and only
//     then the service is stopped and joined against a bounded deadline.
//   * Every wait is a bounded predicate wait. The two second gate guard is a
//     deadlock guard, never a proof: a gate that expires on its own fails
//     the test.
//   * Only the bootstrap and the gate setup may retry a contended admission.
//     A control command is never replayed to manufacture an expected output.
//
// The suite has six cases: the four channels of the parameterized case and
// the two focused cases. It does not expand the matrix.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
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

using namespace std::chrono_literals;

// --- injected clocks --------------------------------------------------------

// The clock domains of the runtime. Every domain is a separate atomic and
// the test advances them explicitly: no test ever waits for wall time to
// order two events.
struct FakeClock {
  std::atomic<double> steady_s{100.0};
  std::atomic<std::int64_t> mono_ns{100'000'000'000LL};
  std::atomic<std::int64_t> driver_mono_ns{100'000'000'000LL};
  std::atomic<std::int64_t> wall_ns{1'700'000'000'000'000'000LL};
  std::atomic<std::int64_t> ros_ns{1'700'000'000'000'000'000LL};

  // Advances every clock domain.
  void advance(double seconds) {
    const std::int64_t delta = static_cast<std::int64_t>(seconds * 1e9);
    steady_s.store(steady_s.load() + seconds);
    mono_ns.store(mono_ns.load() + delta);
    driver_mono_ns.store(driver_mono_ns.load() + delta);
    wall_ns.store(wall_ns.load() + delta);
    ros_ns.store(ros_ns.load() + delta);
  }

  // Advances the two monotonic domains only: the ROS stamp of a command and
  // the steady watchdog clock stay frozen while the transport interval is
  // consumed.
  void advanceMonotonicOnly(double seconds) {
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
  asset.frames.base = "base_link";
  asset.frames.odom = "odom";
  asset.frames.map = "map";
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

// --- recording buffers ------------------------------------------------------

// A mutex and condition variable protected recording of the delivered
// payloads. Only the workers write into it; the test thread reads snapshots
// and waits for a count.
template <typename Value>
class Recorder {
public:
  void add(Value value) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      values_.push_back(std::move(value));
    }
    cv_.notify_all();
  }

  std::vector<Value> snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return values_;
  }

  bool waitForCount(std::size_t count, std::chrono::milliseconds bound) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, bound,
                         [this, count] { return values_.size() >= count; });
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<Value> values_;
};

using LogRecord = std::pair<std::string, std::string>;

// --- the release gate -------------------------------------------------------

// A one shot release gate with a bounded safety guard.
//
// The waiter announces its entry and then blocks until the gate is released.
// The safety bound is a deadlock guard, never a proof of progress: a gate
// that expires on its own is reported as a failure by the test.
class Gate {
public:
  void wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    entered_ = true;
    cv_.notify_all();
    if (!cv_.wait_for(lock, kSafetyBound, [this] { return open_; })) {
      expired_ = true;
    }
  }

  bool waitForEntry(std::chrono::milliseconds bound) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, bound, [this] { return entered_; });
  }

  void release() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      open_ = true;
    }
    cv_.notify_all();
  }

  bool entered() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entered_;
  }

  // The gate is held while the waiter is inside the callback, the gate was not
  // released yet and the safety guard never expired. An expired gate is never
  // a held gate: the delivery was not blocked by the test.
  bool held() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entered_ && !open_ && !expired_;
  }

  bool expired() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return expired_;
  }

private:
  static constexpr std::chrono::milliseconds kSafetyBound{2000};

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  bool entered_ = false;
  bool open_ = false;
  bool expired_ = false;
};

// --- the transport recorder -------------------------------------------------

// One deadline carrying dispatch of the coordinator.
struct AdapterDispatch {
  std::array<double, 6> values{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
  std::int64_t source_stamp_ns = 0;
  std::int64_t deadline_ns = 0;
  std::int64_t dispatch_now_ns = 0;
};

// The shared record of what the transport really received.
//
// The adapter is owned by the coordinator, so the recorder is shared: the
// test reads the counters and the dispatch log while the coordinator owns the
// adapter. Every hook of the adapter belongs to this record.
struct AdapterRecorder {
  std::atomic<int> validate_calls{0};
  std::atomic<int> stop_calls{0};
  std::atomic<int> controlled_stop_calls{0};
  std::atomic<int> zero_outputs{0};
  mutable std::mutex mutex;
  std::vector<AdapterDispatch> dispatches;

  void record(const AdapterDispatch & dispatch) {
    std::lock_guard<std::mutex> lock(mutex);
    dispatches.push_back(dispatch);
  }

  std::vector<AdapterDispatch> dispatchLog() const {
    std::lock_guard<std::mutex> lock(mutex);
    return dispatches;
  }

  // A stop output is counted only when it really zeroed the commanded
  // velocity.
  void noteStopOutput(const std::array<double, 6> & velocity) {
    for (double value : velocity) {
      if (value != 0.0) {
        return;
      }
    }
    zero_outputs.fetch_add(1);
  }
};

// The mock adapter with the transport hooks of the test: the deadline
// carrying dispatch is recorded, the validation delay is injected and the
// watchdog update can fail.
class DeadlineRecordingAdapter : public MockAdapter {
public:
  DeadlineRecordingAdapter(AssetConfig asset,
                            std::shared_ptr<AdapterRecorder> recorder,
                            std::function<std::int64_t()> driver_now_ns)
      : MockAdapter(std::move(asset)),
        recorder_(std::move(recorder)),
        driver_now_ns_(std::move(driver_now_ns)) {}

  // The deterministic validation delay of the command. It runs under the
  // control lock and models the time the transport spends before the
  // dispatch.
  std::function<void()> on_validate;
  // The trigger adapter fails in update(): the production watchdog then logs
  // the driver error.
  bool fail_update = false;

  void validateVelocity(const std::array<double, 6> & values) override {
    if (!dispatching_) {
      recorder_->validate_calls.fetch_add(1);
      if (on_validate) {
        on_validate();
      }
    }
    MockAdapter::validateVelocity(values);
  }

  void setVelocityUntil(const std::array<double, 6> & values,
                        std::int64_t source_stamp_ns,
                        std::int64_t expires_mono_ns) override {
    AdapterDispatch dispatch;
    dispatch.values = values;
    dispatch.source_stamp_ns = source_stamp_ns;
    dispatch.deadline_ns = expires_mono_ns;
    dispatch.dispatch_now_ns = driver_now_ns_();
    recorder_->record(dispatch);
    // The mock revalidates the sample: the delay hook belongs to the
    // admission of the command, not to the dispatch.
    dispatching_ = true;
    MockAdapter::setVelocity(values);
    dispatching_ = false;
  }

  void stop() override {
    recorder_->stop_calls.fetch_add(1);
    MockAdapter::stop();
    recorder_->noteStopOutput(snapshot().velocity);
  }

  void controlledStop() override {
    recorder_->controlled_stop_calls.fetch_add(1);
    MockAdapter::stop();
    recorder_->noteStopOutput(snapshot().velocity);
  }

  void update(double dt) override {
    if (fail_update) {
      throw std::runtime_error("trigger adapter update failed");
    }
    MockAdapter::update(dt);
  }

private:
  std::shared_ptr<AdapterRecorder> recorder_;
  std::function<std::int64_t()> driver_now_ns_;
  bool dispatching_ = false;
};

// --- the trace sinks --------------------------------------------------------

// The trace sink that records the delivered events.
class RecorderTraceSink : public TraceSink {
public:
  explicit RecorderTraceSink(std::shared_ptr<Recorder<TraceEvent>> recorder)
      : recorder_(std::move(recorder)) {}

  void publish(const TraceEvent & event) override { recorder_->add(event); }

private:
  std::shared_ptr<Recorder<TraceEvent>> recorder_;
};

// The trace sink that blocks its channel worker in the gate.
class GatedTraceSink : public TraceSink {
public:
  explicit GatedTraceSink(std::shared_ptr<Gate> gate)
      : gate_(std::move(gate)) {}

  void publish(const TraceEvent &) override { gate_->wait(); }

private:
  std::shared_ptr<Gate> gate_;
};

// --- the bootstrap ----------------------------------------------------------

// Sends one typed bootstrap event of the selected channel into the real
// service.
//
// Only a contended admission is retried: the admission uses a try_lock, so a
// busy registry lock really drops the attempt. Every other outcome is
// reported unchanged and is never retried away.
bool bootstrapChannel(ObservationChannel channel,
                      const std::shared_ptr<ObservationEndpoint> & endpoint) {
  std::function<Admission()> attempt;
  switch (channel) {
    case ObservationChannel::trace: {
      TraceEvent event;
      event.sequence = 1;
      event.stamp_ns = 1;
      event.monotonic_ns = 2;
      event.frame = "base_link";
      event.asset_id = "mock-ugv";
      event.command = "bootstrap";
      event.stage = "bootstrap";
      event.code = "OK";
      attempt = [endpoint, event] { return endpoint->trace(event); };
      break;
    }
    case ObservationChannel::results: {
      CommandResultView view;
      view.stamp_ns = 1;
      view.request_id = "bootstrap";
      view.success = true;
      view.code = "OK";
      view.message = "bootstrap";
      attempt = [endpoint, view] { return endpoint->result(view); };
      break;
    }
    case ObservationChannel::state: {
      StateView view;
      view.stamp_ns = 1;
      view.asset_id = "mock-ugv";
      view.model = "mock-model";
      view.connected = true;
      view.ready = true;
      view.operational_state = "bootstrap";
      attempt = [endpoint, view] { return endpoint->state(view); };
      break;
    }
    case ObservationChannel::log:
      attempt = [endpoint] { return endpoint->log("info", "bootstrap"); };
      break;
  }
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  Admission outcome = Admission::contended;
  while (std::chrono::steady_clock::now() < deadline) {
    outcome = attempt();
    if (outcome != Admission::contended) {
      break;
    }
    std::this_thread::yield();
  }
  return outcome == Admission::admitted || outcome == Admission::coalesced;
}

// --- the fixture ------------------------------------------------------------

// The shared fixture. It owns the service, the endpoints, the coordinators,
// the gates and every recording buffer.
class CoordinatorObservationTest : public ::testing::Test {
protected:
  void SetUp() override {
    std::shared_ptr<ObservationService> service = ObservationService::process();
    if (service->stats().stop_requested) {
      // A leftover stopping generation of an earlier (failed) test: retire it
      // before a fresh generation is acquired.
      service->requestStop();
      EXPECT_TRUE(service->joinFor(10s));
      service = ObservationService::process();
    }
    service_ = std::move(service);
  }

  void TearDown() override { cleanup(); }
  ~CoordinatorObservationTest() override { cleanup(); }

  // The cleanup order is fixed: the gates are opened first, so a blocked
  // worker returns; then the coordinators are destroyed and the endpoints are
  // closed; only then the service is stopped and joined against a bounded
  // deadline.
  void cleanup() {
    if (cleaned_) {
      return;
    }
    cleaned_ = true;
    for (const std::shared_ptr<Gate> & gate : gates_) {
      gate->release();
    }
    subject_.reset();
    trigger_.reset();
    for (const std::shared_ptr<ObservationEndpoint> & endpoint : endpoints_) {
      endpoint->close();
    }
    if (service_) {
      service_->requestStop();
      EXPECT_TRUE(service_->joinFor(15s))
          << "the observation service did not join in the cleanup";
    }
  }

  std::shared_ptr<Gate> makeGate() {
    auto gate = std::make_shared<Gate>();
    gates_.push_back(gate);
    return gate;
  }

  std::shared_ptr<ObservationEndpoint> registerProducer(ObservationSinks sinks) {
    std::shared_ptr<ObservationEndpoint> endpoint =
      service_->registerProducer(std::move(sinks));
    endpoints_.push_back(endpoint);
    return endpoint;
  }

  // The healthy subject: a real coordinator with its own producer on the real
  // service.
  void buildSubject(std::unique_ptr<Adapter> adapter, ObservationSinks sinks) {
    const AssetConfig asset = mockAsset();
    subject_endpoint_ = registerProducer(std::move(sinks));
    subject_ = std::make_shared<ControlCoordinator>(
      asset, std::move(adapter), makeClocks(), subject_endpoint_);
  }

  // The trigger coordinator: a separate producer whose adapter fails in the
  // watchdog update, so the production watchdog logs the driver error. Its
  // fault never reaches the healthy subject.
  void buildTrigger(ObservationSinks sinks) {
    auto adapter = std::make_unique<DeadlineRecordingAdapter>(
      mockAsset(), std::make_shared<AdapterRecorder>(), driverNow());
    adapter->fail_update = true;
    const AssetConfig asset = mockAsset();
    trigger_endpoint_ = registerProducer(std::move(sinks));
    trigger_ = std::make_shared<ControlCoordinator>(
      asset, std::move(adapter), makeClocks(), trigger_endpoint_);
  }

  // The sinks of the healthy subject: every channel is recorded.
  ObservationSinks recordingSinks() {
    ObservationSinks sinks;
    sinks.trace = std::make_shared<RecorderTraceSink>(traces_);
    sinks.results = [recorder = results_](const CommandResultView & view) {
      recorder->add(view);
    };
    sinks.state = [recorder = states_](const StateView & view) {
      recorder->add(view);
    };
    sinks.log = [recorder = logs_](const std::string & level,
                                   const std::string & text) {
      recorder->add(LogRecord{level, text});
    };
    return sinks;
  }

  Clocks makeClocks() const {
    const std::shared_ptr<FakeClock> clock = clock_;
    Clocks clocks;
    clocks.steady_s = [clock] { return clock->steady_s.load(); };
    clocks.mono_ns = [clock] { return clock->mono_ns.load(); };
    clocks.wall_ns = [clock] { return clock->wall_ns.load(); };
    clocks.ros_ns = [clock] { return clock->ros_ns.load(); };
    clocks.driver_mono_ns = [clock] { return clock->driver_mono_ns.load(); };
    return clocks;
  }

  // The driver deadline clock of the adapter. The fake clock is owned by the
  // shared handle, so the adapter never borrows the fixture.
  std::function<std::int64_t()> driverNow() const {
    const std::shared_ptr<FakeClock> clock = clock_;
    return [clock] { return clock->driver_mono_ns.load(); };
  }

  // Acquires a control lease on the healthy subject.
  std::string acquireLease() {
    LeaseRequestView request;
    request.operation = "acquire";
    request.owner = "observation-test";
    request.duration_s = 5.0;
    const LeaseResponseView response = subject_->lease(request);
    return response.success ? response.token : std::string();
  }

  // A velocity command with an explicit source stamp, so the original integer
  // sec/nsec pair is never lost.
  VelocityRequestView velocityRequest(const std::string & token, double ttl,
                                      std::int64_t source_ns) {
    VelocityRequestView request;
    request.stamp_sec = source_ns / 1'000'000'000;
    request.stamp_nanosec = source_ns % 1'000'000'000;
    request.frame_id = "base_link";
    request.lease_token = token;
    request.request_id = "req-velocity";
    request.valid_for_s = ttl;
    request.velocity = {0.5, 0.0, 0.0, 0.0, 0.0, 0.0};
    return request;
  }

  std::shared_ptr<ObservationService> service_;
  std::shared_ptr<FakeClock> clock_ = std::make_shared<FakeClock>();
  std::shared_ptr<Recorder<TraceEvent>> traces_ =
    std::make_shared<Recorder<TraceEvent>>();
  std::shared_ptr<Recorder<CommandResultView>> results_ =
    std::make_shared<Recorder<CommandResultView>>();
  std::shared_ptr<Recorder<StateView>> states_ =
    std::make_shared<Recorder<StateView>>();
  std::shared_ptr<Recorder<LogRecord>> logs_ =
    std::make_shared<Recorder<LogRecord>>();
  std::vector<std::shared_ptr<Gate>> gates_;
  std::vector<std::shared_ptr<ObservationEndpoint>> endpoints_;
  std::shared_ptr<ObservationEndpoint> subject_endpoint_;
  std::shared_ptr<ObservationEndpoint> trigger_endpoint_;
  std::shared_ptr<ControlCoordinator> subject_;
  std::shared_ptr<ControlCoordinator> trigger_;
  bool cleaned_ = false;
};

// --- the parameterized case -------------------------------------------------

class CoordinatorObservationChannelTest
  : public CoordinatorObservationTest,
    public ::testing::WithParamInterface<ObservationChannel> {};

// A blocked channel worker never blocks the control path.
//
// The gate blocks the worker of the selected channel of the real service.
// While the gate is held, the healthy subject completes an explicit stop and
// completes a velocity TTL expiry: both issue their stop output. The
// assertions are unconditional and read the transport recorder and the stop
// response, never an observation of the blocked channel.
TEST_P(CoordinatorObservationChannelTest,
       ControlPathCompletesWhileTheChannelWorkerIsBlocked) {
  const ObservationChannel channel = GetParam();
  const std::shared_ptr<Gate> gate = makeGate();
  const std::shared_ptr<AdapterRecorder> recorder =
    std::make_shared<AdapterRecorder>();

  buildSubject(
    std::make_unique<DeadlineRecordingAdapter>(mockAsset(), recorder,
                                               driverNow()),
    recordingSinks());

  // The trigger producer carries the blocking callback of the selected
  // channel. The gate blocks the worker of that channel of the real service,
  // so it also delays the deliveries of the healthy subject.
  ObservationSinks gated;
  switch (channel) {
    case ObservationChannel::trace:
      gated.trace = std::make_shared<GatedTraceSink>(gate);
      break;
    case ObservationChannel::results:
      gated.results = [gate](const CommandResultView &) { gate->wait(); };
      break;
    case ObservationChannel::state:
      gated.state = [gate](const StateView &) { gate->wait(); };
      break;
    case ObservationChannel::log:
      gated.log = [gate](const std::string &, const std::string &) {
        gate->wait();
      };
      break;
  }
  buildTrigger(std::move(gated));

  // The gate is entered by a real delivery of the selected channel.
  if (channel == ObservationChannel::log) {
    // The log line must come from the real driver error path of the trigger
    // coordinator: its adapter fails in the watchdog update and the
    // production watchdog logs the error. Only the setup ticks are retried,
    // and only while the entry is awaited.
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!gate->entered() && std::chrono::steady_clock::now() < deadline) {
      trigger_->tick();
      gate->waitForEntry(1ms);
    }
  } else {
    ASSERT_TRUE(bootstrapChannel(channel, trigger_endpoint_));
  }
  ASSERT_TRUE(gate->waitForEntry(2s));
  ASSERT_TRUE(gate->entered());
  ASSERT_FALSE(gate->expired());
  EXPECT_TRUE(gate->held());

  // The motion is commanded while the worker is blocked.
  const std::string token = acquireLease();
  ASSERT_FALSE(token.empty());
  subject_->velocity(velocityRequest(token, 0.2, clock_->ros_ns.load()));
  ASSERT_EQ(recorder->dispatchLog().size(), 1U);

  // The explicit stop returns and issues the controlled stop output.
  StopRequestView stop;
  stop.lease_token = token;
  stop.request_id = "stop-1";
  const StopResponseView stopped = subject_->stop(stop);
  EXPECT_TRUE(stopped.success);
  EXPECT_EQ(recorder->controlled_stop_calls.load(), 1);
  EXPECT_EQ(recorder->stop_calls.load(), 0);
  EXPECT_EQ(recorder->zero_outputs.load(), 1);
  // The channel worker is still blocked: the control path never waited for
  // the observation delivery.
  EXPECT_TRUE(gate->held());

  // A fresh lease, a velocity and the expiry of the interval issue the stop
  // output of the watchdog.
  const std::string fresh = acquireLease();
  ASSERT_FALSE(fresh.empty());
  subject_->velocity(velocityRequest(fresh, 0.2, clock_->ros_ns.load()));
  ASSERT_EQ(recorder->dispatchLog().size(), 2U);
  clock_->advance(0.3);
  subject_->tick();
  EXPECT_EQ(recorder->stop_calls.load(), 1);
  EXPECT_EQ(recorder->controlled_stop_calls.load(), 1);
  EXPECT_EQ(recorder->zero_outputs.load(), 2);
  EXPECT_TRUE(gate->held());
  ASSERT_FALSE(gate->expired());

  // The driver error of the trigger producer never reached the healthy
  // subject.
  EXPECT_TRUE(subject_->fault().empty());
}

INSTANTIATE_TEST_SUITE_P(AllChannels, CoordinatorObservationChannelTest,
                         ::testing::Values(ObservationChannel::trace,
                                           ObservationChannel::results,
                                           ObservationChannel::state,
                                           ObservationChannel::log));

// --- the owned lifetime case ------------------------------------------------

// The owned state of the callback. The capture owns it; the test only keeps a
// weak watch.
struct CallbackState {
  std::atomic<int> invocations{0};
  std::atomic<bool> reentry_done{false};
  std::shared_ptr<Gate> gate;
};

// The callback capture outlives the coordinator and the endpoint, and it may
// reenter the coordinator while the delivery runs.
TEST_F(CoordinatorObservationTest,
       CallbackCaptureOutlivesTheCoordinatorAndReentersItSafely) {
  const std::shared_ptr<Gate> gate = makeGate();
  std::shared_ptr<CallbackState> state = std::make_shared<CallbackState>();
  state->gate = gate;
  const std::weak_ptr<CallbackState> watch = state;

  // The callback reaches the coordinator through a weak reference: the
  // capture never owns the coordinator and never borrows it raw.
  const std::shared_ptr<std::weak_ptr<ControlCoordinator>> coordinator_ref =
    std::make_shared<std::weak_ptr<ControlCoordinator>>();

  ObservationSinks sinks = recordingSinks();
  sinks.state = [state, coordinator_ref](const StateView &) {
    state->invocations.fetch_add(1);
    {
      const std::shared_ptr<ControlCoordinator> coordinator =
        coordinator_ref->lock();
      if (coordinator) {
        // The reentrant inspection takes the control mutex. It can only
        // return when the delivery does not hold that mutex.
        coordinator->fault();
        state->reentry_done.store(true);
      }
    }
    // The transient strong reference ends before the gate: the main thread
    // can really destroy the coordinator while the callback blocks.
    state->gate->wait();
  };

  const std::shared_ptr<AdapterRecorder> recorder =
    std::make_shared<AdapterRecorder>();
  buildSubject(
    std::make_unique<DeadlineRecordingAdapter>(mockAsset(), recorder,
                                               driverNow()),
    std::move(sinks));
  *coordinator_ref = subject_;

  // A real motion first.
  const std::string token = acquireLease();
  ASSERT_FALSE(token.empty());
  subject_->velocity(velocityRequest(token, 0.2, clock_->ros_ns.load()));
  ASSERT_EQ(recorder->dispatchLog().size(), 1U);

  // The watchdog publishes the state and the callback enters the gate. Only
  // the setup ticks are retried; the motion command is never replayed.
  //
  // The state publication is throttled by the steady clock: the tick stamps
  // the publication, so the clock advances before every retry tick and the
  // next tick really publishes the state again.
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (!gate->entered() && std::chrono::steady_clock::now() < deadline) {
    clock_->advance(0.11);
    subject_->tick();
    gate->waitForEntry(1ms);
  }
  ASSERT_TRUE(gate->entered());
  ASSERT_TRUE(state->reentry_done.load());
  // The delivery is really blocked in the callback and the safety guard of
  // the gate never expired.
  ASSERT_TRUE(gate->held());
  ASSERT_FALSE(gate->expired());

  // The main thread destroys the coordinator and drops the external handles.
  // The capture is owned by the service and survives both.
  subject_.reset();
  // The coordinator is really destroyed: the weak reference of the callback
  // does not keep it alive.
  ASSERT_TRUE(coordinator_ref->expired());
  endpoints_.erase(std::remove(endpoints_.begin(), endpoints_.end(),
                               subject_endpoint_),
                   endpoints_.end());
  subject_endpoint_.reset();
  state.reset();
  EXPECT_FALSE(watch.expired());

  // The delivery is still blocked in the gate: the worker did not return on
  // its own and the safety guard of the gate never expired.
  ASSERT_TRUE(gate->held());
  ASSERT_FALSE(gate->expired());

  // The gate is released: the worker retires the registration and the
  // capture is reclaimed.
  gate->release();
  service_->requestStop();
  EXPECT_TRUE(service_->joinFor(15s));
  EXPECT_TRUE(watch.expired());
}

// --- the deadline trace case ------------------------------------------------

// The issued velocity trace carries the interval the driver will really
// honour, and the stamps of the issue time.
TEST_F(CoordinatorObservationTest,
       IssuedVelocityTraceKeepsTheIssueTimeIntervalAndStamps) {
  const std::shared_ptr<Gate> trace_gate = makeGate();
  const std::shared_ptr<Gate> results_gate = makeGate();
  const std::shared_ptr<Gate> state_gate = makeGate();
  const std::shared_ptr<Gate> log_gate = makeGate();

  const std::shared_ptr<AdapterRecorder> recorder =
    std::make_shared<AdapterRecorder>();
  auto adapter = std::make_unique<DeadlineRecordingAdapter>(
    mockAsset(), recorder, driverNow());
  // The validation of the command consumes 80 ms of the monotonic interval:
  // the original TTL of 100 ms is partly spent before the dispatch.
  adapter->on_validate = [clock = clock_] { clock->advanceMonotonicOnly(0.08); };
  buildSubject(std::move(adapter), recordingSinks());

  // All four real workers are blocked on the bootstrap gates, so the single
  // velocity command is queued without a try_lock competition of the real
  // service.
  ObservationSinks gated;
  gated.trace = std::make_shared<GatedTraceSink>(trace_gate);
  gated.results = [gate = results_gate](const CommandResultView &) {
    gate->wait();
  };
  gated.state = [gate = state_gate](const StateView &) { gate->wait(); };
  gated.log = [gate = log_gate](const std::string &, const std::string &) {
    gate->wait();
  };
  const std::shared_ptr<ObservationEndpoint> bootstrap =
    registerProducer(std::move(gated));
  ASSERT_TRUE(bootstrapChannel(ObservationChannel::trace, bootstrap));
  ASSERT_TRUE(bootstrapChannel(ObservationChannel::results, bootstrap));
  ASSERT_TRUE(bootstrapChannel(ObservationChannel::state, bootstrap));
  ASSERT_TRUE(bootstrapChannel(ObservationChannel::log, bootstrap));
  ASSERT_TRUE(trace_gate->waitForEntry(2s));
  ASSERT_TRUE(results_gate->waitForEntry(2s));
  ASSERT_TRUE(state_gate->waitForEntry(2s));
  ASSERT_TRUE(log_gate->waitForEntry(2s));

  // Every bootstrap gate is really held: the worker of its channel sits in
  // the callback and no gate expired on its own safety bound.
  const auto assertGatesHeld = [](const std::shared_ptr<Gate> & gate) {
    ASSERT_TRUE(gate->held());
    ASSERT_FALSE(gate->expired());
  };
  assertGatesHeld(trace_gate);
  assertGatesHeld(results_gate);
  assertGatesHeld(state_gate);
  assertGatesHeld(log_gate);

  // The one and only motion command.
  const std::string token = acquireLease();
  ASSERT_FALSE(token.empty());
  const std::int64_t admission_mono = clock_->mono_ns.load();
  const std::int64_t admission_ros = clock_->ros_ns.load();
  const std::int64_t admission_driver = clock_->driver_mono_ns.load();
  subject_->velocity(velocityRequest(token, 0.1, admission_ros));

  // The dispatch carried the original source stamp and the original
  // deadline: the validation delay consumed 80 ms of the 100 ms interval, so
  // 20 ms are left at the dispatch.
  const std::vector<AdapterDispatch> dispatches = recorder->dispatchLog();
  ASSERT_EQ(dispatches.size(), 1U);
  const AdapterDispatch & dispatch = dispatches.front();
  EXPECT_EQ(dispatch.source_stamp_ns, admission_ros);
  EXPECT_EQ(dispatch.deadline_ns, admission_driver + 100'000'000LL);
  EXPECT_EQ(dispatch.dispatch_now_ns, admission_driver + 80'000'000LL);
  EXPECT_EQ(dispatch.deadline_ns - dispatch.dispatch_now_ns, 20'000'000LL);

  // The clocks move on while the workers are still blocked.
  clock_->advance(0.2);
  // The deliveries are still blocked after the clock moved on and no gate
  // expired on its own: the safety guard never released the gate.
  assertGatesHeld(trace_gate);
  assertGatesHeld(results_gate);
  assertGatesHeld(state_gate);
  assertGatesHeld(log_gate);
  trace_gate->release();
  results_gate->release();
  state_gate->release();
  log_gate->release();

  // The delivery happens after the clocks moved on.
  ASSERT_TRUE(traces_->waitForCount(1, 2s));
  const std::vector<TraceEvent> events = traces_->snapshot();
  std::vector<TraceEvent> issued;
  for (const TraceEvent & event : events) {
    if (event.command == "motion.velocity" && event.stage == "issued") {
      issued.push_back(event);
    }
  }
  ASSERT_EQ(issued.size(), 1U);
  const TraceEvent & event = issued.front();
  ASSERT_TRUE(event.velocity.has_value());
  EXPECT_TRUE(event.velocity->valid);
  // The exported interval is the remainder of the original deadline, never
  // the interval the guard committed.
  EXPECT_LE(event.velocity->valid_for_s, 0.020);
  EXPECT_GT(event.velocity->valid_for_s, 0.0);
  EXPECT_EQ(event.velocity->values,
            (std::array<double, 6>{0.5, 0.0, 0.0, 0.0, 0.0, 0.0}));
  // The stamps are the ones of the issue time: the delivery never re-stamps
  // the event. The final stop of the cleanup adds its own traces later and
  // never changes this one.
  EXPECT_EQ(event.stamp_ns, admission_ros);
  EXPECT_EQ(event.monotonic_ns, admission_mono + 80'000'000LL);
}

}  // namespace
}  // namespace chaos::runtime
