#include "chaos_runtime/go2/go2_driver_node.hpp"

#include <chrono>
#include <utility>

#include <geometry_msgs/msg/twist_stamped.hpp>

#include "chaos_runtime/ros_adapters.hpp"

namespace chaos::go2 {

nav_msgs::msg::Odometry fillOdometry(const SportSample & sample,
                                     const std::string & odom_frame,
                                     const std::string & base_frame) {
  nav_msgs::msg::Odometry message;
  // Keep the DDS acquisition timestamp; timer publication never restamps.
  message.header.stamp.sec =
    static_cast<std::int32_t>(sample.ros_stamp_ns / 1000000000LL);
  message.header.stamp.nanosec =
    static_cast<std::uint32_t>(sample.ros_stamp_ns % 1000000000LL);
  message.header.frame_id = odom_frame;
  message.child_frame_id = base_frame;
  message.pose.pose.position.x = sample.position[0];
  message.pose.pose.position.y = sample.position[1];
  message.pose.pose.position.z = sample.position[2];
  message.pose.pose.orientation.x = sample.quaternion[0];
  message.pose.pose.orientation.y = sample.quaternion[1];
  message.pose.pose.orientation.z = sample.quaternion[2];
  message.pose.pose.orientation.w = sample.quaternion[3];
  message.twist.twist.linear.x = sample.linear[0];
  message.twist.twist.linear.y = sample.linear[1];
  message.twist.twist.linear.z = sample.linear[2];
  message.twist.twist.angular.x = sample.angular[0];
  message.twist.twist.angular.y = sample.angular[1];
  message.twist.twist.angular.z = sample.angular[2];
  return message;
}

Go2DriverNode::Go2DriverNode(Go2BackendFactory backend_factory,
                             const rclcpp::NodeOptions & options)
    : rclcpp::Node("go2_driver", options) {
  declare_parameter<std::string>("asset_json", "{}");
  asset_ = runtime::parseAssetJson(get_parameter("asset_json").as_string());
  if (asset_.plugin != "unitree_go2") {
    throw std::runtime_error("go2_driver requires a unitree_go2 asset");
  }
  if (asset_.mode != "real") {
    throw std::runtime_error(
      "go2_driver requires a real asset; mock mode must not load the SDK");
  }
  runtime::Go2Adapter::validateOptions(asset_);
  const std::int64_t context_domain =
    get_node_base_interface()->get_context()->get_domain_id();
  if (context_domain != asset_.ros_domain_id) {
    throw std::runtime_error(
      "Go2 asset ros_domain_id does not match the active ROS context");
  }
  const auto & options_json = asset_.options;
  const int dds_domain = static_cast<int>(
    options_json.at("dds_domain_id").get<std::int64_t>());
  const std::string interface =
    options_json.at("network_interface").get<std::string>();
  const std::string sport_topic =
    options_json.at("sport_state_topic").get<std::string>();
  const std::string cmd_topic =
    options_json.at("cmd_vel_topic").get<std::string>();
  const std::string odom_topic =
    options_json.at("odom_topic").get<std::string>();

  Go2AssetConfig core_config;
  core_config.base_frame = asset_.frames.base;
  core_config.odom_frame = asset_.frames.odom;
  core_config.velocity_frame =
    options_json.at("velocity_frame").get<std::string>();
  core_config.limits = asset_.limits;
  core_ = std::make_unique<Go2DriverCore>(
    core_config, nullptr,
    [this] { return get_clock()->now().nanoseconds(); });

  odom_pub_ = create_publisher<nav_msgs::msg::Odometry>(
    odom_topic, rclcpp::QoS(1));
  cmd_sub_ = create_subscription<geometry_msgs::msg::TwistStamped>(
    cmd_topic, rclcpp::QoS(1),
    [this](const geometry_msgs::msg::TwistStamped & message) {
      onCommand(message);
    });

  if (!backend_factory) {
    // Throws when the build lacks Unitree SDK support: real mode must
    // refuse to start, never degrade to a fake device.
    backend_factory = unitreeSdkBackendFactory(dds_domain, interface,
                                               sport_topic);
  }
  // The factory receives the typed telemetry callback, so the production
  // backend and a fake backend deliver telemetry through the same path.
  backend_ = backend_factory(
    [this](const SportStateSnapshot & snapshot) {
      SportState state;
      state.has_tick = snapshot.has_tick;
      state.tick = snapshot.tick;
      state.has_stamp = snapshot.has_stamp;
      state.stamp = Stamp{snapshot.sec, snapshot.nanosec};
      state.position = snapshot.position;
      state.velocity = snapshot.velocity;
      state.yaw_speed = snapshot.yaw_speed;
      state.quaternion = snapshot.quaternion;
      state.gyroscope = snapshot.gyroscope;
      state.error_code = snapshot.error_code;
      core_->acceptState(state);
    });
  if (!backend_) {
    throw std::runtime_error("go2_driver backend factory returned no backend");
  }

  // Steady-clock watchdog: the /clock may be paused or rewound, but the
  // driver watchdog must keep running. The SDK write happens on this timer
  // callback; it carries the configured vendor timeout, which is a setting of
  // the SDK and not a measured upper bound of this path.
  timer_ = create_wall_timer(std::chrono::milliseconds(50),
                             [this] { tick(); });

  RCLCPP_INFO(get_logger(), "%s: Go2 driver started on %s; waiting for fresh telemetry",
              asset_.id.c_str(), interface.c_str());
}

Go2DriverNode::~Go2DriverNode() { close(); }

void Go2DriverNode::onCommand(
  const geometry_msgs::msg::TwistStamped & message) {
  VelocityCommand command;
  command.stamp = Stamp{message.header.stamp.sec,
                        static_cast<std::int64_t>(message.header.stamp.nanosec)};
  command.frame_id = message.header.frame_id;
  command.values = {message.twist.linear.x, message.twist.linear.y,
                    message.twist.linear.z, message.twist.angular.x,
                    message.twist.angular.y, message.twist.angular.z};
  if (!core_->acceptCommand(command)) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "%s",
                         core_->status().detail.c_str());
  }
}

