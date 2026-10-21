// Independent rclcpp process bridging a configured Go2 to the Unitree SDK2
// backend, with the full watchdog state machine in chaos_go2_core. Port of
// chaos_runtime/go2_driver.py.
//
// The process never changes the robot posture or mode. The SDK writes happen
// on the node's own steady watchdog timer, not on a DDS callback path: the
// vendor call carries a configured vendor timeout, which is a setting of the
// SDK and not a measured upper bound of the write.
//
// Clock semantics: the watchdog runs on the steady clock (create_wall_timer),
// so a paused or rewound /clock cannot stop the expiry, the stop retry or
// the telemetry-loss halt. The odometry carries the ROS acquisition stamp of
// the sample, never a re-stamped "now".
//
// Frames: the velocity command must be expressed in the asset base frame,
// and the odometry uses the asset odom frame with the base frame as its
// child frame.
//
// Shutdown order: close() cancels the watchdog timer, requests the stop,
// performs the zero-output handshake and only then stops the telemetry
// source. The backend (and with it the telemetry source) is declared after
// the core, so it is destroyed before the core that owns the callback
// target: no telemetry callback can reach a released core.
//
// The SDK backend is injected: production main() uses the Unitree factory
// (explicitly refusing to start when the build lacks SDK support), while
// tests drive every state transition through a fake backend over real ROS
// messages.
#pragma once

#include <memory>
#include <string>

#include <geometry_msgs/msg/twist_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>

#include "chaos_runtime/asset_config.hpp"
#include "chaos_runtime/go2/sdk_backend.hpp"
#include "chaos_runtime/go2_driver_core.hpp"

namespace chaos::go2 {

// Fills an Odometry message from a core sample, keeping the acquisition
// timestamp. Port of fill_odometry().
nav_msgs::msg::Odometry fillOdometry(const SportSample & sample,
                                     const std::string & odom_frame,
                                     const std::string & base_frame);

class Go2DriverNode : public rclcpp::Node {
public:
  // The backend is created through a factory that receives the typed
  // telemetry callback, so the production Unitree backend and a fake
  // backend wire the telemetry path identically. An empty factory selects
  // the Unitree SDK factory (real mode), which refuses to start when the
  // build has no SDK support.
  explicit Go2DriverNode(Go2BackendFactory backend_factory = {},
                         const rclcpp::NodeOptions & options =
                           rclcpp::NodeOptions());
  ~Go2DriverNode() override;

  // Explicit shutdown: cancels the watchdog, performs the zero handshake
  // and stops the telemetry source. Idempotent.
  //
  // Bounded by construction: the zero handshake performs at most three write
  // attempts on the calling thread. The wait for an in-flight telemetry
  // callback is bounded by the backend's own stop contract, not by this
  // method.
  void close();

  // A locked copy of the driver state. It is the only way to observe the
  // watchdog reason (for example "Waiting for fresh sport telemetry" or a
  // latched SDK fault) without reaching into the core.
  Status driverStatus() const { return core_->status(); }

private:
  void onCommand(const geometry_msgs::msg::TwistStamped & message);
  void tick();

  runtime::AssetConfig asset_;
  // Go2DriverCore owns a mutex and is therefore neither movable nor
  // assignable: it is held by unique_ptr and constructed in place.
  std::unique_ptr<Go2DriverCore> core_;
  // Declared after core_ so the backend (and therefore the telemetry
  // source) is destroyed before the core that owns the callback target.
  std::shared_ptr<Go2SdkBackend> backend_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr cmd_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
  bool closing_ = false;
  std::string logged_fault_;
};

}  // namespace chaos::go2
