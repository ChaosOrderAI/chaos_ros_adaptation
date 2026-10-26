// Internal host shutdown helper for the robot_runtime executable.
//
// The helper owns the host side of the process lifetime: the scoped signal
// handlers, the single host supervisor thread, the outer steady-clock
// deadline and the one ordered cleanup path.
//
// OWNERSHIP
//   * The helper owns the scoped SIGINT/SIGTERM handlers. The handlers only
//     set a volatile sig_atomic_t flag and write one byte into a nonblocking
//     self-pipe. No allocation, ROS call, logger, mutex, std::function
//     invocation or condition variable runs inside a handler. The previous
//     handlers are saved and restored.
//   * The helper owns exactly ONE host supervisor thread. It is separate from
//     the four observation workers. It observes the shutdown request on a
//     normal thread and cancels the executor spin; it never depends on the
//     ROS clock, on an executor timer or on ROS time at all.
//   * The outer steady-clock deadline is armed when the shutdown is first
//     observed, BEFORE any blocking step. It is armed only then: a healthy
//     idle runtime has no shutdown timer running.
//   * The helper owns the one ordered cleanup path: mark the shutdown ->
//     cancel the spin -> destroy the runtime node -> observation
//     requestStop/join -> destroy the executor -> shut the context down ->
//     mark the cleanup complete -> wake and join the supervisor -> restore the
//     handlers. The same path serves a signal, an ordinary spin return, a
//     node constructor failure and a spin exception.
//   * The node is destroyed through its accepted close -> action worker join
//     -> finalize -> endpoint close order while the ROS context is still
//     valid, so the final control stop is attempted before the context is
//     closed.
//   * Before the supervisor thread exists, no deadline is armed and no
//     cancellation is enforced. A startup failure in that window exits the
//     process immediately with status 1: there is no node and no teardown that
//     could be waited for.
//
// EXIT STATUS
//   0  the runtime stopped cleanly (also after a SIGINT/SIGTERM)
//   1  a startup, spin or validation failure
//   2  the observation join budget expired: every live resource is retained
//   3  the outer host deadline expired
//
// A timeout status is never a stop confirmation: the adapter output and a
// physical stop are never claimed by this helper.
//
// BOUNDARY
//   This helper terminates the embedding process. It is compiled only into
//   the host executables (robot_runtime and the child harness of the host
//   test task). It must never become part of chaos_runtime_core or of the
//   reusable node library. The header is installed for the package build
//   only: it is excluded from the installed public surface.
#pragma once

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <string>
#include <thread>

#include "chaos_runtime/observation.hpp"

namespace chaos::runtime::host {

namespace detail {

// The bits of the executor access handshake. The supervisor may only touch
// the executor while it is live, and it marks the executor as in use while a
// cancel call runs.
inline constexpr std::uint32_t kExecutorLive = 1U;
inline constexpr std::uint32_t kExecutorInUse = 2U;

}  // namespace detail

// The exit statuses of the host executable.
inline constexpr int kExitSuccess = 0;
inline constexpr int kExitFailure = 1;
inline constexpr int kExitObservationTimeout = 2;
inline constexpr int kExitHostTimeout = 3;

// The fixed shutdown budget.
//
// The observation join budget is 2 s and the outer host deadline is 5 s,
// both measured with the steady clock. The values are arguments of the
// internal helper, so the child harness of the host test task may use
// shorter durations. There is no asset schema, ROS parameter, environment
// fault mode or production tuning surface behind them.
struct ShutdownBudgets {
  std::chrono::milliseconds observation_join{std::chrono::seconds(2)};
  std::chrono::milliseconds outer_deadline{std::chrono::seconds(5)};
};

// The runtime under the host control.
//
// The ROS host implements it with the real executor and the real runtime
// node; the child harness implements it with a mock control path. The
// cleanup path only uses this interface, so the host and the harness run the
// same production code.
class HostRuntime {
public:
  virtual ~HostRuntime() = default;

  // Creates the runtime node and adds it to the executor. Throws when the
  // startup fails. The node is owned by the runtime implementation.
  virtual void createNode() = 0;

