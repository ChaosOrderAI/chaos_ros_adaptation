// Regression tests for the native recorder worker control loop
// (chaos_data/recorder_control.hpp).
//
// The loop is driven with a deterministic fake session over a real
// socketpair: no ROS, no rosbag2 and no hardware required. The critical
// regression is that a recorder executor failure on an idle control socket
// ends the worker unsuccessfully through the bounded poll timeout instead of
// blocking forever in poll(control_fd, -1) while the supervisor keeps
// reporting the recording as alive.
#include <gtest/gtest.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include <nlohmann/json.hpp>

#include "chaos_core/clock.hpp"
#include "chaos_data/recorder_control.hpp"

namespace {

class FakeSession : public chaos::data::RecorderControlSession
{
public:
  bool spinFailed() const override { return spin_failed.load(); }
  std::string spinError() const override { return spin_error; }
  bool flush(std::string & error) override
  {
    ++flush_calls;
    if (flush_fails) {
      error = "flush exploded";
      return false;
    }
    if (fail_spin_during_flush) {
      // Simulates the executor dying while the flush runs.
      spin_failed = true;
    }
    return true;
  }
  void pause() override { ++pause_calls; }
  void resume() override { ++resume_calls; }
  void shutdown() noexcept override {
    ++shutdown_calls;
    if (fail_spin_during_shutdown) {
      spin_failed = true;
    }
  }

  std::atomic<bool> spin_failed{false};
  std::string spin_error;
  bool flush_fails = false;
  bool fail_spin_during_flush = false;
  bool fail_spin_during_shutdown = false;
  int flush_calls = 0;
  int pause_calls = 0;
  int resume_calls = 0;
  int shutdown_calls = 0;
};

// Owns the supervisor end of the control socket and reads complete newline
// delimited frames with a deadline.
class ControlPeer
{
public:
  explicit ControlPeer(int fd) : fd_(fd)
  {
    const int flags = ::fcntl(fd_, F_GETFL, 0);
    EXPECT_GE(flags, 0);
    EXPECT_EQ(::fcntl(fd_, F_SETFL, flags | O_NONBLOCK), 0);
  }
  ControlPeer(const ControlPeer &) = delete;
  ControlPeer & operator=(const ControlPeer &) = delete;
  ~ControlPeer()
  {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  bool send(const std::string & data)
  {
    std::size_t sent = 0;
    while (sent < data.size()) {
      const ssize_t count =
        ::send(fd_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
      if (count < 0) {
        if (errno == EINTR) {
          continue;
        }
        return false;
      }
      sent += static_cast<std::size_t>(count);
    }
    return true;
  }

  // Returns one parsed frame, or a null JSON value on timeout/EOF.
  nlohmann::json receive(double timeout_s)
  {
    const double deadline = chaos::monotonicSeconds() + timeout_s;
    while (buffer_.find('\n') == std::string::npos) {
      const double remaining = deadline - chaos::monotonicSeconds();
      if (remaining <= 0) {
        return nullptr;
      }
      struct pollfd descriptor{fd_, POLLIN, 0};
      const int ready = ::poll(&descriptor, 1, static_cast<int>(remaining * 1000.0));
      if (ready < 0) {
        if (errno == EINTR) {
          continue;
        }
        return nullptr;
      }
      if (ready == 0) {
        continue;
      }
      char chunk[4096];
      const ssize_t count = ::recv(fd_, chunk, sizeof(chunk), 0);
      if (count <= 0) {
        return nullptr;
      }
      buffer_.append(chunk, static_cast<std::size_t>(count));
    }
    const std::size_t newline = buffer_.find('\n');
    const std::string payload = buffer_.substr(0, newline);
    buffer_.erase(0, newline + 1);
    return nlohmann::json::parse(payload);
  }

private:
  int fd_;
  std::string buffer_;
};

class RecorderControlLoopTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    int fds[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    worker_fd_ = fds[0];
    peer_ = std::make_unique<ControlPeer>(fds[1]);
  }
  void TearDown() override
  {
    peer_.reset();
    if (worker_fd_ >= 0) {
      ::close(worker_fd_);
    }
  }

  // Short poll interval: the tests must stay fast while still exercising the
  // timeout-driven spin check on an idle socket.
  static constexpr int kFastPollMs = 20;

  int worker_fd_ = -1;
  std::unique_ptr<ControlPeer> peer_;
  FakeSession session_;
};

TEST_F(RecorderControlLoopTest, ExecutorFailureOnIdleSocketExitsUnsuccessfully)
{
  // The control socket stays idle: the failure must be detected purely
  // through the bounded poll timeout, not through a supervisor command.
  session_.spin_failed = true;
  session_.spin_error = "synthetic executor explosion";
  EXPECT_EQ(chaos::data::runRecorderControlLoop(session_, worker_fd_, kFastPollMs), 1);
  // Bounded teardown ran exactly once, and no flush was faked.
  EXPECT_EQ(session_.shutdown_calls, 1);
  EXPECT_EQ(session_.flush_calls, 0);
  const nlohmann::json frame = peer_->receive(1.0);
  ASSERT_TRUE(frame.is_object());
  EXPECT_EQ(frame.value("ok", true), false);
  EXPECT_EQ(frame.value("operation", std::string()), "stop");
  EXPECT_NE(frame.value("error", std::string()).find("recorder executor failed"),
            std::string::npos);
  EXPECT_NE(frame.value("error", std::string()).find("synthetic executor explosion"),
            std::string::npos);
}

TEST_F(RecorderControlLoopTest, HealthyIdlePollTimeoutsDoNotDisturbRecording)
{
  // The start acknowledgement sets O_NONBLOCK on the control descriptor. A
  // healthy recording then idles through several poll timeouts before the
  // stop arrives: those wake-ups must not touch the nonblocking socket (the
  // old code read() on every wake-up and threw EAGAIN at the first timeout).
  ASSERT_TRUE(chaos::data::acknowledgeRecorderStart(session_, worker_fd_, 4242));
  const nlohmann::json start = peer_->receive(1.0);
  ASSERT_TRUE(start.is_object());
  EXPECT_EQ(start.value("ok", false), true);
  std::thread delayed_stop([this]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(3 * kFastPollMs + 20));
    peer_->send("{\"op\":\"stop\"}\n");
  });
  EXPECT_EQ(chaos::data::runRecorderControlLoop(session_, worker_fd_, kFastPollMs), 0);
  delayed_stop.join();
  EXPECT_EQ(session_.flush_calls, 1);
  EXPECT_EQ(session_.shutdown_calls, 1);
  const nlohmann::json frame = peer_->receive(1.0);
  ASSERT_TRUE(frame.is_object());
  EXPECT_EQ(frame.value("ok", false), true);
  EXPECT_EQ(frame.value("operation", std::string()), "stop");
}

