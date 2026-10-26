// Standalone native rosbag2 recorder worker (separate ELF, own process).
//
// Usage: chaos_data_recorder_worker <control_fd> <bag_uri> <node_name> <config_json> <supervisor_pid>
//
// The supervisor spawns this binary with posix_spawn and passes a connected
// Unix socket as <control_fd>. This process owns rclcpp/rosbag2 only; the
// supervisor never initializes ROS after fork.
//
// Control protocol (newline delimited JSON):
//   the worker acknowledges "start" on its own once the recorder runs, then
//   answers the "pause", "resume" and "stop" requests of the supervisor. The
//   supervisor never sends "start".
//
// Signal policy (owned by this process, never re-installed by rclcpp):
//  - SIGINT is ignored. ros2 launch signals the whole process group; the
//    supervisor performs the bounded stop/flush handshake instead of racing
//    that signal.
//  - SIGTERM keeps its default disposition and is the hard stop, also used as
//    the parent death signal so a crashed supervisor cannot leak the worker.
//  - rclcpp is initialized with SignalHandlerOptions::None so it does not
//    replace the policy above.
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <sys/prctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <keyboard_handler/keyboard_handler.hpp>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rosbag2_cpp/writer.hpp>
#include <rosbag2_storage/storage_options.hpp>
#include <rosbag2_transport/recorder.hpp>
#include <rosbag2_transport/record_options.hpp>

#include "chaos_data/recorder_control.hpp"

namespace {

bool endsWithAny(const std::string & topic,
                 const std::vector<std::string> & suffixes)
{
  for (const std::string & suffix : suffixes) {
    if (topic.size() >= suffix.size() &&
        topic.compare(topic.size() - suffix.size(), suffix.size(), suffix) == 0) {
      return true;
    }
  }
  return false;
}

// Owns the recorder, the executor thread and the ROS context.
//
// The constructor only creates the recorder: the executor thread is started
// by start(), so a throwing constructor can never leave a joinable thread
// behind (a joinable std::thread that is destroyed calls std::terminate).
//
// Implements chaos::data::RecorderControlSession so the control loop in
// recorder_control.hpp can be regression tested with a fake session.
class WorkerSession : public chaos::data::RecorderControlSession
{
public:
  WorkerSession(const std::string & uri, const std::string & node_name,
                const nlohmann::json & config)
  {
    rosbag2_storage::StorageOptions storage;
    storage.uri = uri;
    storage.storage_id = config.at("storage_id").get<std::string>();
    storage.max_bagfile_size =
      config.at("max_bagfile_size_mb").get<std::uint64_t>() * 1024 * 1024;
    storage.max_bagfile_duration =
      config.at("max_bagfile_duration_s").get<std::uint64_t>();
    storage.max_cache_size =
      config.at("max_cache_size_mb").get<std::uint64_t>() * 1024 * 1024;

    rosbag2_transport::RecordOptions recording;
    recording.topics = config.at("topics").get<std::vector<std::string>>();
    recording.all_topics = false;
    recording.all_services = false;
    recording.include_hidden_topics = false;
    // The bag stores the original CDR payloads; no conversion happens.
    recording.rmw_serialization_format = "cdr";
    // Subscribing from reader only graph entries can lock RELIABLE QoS before
    // a BEST_EFFORT camera appears, excluding that stream for this recorder.
    recording.include_unpublished_topics = false;
    recording.disable_keyboard_controls = true;
    // Subscriptions are first created after clock approval; a paused recorder
    // would consume and discard latched TF/description messages on resume.
    recording.start_paused = false;
    recording.use_sim_time = false;
    // Static transforms may predate the recording: hold them with a transient
    // reader; rosbag adapts QoS for the sensor streams themselves.
    static const std::vector<std::string> kLatchedSuffixes{
      "/tf_static", "/robot_description", "/sensors/description",
      "/data_collection/status"};
    for (const std::string & topic : recording.topics) {
      if (endsWithAny(topic, kLatchedSuffixes)) {
        recording.topic_qos_profile_overrides.emplace(
          topic, rclcpp::QoS(100).reliable().transient_local());
      }
    }

    auto writer = std::make_shared<rosbag2_cpp::Writer>();
    // Keyboard controls are disabled in RecordOptions; this handler must not
    // install signal handlers either, because the signal policy of this
    // process is explicit.
    auto keyboard = std::make_shared<KeyboardHandler>(false);
    recorder_ = std::make_shared<rosbag2_transport::Recorder>(
      writer, keyboard, storage, recording, node_name);
  }

  WorkerSession(const WorkerSession &) = delete;
  WorkerSession & operator=(const WorkerSession &) = delete;

  ~WorkerSession() { bestEffortShutdown(); }

  // Starts the executor and the recorder. On failure the executor thread is
  // joined before the error propagates.
  void start()
  {
    spin_thread_ = std::thread(
      [this]() {
        try {
          rclcpp::spin(recorder_);
        } catch (const std::exception & error) {
          std::lock_guard<std::mutex> lock(spin_mutex_);
          spin_error_ = error.what();
          spin_failed_ = true;
        } catch (...) {
          std::lock_guard<std::mutex> lock(spin_mutex_);
          spin_error_ = "unknown executor failure";
          spin_failed_ = true;
        }
      });
    try {
      recorder_->record();
    } catch (...) {
      bestEffortShutdown();
      throw;
    }
  }

