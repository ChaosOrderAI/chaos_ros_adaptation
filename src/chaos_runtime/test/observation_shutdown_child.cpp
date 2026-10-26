// The child harness of the robot_runtime host shutdown test.
//
// The parent gtest spawns this executable with posix_spawn before any ROS
// context and before any observation worker exists, so the child never
// inherits the multithreaded mutex state of a running service. The child runs
// the SAME production host helper (chaos_runtime_host) against the REAL
// observation service and reports its progress through a small ordered pipe
// protocol.
//
// The child never asserts and never judges: it reports the markers and its
// exit status, and the parent owns every expectation.
//
// VARIANTS
//   healthy          two sequential healthy lifecycles, each fully joined
//   sigint           a real ROS spin with a frozen ROS clock, SIGINT
//   sigterm-prespin  the signal arrives before the spin starts
//   blocked-sink     a real observation sink callback blocks forever
//   hung-executor    the executor destruction blocks after the outer
//                    protection is armed
//   startup-error    the node construction fails
//   spin-error       the spin fails
//
// The failure injection lives in this harness only: the owned callbacks, the
// local exceptions and the release gates. No production fault flag, no public
// sink tuning and no reimplemented supervisor is used.
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>

#include "chaos_runtime/observation.hpp"
#include "chaos_runtime/runtime_shutdown.hpp"

#ifndef CHAOS_SHUTDOWN_MARKER_FD
#define CHAOS_SHUTDOWN_MARKER_FD 3
#endif
#ifndef CHAOS_SHUTDOWN_COMMAND_FD
#define CHAOS_SHUTDOWN_COMMAND_FD 4
#endif

namespace chaos::runtime::host::test {
namespace {

// The marker protocol. The parent gtest owns the same constants: the two test
// translation units are the only files of this task, so the protocol is
// written down in both of them.
//
//   'R' ready       the host reached the node stage
//   'I' idle        payload: int64 the remaining outer budget in ms
//   'P' spin        the runtime entered the spin
//   'S' sink        a real sink callback entered its gate
//   'C' control     payload: byte the context was valid,
//                   int64 the ROS clock reading (-1 without ROS)
//   'J' joined      the four observation workers were joined
//   'X' closed      the runtime context was shut down
//   'E' error       payload: int64 the error code
namespace protocol {
constexpr char kReady = 'R';
constexpr char kIdle = 'I';
constexpr char kSpin = 'P';
constexpr char kSinkEntered = 'S';
constexpr char kControlStop = 'C';
constexpr char kServiceJoined = 'J';
constexpr char kContextClosed = 'X';
constexpr char kError = 'E';
}  // namespace protocol

// The error codes of the child harness.
enum ChildError : std::int64_t {
  kErrorNone = 0,
  kErrorCommandTimeout = 1,
  kErrorSignalNotObserved = 2,
  kErrorAdmissionFailed = 3,
  kErrorSinkNotEntered = 4,
  kErrorDrainTimeout = 5,
  kErrorServiceNotJoined = 6,
  kErrorUnexpected = 7,
};

// The bounded waits of the harness. They are the harness' own waits: the
// production budgets are the arguments of the helper and the parent owns
// them.
constexpr std::chrono::milliseconds kCommandTimeout{15000};
constexpr std::chrono::milliseconds kAdmissionTimeout{5000};
constexpr std::chrono::milliseconds kDrainTimeout{10000};

// The marker channel: the child writes the ordered markers into the pipe.
class MarkerChannel {
public:
  explicit MarkerChannel(int fd) : fd_(fd) {}

  void put(char marker) {
    const unsigned char buffer[1] = {static_cast<unsigned char>(marker)};
    writeAll(buffer, sizeof(buffer));
  }

  void put(char marker, std::int64_t value) {
    unsigned char buffer[9];
    buffer[0] = static_cast<unsigned char>(marker);
    encode(value, buffer + 1);
    writeAll(buffer, sizeof(buffer));
  }

