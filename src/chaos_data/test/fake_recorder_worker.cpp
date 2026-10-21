// Test fixture: a fake recorder worker that speaks the real control
// protocol without ROS. Used to exercise the actual spawn, IPC, timeout and
// cleanup paths.
//
// Usage: fake_recorder_worker <control_fd> <bag_uri> <node_name> <config_json> <supervisor_pid>
//
// The mode is selected through CHAOS_DATA_FAKE_WORKER_MODE:
//   normal           acknowledge start, answer pause/resume/stop, exit 0
//   crash            exit immediately without acknowledging
//   hang_start       never acknowledge start
//   exit_after_start acknowledge start, then exit 0
//   stop_exit1       acknowledge stop, then exit 1
//   stop_partial     write a truncated stop acknowledgement, then exit
//   eof_no_ack       close the channel on stop without acknowledging
//   huge             answer start with an oversized frame
//   hang_stop        acknowledge start, then never acknowledge stop
//   ignore_eof       ignore the end of the control channel and keep running
//   slow_stop        acknowledge stop late and exit even later
//
// CHAOS_DATA_FAKE_WORKER_REPORT points at a file that receives one JSON
// line per received operation and one line with the descriptor state.
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

std::string mode()
{
  const char * value = std::getenv("CHAOS_DATA_FAKE_WORKER_MODE");
  return value ? std::string(value) : std::string("normal");
}

// Delay used by the slow_stop mode, in microseconds.
useconds_t delayMicroseconds()
{
  const char * value = std::getenv("CHAOS_DATA_FAKE_WORKER_DELAY_MS");
  if (value == nullptr) {
    return 0;
  }
  const long milliseconds = std::atol(value);
  return static_cast<useconds_t>(milliseconds * 1000);
}

bool writeFrame(int fd, const std::string & payload)
{
  std::size_t written = 0;
  while (written < payload.size()) {
    const ssize_t count =
      ::send(fd, payload.data() + written, payload.size() - written, MSG_NOSIGNAL);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (count == 0) {
      return false;
    }
    written += static_cast<std::size_t>(count);
  }
  return true;
}

void report(const std::string & line)
{
  const char * path = std::getenv("CHAOS_DATA_FAKE_WORKER_REPORT");
  if (path == nullptr) {
    return;
  }
  FILE * file = std::fopen(path, "a");
  if (file != nullptr) {
    std::fprintf(file, "%s\n", line.c_str());
    std::fclose(file);
  }
}

std::string operationOf(const std::string & payload)
{
  const std::size_t key = payload.find("\"op\"");
  if (key == std::string::npos) {
    return "unknown";
  }
  const std::size_t colon = payload.find(':', key);
  if (colon == std::string::npos) {
    return "unknown";
  }
  const std::size_t quote = payload.find('"', colon);
  if (quote == std::string::npos) {
    return "unknown";
  }
  const std::size_t end = payload.find('"', quote + 1);
  if (end == std::string::npos) {
    return "unknown";
  }
  return payload.substr(quote + 1, end - quote - 1);
}

}  // namespace

int main(int argc, char ** argv)
{
  if (argc < 2) {
    return 2;
  }
  const int fd = std::atoi(argv[1]);
  const std::string selected = mode();
  if (selected == "crash") {
    std::_Exit(3);
  }
  if (selected == "hang_start") {
    for (;;) {
      ::pause();
    }
  }
  const int flags = ::fcntl(fd, F_GETFD, 0);
  const int cloexec = (flags < 0) ? -1 : ((flags & FD_CLOEXEC) ? 1 : 0);
  report("{\"event\":\"descriptor\",\"fd\":" + std::to_string(fd) +
         ",\"cloexec\":" + std::to_string(cloexec) +
         ",\"pid\":" + std::to_string(static_cast<int>(::getpid())) + "}");

  if (selected == "huge") {
    // An oversized acknowledgement must be rejected by the supervisor.
    std::string padding(200000, 'x');
    writeFrame(fd, "{\"ok\":true,\"operation\":\"start\",\"pad\":\"" + padding + "\"}\n");
    for (;;) {
      ::pause();
    }
  }

  // The worker owns the start handshake: it acknowledges start on its own.
  writeFrame(fd, "{\"ok\":true,\"operation\":\"start\",\"fd\":" + std::to_string(fd) +
         ",\"cloexec\":" + std::to_string(cloexec) +
         ",\"pid\":" + std::to_string(static_cast<int>(::getpid())) + "}\n");
  if (selected == "exit_after_start") {
    return 0;
  }

  char buffer[4096];
  std::string frame;
  while (true) {
    const ssize_t count = ::read(fd, buffer, sizeof(buffer));
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      return 1;
    }
    if (count == 0) {
      // The supervisor closed the channel.
      if (selected == "ignore_eof") {
        // Ignore the end of stream: only the parent death signal can end
        // this worker.
        for (;;) {
          ::pause();
        }
      }
      return 0;
    }
    frame.append(buffer, static_cast<std::size_t>(count));
    std::size_t newline = frame.find('\n');
    while (newline != std::string::npos) {
      const std::string payload = frame.substr(0, newline);
      frame.erase(0, newline + 1);
      newline = frame.find('\n');
      const std::string op = operationOf(payload);
      report("{\"event\":\"operation\",\"op\":\"" + op + "\"}");
      if (op == "stop") {
        if (selected == "hang_stop") {
          for (;;) {
            ::pause();
          }
        }
        if (selected == "eof_no_ack") {
          return 0;
        }
        if (selected == "stop_partial") {
          // A truncated frame followed by the end of the stream.
          writeFrame(fd, "{\"ok\":true,\"operation\":\"st");
          return 0;
        }
        if (selected == "slow_stop") {
          // Acknowledge late and exit even later: the supervisor must not
          // grant a fresh exit budget after the late acknowledgement.
          ::usleep(delayMicroseconds());
          writeFrame(fd, "{\"ok\":true,\"operation\":\"stop\"}\n");
          ::usleep(delayMicroseconds());
          return 0;
        }
        writeFrame(fd, "{\"ok\":true,\"operation\":\"stop\"}\n");
        if (selected == "stop_exit1") {
          return 1;
        }
        return 0;
      }
      if (op == "pause" || op == "resume") {
        writeFrame(fd, "{\"ok\":true,\"operation\":\"" + op + "\"}\n");
        continue;
      }
      // Any other operation is unknown to the worker.
      fprintf(stderr, "fake worker: unknown operation %s\n", op.c_str());
      return 4;
    }
  }
}
