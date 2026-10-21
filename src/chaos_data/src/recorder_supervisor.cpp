// Bounded supervision of a standalone native rosbag2 recorder worker.
#include "chaos_data/recorder_supervisor.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <utility>
#include <vector>

#include <ament_index_cpp/get_package_prefix.hpp>

#include "chaos_core/clock.hpp"
#include "chaos_data/control_channel.hpp"

extern char ** environ;

namespace chaos::data {
namespace {

constexpr std::size_t kMaxFrame = 64 * 1024;
constexpr double kKillGraceS = 1.0;
constexpr double kMaxTimeoutS = 3600.0;

// Owns a descriptor and closes it exactly once unless ownership is released.
class ScopedFd {
public:
  ScopedFd() = default;
  explicit ScopedFd(int fd) : fd_(fd) {}
  ScopedFd(const ScopedFd &) = delete;
  ScopedFd & operator=(const ScopedFd &) = delete;
  ScopedFd(ScopedFd && other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  ScopedFd & operator=(ScopedFd && other) noexcept {
    if (this != &other) {
      reset();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }
  ~ScopedFd() { reset(); }

  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }
  int release() {
    const int fd = fd_;
    fd_ = -1;
    return fd;
  }
  void reset(int fd = -1) {
    if (fd_ >= 0) {
      ::close(fd_);
    }
    fd_ = fd;
  }

private:
  int fd_ = -1;
};

// RAII wrapper for posix_spawn file actions.
class SpawnActions {
public:
  SpawnActions() {
    const int result = posix_spawn_file_actions_init(&actions_);
    if (result != 0) {
      throw std::system_error(result, std::system_category(),
                              "posix_spawn_file_actions_init");
    }
  }
  SpawnActions(const SpawnActions &) = delete;
  SpawnActions & operator=(const SpawnActions &) = delete;
  ~SpawnActions() { posix_spawn_file_actions_destroy(&actions_); }
  posix_spawn_file_actions_t * get() { return &actions_; }

private:
  posix_spawn_file_actions_t actions_{};
};

// RAII wrapper for posix_spawn attributes.
class SpawnAttributes {
public:
  SpawnAttributes() {
    const int result = posix_spawnattr_init(&attributes_);
    if (result != 0) {
      throw std::system_error(result, std::system_category(),
                              "posix_spawnattr_init");
    }
  }
  SpawnAttributes(const SpawnAttributes &) = delete;
  SpawnAttributes & operator=(const SpawnAttributes &) = delete;
  ~SpawnAttributes() { posix_spawnattr_destroy(&attributes_); }
  posix_spawnattr_t * get() { return &attributes_; }

private:
  posix_spawnattr_t attributes_{};
};

