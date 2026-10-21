#include "chaos_assets/process.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/mman.h>
#endif

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "chaos_assets/errors.hpp"

extern char** environ;

namespace chaos::assets {

namespace {

// A file descriptor that is closed exactly once.
class ScopedFd {
 public:
  ScopedFd() = default;
  explicit ScopedFd(int fd) : fd_(fd) {}
  ScopedFd(const ScopedFd&) = delete;
  ScopedFd& operator=(const ScopedFd&) = delete;
  ScopedFd(ScopedFd&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
  ScopedFd& operator=(ScopedFd&& other) noexcept {
    if (this != &other) {
      close();
      fd_ = other.fd_;
      other.fd_ = -1;
    }
    return *this;
  }
  ~ScopedFd() { close(); }

  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }

  // Hand the descriptor over to the caller.
  //
  // The descriptor stays open, but the handle does not own it any more.
  int release() {
    const int fd = fd_;
    fd_ = -1;
    return fd;
  }

  void reset(int fd = -1) {
    close();
    fd_ = fd;
  }

  void close() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

 private:
  int fd_ = -1;
};

// Relocate a descriptor above the standard streams.
//
// The child maps its standard streams with dup2(), and a descriptor that
// already is 0, 1 or 2 would be closed by that mapping. Relocating keeps
// the standard streams and the pipe ends independent.
//
// The duplicate is handed over to the handle immediately, so a later
// failure of another relocation cannot leak it.
bool relocate_above_stdio(ScopedFd& handle) {
  if (handle.get() > STDERR_FILENO) return true;
  const int moved = ::fcntl(handle.get(), F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
  if (moved < 0) return false;
  handle.reset(moved);
  return true;
}

// Set the non-blocking flag of a pipe read end.
bool configure_read_end(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) return false;
  return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// The child process and the process group it owns.
//
// The group is terminated before the child is reaped, so the process group
// identifier still refers to the group of this child and can never be
// reused by an unrelated process.
class ChildGuard {
 public:
  ChildGuard(pid_t pid, pid_t group) : pid_(pid), group_(group) {}
  ChildGuard(const ChildGuard&) = delete;
  ChildGuard& operator=(const ChildGuard&) = delete;

  ~ChildGuard() {
    if (pid_ > 0) terminate();
  }

  // Terminate the group and reap the child. Safe to call twice.
  void terminate() {
    if (pid_ <= 0) return;
    if (group_ > 0) {
      // The child is still unreaped, so its identifier cannot be reused.
      ::kill(-group_, SIGKILL);
    }
    int status = 0;
    while (::waitpid(pid_, &status, 0) < 0) {
      if (errno != EINTR) break;
    }
    pid_ = -1;
  }

  // Report whether the child terminated, without reaping it.
  //
  // The status is converted into the encoding of waitpid(), so the caller
  // can use the usual WIFEXITED/WIFSIGNALED macros.
  bool exited(int& status) const {
    siginfo_t info;
    info.si_pid = 0;
    const int result = ::waitid(P_PID, static_cast<id_t>(pid_), &info, WEXITED | WNOHANG | WNOWAIT);
    if (result < 0) return false;
    if (info.si_pid == 0) return false;
    if (info.si_code == CLD_EXITED) {
      status = (info.si_status & 0xff) << 8;
    } else if (info.si_code == CLD_DUMPED) {
      status = (info.si_status & 0x7f) | 0x80;
    } else {
      status = info.si_status & 0x7f;
    }
    return true;
  }

  pid_t pid() const { return pid_; }
  pid_t group() const { return group_; }

 private:
  pid_t pid_;
  pid_t group_;
};

// Resolve a command against PATH.
//
// posix_spawn() does not search PATH the way execvp() does, so the lookup
// happens here. A name that contains a slash is used unchanged.
std::string resolve_executable(const std::string& command) {
  if (command.find('/') != std::string::npos) return command;
  const char* path = std::getenv("PATH");
  if (path == nullptr) return command;
  const std::string search(path);
  size_t start = 0;
  while (start <= search.size()) {
    const size_t end = search.find(':', start);
    const std::string directory =
        search.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!directory.empty()) {
      const std::string candidate = directory + "/" + command;
      if (::access(candidate.c_str(), X_OK) == 0) return candidate;
    }
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return command;
}

// Create the anonymous regular file that carries the standard input of a
// child process.
//
// The file holds the whole payload before the child is started, so the
// parent never writes to the child and cannot deadlock with it. The
// descriptor is close-on-exec: only the copy that becomes the standard
// input of the child survives the spawn.
//
// memfd avoids a persistent directory entry and releases storage with its
// last descriptor. The fallback is a mode-0600 mkstemp file, unlinked before
// use. These are lifetime properties, not isolation from privileged readers
// or processes that can inspect this process's descriptors.
int create_input_file(const std::string& where, const std::string& payload) {
  int descriptor = -1;
#ifdef __linux__
  descriptor = ::memfd_create("chaos-stdin", MFD_CLOEXEC);
#endif
  if (descriptor < 0) {
    char pattern[] = "/tmp/chaos-stdin-XXXXXX";
    descriptor = ::mkstemp(pattern);
    if (descriptor < 0) {
      throw ConfigurationError(where + ": failed to create the standard input file: " +
                               std::strerror(errno));
    }
    // Remove the temporary name immediately; the descriptor remains valid.
    ::unlink(pattern);
    const int flags = ::fcntl(descriptor, F_GETFD, 0);
    if (flags >= 0) ::fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);
  }
  ScopedFd owned(descriptor);
  size_t written = 0;
  while (written < payload.size()) {
    const ssize_t count =
        ::write(owned.get(), payload.data() + written, payload.size() - written);
    if (count > 0) {
      written += static_cast<size_t>(count);
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    throw ConfigurationError(where + ": failed to write the standard input file: " +
                             std::strerror(errno));
  }
  // The child reads the payload from the beginning of the file.
  if (::lseek(owned.get(), 0, SEEK_SET) != 0) {
    throw ConfigurationError(where + ": failed to rewind the standard input file: " +
                             std::strerror(errno));
  }
  owned.release();
  return descriptor;
}

}  // namespace

ProcessResult run_bounded(const std::vector<std::string>& command, const std::string& where,
                          std::chrono::milliseconds timeout, std::size_t output_limit) {
  return run_bounded(command, where, timeout, output_limit, std::string());
}

ProcessResult run_bounded(const std::vector<std::string>& command, const std::string& where,
                          std::chrono::milliseconds timeout, std::size_t output_limit,
                          const std::string& input) {
  if (command.empty()) {
    throw ConfigurationError(where + ": no executable to run");
  }
  if (input.size() > kMaxProcessInput) {
    throw ConfigurationError(where + ": the standard input exceeds " +
                             std::to_string(kMaxProcessInput) + " bytes");
  }
  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  // Both ends are close-on-exec: the child only receives the descriptors
  // that are mapped onto its standard streams.
  if (::pipe2(out_pipe, O_CLOEXEC) != 0) {
    throw ConfigurationError(where + ": failed to create a pipe: " + std::strerror(errno));
  }
  ScopedFd out_read(out_pipe[0]);
  ScopedFd out_write(out_pipe[1]);
  if (::pipe2(err_pipe, O_CLOEXEC) != 0) {
    throw ConfigurationError(where + ": failed to create a pipe: " + std::strerror(errno));
  }
  ScopedFd err_read(err_pipe[0]);
  ScopedFd err_write(err_pipe[1]);
  if (!configure_read_end(out_read.get()) || !configure_read_end(err_read.get())) {
    throw ConfigurationError(where + ": failed to configure a pipe: " + std::strerror(errno));
  }

  // The standard input of the child is a bounded anonymous regular file.
  //
  // A pipe is not usable here. The parent would have to write the whole
  // payload before the output of the child is drained, and a child that
  // writes more than the capacity of the pipe before it reads its input
  // would deadlock: neither side can make progress. A prefilled regular
  // file removes this dependency on the child's reads. Its preparation can
  // still fail on storage/resource errors. Closing the child's standard
  // input cannot raise SIGPIPE in the parent.
  ScopedFd input_file;
  if (!input.empty()) {
    input_file.reset(create_input_file(where, input));
  }
  int null_fd = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
  if (null_fd < 0) {
    throw ConfigurationError(where + ": failed to open /dev/null: " + std::strerror(errno));
  }
  ScopedFd null_input(null_fd);

  // Relocate the descriptors so that the standard streams of the child and
  // the pipe ends never collide. The read ends matter as well: closing a
  // read end that happens to be the standard input of the child would close
  // the standard input that was just mapped.
  if (!relocate_above_stdio(out_write) || !relocate_above_stdio(err_write) ||
      !relocate_above_stdio(null_input) || !relocate_above_stdio(out_read) ||
      !relocate_above_stdio(err_read)) {
    throw ConfigurationError(where + ": failed to prepare the descriptors: " +
                             std::strerror(errno));
  }
  // The input descriptor only exists when the caller supplied a payload.
  if (input_file.valid() && !relocate_above_stdio(input_file)) {
    throw ConfigurationError(where + ": failed to prepare the descriptors: " +
                             std::strerror(errno));
  }

  // The descriptor that becomes the standard input of the child.
  const int child_stdin = input_file.valid() ? input_file.get() : null_input.get();

  std::vector<char*> argv;
  argv.reserve(command.size() + 1);
  for (const std::string& argument : command) {
    argv.push_back(const_cast<char*>(argument.c_str()));
  }
  argv.push_back(nullptr);

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    throw ConfigurationError(where + ": failed to prepare the child descriptors");
  }
  posix_spawnattr_t attributes;
  if (posix_spawnattr_init(&attributes) != 0) {
    posix_spawn_file_actions_destroy(&actions);
    throw ConfigurationError(where + ": failed to prepare the child attributes");
  }
  // Every setup step is checked: a partially prepared child must never be
  // started, and a failed process group would make the group termination
  // unsafe.
  bool prepared = posix_spawn_file_actions_adddup2(&actions, child_stdin, STDIN_FILENO) == 0 &&
                  posix_spawn_file_actions_adddup2(&actions, out_write.get(), STDOUT_FILENO) == 0 &&
                  posix_spawn_file_actions_adddup2(&actions, err_write.get(), STDERR_FILENO) == 0 &&
                  posix_spawn_file_actions_addclose(&actions, out_read.get()) == 0 &&
                  posix_spawn_file_actions_addclose(&actions, err_read.get()) == 0 &&
                  posix_spawn_file_actions_addclose(&actions, child_stdin) == 0 &&
                  posix_spawn_file_actions_addclose(&actions, out_write.get()) == 0 &&
                  posix_spawn_file_actions_addclose(&actions, err_write.get()) == 0 &&
                  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP) == 0 &&
                  posix_spawnattr_setpgroup(&attributes, 0) == 0;
  if (input_file.valid()) {
    // The write end must not survive in the child: the child would keep the
    // input file open and never see the end of its input.
    prepared = prepared && posix_spawn_file_actions_addclose(&actions, input_file.get()) == 0;
  }
  if (!prepared) {
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    throw ConfigurationError(where + ": failed to prepare the child process");
  }