  void put(char marker, unsigned char context_valid, std::int64_t ros_clock_ns) {
    unsigned char buffer[10];
    buffer[0] = static_cast<unsigned char>(marker);
    buffer[1] = context_valid;
    encode(ros_clock_ns, buffer + 2);
    writeAll(buffer, sizeof(buffer));
  }

private:
  static void encode(std::int64_t value, unsigned char * out) {
    const std::uint64_t raw = static_cast<std::uint64_t>(value);
    for (int index = 0; index < 8; ++index) {
      out[index] = static_cast<unsigned char>((raw >> (8 * index)) & 0xFFU);
    }
  }

  void writeAll(const unsigned char * data, std::size_t size) {
    std::size_t written = 0;
    while (written < size) {
      const ssize_t count = ::write(fd_, data + written, size - written);
      if (count > 0) {
        written += static_cast<std::size_t>(count);
        continue;
      }
      if (count < 0 && errno == EINTR) {
        continue;
      }
      // The parent is gone: the child must never block in a write.
      return;
    }
  }

  int fd_;
};

// The command channel: the parent releases the healthy phases of the child
// with one byte, so no phase depends on a guessed sleep.
class CommandChannel {
public:
  explicit CommandChannel(int fd) : fd_(fd) {}

  bool wait(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        return false;
      }
      const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
      struct pollfd descriptor;
      descriptor.fd = fd_;
      descriptor.events = POLLIN;
      descriptor.revents = 0;
      const int ready = ::poll(
        &descriptor, 1,
        static_cast<int>(std::max<std::int64_t>(1, left.count())));
      if (ready < 0) {
        if (errno == EINTR) {
          continue;
        }
        return false;
      }
      if (ready == 0) {
        continue;
      }
      unsigned char byte = 0;
      const ssize_t count = ::read(fd_, &byte, 1);
      if (count == 1) {
        last_ = static_cast<char>(byte);
        return true;
      }
      if (count < 0 && errno == EINTR) {
        continue;
      }
      // The parent closed the channel.
      return false;
    }
  }

  char last() const { return last_; }

private:
  int fd_;
  char last_ = 0;
};

// A gate that a real observation sink callback enters. The blocked variant
// never releases it: the permanent block stays inside the child, where the
// production deadline and the parent reap guard contain it.
class BlockingGate {
public:
  void enter() {
    std::unique_lock<std::mutex> lock(mutex_);
    entered_ = true;
    cv_.notify_all();
    cv_.wait(lock, [this] { return released_; });
  }

  bool waitEntered(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this] { return entered_; });
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool entered_ = false;
  bool released_ = false;
};

// A counting trace sink: the healthy variant proves that every one of the
// four workers really drained its channel.
class CountingTraceSink : public chaos::runtime::TraceSink {
public:
  explicit CountingTraceSink(std::shared_ptr<std::atomic<int>> deliveries)
    : deliveries_(std::move(deliveries)) {}

  void publish(const chaos::runtime::TraceEvent &) override {
    deliveries_->fetch_add(1);
  }

private:
  std::shared_ptr<std::atomic<int>> deliveries_;
};

// The blocking trace sink of the blocked cleanup variant.
class BlockingTraceSink : public chaos::runtime::TraceSink {
public:
  BlockingTraceSink(std::shared_ptr<BlockingGate> gate, MarkerChannel & markers)
    : gate_(std::move(gate)), markers_(&markers) {}

  void publish(const chaos::runtime::TraceEvent &) override {
    markers_->put(protocol::kSinkEntered);
    gate_->enter();
  }

private:
  std::shared_ptr<BlockingGate> gate_;
  MarkerChannel * markers_;
};

// The host runtime of the child harness.
//
// It implements the production HostRuntime interface, so the child runs the
// SAME production helper, signal, deadline and cleanup path. The fake control
// path emits the stop marker and inspects the context validity; it never
// claims an adapter output or a physical stop.
class HarnessRuntime : public chaos::runtime::host::HostRuntime {
public:
  struct Options {
    bool use_ros = false;
    bool fail_startup = false;
    bool fail_spin = false;
    bool block_sink = false;
    bool block_executor_destruction = false;
    bool drain_check = false;
    bool pre_spin_signal = false;
  };

