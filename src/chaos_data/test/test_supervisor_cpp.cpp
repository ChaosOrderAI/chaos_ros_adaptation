// Real process tests for the native recorder supervisor.
//
// These tests spawn an actual worker executable (a fake worker that speaks
// the real control protocol) so descriptor ownership, the start handshake,
// signal handling, bounded deadlines and cleanup are exercised for real
// instead of inspected statically.
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <sstream>

#include <ament_index_cpp/get_package_prefix.hpp>

#include "chaos_data/control_channel.hpp"
#include "chaos_data/recorder_supervisor.hpp"

namespace {

std::size_t openDescriptorCount()
{
  std::size_t count = 0;
  std::error_code error;
  for (const auto & entry :
       std::filesystem::directory_iterator("/proc/self/fd", error)) {
    (void)entry;
    ++count;
  }
  return count;
}

class TemporaryDirectory
{
public:
  TemporaryDirectory()
  {
    const auto base = std::filesystem::temp_directory_path();
    for (int attempt = 0; attempt < 64; ++attempt) {
      const auto candidate =
        base / ("chaos_data_supervisor_" + std::to_string(::getpid()) + "_" +
                std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(candidate, error)) {
        path_ = candidate;
        return;
      }
    }
    throw std::runtime_error("cannot create a temporary directory");
  }
  ~TemporaryDirectory()
  {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  const std::filesystem::path & path() const { return path_; }

private:
  std::filesystem::path path_;
};

nlohmann::json workerConfig()
{
  return {
    {"storage_id", "mcap"},
    {"max_bagfile_size_mb", 256},
    {"max_bagfile_duration_s", 0},
    {"max_cache_size_mb", 64},
    {"topics", std::vector<std::string>{"/chaos/camera/image_raw"}},
  };
}

class ScopedEnvironment
{
public:
  ScopedEnvironment(const char * name, const std::string & value)
  : name_(name)
  {
    const char * previous = std::getenv(name);
    if (previous != nullptr) {
      previous_ = previous;
      had_previous_ = true;
    }
    ::setenv(name, value.c_str(), 1);
  }
  ~ScopedEnvironment()
  {
    if (had_previous_) {
      ::setenv(name_, previous_.c_str(), 1);
    } else {
      ::unsetenv(name_);
    }
  }

private:
  const char * name_;
  std::string previous_;
  bool had_previous_ = false;
};

// Removes a variable for the lifetime of the object.
class ScopedUnsetEnvironment
{
public:
  explicit ScopedUnsetEnvironment(const char * name)
  : name_(name)
  {
    const char * previous = std::getenv(name);
    if (previous != nullptr) {
      previous_ = previous;
      had_previous_ = true;
    }
    ::unsetenv(name);
  }
  ~ScopedUnsetEnvironment()
  {
    if (had_previous_) {
      ::setenv(name_, previous_.c_str(), 1);
    }
  }

private:
  const char * name_;
  std::string previous_;
  bool had_previous_ = false;
};

std::vector<std::string> readReport(const std::filesystem::path & path)
{
  std::vector<std::string> lines;
  std::ifstream input(path.string());
  std::string line;
  while (std::getline(input, line)) {
    lines.push_back(line);
  }
  return lines;
}

}  // namespace

TEST(ControlChannel, PlanChildFdMappingAvoidsSelfDup)
{
  const chaos::data::ChildFdMapping collision =
    chaos::data::planChildFdMapping(chaos::data::kControlFd);
  EXPECT_EQ(collision.source, chaos::data::kControlScratchFd);
  EXPECT_EQ(collision.scratch, chaos::data::kControlScratchFd);
  EXPECT_NE(collision.source, chaos::data::kControlFd);

  const chaos::data::ChildFdMapping plain =
    chaos::data::planChildFdMapping(chaos::data::kControlFd + 7);
  EXPECT_EQ(plain.source, chaos::data::kControlFd + 7);
  EXPECT_EQ(plain.scratch, 0);
}

// The mapping must clear FD_CLOEXEC on the fixed descriptor: a self-dup
// would keep the flag and the exec would close the control channel.
TEST(ControlChannel, MappingClearsCloseOnExec)
{
  int pipe_fds[2] = {-1, -1};
  ASSERT_EQ(::pipe(pipe_fds), 0);

  int saved = -1;
  const bool had_control =
    (::fcntl(chaos::data::kControlFd, F_GETFD, 0) >= 0);
  if (had_control) {
    saved = ::dup(chaos::data::kControlFd);
    ASSERT_GE(saved, 0);
  }

  // Emulate the collision branch: the child descriptor is the fixed one.
  ASSERT_EQ(::dup2(pipe_fds[0], chaos::data::kControlScratchFd),
            chaos::data::kControlScratchFd);
  ASSERT_EQ(::dup2(pipe_fds[0], chaos::data::kControlFd), chaos::data::kControlFd);
  const chaos::data::ChildFdMapping mapping =
    chaos::data::planChildFdMapping(chaos::data::kControlFd);
  ASSERT_EQ(mapping.source, chaos::data::kControlScratchFd);
  ASSERT_EQ(::dup2(mapping.source, chaos::data::kControlFd), chaos::data::kControlFd);
  EXPECT_EQ(::fcntl(chaos::data::kControlFd, F_GETFD, 0) & FD_CLOEXEC, 0);

  ::close(chaos::data::kControlScratchFd);
  if (had_control) {
    ::dup2(saved, chaos::data::kControlFd);
    ::close(saved);
  } else {
    ::close(chaos::data::kControlFd);
  }
  ::close(pipe_fds[0]);
  ::close(pipe_fds[1]);
}

TEST(Supervisor, StartPauseResumeStopAgainstRealWorker)
{
  TemporaryDirectory directory;
  const auto report = directory.path() / "worker_report.jsonl";
  ScopedEnvironment worker(CHAOS_DATA_FAKE_WORKER_ENV,
                           std::string(CHAOS_DATA_FAKE_WORKER_PATH));
  ScopedEnvironment mode("CHAOS_DATA_FAKE_WORKER_MODE", "normal");
  ScopedEnvironment report_env("CHAOS_DATA_FAKE_WORKER_REPORT", report.string());

  const std::size_t descriptors_before = openDescriptorCount();
  {
    chaos::data::NativeRecorder recorder(workerConfig(),
                                         (directory.path() / "bag").string(),
                                         "chaos_test_recorder", 5.0, 5.0);
    recorder.start();
    EXPECT_TRUE(recorder.alive());
    EXPECT_FALSE(recorder.paused());
    recorder.setPaused(true);
    EXPECT_TRUE(recorder.paused());
    recorder.setPaused(false);
    EXPECT_FALSE(recorder.paused());
    recorder.stop();
    EXPECT_FALSE(recorder.alive());
    EXPECT_TRUE(recorder.reaped());
    EXPECT_TRUE(WIFEXITED(recorder.exitStatus()));
    EXPECT_EQ(WEXITSTATUS(recorder.exitStatus()), 0);
  }
  const std::size_t descriptors_after = openDescriptorCount();
  EXPECT_LE(descriptors_after, descriptors_before)
      << "the supervisor leaked descriptors";

  const auto lines = readReport(report);
  ASSERT_FALSE(lines.empty());
  const nlohmann::json descriptor = nlohmann::json::parse(lines.front());
  EXPECT_EQ(descriptor.at("event").get<std::string>(), "descriptor");
  EXPECT_EQ(descriptor.at("fd").get<int>(), chaos::data::kControlFd);
  EXPECT_EQ(descriptor.at("cloexec").get<int>(), 0)
      << "the control descriptor must not be close-on-exec in the worker";

  // The supervisor must never send a start request: the worker owns the
  // start handshake and would treat an unknown operation as a fatal error.
  std::vector<std::string> operations;
  for (std::size_t index = 1; index < lines.size(); ++index) {
    const nlohmann::json entry = nlohmann::json::parse(lines[index]);
    if (entry.value("event", std::string()) == "operation") {
      operations.push_back(entry.at("op").get<std::string>());
    }
  }
  EXPECT_EQ(operations, (std::vector<std::string>{"pause", "resume", "stop"}));
}

TEST(Supervisor, CrashingWorkerIsDetected)
{
  TemporaryDirectory directory;
  ScopedEnvironment worker(CHAOS_DATA_FAKE_WORKER_ENV,
                           std::string(CHAOS_DATA_FAKE_WORKER_PATH));
  ScopedEnvironment mode("CHAOS_DATA_FAKE_WORKER_MODE", "crash");

  chaos::data::NativeRecorder recorder(workerConfig(),
                                       (directory.path() / "bag").string(),
                                       "chaos_test_recorder", 5.0, 5.0);
  EXPECT_THROW(recorder.start(), std::exception);
  EXPECT_FALSE(recorder.alive());
  EXPECT_TRUE(recorder.reaped());
}

TEST(Supervisor, HangingWorkerIsBoundedByTheStartupDeadline)
{
  TemporaryDirectory directory;
  ScopedEnvironment worker(CHAOS_DATA_FAKE_WORKER_ENV,
                           std::string(CHAOS_DATA_FAKE_WORKER_PATH));
  ScopedEnvironment mode("CHAOS_DATA_FAKE_WORKER_MODE", "hang_start");

  chaos::data::NativeRecorder recorder(workerConfig(),
                                       (directory.path() / "bag").string(),
                                       "chaos_test_recorder", 1.0, 1.0);
  const auto begin = std::chrono::steady_clock::now();
  EXPECT_THROW(recorder.start(), std::exception);
  const auto elapsed = std::chrono::duration<double>(
    std::chrono::steady_clock::now() - begin).count();
  EXPECT_LT(elapsed, 5.0) << "the startup deadline was not enforced";
  EXPECT_FALSE(recorder.alive());
  EXPECT_TRUE(recorder.reaped())
      << "a worker that ignored the request must still be reaped";
}

TEST(Supervisor, WorkerExitIsObserved)
{
  TemporaryDirectory directory;
  ScopedEnvironment worker(CHAOS_DATA_FAKE_WORKER_ENV,
                           std::string(CHAOS_DATA_FAKE_WORKER_PATH));
  ScopedEnvironment mode("CHAOS_DATA_FAKE_WORKER_MODE", "exit_after_start");

  chaos::data::NativeRecorder recorder(workerConfig(),
                                       (directory.path() / "bag").string(),
                                       "chaos_test_recorder", 5.0, 5.0);
  recorder.start();
  for (int attempt = 0; attempt < 200 && recorder.alive(); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_FALSE(recorder.alive()) << "the worker exit was not observed";
  EXPECT_THROW(recorder.setPaused(true), std::exception);
}

TEST(Supervisor, StopTimeoutTerminatesTheWorker)
{
  TemporaryDirectory directory;
  ScopedEnvironment worker(CHAOS_DATA_FAKE_WORKER_ENV,
                           std::string(CHAOS_DATA_FAKE_WORKER_PATH));
  ScopedEnvironment mode("CHAOS_DATA_FAKE_WORKER_MODE", "hang_stop");

  chaos::data::NativeRecorder recorder(workerConfig(),
                                       (directory.path() / "bag").string(),
                                       "chaos_test_recorder", 5.0, 1.0);
  recorder.start();
  const auto begin = std::chrono::steady_clock::now();
  EXPECT_THROW(recorder.stop(), std::exception);
  const auto elapsed = std::chrono::duration<double>(
    std::chrono::steady_clock::now() - begin).count();
  EXPECT_LT(elapsed, 10.0) << "the stop deadline was not enforced";
  EXPECT_FALSE(recorder.alive());
  EXPECT_TRUE(recorder.reaped());
}

// A stop acknowledgement that is followed by a non zero exit is a failure.
TEST(Supervisor, StopAcknowledgedWithFailureExitIsRejected)
{
  TemporaryDirectory directory;
  ScopedEnvironment worker(CHAOS_DATA_FAKE_WORKER_ENV,
                           std::string(CHAOS_DATA_FAKE_WORKER_PATH));
  ScopedEnvironment mode("CHAOS_DATA_FAKE_WORKER_MODE", "stop_exit1");

  chaos::data::NativeRecorder recorder(workerConfig(),
                                       (directory.path() / "bag").string(),
                                       "chaos_test_recorder", 5.0, 5.0);
  recorder.start();
  EXPECT_THROW(recorder.stop(), std::exception);
  EXPECT_TRUE(recorder.reaped());
  EXPECT_TRUE(WIFEXITED(recorder.exitStatus()));
  EXPECT_EQ(WEXITSTATUS(recorder.exitStatus()), 1);
}

// A truncated acknowledgement must not be accepted.
TEST(Supervisor, TruncatedStopAcknowledgementIsRejected)
{
  TemporaryDirectory directory;
  ScopedEnvironment worker(CHAOS_DATA_FAKE_WORKER_ENV,
                           std::string(CHAOS_DATA_FAKE_WORKER_PATH));
  ScopedEnvironment mode("CHAOS_DATA_FAKE_WORKER_MODE", "stop_partial");

  chaos::data::NativeRecorder recorder(workerConfig(),
                                       (directory.path() / "bag").string(),
                                       "chaos_test_recorder", 5.0, 2.0);
  recorder.start();
  EXPECT_THROW(recorder.stop(), std::exception);
  EXPECT_TRUE(recorder.reaped());
}

// A closed channel without any acknowledgement is a failure.
TEST(Supervisor, EndOfStreamWithoutAcknowledgementIsRejected)
{
  TemporaryDirectory directory;
  ScopedEnvironment worker(CHAOS_DATA_FAKE_WORKER_ENV,
                           std::string(CHAOS_DATA_FAKE_WORKER_PATH));
  ScopedEnvironment mode("CHAOS_DATA_FAKE_WORKER_MODE", "eof_no_ack");

  chaos::data::NativeRecorder recorder(workerConfig(),
                                       (directory.path() / "bag").string(),
                                       "chaos_test_recorder", 5.0, 2.0);
  recorder.start();
  EXPECT_THROW(recorder.stop(), std::exception);
  EXPECT_TRUE(recorder.reaped());
}

TEST(Supervisor, OversizedAcknowledgementIsRejected)
{
  TemporaryDirectory directory;
  ScopedEnvironment worker(CHAOS_DATA_FAKE_WORKER_ENV,
                           std::string(CHAOS_DATA_FAKE_WORKER_PATH));
  ScopedEnvironment mode("CHAOS_DATA_FAKE_WORKER_MODE", "huge");

  chaos::data::NativeRecorder recorder(workerConfig(),
                                       (directory.path() / "bag").string(),
                                       "chaos_test_recorder", 2.0, 2.0);
  EXPECT_THROW(recorder.start(), std::exception);
  EXPECT_TRUE(recorder.reaped());
}

TEST(Supervisor, NonFiniteTimeoutsAreRejected)
{
  TemporaryDirectory directory;
  const double nan_value = std::numeric_limits<double>::quiet_NaN();
  const double infinity = std::numeric_limits<double>::infinity();
  EXPECT_THROW(
    chaos::data::NativeRecorder(workerConfig(), (directory.path() / "a").string(),
                                 "chaos_test_recorder", nan_value, 1.0),
    std::exception);
  EXPECT_THROW(
    chaos::data::NativeRecorder(workerConfig(), (directory.path() / "b").string(),
                                 "chaos_test_recorder", infinity, 1.0),
    std::exception);
  EXPECT_THROW(
    chaos::data::NativeRecorder(workerConfig(), (directory.path() / "c").string(),
                                 "chaos_test_recorder", 1.0, nan_value),
    std::exception);
}

TEST(Supervisor, RepeatedCyclesDoNotLeakDescriptors)
{
  TemporaryDirectory directory;
  ScopedEnvironment worker(CHAOS_DATA_FAKE_WORKER_ENV,
                           std::string(CHAOS_DATA_FAKE_WORKER_PATH));
  ScopedEnvironment mode("CHAOS_DATA_FAKE_WORKER_MODE", "normal");

  // Warm up so that one time allocations are not counted as a leak.
  {
    chaos::data::NativeRecorder recorder(workerConfig(),
                                         (directory.path() / "warm").string(),
                                         "chaos_test_recorder", 5.0, 5.0);
    recorder.start();
    recorder.stop();
  }
  const std::size_t descriptors_before = openDescriptorCount();
  for (int cycle = 0; cycle < 5; ++cycle) {
    chaos::data::NativeRecorder recorder(
      workerConfig(), (directory.path() / ("bag" + std::to_string(cycle))).string(),
      "chaos_test_recorder", 5.0, 5.0);
    recorder.start();
    recorder.setPaused(true);
    recorder.setPaused(false);
    recorder.stop();
  }
  const std::size_t descriptors_after = openDescriptorCount();
  EXPECT_LE(descriptors_after, descriptors_before)
      << "descriptor count grew across supervisor cycles";
}

// The worker must not survive its supervisor.
//
// The scenario runs in an isolated subprocess chain:
//
//   test process
//     -> middle child  (PR_SET_CHILD_SUBREAPER: it adopts the orphan)
//          -> grandchild: starts the recorder and dies with std::_Exit
//             while the recorder object is still alive, so no destructor
//             runs and the worker is really orphaned.
//
// The middle child then waits for the adopted worker with waitpid(), so a
// zombie is distinguished from a running process and no pid is ever signalled
// by guesswork.
namespace {

enum class ParentDeathOutcome {
  kWorkerExited = 0,
  kWorkerSurvived = 1,
  kSetupFailed = 2,
};

// Finds the process that this process adopted, by scanning /proc.
std::vector<pid_t> adoptedChildren()
{
  std::vector<pid_t> children;
  std::error_code error;
  for (const auto & entry :
       std::filesystem::directory_iterator("/proc", error)) {
    const std::string name = entry.path().filename().string();
    if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos) {
      continue;
    }
    std::ifstream stat(entry.path() / "stat");
    if (!stat.good()) {
      continue;
    }
    std::string content;
    std::getline(stat, content);
    const std::size_t close = content.rfind(')');
    if (close == std::string::npos || close + 2 >= content.size()) {
      continue;
    }
    // The state is the first field after the executable name.
    std::istringstream fields(content.substr(close + 2));
    std::string state;
    int parent = 0;
    fields >> state >> parent;
    if (parent == ::getpid()) {
      children.push_back(static_cast<pid_t>(std::stol(name)));
    }
  }
  return children;
}

ParentDeathOutcome runParentDeathScenario(const std::string & worker_path,
                                           const std::string & bag_path,
                                           const std::string & mode)
{
  if (::prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0) {
    return ParentDeathOutcome::kSetupFailed;
  }
  const pid_t grandchild = ::fork();
  if (grandchild < 0) {
    return ParentDeathOutcome::kSetupFailed;
  }
  if (grandchild == 0) {
    if (!worker_path.empty()) {
      ::setenv("CHAOS_DATA_RECORDER_WORKER", worker_path.c_str(), 1);
      ::setenv("CHAOS_DATA_FAKE_WORKER_MODE", mode.c_str(), 1);
    }
    // The recorder is allocated on the heap and never released: the exit
    // below happens while it is alive, so its destructor cannot kill the
    // worker and the test cannot pass through the normal cleanup.
    auto * recorder = new chaos::data::NativeRecorder(
      workerConfig(), bag_path, "chaos_test_recorder", 10.0, 5.0);
    try {
      recorder->start();
    } catch (...) {
      std::_Exit(4);
    }
    std::_Exit(0);
  }

  int status = 0;
  if (::waitpid(grandchild, &status, 0) != grandchild) {
    return ParentDeathOutcome::kSetupFailed;
  }
  // The worker is now an orphan and, because this process is a subreaper, it
  // is adopted here. waitpid() reaps it when it ends.
  const double deadline = std::chrono::duration<double>(
                             std::chrono::steady_clock::now().time_since_epoch())
                             .count() +
                           10.0;
  while (std::chrono::duration<double>(
           std::chrono::steady_clock::now().time_since_epoch())
           .count() < deadline) {
    int child_status = 0;
    const pid_t reaped = ::waitpid(-1, &child_status, WNOHANG);
    if (reaped > 0) {
      // The adopted worker ended on its own.
      return ParentDeathOutcome::kWorkerExited;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  // The worker is still running. It is our own adopted child, so the pid
  // cannot have been reused by an unrelated process.
  for (const pid_t child : adoptedChildren()) {
    ::kill(child, SIGKILL);
    int child_status = 0;
    ::waitpid(child, &child_status, 0);
  }
  return ParentDeathOutcome::kWorkerSurvived;
}

}  // namespace

// The fake worker has no parent death signal; it ends when the control
// channel closes. This test covers the end of stream path only.
TEST(Supervisor, FakeWorkerEndsWhenTheControlChannelCloses)
{
  TemporaryDirectory directory;
  const pid_t middle = ::fork();
  ASSERT_GE(middle, 0);
  if (middle == 0) {
    const ParentDeathOutcome outcome = runParentDeathScenario(
      std::string(CHAOS_DATA_FAKE_WORKER_PATH),
      (directory.path() / "bag").string(), "normal");
    std::_Exit(static_cast<int>(outcome));
  }
  int status = 0;
  ASSERT_EQ(::waitpid(middle, &status, 0), middle);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), static_cast<int>(ParentDeathOutcome::kWorkerExited))
      << "the fake worker survived the closed control channel";
}

// The installed worker owns the parent death signal; it must end even though
// the supervisor died without any cleanup.
TEST(Supervisor, InstalledWorkerDoesNotSurviveTheSupervisor)
{
  std::string prefix;
  try {
    prefix = ament_index_cpp::get_package_prefix("chaos_data");
  } catch (const std::exception &) {
    GTEST_SKIP() << "chaos_data is not installed in this environment";
  }
  const auto worker = std::filesystem::path(prefix) / "lib" / "chaos_data" /
                      "chaos_data_recorder_worker";
  if (!std::filesystem::exists(worker)) {
    GTEST_SKIP() << "the recorder worker is not installed: " << worker;
  }
  TemporaryDirectory directory;
  ScopedEnvironment domain("ROS_DOMAIN_ID", "198");
  ScopedEnvironment localhost("ROS_LOCALHOST_ONLY", "1");
  // The installed worker must be resolved through the ament index.
  ScopedUnsetEnvironment no_override("CHAOS_DATA_RECORDER_WORKER");

  const pid_t middle = ::fork();
  ASSERT_GE(middle, 0);
  if (middle == 0) {
    // No test override: the worker is the installed ELF.
    const ParentDeathOutcome outcome =
      runParentDeathScenario(std::string(), (directory.path() / "bag").string(), "");
    std::_Exit(static_cast<int>(outcome));
  }
  int status = 0;
  ASSERT_EQ(::waitpid(middle, &status, 0), middle);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), static_cast<int>(ParentDeathOutcome::kWorkerExited))
      << "the installed worker survived its supervisor";
}