TEST_F(RecorderControlLoopTest, ExecutorFailureDuringIdleIsDetectedThroughPollTimeout)
{
  // The executor is healthy when the loop starts; the failure happens while
  // the control socket idles and must be caught by the timeout-driven spin
  // check, not by a failure pre-set before entering the loop.
  ASSERT_TRUE(chaos::data::acknowledgeRecorderStart(session_, worker_fd_, 4242));
  const nlohmann::json start = peer_->receive(1.0);
  ASSERT_TRUE(start.is_object());
  EXPECT_EQ(start.value("ok", false), true);
  session_.spin_error = "executor died while idle";
  std::thread delayed_failure([this]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(3 * kFastPollMs + 20));
    session_.spin_failed = true;
  });
  EXPECT_EQ(chaos::data::runRecorderControlLoop(session_, worker_fd_, kFastPollMs), 1);
  delayed_failure.join();
  EXPECT_EQ(session_.flush_calls, 0);
  EXPECT_EQ(session_.shutdown_calls, 1);
  const nlohmann::json frame = peer_->receive(1.0);
  ASSERT_TRUE(frame.is_object());
  EXPECT_EQ(frame.value("ok", true), false);
  EXPECT_EQ(frame.value("operation", std::string()), "stop");
  EXPECT_NE(frame.value("error", std::string()).find("executor died while idle"),
            std::string::npos);
}

