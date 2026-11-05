#include "chaos_sim/world.hpp"

#include <mujoco/mujoco.h>

#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joint_state.hpp>
#include <std_srvs/srv/set_bool.hpp>
#include <std_srvs/srv/trigger.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;
using JointState = sensor_msgs::msg::JointState;
using DiagnosticArray = diagnostic_msgs::msg::DiagnosticArray;

struct Instance {
  std::string name, ns;
  std::unique_ptr<chaos::sim::World> world;
  chaos::sim::State state;
  JointState joints;
  nav_msgs::msg::Odometry odom;
  DiagnosticArray diagnostics;
  rclcpp::Publisher<JointState>::SharedPtr joint_pub;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub;
  rclcpp::Publisher<DiagnosticArray>::SharedPtr diagnostic_pub;
  rclcpp::Subscription<JointState>::SharedPtr targets;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr reset;
  Clock::time_point last_command{};
  bool command_active = false;
  uint64_t accepted = 0, rejected = 0, timed_out = 0;
  int divisor = 1, phase = 0;
};

template <typename T>
void number(std::string &destination, T value) {
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  if (result.ec != std::errc{}) throw std::runtime_error("diagnostic formatting failed");
  destination.assign(buffer, result.ptr);
}

bool localhostDiscovery() {
  const char * legacy = std::getenv("ROS_LOCALHOST_ONLY");
  if (legacy != nullptr && std::string(legacy) == "0") return false;
  if (legacy != nullptr && std::string(legacy) == "1") return true;
  const char * range = std::getenv("ROS_AUTOMATIC_DISCOVERY_RANGE");
  return range != nullptr && std::string(range) == "LOCALHOST";
}

class SimulatorNode final : public rclcpp::Node {
 public:
  SimulatorNode() : Node("chaos_simulator") {
    if (!localhostDiscovery()) {
      throw std::invalid_argument(
          "chaos_simulator refuses a non-local ROS discovery range. "
          "Set ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST and do not set ROS_LOCALHOST_ONLY=0");
    }
    const auto json = declare_parameter<std::string>("worlds_json", "[]");
    const auto discard_visual = declare_parameter<bool>("discard_visual", true);
    const auto publish_rate = declare_parameter<double>("publish_rate_hz", 50.0);
    const auto timeout = declare_parameter<double>("command_timeout_s", 0.5);
    if (!std::isfinite(publish_rate) || publish_rate < 0.1 || publish_rate > 1000) {
      throw std::invalid_argument("invalid publish_rate_hz: expected finite value in [0.1,1000] Hz");
    }
    if (!std::isfinite(timeout) || timeout <= 0) {
      throw std::invalid_argument("command_timeout_s must be positive and finite");
    }
    publish_period_ = std::chrono::duration_cast<Nanoseconds>(std::chrono::duration<double>(1 / publish_rate));
    command_timeout_ = std::chrono::duration<double>(timeout);
    const auto entries = nlohmann::json::parse(json);
    if (!entries.is_array() || entries.empty() || entries.size() > 32)
      throw std::invalid_argument("worlds_json requires 1..32 worlds");
    std::set<std::string> names, namespaces;
    const std::regex name_pattern("[A-Za-z_][A-Za-z0-9_]*");
    const std::regex ns_pattern("(/[A-Za-z_][A-Za-z0-9_]*)+");
    instances_.reserve(entries.size());
    int minimum_us = 2000;
    for (const auto &entry : entries) {
      auto instance = std::make_unique<Instance>();
      instance->name = entry.at("name").get<std::string>();
      instance->ns = entry.at("namespace").get<std::string>();
      if (!std::regex_match(instance->name, name_pattern) || !std::regex_match(instance->ns, ns_pattern) ||
          !names.insert(instance->name).second || !namespaces.insert(instance->ns).second)
        throw std::invalid_argument("world names and absolute namespaces must be valid and unique");
      chaos::sim::WorldConfig config;
      config.model_path = entry.at("model_path").get<std::string>();
      config.kp = entry.value("kp", 40.0);
      config.kd = entry.value("kd", 2.0);
      config.discard_visual = discard_visual;
      config.scene.path = entry.value("scene_path", "");
      config.scene.voxel_size = entry.value("scene_voxel_size", 0.1);
      config.scene.point_radius = entry.value("scene_point_radius", 0.03);
      if (entry.contains("scene_max_geoms")) {
        const auto& value = entry["scene_max_geoms"];
        if (!value.is_number_integer()) {
          throw std::invalid_argument("scene_max_geoms must be a positive integer");
        }
        std::int64_t wide = 0;
        try {
          wide = value.get<std::int64_t>();
        } catch (const nlohmann::json::exception&) {
          throw std::invalid_argument("scene_max_geoms must be a positive integer");
        }
        if (wide <= 0) throw std::invalid_argument("scene_max_geoms must be positive");
        config.scene.max_geoms = static_cast<std::size_t>(wide);
      }
      config.scene.collision = entry.value("scene_collision", true);
      if (config.model_path.empty()) throw std::invalid_argument("model_path must not be empty");
      instance->world = std::make_unique<chaos::sim::World>(config);
      const double timestep_us = instance->world->timestep() * 1e6;
      if (std::abs(timestep_us - 1000.0) > 1e-6 && std::abs(timestep_us - 2000.0) > 1e-6)
        throw std::invalid_argument("only model timesteps of 1ms or 2ms are supported");
      minimum_us = std::min(minimum_us, static_cast<int>(std::llround(timestep_us)));
      initialize(*instance);
      instances_.push_back(std::move(instance));
    }
    tick_period_ = std::chrono::microseconds(minimum_us);
    for (auto &instance : instances_)
      instance->divisor = static_cast<int>(std::llround(instance->world->timestep() * 1e6)) / minimum_us;
    pause_ = create_service<std_srvs::srv::SetBool>("/chaos_sim/pause",
      [this](const std::shared_ptr<std_srvs::srv::SetBool::Request> request,
             std::shared_ptr<std_srvs::srv::SetBool::Response> response) {
        paused_ = request->data;
        for (auto &instance : instances_) {
          instance->world->hold();
          instance->command_active = false;
          instance->phase = 0;
        }
        next_tick_ = Clock::now() + tick_period_;
        response->success = true;
        response->message = paused_ ? "paused; targets cleared" : "running; holding current positions";
      });
    next_tick_ = Clock::now() + tick_period_;
    next_publish_ = Clock::now();
    timer_ = create_wall_timer(tick_period_, [this] { tick(); });
  }