// A recorder that cannot start must report the failure instead of hanging
// or terminating the worker process.
TEST(Supervisor, InstalledWorkerReportsAStartupFailure)
{
  std::string prefix;
  try {
    prefix = ament_index_cpp::get_package_prefix("chaos_data");
  } catch (const std::exception &) {
    GTEST_SKIP() << "chaos_data is not installed in this environment";
  }
  const auto worker = std::filesystem::path(prefix) / "lib" / "chaos_data" /
                      "chaos_data_recorder_worker";
  if (!std::filesystem::exists(worker)) {
    GTEST_SKIP() << "the recorder worker is not installed: " << worker;
  }

  TemporaryDirectory directory;
  ScopedEnvironment domain("ROS_DOMAIN_ID", "198");
  ScopedUnsetEnvironment no_override("CHAOS_DATA_RECORDER_WORKER");

  auto config = workerConfig();
  // A storage plugin that does not exist makes the recorder fail.
  config["storage_id"] = "chaos_missing_storage_plugin";

  const auto begin = std::chrono::steady_clock::now();
  chaos::data::NativeRecorder recorder(config,
                                       (directory.path() / "bag").string(),
                                       "chaos_installed_recorder", 30.0, 10.0);
  EXPECT_THROW(recorder.start(), std::exception);
  const auto elapsed = std::chrono::duration<double>(
    std::chrono::steady_clock::now() - begin).count();
  EXPECT_LT(elapsed, 20.0) << "the startup failure was not reported in time";
  EXPECT_FALSE(recorder.alive());
  EXPECT_TRUE(recorder.reaped());
}