TEST_F(RecorderControlLoopTest, ExplicitStopFlushesAndAcksSuccessfully)
{
  ASSERT_TRUE(peer_->send("{\"op\":\"stop\"}\n"));
  EXPECT_EQ(chaos::data::runRecorderControlLoop(session_, worker_fd_, kFastPollMs), 0);
  EXPECT_EQ(session_.flush_calls, 1);
  EXPECT_EQ(session_.shutdown_calls, 1);
  const nlohmann::json frame = peer_->receive(1.0);
  ASSERT_TRUE(frame.is_object());
  EXPECT_EQ(frame.value("ok", false), true);
  EXPECT_EQ(frame.value("operation", std::string()), "stop");
}

TEST_F(RecorderControlLoopTest, ExecutorFailureDuringShutdownNeverAcknowledgesSuccess)
{
  session_.fail_spin_during_shutdown = true;
  session_.spin_error = "last executor callback failed";
  ASSERT_TRUE(peer_->send("{\"op\":\"stop\"}\n"));
  EXPECT_EQ(chaos::data::runRecorderControlLoop(session_, worker_fd_, kFastPollMs), 1);
  EXPECT_EQ(session_.flush_calls, 1);
  EXPECT_EQ(session_.shutdown_calls, 1);
  const auto frame = peer_->receive(1.0);
  ASSERT_TRUE(frame.is_object());
  EXPECT_FALSE(frame.value("ok", true));
  EXPECT_NE(frame.value("error", std::string()).find(session_.spin_error), std::string::npos);
}

TEST_F(RecorderControlLoopTest, SupervisorDisconnectPerformsBoundedTeardown)
{
  // Parent disconnect (EOF on the control channel) exits 0 through the same
  // bounded teardown as an explicit stop, without waiting for a stop command.
  peer_.reset();  // close the supervisor end
  EXPECT_EQ(chaos::data::runRecorderControlLoop(session_, worker_fd_, kFastPollMs), 0);
  EXPECT_EQ(session_.shutdown_calls, 1);
  EXPECT_EQ(session_.flush_calls, 0);
}

TEST_F(RecorderControlLoopTest, StopAfterExecutorFailureIsNotAcknowledgedAsSuccess)
{
  session_.spin_failed = true;
  session_.spin_error = "executor dead before stop";
  ASSERT_TRUE(peer_->send("{\"op\":\"stop\"}\n"));
  // The spin check fires before the stop is answered: the worker exits
  // unsuccessfully without waiting for (or honouring) the explicit stop.
  EXPECT_EQ(chaos::data::runRecorderControlLoop(session_, worker_fd_, kFastPollMs), 1);
  EXPECT_EQ(session_.flush_calls, 0);
  EXPECT_EQ(session_.shutdown_calls, 1);
  const nlohmann::json frame = peer_->receive(1.0);
  ASSERT_TRUE(frame.is_object());
  EXPECT_EQ(frame.value("ok", true), false);
  EXPECT_EQ(frame.value("operation", std::string()), "stop");
}

TEST_F(RecorderControlLoopTest, ExecutorFailureDuringFlushIsNotAcknowledgedAsSuccess)
{
  // The flush succeeds, but the executor fails while it runs: the stop must
  // not look successful.
  session_.fail_spin_during_flush = true;
  session_.spin_error = "executor died mid flush";
  ASSERT_TRUE(peer_->send("{\"op\":\"stop\"}\n"));
  EXPECT_EQ(chaos::data::runRecorderControlLoop(session_, worker_fd_, kFastPollMs), 1);
  EXPECT_EQ(session_.flush_calls, 1);
  EXPECT_EQ(session_.shutdown_calls, 1);
  const nlohmann::json frame = peer_->receive(1.0);
  ASSERT_TRUE(frame.is_object());
  EXPECT_EQ(frame.value("ok", true), false);
  EXPECT_EQ(frame.value("operation", std::string()), "stop");
  EXPECT_NE(frame.value("error", std::string()).find("recorder executor failed"),
            std::string::npos);
}

