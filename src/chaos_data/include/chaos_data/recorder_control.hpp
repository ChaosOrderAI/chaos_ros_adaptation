// Control loop of the standalone native recorder worker.
//
// Extracted from recorder_worker_main.cpp so the failure behaviour of the
// control loop — in particular a recorder executor failure on an idle control
// socket — can be regression tested without initializing ROS or rosbag2. The
// production WorkerSession implements RecorderControlSession; tests drive the
// loop with a deterministic fake session.
//
// Wire protocol (unchanged, see recorder_supervisor.hpp): the worker owns the
// start handshake and answers pause/resume/stop with newline delimited JSON.
// The worker exits with status 0 after a successful stop handshake and also
// when the supervisor disconnects (EOF on the control channel); every other
// exit path is a nonzero status the supervisor detects through the child exit
// status.
#pragma once

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "chaos_core/clock.hpp"

namespace chaos::data {

// Maximum size of one newline delimited control frame.
inline constexpr std::size_t kRecorderControlMaxFrame = 64 * 1024;

// Bounded poll timeout of the control loop. The loop must never block
// forever: a recorder executor failure has to be noticed even when the
// supervisor never sends another command. This interval is the upper bound on
// the detection latency; between wake-ups the idle loop stays asleep.
inline constexpr int kRecorderControlPollTimeoutMs = 200;

// Total budget for one control frame write, not per poll iteration.
inline constexpr double kRecorderControlWriteTimeoutS = 5.0;

// Total budget of the failed-executor teardown. On that path the supervisor
// is not waiting for a stop answer, so its own escalation is not active; when
// the teardown exceeds this budget the watchdog ends the worker process hard
// with std::_Exit(EXIT_FAILURE). The policy is scoped to the isolated
// recorder process and to the failed-executor path only; the healthy
// stop/flush path stays the supervisor's bounded responsibility.
inline constexpr double kRecorderFailureTeardownBudgetS = 2.0;

// Narrow session interface driven by the control loop. Every implementation
// must keep shutdown() idempotent: the loop calls it on every exit path.
// shutdown() is deliberately NOT assumed to be bounded: a wedged teardown is
// bounded by the failed-executor watchdog inside the loop, never by
// detaching a thread that references stack state.
class RecorderControlSession {
public:
  virtual ~RecorderControlSession() = default;
  virtual bool spinFailed() const = 0;
  virtual std::string spinError() const = 0;
  virtual bool flush(std::string & error) = 0;
  virtual void pause() = 0;
  virtual void resume() = 0;
  virtual void shutdown() noexcept = 0;
};

namespace detail {

// Bounded write of one control frame. The whole frame shares one deadline and
// the deadline is checked on every iteration, so a peer that keeps reading
// cannot extend the write.
inline bool writeControlFrame(int fd, const std::string & data)
{
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
    return false;
  }
  const double deadline = chaos::monotonicSeconds() + kRecorderControlWriteTimeoutS;
  std::size_t written = 0;
  while (written < data.size()) {
    const double remaining = deadline - chaos::monotonicSeconds();
    if (remaining <= 0) {
      return false;
    }
    const ssize_t result = ::send(fd, data.data() + written,
                                  data.size() - written, MSG_NOSIGNAL);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        struct pollfd descriptor{fd, POLLOUT, 0};
        const int ready = ::poll(&descriptor, 1, static_cast<int>(remaining * 1000.0));
        if (ready < 0 && errno != EINTR) {
          return false;
        }
        continue;
      }
      return false;
    }
    if (result == 0) {
      return false;
    }
    written += static_cast<std::size_t>(result);
  }
  return true;
}

