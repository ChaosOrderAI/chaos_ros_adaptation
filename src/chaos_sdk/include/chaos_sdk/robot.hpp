#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <builtin_interfaces/msg/time.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_interfaces/action/flight.hpp"
#include "chaos_interfaces/action/navigate_pose.hpp"
#include "chaos_interfaces/action/software_update.hpp"
#include "chaos_interfaces/msg/robot_state.hpp"
#include "chaos_interfaces/msg/velocity_command.hpp"
#include "chaos_interfaces/srv/control_lease.hpp"
#include "chaos_interfaces/srv/get_description.hpp"
#include "chaos_interfaces/srv/stop.hpp"

#include "chaos_sdk/action_task.hpp"
#include "chaos_sdk/sdk_core.hpp"

namespace chaos_sdk
{

/// Native C++ async client for one asset. Tokens are never acquired or
/// renewed implicitly.
///
/// ``describe()`` caches the configured base frame and command timeout. A
/// caller may also supply base_frame and command_timeout_s explicitly, but
/// the runtime remains authoritative. ``command_ttl_s`` bounds when a service
/// or action may start, using Unix time rather than the ROS simulation clock;
/// it is not a task duration or a client-side response timeout. Individual
/// calls can override it with ttl_s.
///
/// Every asynchronous call returns a future that also completes when the
/// client-side response timeout elapses (``chaos_sdk::TimeoutError``) or the
/// client is closed (``chaos_sdk::ClientClosedError``). The number of
/// in-flight requests is bounded; further requests fail fast instead of
/// growing without limit.
///
/// Service/action submission returns std::shared_future results and never
/// spins the executor internally. Applications must keep the executor running
/// and must not block its only worker waiting for these futures. wait_ready()
/// explicitly waits for discovery; standalone close() joins its worker.
///
/// The feedback callbacks of the action goals run on a process-wide pool of
/// bounded workers that is independent of this client: the executor of the
/// client only copies the feedback value and hands it to the pool, so a slow
/// or blocking user callback never occupies the executor. The callbacks of
/// one client are serialized: the pool runs them one after another. A newer
/// sample replaces the sample of the same goal that is still queued, and a
/// sample is dropped when the global budget of the queued samples is
/// exhausted or when the pool is contended. The pool belongs to the process:
/// close() never joins it, and a callback that already started keeps running
/// after the client was closed. A sample that was retired together with its
/// goal is discarded instead of executed: only a callback that a worker
/// already claimed may still run.
///
/// Thread safety: all public members may be called from multiple threads.
/// The node/executor lifetime is owned by the caller when an external node is
/// supplied; use create_standalone() to let the client own both.
class Robot
{
public:
  using Flight = chaos_interfaces::action::Flight;
  using NavigatePose = chaos_interfaces::action::NavigatePose;
  using SoftwareUpdate = chaos_interfaces::action::SoftwareUpdate;
  using NavigationTask = ActionTask<NavigatePose>;
  using FlightTask = ActionTask<Flight>;
  using OtaTask = ActionTask<SoftwareUpdate>;

  /// Client bound to a node owned by the application. The application must
  /// spin that node; the client never spins it.
  Robot(
    rclcpp::Node::SharedPtr node,
    std::string asset_id,
    std::optional<std::string> namespace_ = std::nullopt,
    std::optional<std::string> base_frame = std::nullopt,
    double command_timeout_s = 0.5,
    double command_ttl_s = 5.0);

  /// Client that owns its node and a dedicated executor thread. Useful for
  /// Python bindings and for tools that do not own a ROS node.
  static std::shared_ptr<Robot> create_standalone(
    std::string asset_id,
    std::optional<std::string> namespace_ = std::nullopt,
    std::optional<std::string> base_frame = std::nullopt,
    double command_timeout_s = 0.5,
    double command_ttl_s = 5.0,
    double response_timeout_s = 5.0);

  ~Robot();

  Robot(const Robot &) = delete;
  Robot & operator=(const Robot &) = delete;

  /// Latest received RobotState, or nullptr before the first sample.
  ///
  /// The returned pointer is const: callers cannot mutate the client cache.
  chaos_interfaces::msg::RobotState::ConstSharedPtr state() const;

  /// Wait for description service discovery, not for robot readiness.
  bool wait_ready(std::chrono::nanoseconds timeout = std::chrono::seconds(5));

