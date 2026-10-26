// The internal host shutdown helper. The contract is documented in
// runtime_shutdown.hpp.

#include "chaos_runtime/runtime_shutdown.hpp"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace chaos::runtime::host {
namespace {

// The handler state. The host owns exactly one handler scope, so the state
// is process wide. Both variables are signal visible: the flag is a
// sig_atomic_t and the descriptor is read by the handler itself.
volatile std::sig_atomic_t g_signal_observed = 0;
volatile std::sig_atomic_t g_signal_write_fd = -1;

// The signal handler.
//
// Async-signal-safe work only: it sets a volatile sig_atomic_t flag and
// writes one byte into the nonblocking self-pipe. There is no allocation, no
// ROS call, no logger, no mutex, no std::function invocation and no
// condition variable in the handler.
extern "C" void hostSignalHandler(int) {
  g_signal_observed = 1;
  if (g_signal_write_fd >= 0) {
    // The errno of the interrupted code is preserved: a write that fails may
    // never change it. The save and the restore are async-signal-safe.
    const int saved_errno = errno;
    const char byte = 1;
    const ssize_t written = ::write(g_signal_write_fd, &byte, 1);
    (void)written;
    errno = saved_errno;
  }
}

// The steady clock reading in nanoseconds. The shutdown budgets are measured
// with the steady clock: a frozen or rewound ROS clock never influences them.
std::int64_t steadyNowNs() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
           std::chrono::steady_clock::now().time_since_epoch())
    .count();
}

// Creates the self-pipe of the signal scope.
//
// Both ends are nonblocking: the handler write may never block the handler
// and the supervisor drain may never block on an empty pipe. Both ends are
// closed on exec, so the pipe never leaks into a child process.
bool createSignalPipe(int & read_fd, int & write_fd) noexcept {
  int fds[2] = {-1, -1};
  if (::pipe(fds) != 0) {
    return false;
  }
  for (const int fd : {fds[0], fds[1]}) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
      ::close(fds[0]);
      ::close(fds[1]);
      return false;
    }
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
  }
  read_fd = fds[0];
  write_fd = fds[1];
  return true;
}

}  // namespace

HostShutdown::HostShutdown(
  const ShutdownBudgets & budgets, HostRuntime & runtime,
  ObservationService & service)
  : budgets_(budgets), runtime_(runtime), service_(service) {}

HostShutdown::~HostShutdown() {
  // The cleanup path joins the supervisor. A host that never reached the
  // cleanup path still joins it here: no thread survives this object.
  cleanup_complete_.store(true, std::memory_order_release);
  wakeSupervisor();
  if (supervisor_.joinable()) {
    supervisor_.join();
  }
  restoreSignalHandlers();
  // The handlers are gone: the pipe may be closed.
  g_signal_write_fd = -1;
  if (pipe_read_fd_ >= 0) {
    ::close(pipe_read_fd_);
    pipe_read_fd_ = -1;
  }
  if (pipe_write_fd_ >= 0) {
    ::close(pipe_write_fd_);
    pipe_write_fd_ = -1;
  }
}

int HostShutdown::run() {
  if (!start()) {
    // The supervisor could not start: there is no node, no deadline and no
    // teardown that could be waited for. The host fails immediately.
    std::_Exit(kExitFailure);
  }

  bool failed = false;
  try {
    runtime_.createNode();
  } catch (const std::exception & failure) {
    // The deadline is armed before the diagnostic: a diagnostic may never
    // delay the enforced exit.
    requestShutdown();
    std::fprintf(
      stderr, "robot_runtime failed: startup failed: %s\n", failure.what());
    failed = true;
  }

  if (!failed && !shutdownRequested()) {
    // The check before the spin closes the window between the observation of
    // a request and the start of the spin. The supervisor repeats the
    // cancellation while the spin could be running.
    try {
      runtime_.spin();
    } catch (const std::exception & failure) {
      requestShutdown();
      std::fprintf(stderr, "robot_runtime failed: %s\n", failure.what());
      failed = true;
    }
  }

  return cleanup(failed);
}

void HostShutdown::requestShutdown() noexcept {
  shutdown_requested_.store(true, std::memory_order_release);
  // The outer deadline is armed before any blocking step of the cleanup
  // path. The first observer owns it.
  armDeadline();
  wakeSupervisor();
}