// Bounded process-exit guard for the failed-executor teardown of the isolated
// recorder worker. Arm it before the failure frame write and the shutdown on
// the failed path; disarm() (or destruction) joins the guard thread once the
// teardown completed. When the budget expires before disarm, the current
// process is ended with std::_Exit(EXIT_FAILURE): the policy is scoped to
// the isolated recorder process, whose supervisor is not waiting on this
// path, so the forced exit can never target the caller or the test runner.
// After the timeout no stack state is referenced: the guard owns its state
// and std::_Exit never returns.
class RecorderFailureWatchdog {
public:
  explicit RecorderFailureWatchdog(double budget_s)
  {
    thread_ = std::thread([this, budget_s]() {
      std::unique_lock<std::mutex> lock(mutex_);
      if (condition_.wait_for(lock, std::chrono::duration<double>(budget_s),
                              [this]() { return disarmed_; })) {
        return;  // Disarmed before the deadline: the teardown completed.
      }
      // The failed-executor teardown exceeded its budget. This process is the
      // isolated recorder worker and nobody is waiting on it, so exit hard.
      std::_Exit(EXIT_FAILURE);
    });
  }
  RecorderFailureWatchdog(const RecorderFailureWatchdog &) = delete;
  RecorderFailureWatchdog & operator=(const RecorderFailureWatchdog &) = delete;
  ~RecorderFailureWatchdog() { disarm(); }

  void disarm()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      disarmed_ = true;
    }
    condition_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool disarmed_ = false;
  std::thread thread_;
};

}  // namespace detail

// Writes the start acknowledgement the supervisor waits for. When the
// executor already failed (observable right after start), no successful
// acknowledgement is written: a failure frame is sent instead and false is
// returned so the worker exits unsuccessfully.
inline bool acknowledgeRecorderStart(const RecorderControlSession & session,
                                     int control_fd, int pid)
{
  if (session.spinFailed()) {
    detail::writeControlFrame(
      control_fd,
      nlohmann::json{{"ok", false}, {"operation", "start"},
                     {"error", std::string("recorder executor failed: ") +
                               session.spinError()}}
        .dump() +
        "\n");
    return false;
  }
  return detail::writeControlFrame(
    control_fd,
    nlohmann::json{{"ok", true}, {"operation", "start"}, {"pid", pid}}.dump() + "\n");
}