  std::shared_future<chaos_interfaces::srv::GetDescription::Response> describe();
  std::shared_future<chaos_interfaces::srv::ControlLease::Response> acquire(
    const std::string & owner, double duration_s = 5.0, std::optional<double> ttl_s = std::nullopt);
  /// Renew the current lease. A renewal never rotates the token.
  std::shared_future<chaos_interfaces::srv::ControlLease::Response> renew(
    double duration_s = 5.0, std::optional<double> ttl_s = std::nullopt);
  /// Release the current lease.
  ///
  /// The answer of a release that arrives after a new lease was acquired does
  /// not clear that newer lease.
  std::shared_future<chaos_interfaces::srv::ControlLease::Response> release(
    std::optional<double> ttl_s = std::nullopt);
  /// Stop the motion of the asset and end the control session.
  ///
  /// A stop invalidates the lease: the runtime revokes the token before it
  /// halts the motion, so the cached token is dropped as soon as the runtime
  /// accepted the stop. A stop that reports STOP_FAILED for a failed stop
  /// output ends the session as well, because the revocation happens before
  /// the halt. A rejected stop (an unauthorized or an expired token) never
  /// reached the revocation and keeps the lease.
  ///
  /// The client never acquires a lease implicitly: call acquire() again
  /// before the next motion command. A stop whose answer did not arrive
  /// before the local response timeout leaves the remote state unknown; the
  /// client does not reacquire on its own.
  std::shared_future<chaos_interfaces::srv::Stop::Response> stop(
    std::optional<std::string> request_id = std::nullopt,
    std::optional<double> ttl_s = std::nullopt);

  /// Publish one bounded velocity sample; returns its correlation ID.
  ///
  /// This is enqueueing, not completion; subscribe to command_results for
  /// acceptance. Stream fresh samples deliberately: missing samples cause the
  /// runtime watchdog to stop motion.
  std::string velocity(
    double x = 0.0, double y = 0.0, double z = 0.0, double yaw = 0.0,
    std::optional<double> valid_for_s = std::nullopt,
    std::optional<std::string> request_id = std::nullopt);

  /// Send a navigation goal.
  ///
  /// The returned future completes when the server accepted or rejected the
  /// goal; rejection resolves to nullptr. An accepted task exposes the typed
  /// result and cancellation. The SDK owns ROS resources while observing it;
  /// a retained completed task preserves history without retaining the client.
  ///
  /// The feedback callback runs on a worker of the feedback pool, never on
  /// the executor of the client. The callbacks of one client are serialized,
  /// so a callback that blocks delays the later samples of that client: the
  /// newest sample replaces the queued one, and a sample is dropped when the
  /// global budget of the pool is exhausted.
  ///
  /// A callback is never force-cancelled: a callback that blocks forever
  /// keeps its worker and its captures until it returns, and it occupies one
  /// of the bounded workers of the process. The lifetime of the objects that
  /// a callback captures stays the responsibility of the caller. Own shared
  /// data by value and prefer weak client references: a strong client capture
  /// can retain the client until its operation retires and all claimed
  /// callbacks return. The destruction of a capture happens outside the locks of the
  /// SDK, but the destructor of an arbitrary capture may block for an
  /// arbitrary time.
  ///
  /// A cancellation request only ends the cancellation request itself: the
  /// server confirms the cancellation of a goal, but the goal keeps its own
  /// observation until it reaches a terminal state, times out or is closed.
  /// A cancellation request that runs into the local response timeout retires
  /// the observation of the goal instead: the state of the task becomes
  /// chaos_sdk::TaskState::TIMED_OUT, the result future of the goal fails
  /// with chaos_sdk::TimeoutError and the ROS resources of that goal are
  /// released. A goal that already finished keeps its terminal state: a late
  /// answer of the server never overwrites it.
  std::shared_future<std::shared_ptr<NavigationTask>> navigate(
    const geometry_msgs::msg::PoseStamped & target,
    std::function<void (NavigatePose::Feedback)> feedback_callback = nullptr,
    std::optional<double> ttl_s = std::nullopt);
  /// Send a takeoff goal. The feedback callback behaves like the one of
  /// navigate().
  std::shared_future<std::shared_ptr<FlightTask>> takeoff(
    double altitude_m,
    std::function<void (Flight::Feedback)> feedback_callback = nullptr,
    std::optional<double> ttl_s = std::nullopt);
  /// Send a land goal. The feedback callback behaves like the one of
  /// navigate().
  std::shared_future<std::shared_ptr<FlightTask>> land(
    std::function<void (Flight::Feedback)> feedback_callback = nullptr,
    std::optional<double> ttl_s = std::nullopt);
  /// Send a software-update goal. The feedback callback behaves like the one
  /// of navigate(). The runtime decides whether any bytes are committed;
  /// `result.applied` is the only signal of a commit.
  std::shared_future<std::shared_ptr<OtaTask>> ota(
    const std::string & artifact_id,
    const std::string & version,
    const std::string & sha256,
    std::uint64_t size_bytes,
    std::function<void (SoftwareUpdate::Feedback)> feedback_callback = nullptr,
    std::optional<double> ttl_s = std::nullopt);

