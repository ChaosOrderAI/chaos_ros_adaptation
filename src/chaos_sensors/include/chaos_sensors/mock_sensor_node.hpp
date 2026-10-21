// Native mock sensor node factory. The node publishes synthetic sensor
// messages for integration tests; it never touches hardware.
//
// Clock semantics: the sampling cadence follows the ROS clock, exactly like
// the pre-migration mock. A paused or rewound /clock stops the synthetic
// samples, because a sample is a simulated acquisition and its stamp would
// otherwise repeat. The freshness diagnostics of the sensor monitor and the
// watchdogs of the runtime use the steady clock instead, because their duty
// is liveness, not simulation.
//
// Threading: every timer callback runs on the executor thread that spins the
// node. The payload of a stream is allocated once at construction and is
// reused for every publication; only the header stamp is rewritten.
//
// Endpoints: the publishers are stored in the node, because rclcpp::Node
// keeps only weak references to them. A publisher that was not stored would
// be destroyed immediately and the topic would disappear from the graph.
//
// Errors: the factory throws chaos::sensors::SensorConfigError (a
// std::runtime_error) when the sensor_json parameter is invalid, so the
// executable reports the failure and exits non-zero instead of starting a
// half-configured node.
#pragma once

#include <memory>

#include <rclcpp/rclcpp.hpp>

namespace chaos::sensors {

// Creates the native mock_sensor node. Throws std::runtime_error (a
// chaos::sensors::SensorConfigError) when the sensor_json parameter is
// invalid, so the executable can report the failure and exit non-zero
// instead of starting a half-configured node.
std::shared_ptr<rclcpp::Node> createMockSensorNode(
  const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

}  // namespace chaos::sensors
