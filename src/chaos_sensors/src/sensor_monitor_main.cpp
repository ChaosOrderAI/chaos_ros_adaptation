// Native sensor_monitor entry point. Installed as
// lib/chaos_sensors/sensor_monitor.
#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "chaos_sensors/sensor_monitor_node.hpp"

int main(int argc, char ** argv) {
  try {
    rclcpp::init(argc, argv);
    auto node = chaos::sensors::createSensorMonitorNode();
    rclcpp::spin(node);
  } catch (const std::exception & error) {
    // Startup and argument failures must not terminate through
    // std::terminate.
    fprintf(stderr, "sensor_monitor failed: %s\n", error.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