  std::string lease_token() const;
  std::string owner() const;
  std::string base_frame() const;
  double command_timeout_s() const;
  double command_ttl_s() const {return command_ttl_s_;}

  /// Number of in-flight requests (services and actions) being observed.
  std::size_t pending_count() const;

  /// Feedback samples the process dropped because the feedback pool was full
  /// or its scheduler lock was busy.
  ///
  /// The pool is shared by every client of the process, so the count covers
  /// all of them and is not reset per client. A dropped sample is never
  /// delivered and never changes the result of its goal.
  std::size_t feedback_dropped_count() const;

  /// Local response timeout in seconds; applies to every pending request.
  /// Must be finite and > 0.
  void set_response_timeout(double seconds);
  double response_timeout_s() const;

  /// Local observation window for an accepted action goal, in seconds.
  ///
  /// When the window elapses, the observation of the goal ends on its own:
  /// the result future of the goal fails with chaos_sdk::TimeoutError and the
  /// state of the task becomes chaos_sdk::TaskState::TIMED_OUT. Both are
  /// published together, so a result future that is ready already exposes the
  /// terminal state of the goal. The ROS resources of the goal are released
  /// afterwards, and the remote execution state stays unknown.
  void set_action_timeout(double seconds);
  double action_timeout_s() const;

  /// Upper bound for in-flight requests. Must be > 0.
  void set_max_pending(std::size_t maximum);
  std::size_t max_pending() const;

  /// Number of action client slots that currently hold a ROS client.
  ///
  /// A retired slot (its observation timed out) releases its client, so this
  /// number drops again. The total number of slots is bounded, the number of
  /// live clients is bounded by it.
  std::size_t action_client_count() const;
  std::size_t action_slot_count() const;

  /// Destroy client resources and resolve every pending future.
  ///
  /// close() is idempotent and safe to call concurrently with a send. After
  /// close(), new requests fail with ClientClosedError. Read-only history
  /// remains accessible. Local close does not cancel remote motion; use the
  /// explicit stop/cancel APIs when remote termination is required.
  ///
  /// A standalone client (create_standalone()) owns its node and its
  /// executor: close() cancels that executor and joins its thread. A client
  /// that borrows the node of the application never joins an executor, the
  /// application stays in charge of the spinning of its node. The feedback
  /// workers of the process belong to neither of them: close() never joins
  /// them and never waits for a callback that already started, so a callback
  /// that is still running keeps its captures until it returns.
  ///
  /// The close publishes the terminal state of the observed goals and
  /// settles their futures before it releases the ROS resources. The
  /// terminal state of a goal that was closed is unknown: the local close
  /// says nothing about the remote execution. The capture of a user callback
  /// is released after those futures were settled and outside of every lock
  /// of the client, so a destructor of the caller never runs under a lock of
  /// the client.
  void close();

private:
  struct Impl;

  builtin_interfaces::msg::Time deadline(std::optional<double> ttl_s) const;
  std::string require_token() const;

  template<typename ServiceT>
  std::shared_future<typename ServiceT::Response> send_control(
    typename rclcpp::Client<ServiceT>::SharedPtr client,
    typename ServiceT::Request request,
    std::function<void (const typename ServiceT::Response &)> update = nullptr);

  template<typename ActionT>
  std::shared_future<std::shared_ptr<ActionTask<ActionT>>> send_goal(
    const std::string & action_name,
    typename ActionT::Goal goal,
    std::function<void (typename ActionT::Feedback)> feedback_callback);

  std::shared_ptr<Impl> impl_;
  double command_ttl_s_;
};

}  // namespace chaos_sdk