 private:
  void initialize(Instance &instance) {
    instance.world->readState(instance.state);
    instance.joints.name = instance.world->jointNames();
    const auto count = instance.joints.name.size();
    instance.joints.position.resize(count);
    instance.joints.velocity.resize(count);
    instance.joints.effort.resize(count);
    instance.joints.header.frame_id = instance.name + "/base_link";
    instance.odom.header.frame_id = instance.name + "/odom";
    instance.odom.child_frame_id = instance.name + "/base_link";
    instance.joint_pub = create_publisher<JointState>(instance.ns + "/joint_states", 1);
    instance.odom_pub = create_publisher<nav_msgs::msg::Odometry>(instance.ns + "/odom", 1);
    instance.diagnostic_pub = create_publisher<DiagnosticArray>(instance.ns + "/diagnostics", 1);
    instance.diagnostics.status.resize(1);
    auto &status = instance.diagnostics.status.front();
    status.name = instance.name + "/physics";
    status.hardware_id = "mujoco";
    status.message = "running";
    status.message.reserve(32);
    const std::vector<std::string> keys = {"sim_time", "paused", "accepted_count", "rejected_count",
      "timeout_count", "model_bytes", "arena_bytes", "dropped_ticks", "scene_points", "scene_geoms"};
    status.values.resize(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
      status.values[i].key = keys[i];
      status.values[i].value.reserve(64);
    }
    instance.targets = create_subscription<JointState>(instance.ns + "/joint_targets", rclcpp::QoS(1),
      [this, &instance](JointState::ConstSharedPtr message) {
        std::string error;
        if (paused_) error = "simulation paused";
        else if (!message->velocity.empty() || !message->effort.empty())
          error = "joint_targets accepts only name and position";
        else if (instance.world->setTargets(message->name, message->position, error)) {
          instance.last_command = Clock::now();
          instance.command_active = true;
          ++instance.accepted;
          return;
        }
        ++instance.rejected;
        RCLCPP_WARN_THROTTLE(get_logger(), steady_clock_, 1000, "%s target rejected: %s",
                             instance.name.c_str(), error.c_str());
      });
    instance.reset = create_service<std_srvs::srv::Trigger>(instance.ns + "/reset",
      [&instance](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
                  std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
        instance.world->reset();
        instance.command_active = false;
        instance.phase = 0;
        response->success = true;
        response->message = "reset to model initial state; targets cleared";
      });
    RCLCPP_INFO(get_logger(), "%s loaded: %zu joints, dt=%g, model=%zu bytes, arena=%zu bytes, scene=%zu/%zu points/geoms",
      instance.name.c_str(), count, instance.world->timestep(), instance.world->modelBytes(),
      instance.world->arenaBytes(), instance.world->scenePoints(), instance.world->sceneGeoms());
  }