bool HostShutdown::shutdownRequested() const noexcept {
  return shutdown_requested_.load(std::memory_order_acquire);
}

std::chrono::milliseconds HostShutdown::remaining() const noexcept {
  const std::int64_t deadline = deadline_ns_.load(std::memory_order_acquire);
  if (deadline == 0) {
    return budgets_.outer_deadline;
  }
  const std::int64_t now = steadyNowNs();
  if (now >= deadline) {
    return std::chrono::milliseconds(0);
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::nanoseconds(deadline - now));
}

bool HostShutdown::start() {
  if (!createSignalPipe(pipe_read_fd_, pipe_write_fd_)) {
    return false;
  }
  // The handler may only write into a pipe that already exists.
  g_signal_write_fd = pipe_write_fd_;
  // The signal scope starts unobserved: only a signal of this scope may
  // request the shutdown.
  g_signal_observed = 0;
  if (!installSignalHandlers()) {
    return false;
  }
  try {
    supervisor_ = std::thread([this] { supervise(); });
  } catch (const std::exception &) {
    restoreSignalHandlers();
    return false;
  }
  return true;
}

void HostShutdown::supervise() noexcept {
  while (!cleanup_complete_.load(std::memory_order_acquire)) {
    if (!shutdown_requested_.load(std::memory_order_acquire)) {
      // The runtime is healthy: no deadline is armed and no timer runs. The
      // supervisor only observes the shutdown request.
      waitForSignal(200);
      continue;
    }
    // The shutdown was observed: the outer deadline is armed and enforced
    // until the cleanup is complete.
    armDeadline();
    while (!cleanup_complete_.load(std::memory_order_acquire)) {
      // The cancellation is repeated while the spin could be running: it
      // closes the window between the check before the spin and the start of
      // the spin.
      cancelSpinSafely();
      if (deadlineExpired()) {
        if (cleanup_complete_.load(std::memory_order_acquire)) {
          return;
        }
        // The deadline fired. This branch writes nothing: the deadline may
        // never block on an output. The host exits with the documented
        // timeout status.
        std::_Exit(kExitHostTimeout);
      }
      waitForSignal(20);
    }
    return;
  }
}

int HostShutdown::cleanup(bool failed) {
  // 1. Mark the shutdown, arm the outer deadline and cancel the executor.
  //    The deadline is armed before any blocking step of this path.
  requestShutdown();
  try {
    runtime_.cancelSpin();
  } catch (const std::exception &) {
    // A failing cancel does not stop the cleanup: the supervisor enforces
    // the deadline.
  }

  // 2. Remove and destroy the runtime node. Its accepted teardown order
  //    (close -> action worker join -> finalize -> endpoint close) runs while
  //    the ROS context is still valid, so the final control stop is attempted
  //    before the context is closed.
  try {
    runtime_.destroyNode();
  } catch (const std::exception &) {
    failed = true;
  }

  // 3. Stop and join the observation workers. The join budget is the
  //    observation budget, limited by the remaining outer time.
  if (deadlineExpired()) {
    // The outer budget is gone before the join even started: the supervisor
    // enforces it, and no new blocking wait is started here.
    std::_Exit(kExitHostTimeout);
  }
  service_.requestStop();
  const std::chrono::milliseconds budget =
    std::min(budgets_.observation_join, remaining());
  if (!service_.joinFor(budget)) {
    // The workers are still live: every live resource is retained, nothing
    // is detached, no replacement generation is created and the host exits
    // with the documented timeout status. No adapter output and no physical
    // stop are claimed here.
    std::_Exit(kExitObservationTimeout);
  }

  // 4. The executor is destroyed explicitly and before the cleanup is marked
  //    complete, so the outer deadline also covers its destruction.
  releaseExecutor();
  try {
    runtime_.destroyExecutor();
  } catch (const std::exception &) {
    failed = true;
  }

  // 5. The context is shut down only after a successful observation join.
  try {
    runtime_.shutdownContext();
  } catch (const std::exception &) {
    failed = true;
  }

  // 6. The cleanup is complete: the supervisor disarms and returns.
  cleanup_complete_.store(true, std::memory_order_release);
  finish();

  return failed ? kExitFailure : kExitSuccess;
}

