// rclcpp facade for the control coordinator. Thin translation layer:
// ROS messages/callback groups in, coordinator views out. No control
// decisions live here. Port of the ROS-facing parts of
// chaos_runtime/node.py.
//
// Threading model (one MultiThreadedExecutor, 4 threads):
// - services_group (mutually exclusive): lease / stop / description.
// - action_group (mutually exclusive): goal/cancel/accept callbacks only.
//   The accepted callback never blocks and never detaches a per-goal thread:
//   it hands the goal to a single persistent action worker.
// - timer_group (mutually exclusive): the steady-clock watchdog. It uses
//   create_wall_timer, so a paused or rewound /clock cannot stop lease
//   expiry, the velocity timeout, or the stop retry loop.
// - default group: adapter transport subscriptions (brief cache updates).
//
// Endpoint lifetime: rclcpp::Node keeps only weak references to services
// and subscriptions, so every endpoint is stored as a member here. A
// temporary returned by create_service()/create_subscription() would be
// destroyed immediately and the endpoint would silently disappear.
#pragma once

#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_interfaces/action/flight.hpp"
#include "chaos_interfaces/action/navigate_pose.hpp"
#include "chaos_interfaces/msg/command_result.hpp"
#include "chaos_interfaces/msg/control_trace.hpp"
#include "chaos_interfaces/msg/robot_state.hpp"
#include "chaos_interfaces/msg/velocity_command.hpp"
#include "chaos_interfaces/srv/control_lease.hpp"
#include "chaos_interfaces/srv/get_description.hpp"
#include "chaos_interfaces/srv/stop.hpp"

#include "chaos_runtime/adapter.hpp"
#include "chaos_runtime/coordinator.hpp"
#include "chaos_runtime/trace.hpp"

namespace chaos::runtime {

class RobotRuntimeNode : public rclcpp::Node, public TraceSink {
public:
  explicit RobotRuntimeNode(const rclcpp::NodeOptions & options =
                          rclcpp::NodeOptions());
  ~RobotRuntimeNode() override;

  void publish(const TraceEvent & event) override;

private:
  using NavigatePose = chaos_interfaces::action::NavigatePose;
  using Flight = chaos_interfaces::action::Flight;

  // Per-goal bookkeeping. The token map is bounded: every entry is removed
  // when the goal reaches a terminal state or is rejected, so a client that
  // floods rejected goals cannot grow the cache without limit.
  struct GoalTokens {
    const void * goal_token = nullptr;
    // Set by the accepted callback: delivers a typed terminal result to the
    // action server.
    std::function<void(const OperationResult &)> terminal;
    // Terminal result that arrived before the goal was accepted.
    std::optional<OperationResult> pending_terminal;
    // Guards terminal/pending_terminal: the terminal publisher may run on
    // the coordinator's watchdog thread while the accepted callback runs on
    // the executor thread.
    std::mutex terminal_mutex;
  };
  using GoalTokensPtr = std::shared_ptr<GoalTokens>;
  using GoalIdBytes = std::array<std::uint8_t, 16>;

  // One persistent worker thread drives all action execution. The accepted
  // callback only enqueues a job (never blocks, never detaches per-goal
  // threads). The coordinator admits at most one reservation, so the queue
  // stays shallow by construction; it is still bounded defensively.
  class ActionWorker {
  public:
    ActionWorker() : thread_([this] { run(); }) {}
    ~ActionWorker();
    // Returns false when the worker is stopping or the queue is full; the
    // caller must then deliver a terminal result to the client.
    bool submit(std::function<void()> job);
    void stop();
    bool stopping() const;

  private:
    void run();
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
    std::deque<std::function<void()>> jobs_;
    std::thread thread_;
  };

  template <typename ActionT>
  void makeActionServer(OperationKind kind, const std::string & name);

  template <typename ActionT>
  void executeGoal(
    OperationKind kind, const rclcpp_action::GoalUUID & uuid,
    std::shared_ptr<rclcpp_action::ServerGoalHandle<ActionT>> goal_handle);

  // Business endpoints are absolute and derived from the asset namespace,
  // exactly like the Python runtime: `namespace.rstrip('/') + '/' + name`.
  // A launch file may still set the node name and the ROS namespace, but
  // the asset contract owns the business endpoints.
  std::string endpoint(const std::string & name) const;

  GoalTokensPtr tokensFor(const rclcpp_action::GoalUUID & uuid);
  void dropTokens(const rclcpp_action::GoalUUID & uuid);

  void onVelocity(const chaos_interfaces::msg::VelocityCommand & message);
  void onDescribe(
    const std::shared_ptr<chaos_interfaces::srv::GetDescription::Request> request,
    std::shared_ptr<chaos_interfaces::srv::GetDescription::Response> response)
    const;
  void onLease(
    const std::shared_ptr<chaos_interfaces::srv::ControlLease::Request> request,
    std::shared_ptr<chaos_interfaces::srv::ControlLease::Response> response);
  void onStop(
    const std::shared_ptr<chaos_interfaces::srv::Stop::Request> request,
    std::shared_ptr<chaos_interfaces::srv::Stop::Response> response);

  AssetConfig asset_;
  Clocks clocks_;
  std::unique_ptr<ControlCoordinator> coordinator_;
  std::unique_ptr<ActionWorker> action_worker_;
  mutable std::mutex tokens_mutex_;
  std::map<GoalIdBytes, GoalTokensPtr> goal_tokens_;

  rclcpp::CallbackGroup::SharedPtr services_group_;
  rclcpp::CallbackGroup::SharedPtr action_group_;
  rclcpp::CallbackGroup::SharedPtr timer_group_;

  rclcpp::Publisher<chaos_interfaces::msg::RobotState>::SharedPtr state_pub_;
  rclcpp::Publisher<chaos_interfaces::msg::CommandResult>::SharedPtr
    results_pub_;
  rclcpp::Publisher<chaos_interfaces::msg::ControlTrace>::SharedPtr trace_pub_;

  // Endpoints must outlive the node: rclcpp::Node stores weak references.
  rclcpp::Service<chaos_interfaces::srv::GetDescription>::SharedPtr
    description_service_;
  rclcpp::Service<chaos_interfaces::srv::ControlLease>::SharedPtr
    lease_service_;
  rclcpp::Service<chaos_interfaces::srv::Stop>::SharedPtr stop_service_;
  rclcpp::Subscription<chaos_interfaces::msg::VelocityCommand>::SharedPtr
    velocity_sub_;
  rclcpp::TimerBase::SharedPtr timer_;

  std::vector<rclcpp_action::ServerBase::SharedPtr> action_servers_;
};

}  // namespace chaos::runtime
