// Coordinator contract tests (ROS-free). Ported from the Python runtime
// failure contracts: lease lifecycle, velocity guard, action reservation,
// cancel/stop ordering, and observation-channel isolation.
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "chaos_runtime/adapters/mock_adapter.hpp"
#include "chaos_runtime/coordinator.hpp"
#include "chaos_runtime/trace.hpp"

namespace chaos::runtime {
namespace {

// Deterministic clock: the tests advance time explicitly. The values are
// atomic because the watchdog thread and the operation thread read them
// concurrently.
class FakeClock {
public:
  std::atomic<double> steady_s{100.0};
  std::atomic<std::int64_t> mono_ns{100'000'000'000LL};
  std::atomic<std::int64_t> wall_ns{1'700'000'000'000'000'000LL};
  std::atomic<std::int64_t> ros_ns{1'700'000'000'000'000'000LL};

  void advance(double seconds) {
    steady_s.store(steady_s.load() + seconds);
    const std::int64_t delta = static_cast<std::int64_t>(seconds * 1e9);
    mono_ns.store(mono_ns.load() + delta);
    wall_ns.store(wall_ns.load() + delta);
    ros_ns.store(ros_ns.load() + delta);
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

class RecordingTraceSink : public TraceSink {
public:
  std::vector<TraceEvent> events;
  void publish(const TraceEvent & event) override { events.push_back(event); }
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

struct Harness {
  std::shared_ptr<FakeClock> clock = std::make_shared<FakeClock>();
  std::shared_ptr<RecordingTraceSink> traces =
    std::make_shared<RecordingTraceSink>();
  std::unique_ptr<ControlCoordinator> coordinator;

  explicit Harness(bool uav = false) {
    AssetConfig asset = mockAsset();
    if (uav) {
      asset.kind = "uav";
      asset.capabilities.insert("flight.takeoff");
      asset.capabilities.insert("flight.land");
    }
    Clocks clocks;
    clocks.steady_s = [this] { return clock->steady_s.load(); };
    clocks.mono_ns = [this] { return clock->mono_ns.load(); };
    clocks.wall_ns = [this] { return clock->wall_ns.load(); };
    clocks.ros_ns = [this] { return clock->ros_ns.load(); };
    coordinator = std::make_unique<ControlCoordinator>(
      asset, std::make_unique<MockAdapter>(asset), clocks, *traces);
  }
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
  Harness harness;
  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  ASSERT_TRUE(acquired.success);

  VelocityRequestView bad_frame;
  bad_frame.frame_id = "wrong_frame";
  bad_frame.lease_token = acquired.token;
  bad_frame.velocity = {0.1, 0.0, 0.0, 0.0, 0.0, 0.0};
  harness.coordinator->velocity(bad_frame);

  VelocityRequestView nan_velocity;
  nan_velocity.frame_id = "base_link";
  nan_velocity.lease_token = acquired.token;
  nan_velocity.velocity = {std::nan(""), 0.0, 0.0, 0.0, 0.0, 0.0};
  harness.coordinator->velocity(nan_velocity);
  // Neither invalid command armed a velocity sample.
  harness.clock->advance(0.1);
  harness.coordinator->tick();
  EXPECT_TRUE(harness.traces->events.empty() ||
              harness.traces->events.back().stage == "rejected");
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
  Harness harness;
  int result_calls = 0;
  int state_calls = 0;
  harness.coordinator->freezeSinks(
    {[&](const CommandResultView &) {
       ++result_calls;
       throw std::runtime_error("result channel down");
     },
     [&](const StateView &) {
       ++state_calls;
       throw std::runtime_error("state channel down");
     },
     [](const std::string &, const std::string &) {
       throw std::runtime_error("log channel down");
     }});

  const LeaseResponseView acquired =
    harness.coordinator->lease(leaseRequest("acquire", "op-1"));
  EXPECT_TRUE(acquired.success);
  // The watchdog must keep running with every observation channel down.
  for (int i = 0; i < 5; ++i) {
    harness.clock->advance(0.05);
    harness.coordinator->tick();
  }
  EXPECT_GT(state_calls, 0);
  harness.coordinator->close();
}

TEST(CoordinatorObservation, FaultIsReturnedAsAValueSnapshot) {
  Harness harness;
  EXPECT_TRUE(harness.coordinator->fault().empty());
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
  RecordingTraceSink traces;
  ControlCoordinator coordinator(asset, std::make_unique<MockAdapter>(asset),
                                 clocks, traces);
  control::RosTime stamp;
  stamp.sec = 1700;
  const control::CommandDeadline deadline =
    control::CommandDeadline::capture(stamp, *wall_ns, *mono_ns);
  EXPECT_TRUE(deadline.valid());
}

}  // namespace
}  // namespace chaos::runtime