void HostShutdown::armDeadline() noexcept {
  const std::int64_t budget_ns =
    std::chrono::duration_cast<std::chrono::nanoseconds>(budgets_.outer_deadline)
      .count();
  const std::int64_t target = steadyNowNs() + budget_ns;
  std::int64_t expected = 0;
  // The first observer owns the deadline: a later arming never extends it.
  deadline_ns_.compare_exchange_strong(
    expected, target, std::memory_order_acq_rel);
}

bool HostShutdown::deadlineExpired() const noexcept {
  const std::int64_t deadline = deadline_ns_.load(std::memory_order_acquire);
  return deadline != 0 && steadyNowNs() >= deadline;
}

void HostShutdown::cancelSpinSafely() noexcept {
  std::uint32_t state = executor_state_.load(std::memory_order_acquire);
  while ((state & detail::kExecutorLive) != 0U) {
    if (executor_state_.compare_exchange_weak(
          state, state | detail::kExecutorInUse, std::memory_order_acq_rel,
          std::memory_order_acquire)) {
      try {
        runtime_.cancelSpin();
      } catch (const std::exception &) {
        // The one exception boundary of the supervisor: a failing cancel may
        // never stop the timeout enforcement.
      }
      executor_state_.fetch_and(
        ~detail::kExecutorInUse, std::memory_order_release);
      return;
    }
  }
}

void HostShutdown::releaseExecutor() noexcept {
  // The handshake makes sure that no cancel call is in flight and that no new
  // one starts, so the executor may be destroyed safely while the supervisor
  // still enforces the deadline.
  while (true) {
    std::uint32_t state = executor_state_.load(std::memory_order_acquire);
    if ((state & detail::kExecutorInUse) == 0U &&
        executor_state_.compare_exchange_weak(
          state, state & ~detail::kExecutorLive, std::memory_order_acq_rel,
          std::memory_order_acquire)) {
      return;
    }
    if (deadlineExpired()) {
      // The supervisor may be stuck in a cancel call: the deadline is
      // enforced from here.
      std::_Exit(kExitHostTimeout);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

void HostShutdown::finish() noexcept {
  // The cleanup is complete: the supervisor disarms and returns. The wake up
  // lets it leave its wait immediately.
  wakeSupervisor();
  if (supervisor_.joinable()) {
    supervisor_.join();
  }
  restoreSignalHandlers();
}

void HostShutdown::waitForSignal(int timeout_ms) noexcept {
  struct pollfd descriptor;
  descriptor.fd = pipe_read_fd_;
  descriptor.events = POLLIN;
  descriptor.revents = 0;
  const int ready = ::poll(&descriptor, 1, timeout_ms);
  if (ready > 0 && (descriptor.revents & POLLIN) != 0) {
    char buffer[64];
    while (::read(pipe_read_fd_, buffer, sizeof(buffer)) > 0) {
      // The pipe carries no payload: the bytes are only a wake up.
    }
  }
  if (g_signal_observed != 0) {
    shutdown_requested_.store(true, std::memory_order_release);
  }
}

void HostShutdown::wakeSupervisor() noexcept {
  if (pipe_write_fd_ < 0) {
    return;
  }
  const char byte = 1;
  const ssize_t written = ::write(pipe_write_fd_, &byte, 1);
  (void)written;
}

bool HostShutdown::installSignalHandlers() noexcept {
  struct ::sigaction action;
  std::memset(&action, 0, sizeof(action));
  action.sa_handler = hostSignalHandler;
  ::sigemptyset(&action.sa_mask);
  // SA_RESTART: a signal does not interrupt a blocking read of the pipe.
  action.sa_flags = SA_RESTART;
  if (::sigaction(SIGINT, &action, &previous_int_) != 0) {
    return false;
  }
  if (::sigaction(SIGTERM, &action, &previous_term_) != 0) {
    ::sigaction(SIGINT, &previous_int_, nullptr);
    return false;
  }
  handlers_installed_ = true;
  return true;
}

void HostShutdown::restoreSignalHandlers() noexcept {
  if (!handlers_installed_) {
    return;
  }
  handlers_installed_ = false;
  ::sigaction(SIGINT, &previous_int_, nullptr);
  ::sigaction(SIGTERM, &previous_term_, nullptr);
}

}  // namespace chaos::runtime::host
