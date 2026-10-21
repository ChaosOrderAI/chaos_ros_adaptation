// Mock asset for the native SDK: description, control lease and a navigation
// action server. It is used by the Python integration test and by the
// example client, so the native client can be exercised against a real ROS
// peer in a separate process.
//
// Usage: chaos_sdk_mock_asset [asset_id] [--namespace /robots/<id>]
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include <geometry_msgs/msg/pose_stamped.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>

#include "chaos_interfaces/action/navigate_pose.hpp"
#include "chaos_interfaces/srv/control_lease.hpp"
#include "chaos_interfaces/srv/get_description.hpp"
#include "chaos_interfaces/srv/stop.hpp"

namespace
{

/// Compact and stable description of a target pose.
///
/// The mock echoes the target it received, so a test can verify that the
/// client really sent the coordinates and the frame it was given.
std::string describe_target(const geometry_msgs::msg::PoseStamped & target)
{
  std::ostringstream out;
  out << "frame=" << target.header.frame_id
      << " x=" << target.pose.position.x
      << " y=" << target.pose.position.y
      << " z=" << target.pose.position.z
      << " qx=" << target.pose.orientation.x
      << " qy=" << target.pose.orientation.y
      << " qz=" << target.pose.orientation.z
      << " qw=" << target.pose.orientation.w;
  return out.str();
}

}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  std::string asset_id = "sdk_mock_asset";
  std::string namespace_ = "/robots/" + asset_id;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--namespace" && index + 1 < argc) {
      namespace_ = argv[++index];
    } else if (argument.rfind("--namespace=", 0) == 0) {
      namespace_ = argument.substr(std::string("--namespace=").size());
    } else if (argument.rfind("--", 0) != 0) {
      asset_id = argument;
    }
  }
  namespace_ = namespace_.empty() ? namespace_ : "/" + namespace_;
  while (!namespace_.empty() && namespace_.front() == '/') {
    namespace_.erase(namespace_.begin());
  }
  namespace_ = "/" + namespace_;
  while (!namespace_.empty() && namespace_.back() == '/') {
    namespace_.pop_back();
  }

  auto node = std::make_shared<rclcpp::Node>("chaos_sdk_mock_asset");
  auto group = node->create_callback_group(rclcpp::CallbackGroupType::Reentrant);

  auto description = node->create_service<chaos_interfaces::srv::GetDescription>(
    namespace_ + "/description",
    [asset_id](
      const std::shared_ptr<chaos_interfaces::srv::GetDescription::Request>,
      std::shared_ptr<chaos_interfaces::srv::GetDescription::Response> response) {
      response->asset_id = asset_id;
      response->model = "mock";
      response->base_frame = asset_id + "/base_link";
      response->odom_frame = asset_id + "/odom";
      response->map_frame = asset_id + "/map";
      response->command_timeout_s = 0.5;
      response->lease_max_s = 30.0;
      response->capabilities = {"motion.velocity", "navigation.to_pose"};
    },
    rclcpp::ServicesQoS(), group);

  auto lease = node->create_service<chaos_interfaces::srv::ControlLease>(
    namespace_ + "/control/lease",
    [](
      const std::shared_ptr<chaos_interfaces::srv::ControlLease::Request> request,
      std::shared_ptr<chaos_interfaces::srv::ControlLease::Response> response) {
      if (request->operation == "release") {
        response->success = true;
        response->code = "OK";
        response->token = "";
        return;
      }
      response->success = true;
      response->code = "OK";
      response->token = "mock-lease-token";
      response->remaining_s = request->duration_s;
    },
    rclcpp::ServicesQoS(), group);

  auto stop = node->create_service<chaos_interfaces::srv::Stop>(
    namespace_ + "/motion/stop",
    [](
      const std::shared_ptr<chaos_interfaces::srv::Stop::Request>,
      std::shared_ptr<chaos_interfaces::srv::Stop::Response> response) {
      response->success = true;
      response->code = "OK";
    },
    rclcpp::ServicesQoS(), group);

  auto action = rclcpp_action::create_server<chaos_interfaces::action::NavigatePose>(
    node, namespace_ + "/navigation/to_pose",
    [](
      const rclcpp_action::GoalUUID &,
      std::shared_ptr<const chaos_interfaces::action::NavigatePose::Goal>) {
      return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
    },
    [](const std::shared_ptr<rclcpp_action::ServerGoalHandle<
      chaos_interfaces::action::NavigatePose>>) {
      return rclcpp_action::CancelResponse::ACCEPT;
    },
    [](std::shared_ptr<rclcpp_action::ServerGoalHandle<chaos_interfaces::action::NavigatePose>>
    handle) {
      auto feedback = std::make_shared<chaos_interfaces::action::NavigatePose::Feedback>();
      feedback->progress = 1.0f;
      handle->publish_feedback(feedback);
      auto result = std::make_shared<chaos_interfaces::action::NavigatePose::Result>();
      result->success = true;
      result->code = "OK";
      result->message = describe_target(handle->get_goal()->target);
      handle->succeed(result);
    },
    rcl_action_server_get_default_options(), group);

  RCLCPP_INFO(node->get_logger(), "mock asset '%s' serving %s", asset_id.c_str(), namespace_.c_str());
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