// A late stop acknowledgement must not buy a fresh exit budget: the stop
// request, the acknowledgement and the exit share one deadline.
TEST(Supervisor, LateAcknowledgementDoesNotExtendTheStopDeadline)
{
  TemporaryDirectory directory;
  ScopedEnvironment worker(CHAOS_DATA_FAKE_WORKER_ENV,
                           std::string(CHAOS_DATA_FAKE_WORKER_PATH));
  ScopedEnvironment mode("CHAOS_DATA_FAKE_WORKER_MODE", "slow_stop");
  ScopedEnvironment delay("CHAOS_DATA_FAKE_WORKER_DELAY_MS", "1500");

  chaos::data::NativeRecorder recorder(workerConfig(),
                                       (directory.path() / "bag").string(),
                                       "chaos_test_recorder", 5.0, 2.0);
  recorder.start();
  const auto begin = std::chrono::steady_clock::now();
  EXPECT_THROW(recorder.stop(), std::exception);
  const auto elapsed = std::chrono::duration<double>(
    std::chrono::steady_clock::now() - begin).count();
  // The acknowledgement arrives after 1.5s, the worker would exit after
  // 3.0s. The single 2s deadline must cut the wait short.
  EXPECT_LT(elapsed, 2.8) << "the stop deadline was restarted after the ack";
  EXPECT_TRUE(recorder.reaped());
}

