// Native sensor monitor node factory. The monitor reports per-stream
// metadata health; it never alters robot control or readiness.
//
// Endpoints: the monitor publishes under the *asset* namespace
// (`<namespace>/sensors/diagnostics` and `<namespace>/sensors/description`),
// never under a per-sensor namespace. The endpoints therefore stay
// discoverable for an asset with several sensors and for an asset without
// any sensor.
//
// Clock semantics: the diagnostic timer runs on the steady clock
// (create_wall_timer), so a paused or rewound /clock cannot stop the health
// reporting: the duty of the monitor is liveness, not simulation. The
// freshness of a single stream is judged with both clocks - the ROS clock
// for the source stamp and the steady clock for the receipt age.
//
// Threading: the timer callback runs on the executor thread that spins the
// node. The per-stream trackers are owned by that node and are only touched
// by that thread.
//
// Errors: the factory throws chaos::sensors::SensorConfigError (a
// std::runtime_error) when the asset_json parameter is invalid.
#pragma once

#include <memory>

#include <rclcpp/rclcpp.hpp>

namespace chaos::sensors {

// Creates the native sensor_monitor node. Throws std::runtime_error (a
// chaos::sensors::SensorConfigError) when the asset_json parameter is
// invalid.
std::shared_ptr<rclcpp::Node> createSensorMonitorNode(
  const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

}  // namespace chaos::sensors