TEST_F(RecorderControlLoopTest, FailedFlushIsNeverAcknowledgedAsSuccess)
{
  session_.flush_fails = true;
  ASSERT_TRUE(peer_->send("{\"op\":\"stop\"}\n"));
  EXPECT_EQ(chaos::data::runRecorderControlLoop(session_, worker_fd_, kFastPollMs), 1);
  EXPECT_EQ(session_.flush_calls, 1);
  EXPECT_EQ(session_.shutdown_calls, 1);
  const nlohmann::json frame = peer_->receive(1.0);
  ASSERT_TRUE(frame.is_object());
  EXPECT_EQ(frame.value("ok", true), false);
  EXPECT_EQ(frame.value("operation", std::string()), "stop");
  EXPECT_NE(frame.value("error", std::string()).find("flush exploded"), std::string::npos);
}

TEST_F(RecorderControlLoopTest, FailedStopAcknowledgementWriteIsAFailedStop)
{
  // The supervisor end is closed before the loop runs: the buffered stop
  // command is still delivered to the first read, but the success
  // acknowledgement cannot be sent. A stop whose success ACK cannot be
  // delivered must return failure, not 0.
  ASSERT_TRUE(peer_->send("{\"op\":\"stop\"}\n"));
  peer_.reset();  // close the supervisor end
  EXPECT_EQ(chaos::data::runRecorderControlLoop(session_, worker_fd_, kFastPollMs), 1);
  EXPECT_EQ(session_.flush_calls, 1);
  EXPECT_EQ(session_.shutdown_calls, 1);
}

TEST_F(RecorderControlLoopTest, PauseResumeAndStopAreAckedInOrder)
{
  ASSERT_TRUE(peer_->send(
    "{\"op\":\"pause\"}\n{\"op\":\"resume\"}\n{\"op\":\"stop\"}\n"));
  EXPECT_EQ(chaos::data::runRecorderControlLoop(session_, worker_fd_, kFastPollMs), 0);
  EXPECT_EQ(session_.pause_calls, 1);
  EXPECT_EQ(session_.resume_calls, 1);
  EXPECT_EQ(session_.flush_calls, 1);
  const nlohmann::json pause = peer_->receive(1.0);
  const nlohmann::json resume = peer_->receive(1.0);
  const nlohmann::json stop = peer_->receive(1.0);
  ASSERT_TRUE(pause.is_object());
  ASSERT_TRUE(resume.is_object());
  ASSERT_TRUE(stop.is_object());
  EXPECT_EQ(pause.value("ok", false), true);
  EXPECT_EQ(pause.value("operation", std::string()), "pause");
  EXPECT_EQ(resume.value("ok", false), true);
  EXPECT_EQ(resume.value("operation", std::string()), "resume");
  EXPECT_EQ(stop.value("ok", false), true);
  EXPECT_EQ(stop.value("operation", std::string()), "stop");
}

TEST_F(RecorderControlLoopTest, UnknownOperationIsAProtocolError)
{
  ASSERT_TRUE(peer_->send("{\"op\":\"bogus\"}\n"));
  EXPECT_THROW(chaos::data::runRecorderControlLoop(session_, worker_fd_, kFastPollMs),
               std::runtime_error);
}

TEST_F(RecorderControlLoopTest, StartAcknowledgementIsNotWrittenAfterExecutorFailure)
{
  session_.spin_failed = true;
  session_.spin_error = "died during start";
  EXPECT_FALSE(chaos::data::acknowledgeRecorderStart(session_, worker_fd_, 4242));
  const nlohmann::json frame = peer_->receive(1.0);
  ASSERT_TRUE(frame.is_object());
  EXPECT_EQ(frame.value("ok", true), false);
  EXPECT_EQ(frame.value("operation", std::string()), "start");
  EXPECT_NE(frame.value("error", std::string()).find("recorder executor failed"),
            std::string::npos);
}

TEST_F(RecorderControlLoopTest, StartAcknowledgementIsWrittenForAHealthyExecutor)
{
  ASSERT_TRUE(chaos::data::acknowledgeRecorderStart(session_, worker_fd_, 4242));
  const nlohmann::json frame = peer_->receive(1.0);
  ASSERT_TRUE(frame.is_object());
  EXPECT_EQ(frame.value("ok", false), true);
  EXPECT_EQ(frame.value("operation", std::string()), "start");
  EXPECT_EQ(frame.value("pid", 0), 4242);
}