// The control socket may land on the fixed descriptor 3. The whole spawn
// path is executed in a forked child that frees descriptor 2 first, so the
// socket pair really occupies 2 and 3 and the collision branch runs.
TEST(Supervisor, ControlDescriptorCollisionIsHandled)
{
  TemporaryDirectory directory;
  const auto report = directory.path() / "collision_report.jsonl";
  const std::string fake_worker = CHAOS_DATA_FAKE_WORKER_PATH;

  const pid_t child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    // Free descriptor 2 and keep it free: the socket pair then occupies 2
    // (parent end) and 3 (child end), so the child end collides with the
    // fixed control descriptor and the collision branch really runs.
    ::close(2);
    ::setenv("CHAOS_DATA_RECORDER_WORKER", fake_worker.c_str(), 1);
    ::setenv("CHAOS_DATA_FAKE_WORKER_MODE", "normal", 1);
    ::setenv("CHAOS_DATA_FAKE_WORKER_REPORT", report.string().c_str(), 1);
    int result = 0;
    try {
      // Probe which descriptors the next socket pair will occupy. The probe
      // is closed again, so the supervisor socket pair takes the same two
      // lowest free descriptors: 2 (parent end) and 3 (child end).
      int probe[2] = {-1, -1};
      if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, probe) == 0) {
        const std::string probe_line =
          "{\"event\":\"probe\",\"parent_fd\":" + std::to_string(probe[0]) +
          ",\"child_fd\":" + std::to_string(probe[1]) + "}";
        const char * report_path = std::getenv("CHAOS_DATA_FAKE_WORKER_REPORT");
        if (report_path != nullptr) {
          FILE * file = std::fopen(report_path, "a");
          if (file != nullptr) {
            std::fprintf(file, "%s\n", probe_line.c_str());
            std::fclose(file);
          }
        }
        ::close(probe[0]);
        ::close(probe[1]);
      }
      chaos::data::NativeRecorder recorder(
        workerConfig(), (directory.path() / "bag").string(),
        "chaos_test_recorder", 5.0, 5.0);
      recorder.start();
      recorder.stop();
      result = recorder.reaped() ? 0 : 1;
    } catch (...) {
      result = 2;
    }
    std::_Exit(result);
  }

  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0)
      << "the control descriptor collision broke the spawn path";

  const auto lines = readReport(report);
  ASSERT_GE(lines.size(), 2U);
  const nlohmann::json probe = nlohmann::json::parse(lines.front());
  ASSERT_EQ(probe.at("event").get<std::string>(), "probe");
  EXPECT_EQ(probe.at("child_fd").get<int>(), chaos::data::kControlFd)
      << "the probe did not reproduce the descriptor collision";
  const nlohmann::json descriptor = nlohmann::json::parse(lines.at(1));
  EXPECT_EQ(descriptor.at("fd").get<int>(), chaos::data::kControlFd);
  EXPECT_EQ(descriptor.at("cloexec").get<int>(), 0);
}

