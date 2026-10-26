#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <std_msgs/msg/string.hpp>

#include "chaos_interfaces/action/flight.hpp"
#include "chaos_interfaces/action/navigate_pose.hpp"
#include "chaos_interfaces/msg/command_result.hpp"
#include "chaos_interfaces/msg/robot_state.hpp"
#include "chaos_interfaces/msg/velocity_command.hpp"
#include "chaos_interfaces/srv/control_lease.hpp"
#include "chaos_interfaces/srv/get_description.hpp"
#include "chaos_interfaces/srv/stop.hpp"

#include "chaos_mqtt/action_observer.hpp"
#include "chaos_mqtt/protocol.hpp"

namespace chaos_mqtt
{

/// Fixed MQTT commands translated into the asset runtime's public API.
///
/// Mirrors the former rclpy Gateway node: identical parameters, topic/service/
/// action routing, correlation replay, event phases and terminal states.
/// Telemetry and responses are validated upstream by the Rust transport
/// (16 KiB bound); this node re-applies the wire protocol validation and never
/// logs tokens or payloads.
///
/// Threading contract: the node is designed for a MutuallyExclusive executor.
/// Every ROS callback of this node therefore runs on the same thread and the
/// correlation records are additionally guarded by `records_mutex_` so the
/// code stays correct if the executor is ever reconfigured. No pointer into
/// the correlation map is used outside of the lock.
class GatewayNode : public rclcpp::Node, private ActionSink
{
public:
  using Flight = chaos_interfaces::action::Flight;
  using NavigatePose = chaos_interfaces::action::NavigatePose;

  explicit GatewayNode(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

  /// Convert a validated command deadline into a ROS Time message. Rounding
  /// down cannot grant the remote command any additional validity. Throws
  /// CommandError when outside the ROS Time range.
  static builtin_interfaces::msg::Time deadline_time(double deadline_ms);

  ~GatewayNode() override;

private:
  // --- ActionSink ------------------------------------------------------------
  // The action observer reports the goal lifecycle here. Every callback first
  // checks that the correlation record still exists: a late acknowledgment or
  // a late result of a goal that already timed out must never resurrect a
  // record, publish a second terminal state or change the replay cache.
  void on_action_accepted(const std::string & request_id, const std::string & goal_id_hex) override;
  void on_action_rejected(const std::string & request_id) override;
  void on_action_result(const std::string & request_id, const ActionOutcome & outcome) override;

  static void publish_json(const rclcpp::Publisher<std_msgs::msg::String>::SharedPtr & publisher,
    const Json & value);

  void state_callback(const chaos_interfaces::msg::RobotState::SharedPtr message);
  void command_callback(const std_msgs::msg::String::SharedPtr message);
  void dispatch(const Command & command);
  void service_error(const std::string & request_id, const std::string & code,
    const std::string & message);
  /// Publish the terminal state of an accepted action goal.
  void action_done(const std::string & request_id, const Json & result_data);
  /// Build a task event. Returns null when the request is not correlated any
  /// more. The caller publishes the returned value.
  Json build_event(const std::string & request_id, const std::string & phase, const Json & data);
  /// Build the correlated reply of a finished action.
  Json build_action_reply(const Pending & pending, const Json & terminal_event);
  void expire();
  template<typename ServiceT>
  void send_service(const Command & command, typename rclcpp::Client<ServiceT>::SharedPtr client,
    typename ServiceT::Request request);
  void send_action(const Command & command);
  template<typename ActionT>
  void send_typed_action(const Command & command, ActionSlotPool<ActionT> & pool);
  template<typename ActionT>
  void feedback_callback(const std::string & command_name,
    const typename ActionT::Impl::FeedbackMessage::SharedPtr message);

  /// True while the gateway still correlates `request_id`.
  bool still_tracked(const std::string & request_id);

  /// True when the original command TTL of `request_id` elapsed.
  ///
  /// The deadline was captured when the command arrived: the remaining TTL
  /// measured on the monotonic clock, so a wall clock rollback can neither
  /// shorten nor extend it. The wall clock deadline of the sender may expire
  /// the request earlier, never later.
  bool dispatch_expired(const std::string & request_id);

  Json asset_;
  std::string asset_id_;
  std::vector<std::string> capabilities_;
  std::int64_t clock_skew_ms_;
  double request_timeout_s_;
  double action_timeout_s_;
  Correlations records_;
  std::mutex records_mutex_;
  // request_id -> accepted goal UUID hex, for feedback correlation and events.
  std::map<std::string, std::string> goal_ids_;

  // Bounded action observation pools. Every in-flight goal owns a dedicated
  // action client, so retiring one observation never affects another goal.
  // The capacity bounds the number of simultaneously observed goals.
  ActionSlotPool<NavigatePose> navigation_pool_;
  ActionSlotPool<Flight> takeoff_pool_;
  ActionSlotPool<Flight> land_pool_;

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr result_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr event_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr state_pub_;
  rclcpp::Publisher<chaos_interfaces::msg::VelocityCommand>::SharedPtr velocity_pub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr command_sub_;
  rclcpp::Subscription<chaos_interfaces::msg::CommandResult>::SharedPtr velocity_result_sub_;
  rclcpp::Subscription<chaos_interfaces::msg::RobotState>::SharedPtr state_sub_;
  rclcpp::Client<chaos_interfaces::srv::GetDescription>::SharedPtr describe_client_;
  rclcpp::Client<chaos_interfaces::srv::ControlLease>::SharedPtr lease_client_;
  rclcpp::Client<chaos_interfaces::srv::Stop>::SharedPtr stop_client_;
  rclcpp::Subscription<NavigatePose::Impl::FeedbackMessage>::SharedPtr navigation_feedback_sub_;
  rclcpp::Subscription<Flight::Impl::FeedbackMessage>::SharedPtr takeoff_feedback_sub_;
  rclcpp::Subscription<Flight::Impl::FeedbackMessage>::SharedPtr land_feedback_sub_;
  rclcpp::TimerBase::SharedPtr expire_timer_;
};

}  // namespace chaos_mqtt
