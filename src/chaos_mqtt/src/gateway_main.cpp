#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "chaos_mqtt/gateway_node.hpp"

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    auto node = std::make_shared<chaos_mqtt::GatewayNode>();
    rclcpp::spin(node);
  } catch (const std::exception & error) {
    RCLCPP_ERROR(rclcpp::get_logger("mqtt_gateway"), "gateway failed: %s", error.what());
    if (rclcpp::ok()) {
      rclcpp::shutdown();
    }
    return 1;
  }
  if (rclcpp::ok()) {
    rclcpp::shutdown();
  }
  return 0;
}
