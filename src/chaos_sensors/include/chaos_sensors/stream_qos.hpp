#pragma once

#include <string>

#include <rclcpp/qos.hpp>

#include "chaos_sensors/sensor_config.hpp"

namespace chaos::sensors {

// The only QoS names the inventory allows. Publishers and the monitor must
// use this mapping, or a stream declared reliable is published reliably and
// subscribed with sensor-data QoS, and the two endpoints never connect.
inline rclcpp::QoS streamQoS(const std::string & name) {
  if (name == "sensor_data") {
    return rclcpp::SensorDataQoS();
  }
  if (name == "reliable") {
    return rclcpp::QoS(5).reliability(rclcpp::ReliabilityPolicy::Reliable);
  }
  throw SensorConfigError("unsupported sensor QoS");
}

}  // namespace chaos::sensors