  void checkSpawn(int result, const char * what) {
    if (result != 0) {
      throw std::system_error(result, std::system_category(),
                              std::string("posix_spawn setup: ") + what);
    }
  }

double finiteTimeout(double value, const char * name) {
  if (!std::isfinite(value) || value <= 0 || value > kMaxTimeoutS) {
    throw std::invalid_argument(std::string(name) +
                                " must be finite and in (0, 3600] seconds");
  }
  return value;
}

int pollTimeoutMs(double remaining_s) {
  if (remaining_s <= 0) {
    return 0;
  }
  const double ms = std::ceil(remaining_s * 1000.0);
  if (ms >= static_cast<double>(std::numeric_limits<int>::max())) {
    return std::numeric_limits<int>::max();
  }
  return static_cast<int>(ms);
}

std::string describeStatus(int status) {
  if (WIFEXITED(status)) {
    return "exit code " + std::to_string(WEXITSTATUS(status));
  }
  if (WIFSIGNALED(status)) {
    return "signal " + std::to_string(WTERMSIG(status));
  }
  return "status " + std::to_string(status);
}

// Resolves the worker ELF. The environment override exists for out-of-tree
// tests; production resolves the installed worker through the ament index.
std::string workerPath() {
  if (const char * override_value = std::getenv("CHAOS_DATA_RECORDER_WORKER")) {
    if (*override_value != '\0') {
      return override_value;
    }
  }
  std::string prefix;
  try {
    prefix = ament_index_cpp::get_package_prefix("chaos_data");
  } catch (const std::exception & error) {
    throw std::runtime_error(
      std::string("cannot resolve the chaos_data install prefix: ") +
      error.what() +
      " (set CHAOS_DATA_RECORDER_WORKER for out-of-tree tests)");
  }
  const std::filesystem::path path =
    std::filesystem::path(prefix) / "lib" / "chaos_data" /
    "chaos_data_recorder_worker";
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    throw std::runtime_error(
      "recorder worker ELF not found: " + path.string() +
      " (set CHAOS_DATA_RECORDER_WORKER for out-of-tree tests)");
  }
  return path.string();
}

}  // namespace

ChildFdMapping planChildFdMapping(int child_fd) {
  ChildFdMapping mapping;
  if (child_fd == kControlFd) {
    // A self-dup would keep FD_CLOEXEC set, so the channel would be closed
    // by the exec. Park the descriptor first.
    mapping.source = kControlScratchFd;
    mapping.scratch = kControlScratchFd;
  } else {
    mapping.source = child_fd;
  }
  return mapping;
}

NativeRecorder::NativeRecorder(const nlohmann::json & config, std::string uri,
                               std::string node_name, double startup_timeout_s,
                               double shutdown_timeout_s)
: config_(config), uri_(std::move(uri)), node_name_(std::move(node_name)),
  startup_timeout_s_(finiteTimeout(startup_timeout_s, "startup_timeout_s")),
  shutdown_timeout_s_(finiteTimeout(shutdown_timeout_s, "shutdown_timeout_s")) {
}

NativeRecorder::~NativeRecorder() {
  try {
    closeControlChannel();
    terminateChild();
  } catch (...) {
  }
}

void NativeRecorder::refreshLiveness() const {
  if (pid_ <= 0 || !alive_) {
    return;
  }
  int status = 0;
  const pid_t result = ::waitpid(pid_, &status, WNOHANG);
  if (result == pid_) {
    alive_ = false;
    reaped_ = true;
    exit_status_ = status;
  } else if (result < 0 && (errno == ECHILD || errno == EINTR)) {
    if (errno == ECHILD) {
      alive_ = false;
      reaped_ = true;
    }
  } else if (result < 0) {
    alive_ = false;
    reaped_ = true;
    exit_status_ = -1;
  }
}

bool NativeRecorder::alive() const {
  refreshLiveness();
  return alive_;
}

void NativeRecorder::closeControlChannel() {
  if (control_fd_ >= 0) {
    ::close(control_fd_);
    control_fd_ = -1;
  }
}

void NativeRecorder::sendFrame(const std::string & operation, double deadline) {
  if (control_fd_ < 0) {
    throw std::runtime_error("native recorder control channel is closed");
  }
  const std::string frame =
    nlohmann::json{{"op", operation}}.dump() + "\n";
  if (frame.size() > kMaxFrame) {
    throw std::runtime_error("native recorder control frame oversized");
  }
  std::size_t sent = 0;
  while (sent < frame.size()) {
    refreshLiveness();
    if (!alive_) {
      throw std::runtime_error("native recorder exited before the request was sent");
    }
    const double remaining = deadline - chaos::monotonicSeconds();
    if (remaining <= 0) {
      throw std::runtime_error("native recorder " + operation +
                               " exceeded its deadline while sending");
    }
    const ssize_t result =
      ::send(control_fd_, frame.data() + sent, frame.size() - sent,
             MSG_NOSIGNAL);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        struct pollfd descriptor{control_fd_, POLLOUT, 0};
        const int ready = ::poll(&descriptor, 1, pollTimeoutMs(remaining));
        if (ready < 0 && errno != EINTR) {
          throw std::runtime_error(std::string("recorder control poll failed: ") +
                                   std::strerror(errno));
        }
        continue;
      }
      throw std::runtime_error(std::string("recorder control send failed: ") +
                               std::strerror(errno));
    }
    if (result == 0) {
      throw std::runtime_error("native recorder control channel closed while sending");
    }
    sent += static_cast<std::size_t>(result);
  }
}

