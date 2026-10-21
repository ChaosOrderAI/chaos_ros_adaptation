#pragma once

#include <chrono>
#include <cstddef>
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
/// Thread safety: all public members may be called from multiple threads.
/// The node/executor lifetime is owned by the caller when an external node is
/// supplied; use create_standalone() to let the client own both.
class Robot
{
public:
  using Flight = chaos_interfaces::action::Flight;
  using NavigatePose = chaos_interfaces::action::NavigatePose;
  using NavigationTask = ActionTask<NavigatePose>;
  using FlightTask = ActionTask<Flight>;

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
  std::shared_future<chaos_interfaces::srv::ControlLease::Response> renew(
    double duration_s = 5.0, std::optional<double> ttl_s = std::nullopt);
  std::shared_future<chaos_interfaces::srv::ControlLease::Response> release(
    std::optional<double> ttl_s = std::nullopt);
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
  std::shared_future<std::shared_ptr<NavigationTask>> navigate(
    const geometry_msgs::msg::PoseStamped & target,
    std::function<void (NavigatePose::Feedback)> feedback_callback = nullptr,
    std::optional<double> ttl_s = std::nullopt);
  std::shared_future<std::shared_ptr<FlightTask>> takeoff(
    double altitude_m,
    std::function<void (Flight::Feedback)> feedback_callback = nullptr,
    std::optional<double> ttl_s = std::nullopt);
  std::shared_future<std::shared_ptr<FlightTask>> land(
    std::function<void (Flight::Feedback)> feedback_callback = nullptr,
    std::optional<double> ttl_s = std::nullopt);

  std::string lease_token() const;
  std::string owner() const;
  std::string base_frame() const;
  double command_timeout_s() const;
  double command_ttl_s() const {return command_ttl_s_;}

  /// Number of in-flight requests (services and actions) being observed.
  std::size_t pending_count() const;

  /// Local response timeout in seconds; applies to every pending request.
  /// Must be finite and > 0.
  void set_response_timeout(double seconds);
  double response_timeout_s() const;

  /// Local observation window for an accepted action goal, in seconds.
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