void Go2DriverNode::tick() {
  const auto sample = core_->step(
    [this](double vx, double vy, double wz) {
      return backend_->move(vx, vy, wz);
    });
  const auto status = core_->status();
  if (!status.fault.empty() && status.fault != logged_fault_) {
    logged_fault_ = status.fault;
    RCLCPP_ERROR(get_logger(), "%s; motion disabled until driver restart",
                 logged_fault_.c_str());
  } else if (!status.output_error.empty() && !status.initialized) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                         "%s; retrying initial zero output",
                         status.output_error.c_str());
  }
  if (sample.has_value()) {
    odom_pub_->publish(fillOdometry(*sample, core_->odomFrame(),
                                    core_->baseFrame()));
  }
}

void Go2DriverNode::close() {
  if (closing_) {
    return;
  }
  closing_ = true;
  if (timer_) {
    timer_->cancel();
  }
  core_->requestStop(true);
  if (backend_) {
    // Bounded shutdown handshake: at most three zero attempts on this
    // thread; the backend destructor releases SDK resources.
    for (int i = 0; i < 3; ++i) {
      core_->step([this](double vx, double vy, double wz) {
        return backend_->move(vx, vy, wz);
      });
      if (!core_->status().stop_pending) {
        break;
      }
    }
    if (core_->status().stop_pending) {
      RCLCPP_ERROR(get_logger(),
                   "Shutdown zero output failed after three attempts");
    }
    // The telemetry source is stopped before the core that owns the
    // callback target is destroyed: no telemetry can arrive afterwards.
    backend_->stop();
  }
}

}  // namespace chaos::go2