  const std::string executable = resolve_executable(command.front());
  pid_t pid = -1;
  const int spawned = posix_spawn(&pid, executable.c_str(), &actions, &attributes, argv.data(),
                                  environ);
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attributes);
  if (spawned != 0) {
    throw ConfigurationError(where + ": failed to execute " + command.front() + ": " +
                             std::strerror(spawned));
  }
  // The parent keeps only the read ends; closing the write ends lets the
  // reader see the end of file once every writer is gone.
  out_write.reset();
  err_write.reset();
  // The parent keeps no copy of the input file: the child owns the only
  // descriptor it needs, and the file disappears with the child.
  input_file.reset();

  // The guard terminates the process group and reaps the child on every
  // path, including an exception thrown while the output is collected.
  ChildGuard guard(pid, pid);

  ProcessResult result;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  bool limit_exceeded = false;
  bool timed_out = false;
  bool failed = false;
  std::string failure;
  int status = 0;

  struct pollfd fds[2];
  fds[0].fd = out_read.get();
  fds[1].fd = err_read.get();

  // Read everything that is currently available from one pipe. Returns
  // false when the pipe failed; the output limit and the deadline are
  // reported separately.
  //
  // The deadline is checked on every read: a child that keeps its output
  // pipe readable must not extend the run beyond the deadline.
  auto drain = [&](int fd, std::string& target) -> bool {
    char buffer[65536];
    while (true) {
      if (std::chrono::steady_clock::now() >= deadline) {
        timed_out = true;
        return true;
      }
      const ssize_t count = ::read(fd, buffer, sizeof(buffer));
      if (count > 0) {
        if (result.out.size() + result.err.size() + static_cast<size_t>(count) > output_limit) {
          limit_exceeded = true;
          return true;
        }
        target.append(buffer, static_cast<size_t>(count));
        continue;
      }
      if (count == 0) return true;  // end of file
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
      failed = true;
      failure = "the output pipes failed: " + std::string(std::strerror(errno));
      return false;
    }
  };

  try {
    while (!failed && !limit_exceeded && !timed_out) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        timed_out = true;
        break;
      }
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
      // A short poll interval keeps the exit detection responsive even when
      // another process keeps the output pipe open.
      const int wait_ms = static_cast<int>(std::min<int64_t>(remaining.count(), 50));
      fds[0].events = POLLIN;
      fds[1].events = POLLIN;
      fds[0].revents = 0;
      fds[1].revents = 0;
      const int ready = ::poll(fds, 2, wait_ms);
      if (ready < 0) {
        if (errno == EINTR) continue;
        failed = true;
        failure = "the output pipes failed: " + std::string(std::strerror(errno));
        break;
      }
      for (int index = 0; index < 2 && !limit_exceeded && !failed && !timed_out; ++index) {
        if ((fds[index].revents & (POLLIN | POLLHUP | POLLERR)) == 0) continue;
        if (!drain(fds[index].fd, index == 0 ? result.out : result.err)) break;
      }
      if (failed || limit_exceeded || timed_out) break;

      // Detect the exit without reaping the child: the identifier stays
      // reserved while the process group is terminated.
      if (guard.exited(status)) {
        // Another process of the group may still hold the output pipe. The
        // group is terminated before the child is reaped, so no writer can
        // add further output.
        guard.terminate();
        break;
      }
    }
    if (!failed && !limit_exceeded && !timed_out) {
      // Collect the output that the child produced before it exited.
      for (int index = 0; index < 2 && !failed && !limit_exceeded; ++index) {
        if (fds[index].fd < 0) continue;
        if (!drain(fds[index].fd, index == 0 ? result.out : result.err)) break;
      }
    }
  } catch (...) {
    guard.terminate();
    throw;
  }

  out_read.reset();
  err_read.reset();

  if (limit_exceeded) {
    throw ConfigurationError(where + ": validation/expansion output exceeds " +
                             std::to_string(output_limit) + " bytes");
  }
  if (timed_out) {
    throw ConfigurationError(where + ": validation/expansion timed out after " +
                             std::to_string(timeout.count()) + "ms");
  }
  if (failed) {
    throw ConfigurationError(where + ": " + failure);
  }
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.exit_code = 128 + WTERMSIG(status);
  }
  return result;
}

}  // namespace chaos::assets