  HarnessRuntime(
    const Options & options, MarkerChannel & markers, CommandChannel & commands)
    : options_(options), markers_(markers), commands_(commands) {
    if (options_.use_ros) {
      executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
    }
  }

  void setService(chaos::runtime::ObservationService * service) {
    service_ = service;
  }

  void setEndpoint(std::shared_ptr<chaos::runtime::ObservationEndpoint> endpoint) {
    endpoint_ = std::move(endpoint);
  }

  void setDeliveries(std::shared_ptr<std::atomic<int>> deliveries) {
    deliveries_ = std::move(deliveries);
  }

  void setRemainingProbe(std::function<std::chrono::milliseconds()> probe) {
    remaining_probe_ = std::move(probe);
  }

  void setShutdownRequestedProbe(std::function<bool()> probe) {
    shutdown_requested_probe_ = std::move(probe);
  }

  void setRequestShutdown(std::function<void()> request) {
    request_shutdown_ = std::move(request);
  }

  bool failed() const { return failed_.load(); }

  void createNode() override {
    if (options_.use_ros) {
      // The real ROS runtime. The node runs with a frozen ROS clock, so a
      // shutdown that depended on the ROS time could never finish.
      rclcpp::NodeOptions node_options;
      node_options.append_parameter_override("use_sim_time", true);
      node_ = std::make_shared<rclcpp::Node>(
        "chaos_host_shutdown_child", node_options);
      // The one wall timer of the child: it reports the idle marker from
      // inside the running ROS executor. The marker is the proof that the
      // ROS spin was really executing when the parent sent the signal. The
      // wall timer never depends on the ROS time.
      idle_timer_ = node_->create_wall_timer(
        std::chrono::milliseconds(10), [this] { reportIdle(); });
      executor_->add_node(node_);
    }
    if (options_.fail_startup) {
      // The injected startup failure: the production helper owns the cleanup.
      // No node exists, so the child reports no node marker and no stop
      // result: the only markers of this variant are the join and the
      // context closure of the cleanup path.
      throw std::runtime_error(
        "chaos_runtime host shutdown child: the injected startup failure");
    }
    node_created_ = true;
    markers_.put(protocol::kReady);
    if (options_.pre_spin_signal) {
      // The pre-spin signalling: the child holds the node stage until the
      // parent released it and until the host observed the signal. The spin
      // never starts.
      markers_.put(protocol::kIdle, remainingMs());
      if (!commands_.wait(kCommandTimeout)) {
        fail(kErrorCommandTimeout);
        return;
      }
      const auto deadline = std::chrono::steady_clock::now() + kCommandTimeout;
      while (!shutdownRequested() &&
             std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      if (!shutdownRequested()) {
        fail(kErrorSignalNotObserved);
      }
    }
  }

  void spin() override {
    markers_.put(protocol::kSpin);
    if (options_.fail_spin) {
      markers_.put(protocol::kIdle, remainingMs());
      throw std::runtime_error(
        "chaos_runtime host shutdown child: the injected spin failure");
    }
    waitForDrain();
    // The idle phase is released by the parent: the idle interval is a
    // handshake and never a guessed sleep.
    if (!commands_.wait(kCommandTimeout)) {
      fail(kErrorCommandTimeout);
      return;
    }
    if (options_.use_ros) {
      // The real ROS spin: it returns when the supervisor cancels it. The
      // idle marker is NOT sent here: the wall timer reports it from inside
      // the running executor, so the marker proves that the ROS spin was
      // really executing when the parent sent the signal.
      executor_->spin();
      return;
    }
    // The healthy host has no shutdown timer running.
    markers_.put(protocol::kIdle, remainingMs());
    // The orderly stop of a healthy runtime.
    requestShutdown();
  }

  void cancelSpin() override {
    if (options_.use_ros && executor_) {
      executor_->cancel();
    }
    spin_cancelled_.store(true);
  }

  void destroyNode() override {
    if (!node_created_) {
      // No runtime node was created: no stop result is fabricated.
      return;
    }
    // The control stop is attempted while the context is still valid.
    markers_.put(
      protocol::kControlStop,
      static_cast<unsigned char>(contextValid() ? 1 : 0), rosClockNs());
    stop_attempted_.store(true);
    if (idle_timer_) {
      // The timer callback belongs to this runtime: it is cancelled and
      // released before the node and the runtime are destroyed, so no
      // callback outlives the object it reports for.
      idle_timer_->cancel();
      idle_timer_.reset();
    }
    if (options_.use_ros && node_) {
      std::shared_ptr<rclcpp::Node> node = std::move(node_);
      try {
        executor_->remove_node(node);
      } catch (const std::exception &) {
        // The removal is best effort, exactly like the production host.
      }
      node.reset();
    }
    // The accepted teardown closes the endpoint while the context is still
    // valid.
    if (endpoint_) {
      endpoint_->close();
    }
  }

  void destroyExecutor() override {
    if (service_ && !service_->joined()) {
      fail(kErrorServiceNotJoined);
    }
    markers_.put(protocol::kServiceJoined);
    if (options_.block_executor_destruction) {
      // The permanent block of the second blocked cleanup variant: it happens
      // after the outer protection is armed, so only the independent outer
      // deadline can end the host. The cleanup thread never returns.
      for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
    if (options_.use_ros) {
      executor_.reset();
    }
  }

  void shutdownContext() override {
    if (options_.use_ros && rclcpp::ok()) {
      rclcpp::shutdown();
    }
    context_valid_.store(false);
    markers_.put(protocol::kContextClosed);
  }

private:
  void fail(ChildError code) {
    if (failed_.exchange(true)) {
      return;
    }
    markers_.put(protocol::kError, static_cast<std::int64_t>(code));
  }

  bool shutdownRequested() const {
    return shutdown_requested_probe_ && shutdown_requested_probe_();
  }

  void requestShutdown() {
    if (request_shutdown_) {
      request_shutdown_();
    }
  }

  std::int64_t remainingMs() const {
    if (!remaining_probe_) {
      return -1;
    }
    return static_cast<std::int64_t>(remaining_probe_().count());
  }

  bool contextValid() const {
    if (options_.use_ros) {
      return rclcpp::ok();
    }
    return context_valid_.load();
  }

  std::int64_t rosClockNs() const {
    if (!options_.use_ros || !node_) {
      return -1;
    }
    return node_->get_clock()->now().nanoseconds();
  }

  void waitForDrain() {
    if (!options_.drain_check || !deliveries_) {
      return;
    }
    const auto deadline = std::chrono::steady_clock::now() + kDrainTimeout;
    while (deliveries_->load() < 4) {
      if (std::chrono::steady_clock::now() >= deadline) {
        fail(kErrorDrainTimeout);
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  Options options_;
  MarkerChannel & markers_;
  CommandChannel & commands_;
  chaos::runtime::ObservationService * service_ = nullptr;
  std::shared_ptr<chaos::runtime::ObservationEndpoint> endpoint_;
  std::shared_ptr<std::atomic<int>> deliveries_;
  std::function<std::chrono::milliseconds()> remaining_probe_;
  std::function<bool()> shutdown_requested_probe_;
  std::function<void()> request_shutdown_;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::shared_ptr<rclcpp::Node> node_;
  bool node_created_ = false;
  std::atomic<bool> context_valid_{true};
  std::atomic<bool> spin_cancelled_{false};
  std::atomic<bool> stop_attempted_{false};
  std::atomic<bool> failed_{false};

  // The one idle report of the child.
  //
  // The wall timer of the real ROS node calls it from inside the running
  // executor, so the idle marker is only sent when the ROS spin is really
  // executing. The timer cancels itself after the first report.
  void reportIdle() {
    if (idle_reported_.exchange(true)) {
      return;
    }
    markers_.put(protocol::kIdle, remainingMs());
    if (idle_timer_) {
      idle_timer_->cancel();
    }
  }

  rclcpp::TimerBase::SharedPtr idle_timer_;
  std::atomic<bool> idle_reported_{false};
};

// The admission may lose a race on the internal queue lock. The retry is
// bounded and it never weakens the expectation: the event is admitted.
chaos::runtime::Admission admitWithRetry(
  const std::function<chaos::runtime::Admission()> & attempt) {
  const auto deadline = std::chrono::steady_clock::now() + kAdmissionTimeout;
  for (;;) {
    const chaos::runtime::Admission outcome = attempt();
    if (outcome != chaos::runtime::Admission::contended) {
      return outcome;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      return outcome;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

// One host lifecycle: the real observation service, the production host
// helper and the one ordered cleanup path.
//
// The service generation is acquired here: a new generation is only created
// after the previous one reported a successful join.
int runLifecycle(
  const HarnessRuntime::Options & options, MarkerChannel & markers,
  CommandChannel & commands,
  const chaos::runtime::host::ShutdownBudgets & budgets) {
  std::shared_ptr<chaos::runtime::ObservationService> service =
    chaos::runtime::ObservationService::process();

  HarnessRuntime runtime(options, markers, commands);
  runtime.setService(service.get());

  std::shared_ptr<chaos::runtime::ObservationEndpoint> endpoint;
  std::shared_ptr<std::atomic<int>> deliveries;
  if (options.block_sink) {
    auto gate = std::make_shared<BlockingGate>();
    chaos::runtime::ObservationSinks sinks;
    sinks.trace = std::make_shared<BlockingTraceSink>(gate, markers);
    endpoint = service->registerProducer(std::move(sinks));
    chaos::runtime::TraceEvent event;
    event.sequence = 1;
    event.command = "blocked";
    if (admitWithRetry([&] { return endpoint->trace(event); }) !=
        chaos::runtime::Admission::admitted) {
      markers.put(
        protocol::kError, static_cast<std::int64_t>(kErrorAdmissionFailed));
      return chaos::runtime::host::kExitFailure;
    }
    // The real sink callback must have entered its gate before the host
    // starts: the shutdown is requested after the parent confirmed it.
    if (!gate->waitEntered(kCommandTimeout)) {
      markers.put(
        protocol::kError, static_cast<std::int64_t>(kErrorSinkNotEntered));
      return chaos::runtime::host::kExitFailure;
    }
  } else {
    deliveries = std::make_shared<std::atomic<int>>(0);
    chaos::runtime::ObservationSinks sinks;
    sinks.trace = std::make_shared<CountingTraceSink>(deliveries);
    sinks.results = [deliveries](const chaos::runtime::CommandResultView &) {
      deliveries->fetch_add(1);
    };
    sinks.state = [deliveries](const chaos::runtime::StateView &) {
      deliveries->fetch_add(1);
    };
    sinks.log = [deliveries](const std::string &, const std::string &) {
      deliveries->fetch_add(1);
    };
    endpoint = service->registerProducer(std::move(sinks));
    chaos::runtime::TraceEvent trace_event;
    trace_event.sequence = 1;
    trace_event.command = "shutdown-probe";
    chaos::runtime::CommandResultView result;
    result.request_id = "shutdown-probe";
    result.success = true;
    chaos::runtime::StateView state;
    state.asset_id = "shutdown-probe";
    const chaos::runtime::Admission admissions[4] = {
      admitWithRetry([&] { return endpoint->trace(trace_event); }),
      admitWithRetry([&] { return endpoint->result(result); }),
      admitWithRetry([&] { return endpoint->state(state); }),
      admitWithRetry([&] { return endpoint->log("INFO", "shutdown-probe"); })};
    for (const chaos::runtime::Admission admission : admissions) {
      if (admission != chaos::runtime::Admission::admitted) {
        markers.put(
          protocol::kError, static_cast<std::int64_t>(kErrorAdmissionFailed));
        return chaos::runtime::host::kExitFailure;
      }
    }
  }
  runtime.setEndpoint(endpoint);
  runtime.setDeliveries(deliveries);

  chaos::runtime::host::HostShutdown shutdown(budgets, runtime, *service);
  runtime.setRemainingProbe([&shutdown] { return shutdown.remaining(); });
  runtime.setShutdownRequestedProbe(
    [&shutdown] { return shutdown.shutdownRequested(); });
  runtime.setRequestShutdown([&shutdown] { shutdown.requestShutdown(); });

  const int status = shutdown.run();
  if (runtime.failed()) {
    return chaos::runtime::host::kExitFailure;
  }
  return status;
}

}  // namespace
}  // namespace chaos::runtime::host::test

int main(int argc, char ** argv) {
  using namespace chaos::runtime::host::test;
  if (argc != 4) {
    std::fprintf(
      stderr,
      "usage: observation_shutdown_child <variant> <join_ms> <outer_ms>\n");
    return 64;
  }
  const std::string variant = argv[1];
  const long join_ms = std::strtol(argv[2], nullptr, 10);
  const long outer_ms = std::strtol(argv[3], nullptr, 10);
  if (join_ms <= 0 || outer_ms <= 0) {
    std::fprintf(stderr, "the child budgets must be positive\n");
    return 64;
  }

  HarnessRuntime::Options options;
  bool two_lifecycles = false;
  if (variant == "healthy") {
    options.drain_check = true;
    two_lifecycles = true;
  } else if (variant == "sigint") {
    options.use_ros = true;
  } else if (variant == "sigterm-prespin") {
    options.pre_spin_signal = true;
  } else if (variant == "blocked-sink") {
    options.block_sink = true;
  } else if (variant == "hung-executor") {
    options.block_executor_destruction = true;
  } else if (variant == "startup-error") {
    options.fail_startup = true;
  } else if (variant == "spin-error") {
    options.fail_spin = true;
  } else {
    std::fprintf(stderr, "unknown variant: %s\n", variant.c_str());
    return 64;
  }

  chaos::runtime::host::ShutdownBudgets budgets;
  budgets.observation_join = std::chrono::milliseconds(join_ms);
  budgets.outer_deadline = std::chrono::milliseconds(outer_ms);

  MarkerChannel markers(CHAOS_SHUTDOWN_MARKER_FD);
  CommandChannel commands(CHAOS_SHUTDOWN_COMMAND_FD);

  // The ROS variant: the automatic rclcpp signal handling is disabled, so the
  // context survives the final control stop. The test never uses a robot and
  // never uses a production ROS domain.
  if (options.use_ros) {
    // The isolated ROS domain of the child: the port range of the domain
    // 241 overflows, so the child uses the domain 88. The static peers and
    // the localhost flag of the parent are dropped, so the child only ever
    // discovers the localhost participants of its own domain.
    ::setenv("ROS_DOMAIN_ID", "88", 1);
    ::unsetenv("ROS_STATIC_PEERS");
    ::unsetenv("ROS_LOCALHOST_ONLY");
    ::setenv("ROS_AUTOMATIC_DISCOVERY_RANGE", "LOCALHOST", 1);
    char program[] = "observation_shutdown_child";
    char * arguments[] = {program, nullptr};
    rclcpp::init(
      1, arguments, rclcpp::InitOptions(), rclcpp::SignalHandlerOptions::None);
  }

  try {
    const int lifecycles = two_lifecycles ? 2 : 1;
    for (int lifecycle = 0; lifecycle < lifecycles; ++lifecycle) {
      const int status = runLifecycle(options, markers, commands, budgets);
      if (status != chaos::runtime::host::kExitSuccess) {
        return status;
      }
    }
  } catch (const std::exception & error) {
    markers.put(protocol::kError, static_cast<std::int64_t>(kErrorUnexpected));
    std::fprintf(
      stderr, "observation_shutdown_child failed: %s\n", error.what());
    return chaos::runtime::host::kExitFailure;
  }
  return chaos::runtime::host::kExitSuccess;
}
