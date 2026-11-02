// Native independent humanoid watchdog process. The capability query is
// handled before ROS or SDK initialization and never touches hardware.
// The parent stays alive only to send one zero if the child disappears
// without releasing its vendor session. MoveCommand itself has no deadline.
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

#include <rclcpp/rclcpp.hpp>

#include "chaos_runtime/humanoid/booster_backend.hpp"
#include "chaos_runtime/humanoid/driver_node.hpp"
#include "chaos_runtime/humanoid/process_guard.hpp"

namespace {
std::atomic<pid_t> child_pid{0};

void forwardSignal(int signo) {
  const pid_t child = child_pid.load();
  if (child > 0) {
    ::kill(child, signo);
  }
}

int runDriver(int argc, char ** argv) {
  chaos::humanoid::registerBoosterSdkBackend();
  rclcpp::init(argc, argv);
  int result = 0;
  try {
    auto node = std::make_shared<chaos::humanoid::HumanoidDriverNode>();
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.spin();
    executor.remove_node(node);
    node.reset();
  } catch (const std::exception & error) {
    RCLCPP_FATAL(rclcpp::get_logger("humanoid_driver"), "startup failed: %s",
                 error.what());
    result = 1;
  }
  rclcpp::shutdown();
  return result;
}

int childStatus(int status) {
  if (WIFEXITED(status)) {
    return WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    return 128 + WTERMSIG(status);
  }
  return 1;
}

int supervise(int argc, char ** argv) {
  int fds[2] = {-1, -1};
  if (::pipe(fds) != 0) {
    std::fprintf(stderr, "humanoid_driver failed to create the stop channel\n");
    return 1;
  }
  sigset_t blocked, previous;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGINT);
  sigaddset(&blocked, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &blocked, &previous);
  const pid_t pid = ::fork();
  if (pid < 0) {
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    ::close(fds[0]);
    ::close(fds[1]);
    std::fprintf(stderr, "humanoid_driver failed to start the supervised child\n");
    return 1;
  }
  if (pid == 0) {
    pthread_sigmask(SIG_SETMASK, &previous, nullptr);
    ::close(fds[0]);
    chaos::humanoid::armVendorSessionChannel(fds[1]);
    int code = 1;
    try {
      code = runDriver(argc, argv);
    } catch (const std::exception & error) {
      std::fprintf(stderr, "humanoid_driver failed: %s\n", error.what());
    }
    ::close(fds[1]);
    _exit(code);
  }
  ::close(fds[1]);
  child_pid.store(pid);
  struct sigaction action;
  std::memset(&action, 0, sizeof(action));
  action.sa_handler = forwardSignal;
  ::sigaction(SIGINT, &action, nullptr);
  ::sigaction(SIGTERM, &action, nullptr);
  pthread_sigmask(SIG_SETMASK, &previous, nullptr);

  int status = 0;
  while (::waitpid(pid, &status, 0) < 0) {
    if (errno != EINTR) {
      status = 0;
      break;
    }
  }
  std::string bytes;
  char buffer[256];
  while (true) {
    const ssize_t count = ::read(fds[0], buffer, sizeof(buffer));
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (count == 0) {
      break;
    }
    bytes.append(buffer, static_cast<std::size_t>(count));
  }
  ::close(fds[0]);
  const auto session = chaos::humanoid::parseVendorSession(bytes);
  if (chaos::humanoid::emergencyZeroRequired(session)) {
    const int stopped = chaos::humanoid::emergencyZero(
      session.domain, session.interface_name);
    if (stopped != 0) {
      std::fprintf(stderr,
        "humanoid emergency zero failed on %s domain %d: %d\n",
        session.interface_name.c_str(), session.domain, stopped);
      return 1;
    }
  }
  return childStatus(status);
}

}  // namespace

int main(int argc, char ** argv) {
  for (int index = 1; index < argc; ++index) {
    if (std::string(argv[index]) == "--build-capabilities") {
#if defined(CHAOS_HUMANOID_WITH_BOOSTER_SDK)
      std::cout << "{\"booster_sdk\":true}\n";
#else
      std::cout << "{\"booster_sdk\":false}\n";
#endif
      return 0;
    }
  }
  if (!chaos::humanoid::localhostRosDiscovery()) {
    std::fprintf(stderr,
      "humanoid_driver refuses a non-local ROS discovery range. "
      "Set ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST and do not set "
      "ROS_LOCALHOST_ONLY=0.\n");
    return 1;
  }
  try {
    return supervise(argc, argv);
  } catch (const std::exception & error) {
    std::fprintf(stderr, "humanoid_driver failed: %s\n", error.what());
    return 1;
  }
}