nlohmann::json NativeRecorder::receiveAck(const std::string & operation,
                                          double deadline) {
  if (control_fd_ < 0) {
    throw std::runtime_error("native recorder control channel is closed");
  }
  std::string frame;
  while (true) {
    // A complete frame that is already buffered is accepted: the data was
    // received before the deadline.
    const std::size_t newline = frame.find('\n');
    if (newline != std::string::npos) {
      return parseAck(frame.substr(0, newline));
    }
    // The deadline is checked on every iteration, not only when the read
    // would block: a peer that keeps sending must not extend the wait.
    if (chaos::monotonicSeconds() >= deadline) {
      throw std::runtime_error("native recorder " + operation +
                               " exceeded its deadline while waiting for the "
                               "acknowledgement");
    }
    // Always drain the socket first. A worker may have written the
    // acknowledgement and exited before this call; the data is still in the
    // socket buffer and must not be lost because the child was reaped.
    const ssize_t count =
      ::recv(control_fd_, buffer_, sizeof(buffer_), MSG_DONTWAIT);
    if (count > 0) {
      frame.append(buffer_, static_cast<std::size_t>(count));
      if (frame.size() > kMaxFrame) {
        throw std::runtime_error("native recorder acknowledgement oversized");
      }
      continue;
    }
    if (count == 0) {
      throw std::runtime_error(
        "native recorder closed the control channel before acknowledging " +
        operation);
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      throw std::runtime_error(std::string("recorder control read failed: ") +
                               std::strerror(errno));
    }
    refreshLiveness();
    if (!alive_) {
      // The worker exited. The acknowledgement may have been written just
      // before the exit, so drain once more before reporting the failure.
      const ssize_t final_count =
        ::recv(control_fd_, buffer_, sizeof(buffer_), MSG_DONTWAIT);
      if (final_count > 0) {
        frame.append(buffer_, static_cast<std::size_t>(final_count));
        if (frame.size() > kMaxFrame) {
          throw std::runtime_error("native recorder acknowledgement oversized");
        }
        const std::size_t final_newline = frame.find('\n');
        if (final_newline != std::string::npos) {
          return parseAck(frame.substr(0, final_newline));
        }
      }
      throw std::runtime_error("native recorder exited (" +
                               describeStatus(exit_status_) + ")");
    }
    const double remaining = deadline - chaos::monotonicSeconds();
    if (remaining <= 0) {
      throw std::runtime_error("native recorder " + operation +
                               " exceeded its deadline while waiting for the "
                               "acknowledgement");
    }
    struct pollfd descriptor{control_fd_, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, pollTimeoutMs(remaining));
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error(std::string("recorder control poll failed: ") +
                               std::strerror(errno));
    }
  }
}

nlohmann::json NativeRecorder::parseAck(const std::string & payload) {
  nlohmann::json reply;
  try {
    reply = nlohmann::json::parse(payload);
  } catch (const nlohmann::json::parse_error &) {
    throw std::runtime_error("native recorder sent malformed acknowledgement");
  }
  if (!reply.is_object()) {
    throw std::runtime_error("native recorder sent a malformed acknowledgement");
  }
  return reply;
}

void NativeRecorder::exchange(const std::string & operation, double timeout_s) {
  // One steady deadline covers the request write and the acknowledgement
  // read; neither half may restart the clock.
  const double deadline = chaos::monotonicSeconds() + timeout_s;
  sendFrame(operation, deadline);
  const nlohmann::json reply = receiveAck(operation, deadline);
  if (!reply.value("ok", false)) {
    throw std::runtime_error("native recorder: " +
                             reply.value("error", std::string("failed")));
  }
  if (reply.value("operation", std::string()) != operation) {
    throw std::runtime_error("unexpected native recorder acknowledgement");
  }
}

void NativeRecorder::start() {
  if (started_) {
    throw std::runtime_error("native recorder has already been started");
  }
  int raw[2] = {-1, -1};
  if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, raw) != 0) {
    throw std::system_error(errno, std::generic_category(),
                            "create recorder control socket");
  }
  ScopedFd parent_end(raw[0]);
  ScopedFd child_end(raw[1]);

  // The control channel must never block the supervisor: a wedged worker
  // must be detected by the deadline, not by a stuck write.
  const int flags = ::fcntl(parent_end.get(), F_GETFL, 0);
  if (flags < 0) {
    throw std::system_error(errno, std::generic_category(),
                            "read recorder control socket flags");
  }
  if (::fcntl(parent_end.get(), F_SETFL, flags | O_NONBLOCK) != 0) {
    throw std::system_error(errno, std::generic_category(),
                            "set recorder control socket non-blocking");
  }

  const std::string worker = workerPath();
  const std::string config = config_.dump();
  const std::string fd_arg = std::to_string(kControlFd);
  // The worker verifies that this process is still its parent; the parent
  // death signal alone cannot cover the window before prctl() runs.
  const std::string parent_arg = std::to_string(static_cast<long>(::getpid()));
  std::vector<char *> argv;
  argv.push_back(const_cast<char *>(worker.c_str()));
  argv.push_back(const_cast<char *>(fd_arg.c_str()));
  argv.push_back(const_cast<char *>(uri_.c_str()));
  argv.push_back(const_cast<char *>(node_name_.c_str()));
  argv.push_back(const_cast<char *>(config.c_str()));
  argv.push_back(const_cast<char *>(parent_arg.c_str()));
  argv.push_back(nullptr);

  const ChildFdMapping mapping = planChildFdMapping(child_end.get());
  SpawnActions actions;
  if (mapping.scratch != 0) {
    checkSpawn(posix_spawn_file_actions_adddup2(actions.get(), child_end.get(),
                                                mapping.scratch),
               "park colliding child descriptor");
  }
  checkSpawn(posix_spawn_file_actions_adddup2(actions.get(), mapping.source,
                                              kControlFd),
             "map child control descriptor");
  if (mapping.scratch != 0) {
    checkSpawn(posix_spawn_file_actions_addclose(actions.get(), mapping.scratch),
               "close scratch descriptor");
  }

  SpawnAttributes attributes;
  const int spawn_result =
    ::posix_spawnp(&pid_, worker.c_str(), actions.get(), attributes.get(),
                   argv.data(), environ);
  if (spawn_result != 0) {
    // The descriptors are still owned by the RAII wrappers above; the child
    // was never created, so nothing has to be reaped.
    pid_ = -1;
    throw std::system_error(spawn_result, std::system_category(),
                            "spawn native recorder worker");
  }
  // posix_spawn does not consume the parent's descriptors: the parent must
  // close its copy of the child end, otherwise the worker never sees EOF and
  // the descriptor leaks for the lifetime of the supervisor.
  child_end.reset();
  control_fd_ = parent_end.release();
  started_ = true;
  alive_ = true;

  try {
    // The worker owns the start handshake: it acknowledges "start" on its
    // own once the recorder is running. The supervisor must not send a
    // "start" request; a worker would treat it as an unknown operation.
    const double deadline = chaos::monotonicSeconds() + startup_timeout_s_;
    const nlohmann::json reply = receiveAck("start", deadline);
    if (!reply.value("ok", false)) {
      throw std::runtime_error(
        "native recorder failed to start: " +
        reply.value("error", std::string("unknown error")));
    }
    if (reply.value("operation", std::string()) != "start") {
      throw std::runtime_error("unexpected native recorder start acknowledgement");
    }
    start_acknowledged_ = true;
  } catch (...) {
    closeControlChannel();
    terminateChild();
    throw;
  }
}