// The installed worker must start and stop without any test override. The
// recorder writes a real bag; no hardware and no remote broker are needed.
TEST(Supervisor, InstalledWorkerStartsAndStops)
{
  std::string prefix;
  try {
    prefix = ament_index_cpp::get_package_prefix("chaos_data");
  } catch (const std::exception &) {
    GTEST_SKIP() << "chaos_data is not installed in this environment";
  }
  const auto worker = std::filesystem::path(prefix) / "lib" / "chaos_data" /
                      "chaos_data_recorder_worker";
  if (!std::filesystem::exists(worker)) {
    GTEST_SKIP() << "the recorder worker is not installed: " << worker;
  }

  TemporaryDirectory directory;
  // A private ROS domain: no hardware, no remote broker.
  ScopedEnvironment domain("ROS_DOMAIN_ID", "198");
  ScopedEnvironment localhost("ROS_LOCALHOST_ONLY", "1");
  // The installed worker must be resolved through the ament index.
  ScopedUnsetEnvironment no_override("CHAOS_DATA_RECORDER_WORKER");

  auto config = workerConfig();
  config["topics"] = std::vector<std::string>{"/chaos/camera/image_raw"};
  const auto bag = directory.path() / "bag";

  chaos::data::NativeRecorder recorder(config, bag.string(),
                                       "chaos_installed_recorder", 30.0, 30.0);
  recorder.start();
  EXPECT_TRUE(recorder.alive());
  recorder.stop();
  EXPECT_TRUE(recorder.reaped());
  EXPECT_TRUE(WIFEXITED(recorder.exitStatus()));
  EXPECT_EQ(WEXITSTATUS(recorder.exitStatus()), 0);
  EXPECT_TRUE(std::filesystem::exists(bag / "metadata.yaml"))
      << "the installed worker did not write a rosbag";
}