  bool spinFailed() const override
  {
    std::lock_guard<std::mutex> lock(spin_mutex_);
    return spin_failed_;
  }

  std::string spinError() const override
  {
    std::lock_guard<std::mutex> lock(spin_mutex_);
    return spin_error_;
  }

  void pause() override { recorder_->pause(); }

  void resume() override { recorder_->resume(); }

  // Flushes the recording. Returns false and fills error when the flush
  // failed. A failed flush is never acknowledged as a success.
  //
  // The flush is synchronous on purpose: a background flush thread would
  // either destroy a joinable std::thread (std::terminate) or leave the
  // captured state dangling. A wedged writer is bounded by the supervisor:
  // it owns the stop deadline and sends SIGTERM, then SIGKILL. The block
  // stays inside this isolated process and cannot affect the supervisor.
  bool flush(std::string & error) override
  {
    if (flushed_) {
      return true;
    }
    flushed_ = true;
    try {
      recorder_->stop();
    } catch (const std::exception & caught) {
      error = caught.what();
      return false;
    } catch (...) {
      error = "unknown recorder failure";
      return false;
    }
    return true;
  }

  // Idempotent teardown for the normal and the error paths: stop the
  // context, join the executor thread. Never throws.
  void bestEffortShutdown() noexcept
  {
    shutdown();
  }

  void shutdown() noexcept override
  {
    if (recorder_ && !flushed_) {
      flushed_ = true;
      try {
        recorder_->stop();
      } catch (...) {
      }
    }
    if (rclcpp::ok()) {
      try {
        rclcpp::shutdown();
      } catch (...) {
      }
    }
    if (spin_thread_.joinable()) {
      spin_thread_.join();
    }
  }

private:
  std::shared_ptr<rosbag2_transport::Recorder> recorder_;
  std::thread spin_thread_;
  mutable std::mutex spin_mutex_;
  mutable bool spin_failed_ = false;
  mutable std::string spin_error_;
  bool flushed_ = false;
};

}  // namespace

int main(int argc, char ** argv)
{
  if (argc != 6) {
    fprintf(stderr,
            "usage: %s <control_fd> <bag_uri> <node_name> <config_json> <supervisor_pid>\n",
            argc > 0 ? argv[0] : "recorder_worker");
    return 2;
  }
  const int control_fd = std::atoi(argv[1]);
  if (control_fd < 0) {
    fprintf(stderr, "recorder worker: invalid control descriptor\n");
    return 2;
  }
  const std::string uri = argv[2];
  const std::string node_name = argv[3];
  const long expected_parent = std::atol(argv[5]);
  nlohmann::json config;
  try {
    config = nlohmann::json::parse(argv[4]);
  } catch (const std::exception & error) {
    fprintf(stderr, "recorder worker: invalid configuration: %s\n", error.what());
    return 2;
  }

  // ros2 launch signals the whole process group; the supervisor owns the
  // bounded stop/flush handshake instead of racing that signal. SIGTERM
  // remains a hard stop if the supervisor itself crashed.
  std::signal(SIGINT, SIG_IGN);
  std::signal(SIGTERM, SIG_DFL);
  // If the supervisor dies, the kernel terminates this process instead of
  // leaving an orphaned writer behind.
  ::prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
  // The parent death signal cannot cover the window before prctl() ran. When
  // the supervisor already died, this process is reparented and must exit
  // instead of recording into a bag nobody owns.
  if (expected_parent > 0 && ::getppid() != static_cast<pid_t>(expected_parent)) {
    fprintf(stderr, "recorder worker: the supervisor disappeared before the handshake\n");
    return 3;
  }

  int exit_code = 0;
  try {
    // SignalHandlerOptions::None keeps rclcpp from replacing the signal
    // policy installed above.
    rclcpp::init(1, argv, rclcpp::InitOptions(), rclcpp::SignalHandlerOptions::None);
    WorkerSession session(uri, node_name, config);
    session.start();
    // The worker owns the start handshake; the supervisor only reads it. When
    // the executor already failed, no successful acknowledgement is written.
    if (!chaos::data::acknowledgeRecorderStart(session, control_fd,
                                               static_cast<int>(::getpid()))) {
      fprintf(stderr,
              "recorder worker: recorder executor failed before the start "
              "acknowledgement\n");
      session.shutdown();
      return 1;
    }
    // The bounded control loop returns the process exit code: 0 only after a
    // successful flush that was acknowledged, 1 as soon as the recording can
    // no longer be trusted. A recorder executor failure on an idle control
    // socket is detected through the bounded poll timeout, never by blocking
    // forever in poll(control_fd, -1).
    exit_code = chaos::data::runRecorderControlLoop(session, control_fd);
  } catch (const std::exception & error) {
    chaos::data::detail::writeControlFrame(
      control_fd,
      nlohmann::json{{"ok", false},
                     {"operation", "start"},
                     {"error", std::string("RecorderWorkerError: ") +
                                 error.what()}}
        .dump() +
        "\n");
    exit_code = 1;
  }
  if (rclcpp::ok()) {
    rclcpp::shutdown();
  }
  return exit_code;
}
