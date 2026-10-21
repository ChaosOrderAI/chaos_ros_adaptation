// Bounded supervision of a standalone native rosbag2 recorder worker.
// IPC carries control frames only; sensor payloads never cross this channel.
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <sys/types.h>

namespace chaos::data {

// Narrow data-plane interface so collection logic stays testable without
// spawning processes.
class RecorderInterface {
public:
  virtual ~RecorderInterface() = default;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual bool alive() const = 0;
  virtual bool paused() const = 0;
  virtual void setPaused(bool paused) = 0;
};

// Control protocol (newline delimited JSON).
//
// The worker owns the start handshake: it acknowledges "start" on its own as
// soon as the recorder is running. The supervisor never sends a "start"
// request, so a worker can never receive an operation it does not know.
//
//   worker -> supervisor: {"ok":true,"operation":"start", ...}
//   worker -> supervisor: {"ok":false,"operation":"start","error":"..."}
//   supervisor -> worker: {"op":"pause"}
//   supervisor -> worker: {"op":"resume"}
//   supervisor -> worker: {"op":"stop"}
//   worker -> supervisor: {"ok":true,"operation":"pause"}
//   worker -> supervisor: {"ok":false,"operation":"stop","error":"..."}
//
// The worker exits with status 0 only after a successful flush that was
// acknowledged; the supervisor verifies that exit status and does not accept
// an acknowledgement that is followed by a crash.
class NativeRecorder : public RecorderInterface {
public:
  NativeRecorder(const nlohmann::json & config, std::string uri,
                 std::string node_name, double startup_timeout_s = 15.0,
                 double shutdown_timeout_s = 10.0);
  ~NativeRecorder() override;

  NativeRecorder(const NativeRecorder &) = delete;
  NativeRecorder & operator=(const NativeRecorder &) = delete;

  void start() override;
  void stop() override;
  bool alive() const override;
  bool paused() const override { return paused_; }
  void setPaused(bool paused) override;

  // True once the worker was waited for; the exit status is then final.
  bool reaped() const { return reaped_; }
  int exitStatus() const { return exit_status_; }

private:
  // One request/response round trip bounded by a single steady deadline.
  void exchange(const std::string & operation, double timeout_s);
  void sendFrame(const std::string & operation, double deadline);
  nlohmann::json receiveAck(const std::string & operation, double deadline);
  static nlohmann::json parseAck(const std::string & payload);
  // Waits for the worker to exit; returns false when the absolute deadline
  // passed. The deadline is shared with the request and the acknowledgement.
  bool waitForExit(double deadline);
  // SIGTERM, then SIGKILL, then reap. Returns true when the child was
  // reaped; false when the child is still running and stays recorded.
  bool terminateChild();
  void closeControlChannel();
  void refreshLiveness() const;

  nlohmann::json config_;
  std::string uri_;
  std::string node_name_;
  double startup_timeout_s_;
  double shutdown_timeout_s_;
  pid_t pid_ = -1;
  int control_fd_ = -1;
  char buffer_[4096] = {};
  bool paused_ = false;
  bool started_ = false;
  bool start_acknowledged_ = false;
  mutable bool reaped_ = false;
  mutable bool alive_ = false;
  mutable int exit_status_ = -1;
};

}  // namespace chaos::data