// Runs the newline delimited JSON control loop until the supervisor stops the
// recorder, disconnects, or the recorder executor fails. Returns the process
// exit code: 0 after a successful stop handshake (the flush succeeded, no
// executor failure was observed and the success acknowledgement was written)
// and 0 on supervisor disconnect (EOF — best-effort shutdown without a
// flush-success acknowledgement, preserving normal disconnect behaviour); 1 as
// soon as the recording can no longer be trusted, which includes a failed
// flush, an executor failure and a failed stop acknowledgement write. Throws
// std::runtime_error on protocol violations; the caller reports those as a
// start failure. failure_watchdog_s bounds only the failed-executor teardown
// of this process (see detail::RecorderFailureWatchdog); the healthy
// stop/flush path stays the supervisor's bounded responsibility.
inline int runRecorderControlLoop(RecorderControlSession & session, int control_fd,
                                  int poll_timeout_ms = kRecorderControlPollTimeoutMs,
                                  double failure_watchdog_s = kRecorderFailureTeardownBudgetS)
{
  std::string frame;
  char buffer[4096];
  while (true) {
    struct pollfd descriptor{control_fd, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, poll_timeout_ms);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error(std::string("control poll failed: ") +
                               std::strerror(errno));
    }
    // The bounded poll timeout is what wakes the loop on an idle control
    // socket. The spin check runs on every wake-up before any command is
    // read or answered so an executor failure is never acknowledged as a
    // success and the worker shuts down without waiting for an explicit
    // stop.
    if (session.spinFailed()) {
      // Arm the watchdog before the failure frame write and the teardown: on
      // this path the supervisor is not waiting for a stop answer, so its
      // own escalation is not active. If the teardown hangs, the watchdog
      // ends this process hard after the finite budget.
      detail::RecorderFailureWatchdog watchdog(failure_watchdog_s);
      const std::string error = "recorder executor failed: " + session.spinError();
      detail::writeControlFrame(control_fd,
                                nlohmann::json{{"ok", false}, {"operation", "stop"},
                                               {"error", error}}
                                  .dump() +
                                  "\n");
      session.shutdown();
      watchdog.disarm();
      return 1;
    }
    if (ready == 0) {
      // Idle wake-up: only the spin check above ran. The socket has no data
      // and the descriptor is nonblocking (the start acknowledgement set
      // O_NONBLOCK), so reading here would fail with EAGAIN on a healthy
      // idle recording.
      continue;
    }
    const ssize_t count = ::read(control_fd, buffer, sizeof(buffer));
    if (count < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      throw std::runtime_error("control channel failed");
    }
    if (count == 0) {
      // The supervisor vanished (EOF). There is no peer left to acknowledge
      // a flush to. Shutdown may attempt a flush but does not guarantee it;
      // preserve the established disconnect exit status.
      session.shutdown();
      return 0;
    }
    frame.append(buffer, static_cast<std::size_t>(count));
    if (frame.size() > kRecorderControlMaxFrame) {
      throw std::runtime_error("control frame oversized");
    }
    std::size_t newline = frame.find('\n');
    while (newline != std::string::npos) {
      const std::string payload = frame.substr(0, newline);
      frame.erase(0, newline + 1);
      newline = frame.find('\n');
      nlohmann::json command;
      try {
        command = nlohmann::json::parse(payload);
      } catch (const std::exception &) {
        throw std::runtime_error("malformed control frame");
      }
      if (!command.is_object() || !command.contains("op")) {
        throw std::runtime_error("malformed control frame");
      }
      const std::string op = command.at("op").get<std::string>();
      if (op == "stop") {
        // A failed executor is not a successful recording: the failure must
        // be visible to the supervisor.
        if (session.spinFailed()) {
          // Same unbounded-teardown exposure as the idle detection path:
          // the supervisor never gets a successful stop answer here, so the
          // watchdog bounds this process exit too.
          detail::RecorderFailureWatchdog watchdog(failure_watchdog_s);
          const std::string error = "recorder executor failed: " + session.spinError();
          detail::writeControlFrame(control_fd,
                                    nlohmann::json{{"ok", false}, {"operation", "stop"},
                                                   {"error", error}}
                                      .dump() +
                                      "\n");
          session.shutdown();
          watchdog.disarm();
          return 1;
        }
        std::string error;
        if (!session.flush(error)) {
          // A failed flush must never be acknowledged as a success.
          detail::writeControlFrame(control_fd,
                                    nlohmann::json{{"ok", false}, {"operation", "stop"},
                                                   {"error", error}}
                                      .dump() +
                                      "\n");
          session.shutdown();
          return 1;
        }
        // Join the executor before deciding success: its final callback may
        // fail during teardown even after flush() returned successfully.
        session.shutdown();
        if (session.spinFailed()) {
          const std::string error = "recorder executor failed: " + session.spinError();
          detail::writeControlFrame(control_fd,
                                    nlohmann::json{{"ok", false}, {"operation", "stop"},
                                                   {"error", error}}
                                      .dump() +
                                      "\n");
          return 1;
        }
        if (!detail::writeControlFrame(control_fd,
                                       nlohmann::json{{"ok", true}, {"operation", "stop"}}
                                         .dump() +
                                         "\n")) {
          // The success acknowledgement could not be delivered; the
          // supervisor cannot treat this stop as successful.
          return 1;
        }
        return 0;
      }
      if (op == "pause") {
        session.pause();
      } else if (op == "resume") {
        session.resume();
      } else {
        throw std::runtime_error("unknown recorder operation");
      }
      detail::writeControlFrame(control_fd,
                                nlohmann::json{{"ok", true}, {"operation", op}}
                                  .dump() +
                                  "\n");
    }
  }
}

}  // namespace chaos::data