  // Spins the executor until the spin is cancelled. Throws when the spin
  // fails.
  virtual void spin() = 0;

  // Cancels a running spin. It is called from the supervisor thread and from
  // the cleanup path and it must be safe when no spin is running.
  virtual void cancelSpin() = 0;

  // Removes the node from the executor and destroys it through the accepted
  // close -> action worker join -> finalize -> endpoint close order, while
  // the ROS context is still valid. A runtime without a node does nothing:
  // no stop result is fabricated.
  virtual void destroyNode() = 0;

  // Destroys the executor. It is called after the node is gone and before
  // the cleanup is marked complete, so the outer deadline also covers the
  // executor destruction.
  virtual void destroyExecutor() = 0;

  // Shuts the ROS context down. It is called only after the observation
  // workers were joined successfully.
  virtual void shutdownContext() = 0;
};

// The host shutdown controller.
//
// Exactly one instance may be active at a time: the signal handlers and the
// self-pipe are process wide.
class HostShutdown {
public:
  HostShutdown(
    const ShutdownBudgets & budgets, HostRuntime & runtime,
    ObservationService & service);
  ~HostShutdown();

  HostShutdown(const HostShutdown &) = delete;
  HostShutdown & operator=(const HostShutdown &) = delete;

  // Runs the whole host flow: it installs the scoped signal handlers, starts
  // the single supervisor thread, creates the runtime node, spins and runs
  // the one ordered cleanup path. Returns the exit status of the host. A
  // failure that happens before the supervisor thread exists exits the
  // process immediately with the failure status.
  int run();

  // Requests the shutdown: the outer deadline is armed and the supervisor is
  // woken. Idempotent. It is the orderly stop of a healthy runtime.
  void requestShutdown() noexcept;

  // True once a shutdown was observed (a signal or an explicit request).
  bool shutdownRequested() const noexcept;

  // The outer time that is left. It is the full budget while the deadline is
  // not armed yet.
  std::chrono::milliseconds remaining() const noexcept;

private:
  // Installs the handlers and starts the supervisor thread. False when the
  // scope could not be entered: no supervisor exists then.
  bool start();
  // The supervisor body: it observes the request and enforces the deadline.
  void supervise() noexcept;
  // The one ordered cleanup path.
  int cleanup(bool failed);
  // Arms the outer deadline. The first observer owns it.
  void armDeadline() noexcept;
  // True when the armed deadline passed.
  bool deadlineExpired() const noexcept;
  // Cancels the spin with the one exception boundary.
  void cancelSpinSafely() noexcept;
  // Stops the supervisor from touching the executor.
  void releaseExecutor() noexcept;
  // Wakes and joins the supervisor, restores the handlers.
  void finish() noexcept;
  // Waits for the signal pipe and observes the handler flag.
  void waitForSignal(int timeout_ms) noexcept;
  // Wakes the supervisor.
  void wakeSupervisor() noexcept;
  // Installs and restores the scoped handlers.
  bool installSignalHandlers() noexcept;
  void restoreSignalHandlers() noexcept;

  ShutdownBudgets budgets_;
  HostRuntime & runtime_;
  ObservationService & service_;

  std::atomic<bool> shutdown_requested_{false};
  std::atomic<bool> cleanup_complete_{false};
  // The armed deadline as a steady clock reading in nanoseconds, 0 while it
  // is not armed.
  std::atomic<std::int64_t> deadline_ns_{0};
  // The executor access handshake between the supervisor thread and the
  // cleanup path: the supervisor may only cancel the spin while the executor
  // is live, and the cleanup path releases it before it is destroyed.
  std::atomic<std::uint32_t> executor_state_{detail::kExecutorLive};

  std::thread supervisor_;
  int pipe_read_fd_ = -1;
  int pipe_write_fd_ = -1;
  struct ::sigaction previous_int_{};
  struct ::sigaction previous_term_{};
  bool handlers_installed_ = false;
};

}  // namespace chaos::runtime::host
