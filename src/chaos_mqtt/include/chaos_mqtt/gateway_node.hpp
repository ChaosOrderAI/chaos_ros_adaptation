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
#include "chaos_interfaces/action/software_update.hpp"
#include "chaos_interfaces/msg/announcement.hpp"
#include "chaos_interfaces/msg/command_result.hpp"
#include "chaos_interfaces/msg/robot_state.hpp"
#include "chaos_interfaces/msg/skill_event.hpp"
#include "chaos_interfaces/msg/skill_intent.hpp"
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
  using SoftwareUpdate = chaos_interfaces::action::SoftwareUpdate;

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
  /// Forward an announcement of the skill layer to the external channel.
  ///
  /// The skill layer writes its announcements to a plain ROS topic, so the
  /// transport of the day forwards them. Nothing in the skill layer knows
  /// that this transport is MQTT.
  void announcement_callback(const chaos_interfaces::msg::Announcement::SharedPtr message);
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
  /// Hand a capability command to the skill layer as an intent.
  ///
  /// The skill layer is the only caller of the base capabilities, so a
  /// navigation or a flight command does not reach the runtime directly. The
  /// gateway keeps the wire duties: the envelope, the deadline, the
  /// correlation and the reply.
  void send_skill(const Command & command);
  /// Map a capability command to the skill token and its arguments.
  bool skill_for_command(
    const Command & command, std::string & token, Json & arguments, std::string & error) const;
  /// Follow the lifecycle of a skill request back onto the wire.
  void skill_event_callback(const chaos_interfaces::msg::SkillEvent::SharedPtr & message);
  /// Republish the intents that were not accepted yet.
  ///
  /// The skill layer may not be up when the command arrives. The intent is
  /// republished until it is accepted, because the skill layer remembers the
  /// request id and replays the last outcome instead of running the skill a
  /// second time.
  void retry_skill_requests();
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
  ActionSlotPool<SoftwareUpdate> ota_pool_;

  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr result_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr event_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr state_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr announcement_pub_;
  rclcpp::Subscription<chaos_interfaces::msg::Announcement>::SharedPtr announcement_sub_;
  rclcpp::Publisher<chaos_interfaces::msg::VelocityCommand>::SharedPtr velocity_pub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr command_sub_;
  rclcpp::Subscription<chaos_interfaces::msg::CommandResult>::SharedPtr velocity_result_sub_;
  rclcpp::Subscription<chaos_interfaces::msg::RobotState>::SharedPtr state_sub_;
  rclcpp::Publisher<chaos_interfaces::msg::SkillIntent>::SharedPtr skill_intent_pub_;
  rclcpp::Subscription<chaos_interfaces::msg::SkillEvent>::SharedPtr skill_event_sub_;
  // The skill requests the gateway observes, by request id. An entry lives
  // from the publication of the intent until its terminal event.
  std::map<std::string, chaos_interfaces::msg::SkillIntent> skill_requests_;
  // Monotonic time of the last publication of an intent, for the requests
  // that the skill layer did not accept yet.
  std::map<std::string, double> skill_published_at_;
  // Upper bound of the skill requests that are observed at the same time.
  std::size_t skill_request_capacity_ = 48;
  rclcpp::Client<chaos_interfaces::srv::GetDescription>::SharedPtr describe_client_;
  rclcpp::Client<chaos_interfaces::srv::ControlLease>::SharedPtr lease_client_;
  rclcpp::Client<chaos_interfaces::srv::Stop>::SharedPtr stop_client_;
  rclcpp::Subscription<NavigatePose::Impl::FeedbackMessage>::SharedPtr navigation_feedback_sub_;
  rclcpp::Subscription<Flight::Impl::FeedbackMessage>::SharedPtr takeoff_feedback_sub_;
  rclcpp::Subscription<Flight::Impl::FeedbackMessage>::SharedPtr land_feedback_sub_;
  rclcpp::Subscription<SoftwareUpdate::Impl::FeedbackMessage>::SharedPtr ota_feedback_sub_;
  rclcpp::TimerBase::SharedPtr expire_timer_;
};

}  // namespace chaos_mqtt
