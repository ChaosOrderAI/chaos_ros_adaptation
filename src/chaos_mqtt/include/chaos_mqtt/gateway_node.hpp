#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

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
class GatewayNode : public rclcpp::Node
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
  static void publish_json(const rclcpp::Publisher<std_msgs::msg::String>::SharedPtr & publisher,
    const Json & value);

  void state_callback(const chaos_interfaces::msg::RobotState::SharedPtr message);
  void command_callback(const std_msgs::msg::String::SharedPtr message);
  void dispatch(const Command & command);
  void service_error(const std::string & request_id, const std::string & code,
    const std::string & message);
  void complete_action(Pending & pending, const Json & event);
  void action_done(const std::string & request_id, const std::string & command_name,
    const rclcpp_action::ResultCode & code, const Json & result_data);
  Json event(const std::string & request_id, const std::string & phase, const Json & data);
  void expire();

  template<typename ServiceT>
  void send_service(const Command & command, typename rclcpp::Client<ServiceT>::SharedPtr client,
    typename ServiceT::Request request);
  void send_action(const Command & command);
  template<typename ActionT>
  void send_typed_action(const Command & command,
    typename rclcpp_action::Client<ActionT>::SharedPtr client);
  template<typename ActionT>
  void feedback_callback(const std::string & command_name,
    const typename ActionT::Impl::FeedbackMessage::SharedPtr message);

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
  rclcpp_action::Client<NavigatePose>::SharedPtr navigation_client_;
  rclcpp_action::Client<Flight>::SharedPtr takeoff_client_;
  rclcpp_action::Client<Flight>::SharedPtr land_client_;
  rclcpp::Subscription<NavigatePose::Impl::FeedbackMessage>::SharedPtr navigation_feedback_sub_;
  rclcpp::Subscription<Flight::Impl::FeedbackMessage>::SharedPtr takeoff_feedback_sub_;
  rclcpp::Subscription<Flight::Impl::FeedbackMessage>::SharedPtr land_feedback_sub_;
  rclcpp::TimerBase::SharedPtr expire_timer_;
};

}  // namespace chaos_mqtt