bool NativeRecorder::waitForExit(double deadline) {
  while (true) {
    refreshLiveness();
    if (!alive_) {
      return true;
    }
    if (chaos::monotonicSeconds() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

void NativeRecorder::setPaused(bool paused) {
  if (!alive()) {
    throw std::runtime_error("native recorder is not running");
  }
  if (paused == paused_) {
    return;
  }
  const std::string operation = paused ? "pause" : "resume";
  exchange(operation, 2.0);
  paused_ = paused;
}

bool NativeRecorder::terminateChild() {
  if (pid_ <= 0) {
    return true;
  }
  refreshLiveness();
  if (!alive_) {
    reaped_ = true;
    pid_ = -1;
    return true;
  }
  ::kill(pid_, SIGTERM);
  const double grace = chaos::monotonicSeconds() + kKillGraceS;
  while (alive() && chaos::monotonicSeconds() < grace) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (alive()) {
    ::kill(pid_, SIGKILL);
    const double hard = chaos::monotonicSeconds() + kKillGraceS;
    while (alive() && chaos::monotonicSeconds() < hard) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  if (alive()) {
    // The child is unkillable (uninterruptible sleep). Keep the pid so the
    // destructor retries instead of pretending the process is gone.
    return false;
  }
  reaped_ = true;
  pid_ = -1;
  return true;
}

void NativeRecorder::stop() {
  if (!started_ || pid_ <= 0) {
    closeControlChannel();
    return;
  }
  try {
    if (!alive()) {
      throw std::runtime_error("native recorder exited (" +
                               describeStatus(exit_status_) + ")");
    }
    // One deadline covers the request, the acknowledgement and the exit.
    const double deadline = chaos::monotonicSeconds() + shutdown_timeout_s_;
    sendFrame("stop", deadline);
    const nlohmann::json reply = receiveAck("stop", deadline);
    if (!reply.value("ok", false)) {
      throw std::runtime_error(
        "native recorder failed to stop: " +
        reply.value("error", std::string("unknown error")));
    }
    if (reply.value("operation", std::string()) != "stop") {
      throw std::runtime_error("unexpected native recorder stop acknowledgement");
    }
    // An acknowledgement is not a success: the worker must also exit with
    // status 0. A crash or a signal after the acknowledgement is a failure.
    // Only the remaining budget of the single deadline is left.
    if (!waitForExit(deadline)) {
      throw std::runtime_error("native recorder did not exit after stop");
    }
    if (!WIFEXITED(exit_status_) || WEXITSTATUS(exit_status_) != 0) {
      throw std::runtime_error("native recorder exited with " +
                               describeStatus(exit_status_) +
                               " after acknowledging stop");
    }
  } catch (...) {
    closeControlChannel();
    terminateChild();
    throw;
  }
  closeControlChannel();
  terminateChild();
  paused_ = false;
}

}  // namespace chaos::data