  void tick() {
    const auto steady_now = Clock::now();
    if (!paused_) {
      for (auto &instance : instances_) {
        if (instance->command_active && steady_now - instance->last_command >= command_timeout_) {
          instance->world->hold();
          instance->command_active = false;
          ++instance->timed_out;
        }
      }
      if (steady_now >= next_tick_) {
        const auto due = 1 + (steady_now - next_tick_) / tick_period_;
        const auto steps = std::min<int64_t>(due, 8);
        if (due > steps) {
          dropped_ticks_ += due - steps;
          next_tick_ = steady_now + tick_period_;
        } else next_tick_ += due * tick_period_;
        for (int64_t tick = 0; tick < steps; ++tick) {
          for (auto &instance : instances_) {
            if (++instance->phase == instance->divisor) {
              instance->world->step();
              instance->phase = 0;
            }
          }
        }
      }
    } else next_tick_ = steady_now + tick_period_;
    if (steady_now >= next_publish_) {
      next_publish_ = steady_now + publish_period_;
      const auto stamp = now();
      for (auto &instance : instances_) publish(*instance, stamp);
    }
  }

  void publish(Instance &instance, const rclcpp::Time &stamp) {
    instance.world->readState(instance.state);
    const auto &state = instance.state;
    instance.joints.header.stamp = stamp;
    std::copy(state.position.begin(), state.position.end(), instance.joints.position.begin());
    std::copy(state.velocity.begin(), state.velocity.end(), instance.joints.velocity.begin());
    std::copy(state.effort.begin(), state.effort.end(), instance.joints.effort.begin());
    auto &odom = instance.odom;
    odom.header.stamp = stamp;
    odom.pose.pose.position.x = state.base_position[0];
    odom.pose.pose.position.y = state.base_position[1];
    odom.pose.pose.position.z = state.base_position[2];
    odom.pose.pose.orientation.x = state.base_orientation[0];
    odom.pose.pose.orientation.y = state.base_orientation[1];
    odom.pose.pose.orientation.z = state.base_orientation[2];
    odom.pose.pose.orientation.w = state.base_orientation[3];
    odom.twist.twist.linear.x = state.base_twist[0];
    odom.twist.twist.linear.y = state.base_twist[1];
    odom.twist.twist.linear.z = state.base_twist[2];
    odom.twist.twist.angular.x = state.base_twist[3];
    odom.twist.twist.angular.y = state.base_twist[4];
    odom.twist.twist.angular.z = state.base_twist[5];
    instance.diagnostics.header.stamp = stamp;
    auto &status = instance.diagnostics.status.front();
    status.message = paused_ ? "paused" : "running";
    auto &values = status.values;
    number(values[0].value, state.simulation_time);
    values[1].value = paused_ ? "true" : "false";
    number(values[2].value, instance.accepted);
    number(values[3].value, instance.rejected);
    number(values[4].value, instance.timed_out);
    number(values[5].value, instance.world->modelBytes());
    number(values[6].value, instance.world->arenaBytes());
    number(values[7].value, dropped_ticks_);
    number(values[8].value, instance.world->scenePoints());
    number(values[9].value, instance.world->sceneGeoms());
    instance.joint_pub->publish(instance.joints);
    instance.odom_pub->publish(instance.odom);
    instance.diagnostic_pub->publish(instance.diagnostics);
  }

  rclcpp::Clock steady_clock_{RCL_STEADY_TIME};
  std::vector<std::unique_ptr<Instance>> instances_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr pause_;
  Nanoseconds tick_period_{1000000}, publish_period_{20000000};
  std::chrono::duration<double> command_timeout_{0.5};
  Clock::time_point next_tick_, next_publish_;
  bool paused_ = false;
  uint64_t dropped_ticks_ = 0;
};
}  // namespace

int main(int argc, char **argv) {
  // These worlds compile once at startup; a process-wide asset cache would
  // retain hundreds of MB without benefiting subsequent physics steps.
  mj_setCacheCapacity(mj_getCache(), 0);
  rclcpp::init(argc, argv);
  int result = 0;
  try {
    auto node = std::make_shared<SimulatorNode>();
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.spin();
    executor.remove_node(node);
  } catch (const std::exception &error) {
    RCLCPP_ERROR(rclcpp::get_logger("chaos_simulator"), "%s", error.what());
    result = 1;
  }
  rclcpp::shutdown();
  return result;
}