// --- Failed-executor exit watchdog (forked regressions) ---------------------
//
// The watchdog ends the process with std::_Exit(EXIT_FAILURE), so these
// regressions run the control loop in a forked child and assert on the child
// exit status: a wedged failure teardown must exit nonzero within the budget,
// and a normal stop must never trigger the exit guard.

class HungShutdownSession : public chaos::data::RecorderControlSession
{
public:
  bool spinFailed() const override { return true; }
  std::string spinError() const override { return "synthetic executor explosion"; }
  bool flush(std::string &) override { return true; }
  void pause() override {}
  void resume() override {}
  // A wedged teardown: simulates a recorder stop/join that never returns.
  void shutdown() noexcept override
  {
    for (;;) {
      struct pollfd idle{-1, 0, 0};
      ::poll(&idle, 1, -1);
    }
  }
};

class HealthySession : public chaos::data::RecorderControlSession
{
public:
  bool spinFailed() const override { return false; }
  std::string spinError() const override { return ""; }
  bool flush(std::string &) override { return true; }
  void pause() override {}
  void resume() override {}
  void shutdown() noexcept override {}
};

// Waits for the child with a deadline; on timeout kills it so a broken
// watchdog cannot hang the test runner. Returns true when the child exited
// (status filled), false on timeout or wait failure.
bool waitChildWithDeadline(pid_t pid, double deadline_s, int & status)
{
  const double deadline = chaos::monotonicSeconds() + deadline_s;
  while (true) {
    const pid_t result = ::waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      return true;
    }
    if (result < 0 && errno != EINTR) {
      return false;
    }
    if (chaos::monotonicSeconds() >= deadline) {
      ::kill(pid, SIGKILL);
      while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
      return false;
    }
    ::usleep(10 * 1000);
  }
}

TEST(RecorderFailureWatchdogForkTest, HungFailureTeardownExitsNonzeroWithinBudget)
{
  int fds[2] = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  const pid_t pid = ::fork();
  ASSERT_NE(pid, -1);
  if (pid == 0) {
    // Child: the isolated recorder process. The supervisor end is gone and
    // the failed-executor teardown wedges; the watchdog must end the child
    // hard within its budget. No gtest assertions here: the child only
    // reports through its exit status.
    ::close(fds[1]);
    HungShutdownSession session;
    const int rc = chaos::data::runRecorderControlLoop(session, fds[0], 20, 0.3);
    // Unreachable while the teardown hangs; deliberately distinct from
    // EXIT_FAILURE so the watchdog firing is what the parent observes.
    ::close(fds[0]);
    std::_Exit(rc == EXIT_FAILURE ? 10 : 11);
  }
  ::close(fds[0]);
  ::close(fds[1]);
  const double start = chaos::monotonicSeconds();
  int status = 0;
  ASSERT_TRUE(waitChildWithDeadline(pid, 10.0, status));
  const double elapsed = chaos::monotonicSeconds() - start;
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), EXIT_FAILURE);
  // The watchdog fired near its 0.3 s budget, not near the wait deadline.
  EXPECT_LT(elapsed, 5.0);
}

TEST(RecorderFailureWatchdogForkTest, NormalStopDoesNotTriggerExitGuard)
{
  int fds[2] = {-1, -1};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  const pid_t pid = ::fork();
  ASSERT_NE(pid, -1);
  if (pid == 0) {
    // Child: healthy recording, explicit stop. The exit guard is armed only
    // on the failed-executor path, so this child must exit 0.
    ::close(fds[1]);
    HealthySession session;
    const int rc = chaos::data::runRecorderControlLoop(session, fds[0], 20, 0.3);
    ::close(fds[0]);
    std::_Exit(rc);
  }
  ::close(fds[0]);
  {
    ControlPeer peer(fds[1]);
    ASSERT_TRUE(peer.send("{\"op\":\"stop\"}\n"));
    const nlohmann::json frame = peer.receive(1.0);
    ASSERT_TRUE(frame.is_object());
    EXPECT_EQ(frame.value("ok", false), true);
    EXPECT_EQ(frame.value("operation", std::string()), "stop");
  }
  int status = 0;
  ASSERT_TRUE(waitChildWithDeadline(pid, 10.0, status));
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

}  // namespace
