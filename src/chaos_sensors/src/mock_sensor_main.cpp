// Native mock_sensor entry point. Installed as lib/chaos_sensors/mock_sensor.
#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "chaos_sensors/mock_sensor_node.hpp"

int main(int argc, char ** argv) {
  try {
    rclcpp::init(argc, argv);
    auto node = chaos::sensors::createMockSensorNode();
    rclcpp::spin(node);
  } catch (const std::exception & error) {
    // Startup and argument failures must not terminate through
    // std::terminate.
    fprintf(stderr, "mock_sensor failed: %s\n", error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
