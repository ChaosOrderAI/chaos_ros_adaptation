// Ports of chaos_runtime/adapters/{lingao,go2,dji_legacy}.py.
#include "chaos_runtime/ros_adapters.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>

#include <chaos_interfaces/msg/driver_velocity_command.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <std_msgs/msg/string.hpp>

#include "chaos_core/clock.hpp"
#include "chaos_core/time.hpp"
#include "chaos_runtime/adapters/mock_adapter.hpp"
#include "chaos_runtime/asset_validation.hpp"
#include "chaos_runtime/driver_deadline.hpp"

namespace chaos::runtime {

namespace {


void requireKnownOptions(const nlohmann::json & options,
                         const std::set<std::string> & allowed,
                         const std::string & plugin) {
  for (const auto & item : options.items()) {
    if (allowed.count(item.key()) == 0) {
      throw AssetConfigError("Unsupported " + plugin + " options: " +
                             item.key());
    }
  }
}

// Strict integer option: JSON booleans are rejected explicitly even though
// nlohmann already separates the types.
std::int64_t requireIntegerOption(const nlohmann::json & options,
                                  const std::string & name,
                                  const std::string & where) {
  auto found = options.find(name);
  if (found == options.end() || found->is_boolean() ||
      !found->is_number_integer()) {
    throw AssetConfigError(where + " requires integer option " + name);
  }
  return found->get<std::int64_t>();
}

std::string requireStringOption(const nlohmann::json & options,
                                const std::string & name,
                                const std::string & where) {
  auto found = options.find(name);
  if (found == options.end() || !found->is_string()) {
    throw AssetConfigError(where + " requires string option " + name);
  }
  return found->get<std::string>();
}

// Depth-capped SAX handler for the legacy DJI status JSON. Only the two
// boolean fields the adapter consumes are captured; anything else is
// validated as well-formed JSON without building a DOM.
constexpr int kMaxJsonDepth = 32;
constexpr std::size_t kMaxStatusCharacters = 64 * 1024;

struct StatusSax {
  bool ok = false;
  bool flight_fresh = false;
  int depth = 0;
  // Depth of the object that owns the current key. Only members of the
  // top-level object may influence the state: a nested "ok" must never
  // impersonate a ready status.
  int key_depth = -1;
  std::string current_key;

  // Only a top-level boolean may influence the control-facing state, and
  // only when the value really is a boolean: an array such as {"ok":[true]}
  // or a nested object must not impersonate a ready status. A non-boolean
  // value for the same key resets the flag, so {"ok":true,"ok":0} fails
  // closed instead of keeping the earlier true.
  bool null() { reset(); return true; }
  bool boolean(bool value) {
    if (key_depth == 1 && depth == 1) {
      if (current_key == "ok") {
        ok = value;
      } else if (current_key == "flight_telemetry_fresh") {
        flight_fresh = value;
      }
    }
    return true;
  }
  bool number_integer(std::int64_t) { reset(); return true; }
  bool number_unsigned(std::uint64_t) { reset(); return true; }
  // nlohmann requires the (value, raw) overload; a non-finite literal is
  // rejected so malformed status payloads never reach the state cache.
  bool number_float(double value, const std::string &) {
    reset();
    return std::isfinite(value);
  }
  bool string(const std::string &) { reset(); return true; }
  bool binary(const nlohmann::json::binary_t &) { return false; }
  bool start_object(std::size_t) {
    return ++depth <= kMaxJsonDepth;
  }
  bool key(const std::string & key) {
    current_key = key;
    key_depth = depth;
    return key.size() <= 256;
  }

  bool end_object() {
    --depth;
    return true;
  }
  bool start_array(std::size_t) { return ++depth <= kMaxJsonDepth; }
  bool end_array() {
    --depth;
    return true;
  }
  bool parse_error(std::size_t, const std::string &, const nlohmann::json::exception &) {
    return false;
  }

private:
  // Clears the flag of the current key when the value is not a boolean.
  void reset() {
    if (key_depth != 1) {
      return;
    }
    if (current_key == "ok") {
      ok = false;
    } else if (current_key == "flight_telemetry_fresh") {
      flight_fresh = false;
    }
  }
};

bool parseStatus(const std::string & data, bool & ok, bool & flight_fresh) {
  if (data.empty() || data.size() > kMaxStatusCharacters) {
    return false;
  }
  StatusSax sax;
  if (!nlohmann::json::sax_parse(data, &sax)) {
    return false;
  }
  ok = sax.ok;
  flight_fresh = sax.flight_fresh;
  return true;
}

// Lingao and Go2 share the odometry cache. The attached and stale strings
// stay at the call site; the stop path does not, because Go2 publishes a
// deadline stop rather than an empty Twist.
void ingestOdometry(const nav_msgs::msg::Odometry & message,
                    const AssetFrames & frames, const control::Limits & limits,
                    const std::function<std::int64_t()> & ros_now_ns,
                    const std::function<double()> & mono_s,
                    const std::string & attached_detail,
                    const std::string & invalid_detail, std::mutex & cache_mutex,
                    double & last_received_s, AdapterSnapshot & data) {
  const auto & p = message.pose.pose.position;
  const auto & q = message.pose.pose.orientation;
  const auto & t = message.twist.twist;
  auto snapshot = odometrySnapshot(
    frames.odom, frames.base, message.header.frame_id, message.child_frame_id,
    message.header.stamp.sec, message.header.stamp.nanosec,
    {p.x, p.y, p.z}, {q.x, q.y, q.z, q.w},
    {t.linear.x, t.linear.y, t.linear.z, t.angular.x, t.angular.y, t.angular.z},
    limits.state_timeout_s, ros_now_ns(), mono_s(), attached_detail);
  std::lock_guard<std::mutex> guard(cache_mutex);
  if (!snapshot.has_value()) {
    last_received_s = 0.0;
    data = AdapterSnapshot{};
    data.detail = invalid_detail;
    return;
  }
  last_received_s = mono_s() -
    std::max(0.0, ros_now_ns() / 1e9 - *snapshot->source_stamp_ns / 1e9);
  data = *snapshot;
}

AdapterSnapshot cachedOrStale(std::mutex & cache_mutex, double & last_received_s,
                              const AdapterSnapshot & data,
                              const std::function<double()> & mono_s,
                              double timeout_s, const std::string & stale_detail) {
  std::lock_guard<std::mutex> guard(cache_mutex);
  if (!last_received_s || mono_s() - last_received_s >= timeout_s) {
    AdapterSnapshot stale;
    stale.detail = stale_detail;
    return stale;
  }
  return data;
}

}  // namespace

std::string requireTopicOption(const nlohmann::json & options,
                               const std::string & name,
                               const std::string & plugin) {
  const std::string topic = requireStringOption(options, name, plugin);
  if (!chaos::runtime::isAbsoluteTopic(topic)) {
    throw AssetConfigError(plugin + " " + name +
                           " must be a valid absolute ROS topic");
  }
  return topic;
}

std::optional<AdapterSnapshot> odometrySnapshot(
  const std::string & odom_frame, const std::string & base_frame,
  const std::string & header_frame, const std::string & child_frame,
  std::int64_t stamp_sec, std::int64_t stamp_nanosec,
  const std::array<double, 3> & position,
  const std::array<double, 4> & quaternion_xyzw,
  const std::array<double, 6> & twist, double state_timeout_s,
  std::int64_t ros_now_ns, double mono_s, const std::string & detail) {
  auto finite = [](double value) { return std::isfinite(value); };
  const bool values_finite =
    std::all_of(position.begin(), position.end(), finite) &&
    std::all_of(quaternion_xyzw.begin(), quaternion_xyzw.end(), finite) &&
    std::all_of(twist.begin(), twist.end(), finite);
  const double norm = std::hypot(
    quaternion_xyzw[0],
    std::hypot(quaternion_xyzw[1],
               std::hypot(quaternion_xyzw[2], quaternion_xyzw[3])));
  if (header_frame != odom_frame || child_frame != base_frame ||
      !values_finite || std::abs(norm - 1.0) > 0.01) {
    return std::nullopt;
  }
  std::int64_t source_ns = 0;
  if (!chaos::rosStampToNs(stamp_sec, stamp_nanosec, source_ns)) {
    return std::nullopt;
  }
  const double source_s = source_ns / 1e9;
  const double age = ros_now_ns / 1e9 - source_s;
  if (source_ns <= 0 || age < -0.1 || age >= state_timeout_s) {
    return std::nullopt;
  }
  AdapterSnapshot snapshot;
  snapshot.connected = true;
  snapshot.ready = true;
  snapshot.pose_valid = true;
  snapshot.position = position;
  snapshot.quaternion = quaternion_xyzw;
  snapshot.velocity = twist;
  snapshot.detail = detail;
  snapshot.pose_frame = header_frame;
  snapshot.twist_frame = child_frame;
  // Account for source age so receiving an old sample cannot refresh it.
  snapshot.source_stamp_ns = source_ns;
  (void)mono_s;
  return snapshot;
}

// --- Lingao --------------------------------------------------------------

void LingaoAdapter::validateOptions(const AssetConfig & asset) {
  requireKnownOptions(asset.options, pluginSpec("lingao_ros2").option_names, "Lingao");
  if (asset.options.find("odom_topic") == asset.options.end() ||
      asset.options.find("cmd_vel_topic") == asset.options.end()) {
    throw AssetConfigError(
      "Lingao requires explicit absolute odom_topic and cmd_vel_topic options");
  }
  requireTopicOption(asset.options, "odom_topic", "Lingao");
  requireTopicOption(asset.options, "cmd_vel_topic", "Lingao");
}

LingaoAdapter::LingaoAdapter(rclcpp::Node & node, const AssetConfig & asset)
    : limits_(asset.limits),
      frames_(asset.frames),
      ros_now_ns_([&node] {
        return node.get_clock()->now().nanoseconds();
      }),
      mono_s_(chaos::monotonicSeconds) {
  validateOptions(asset);
  const std::string cmd_topic =
    requireStringOption(asset.options, "cmd_vel_topic", "Lingao");
  const std::string odom_topic =
    requireStringOption(asset.options, "odom_topic", "Lingao");
  publisher_ = node.create_publisher<geometry_msgs::msg::Twist>(cmd_topic, 10);
  odom_sub_ = node.create_subscription<nav_msgs::msg::Odometry>(
    odom_topic, rclcpp::SensorDataQoS(),
    [this](const nav_msgs::msg::Odometry & message) {
      onOdometry(message);
    });
}

void LingaoAdapter::onOdometry(const nav_msgs::msg::Odometry & message) {
  ingestOdometry(message, frames_, limits_, ros_now_ns_, mono_s_,
                 "Attached to Lingao odometry; driver must be independently running",
                 "Invalid odometry values or frame mismatch or stale source stamp",
                 cache_mutex_, last_received_s_, data_);
}

AdapterSnapshot LingaoAdapter::snapshot() {
  return cachedOrStale(cache_mutex_, last_received_s_, data_, mono_s_,
                       limits_.state_timeout_s, "Lingao odometry unavailable or stale");
}

void LingaoAdapter::validateVelocity(const std::array<double, 6> & values) {
  if (std::abs(values[1]) > 1e-9 || std::abs(values[2]) > 1e-9 ||
      std::abs(values[3]) > 1e-9 || std::abs(values[4]) > 1e-9) {
    throw chaos::Rejected("UNSUPPORTED",
                          "Lingao accepts linear x and angular z only");
  }
}

void LingaoAdapter::setVelocity(const std::array<double, 6> & values) {
  validateVelocity(values);
  geometry_msgs::msg::Twist message;
  message.linear.x = values[0];
  message.angular.z = values[5];
  output_owned_ = true;
  publisher_->publish(message);
}

void LingaoAdapter::stop() {
  // A configured stop command sets output_owned in the runtime; shutdown
  // alone does not touch an independently controlled driver we never
  // commanded.
  if (output_owned_) {
    publisher_->publish(geometry_msgs::msg::Twist());
    output_owned_ = false;
  }
}

void LingaoAdapter::controlledStop() {
  output_owned_ = true;
  stop();
}

// --- Unitree Go2 ---------------------------------------------------------

void Go2Adapter::validateOptions(const AssetConfig & asset) {
  const PluginSpec & spec = pluginSpec("unitree_go2");
  requireKnownOptions(asset.options, spec.option_names, "Go2");
  for (const auto & name : spec.option_names) {
    if (asset.options.find(name) == asset.options.end()) {
      throw AssetConfigError("Go2 requires explicit option " + name);
    }
  }
  if (asset.kind != "quadruped") {
    throw AssetConfigError("Go2 requires kind quadruped");
  }
  const std::string odom = requireTopicOption(asset.options, "odom_topic", "Go2");
  const std::string cmd = requireTopicOption(asset.options, "cmd_vel_topic", "Go2");
  if (odom == cmd) {
    throw AssetConfigError("Go2 odom_topic and cmd_vel_topic must differ");
  }
  const std::string interface =
    requireStringOption(asset.options, "network_interface", "Go2");
  if (interface.size() > 15) {
    throw AssetConfigError(
      "Go2 network_interface must name an explicit network interface");
  }
  for (char ch : interface) {
    const bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                    (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' ||
                    ch == ':' || ch == '-';
    if (!ok) {
      throw AssetConfigError(
        "Go2 network_interface must name an explicit network interface");
    }
  }
  const std::int64_t domain =
    requireIntegerOption(asset.options, "dds_domain_id", "Go2");
  if (domain < 0 || domain > 232) {
    throw AssetConfigError("Go2 dds_domain_id must be an integer from 0 to 232");
  }
  const std::string sport =
    requireStringOption(asset.options, "sport_state_topic", "Go2");
  if (sport != "rt/sportmodestate" && sport != "rt/lf/sportmodestate") {
    throw AssetConfigError(
      "Go2 sport_state_topic must be rt/sportmodestate or rt/lf/sportmodestate");
  }
  const std::string velocity_frame =
    requireStringOption(asset.options, "velocity_frame", "Go2");
  if (velocity_frame != "odom" && velocity_frame != "body") {
    throw AssetConfigError("Go2 velocity_frame must be odom or body");
  }
}

Go2Adapter::Go2Adapter(rclcpp::Node & node, const AssetConfig & asset)
    : limits_(asset.limits),
      frames_(asset.frames),
      base_frame_(asset.frames.base),
      // The clock-domain identity is read once, here, and cached for the
      // lifetime of the adapter. A failure throws out of the constructor: an
      // adapter must never publish a command with an empty or guessed domain,
      // because the driver compares the domain before it admits any motion.
      clock_domain_id_(driver_deadline::localClockDomainId()),
      ros_now_ns_([&node] {
        return node.get_clock()->now().nanoseconds();
      }),
      mono_s_(chaos::monotonicSeconds) {
  validateOptions(asset);
  const std::string cmd_topic =
    requireStringOption(asset.options, "cmd_vel_topic", "Go2");
  const std::string odom_topic =
    requireStringOption(asset.options, "odom_topic", "Go2");
  // A single pending command cannot accumulate a queue of old motion.
  publisher_ = node.create_publisher<chaos_interfaces::msg::DriverVelocityCommand>(
    cmd_topic, rclcpp::QoS(1));
  odom_sub_ = node.create_subscription<nav_msgs::msg::Odometry>(
    odom_topic, rclcpp::SensorDataQoS(),
    [this](const nav_msgs::msg::Odometry & message) {
      onOdometry(message);
    });
}

void Go2Adapter::onOdometry(const nav_msgs::msg::Odometry & message) {
  ingestOdometry(message, frames_, limits_, ros_now_ns_, mono_s_,
                 "Go2 DDS telemetry via managed driver; movement requires an operable sport mode",
                 "Go2 odometry invalid, stale or frame mismatch",
                 cache_mutex_, last_received_s_, data_);
}

AdapterSnapshot Go2Adapter::snapshot() {
  return cachedOrStale(cache_mutex_, last_received_s_, data_, mono_s_,
                       limits_.state_timeout_s,
                       "Go2 driver telemetry unavailable or stale");
}

void Go2Adapter::validateVelocity(const std::array<double, 6> & values) {
  for (double value : values) {
    if (!std::isfinite(value)) {
      throw chaos::Rejected("INVALID_ARGUMENT",
                            "Go2 velocity must contain six finite components");
    }
  }
  if (values[2] != 0.0 || values[3] != 0.0 || values[4] != 0.0) {
    throw chaos::Rejected("UNSUPPORTED",
                          "Go2 accepts linear x/y and angular z only");
  }
}

void Go2Adapter::publishCommand(const std::array<double, 6> & values,
                                std::int64_t source_stamp_ns,
                                std::int64_t issued_mono_ns,
                                std::int64_t expires_mono_ns, bool stop) {
  chaos_interfaces::msg::DriverVelocityCommand message;
  // The source stamp is provenance only: it is carried unchanged and it
  // never extends the deadline below.
  std::int64_t sec = 0;
  std::int64_t nanosec = 0;
  chaos::rosStampFromNs(source_stamp_ns, sec, nanosec);
  message.header.stamp.sec = static_cast<std::int32_t>(sec);
  message.header.stamp.nanosec = static_cast<std::uint32_t>(nanosec);
  message.header.frame_id = base_frame_;
  message.clock_domain_id = clock_domain_id_;
  message.issued_monotonic_ns = issued_mono_ns;
  message.expires_monotonic_ns = expires_mono_ns;
  message.stop = stop;
  message.velocity.linear.x = values[0];
  message.velocity.linear.y = values[1];
  message.velocity.linear.z = values[2];
  message.velocity.angular.x = values[3];
  message.velocity.angular.y = values[4];
  message.velocity.angular.z = values[5];
  publisher_->publish(message);
}

void Go2Adapter::publishStop() {
  // An explicit stop barrier: six exact zeros, the current actual issue time
  // and no deadline. It is never routed through the motion validity rules,
  // so it needs neither a motion expiry nor fresh telemetry.
  const std::int64_t issued = driver_deadline::monotonicNowNs();
  publishCommand({0.0, 0.0, 0.0, 0.0, 0.0, 0.0}, 0, issued, 0, true);
}

void Go2Adapter::setVelocity(const std::array<double, 6> & values) {
  validateVelocity(values);
  bool nonzero = false;
  for (double value : values) {
    if (value != 0.0) {
      nonzero = true;
      break;
    }
  }
  if (nonzero) {
    // A motion command without an absolute deadline is unbounded in time: the
    // driver would have no instant at which it must stop. The deadline API is
    // the only admitted path for Go2 motion, so the legacy path fails closed
    // instead of inventing a validity interval of its own.
    throw chaos::Rejected("INVALID_ARGUMENT",
                          "Go2 motion requires an absolute monotonic deadline");
  }
  // A zero command uses the existing controlled-stop path.
  controlledStop();
}

void Go2Adapter::setVelocityUntil(const std::array<double, 6> & values,
                                  std::int64_t source_stamp_ns,
                                  std::int64_t expires_mono_ns) {
  validateVelocity(values);
  if (source_stamp_ns <= 0) {
    throw chaos::Rejected("INVALID_ARGUMENT",
                          "Go2 motion requires a positive source stamp");
  }
  // The original source stamp must fit the ROS header range and it is
  // preserved exactly: the driver must be able to see the stamp that the
  // coordinator admitted. The split is exact, so the recombined value is
  // the source stamp itself and only the ROS range is checked here.
  std::int64_t sec = 0;
  std::int64_t nanosec = 0;
  chaos::rosStampFromNs(source_stamp_ns, sec, nanosec);
  std::int64_t header_ns = 0;
  if (!chaos::rosStampToNs(sec, nanosec, header_ns)) {
    throw chaos::Rejected("INVALID_ARGUMENT",
                          "Go2 motion source stamp does not fit the ROS header "
                          "range");
  }
  if (expires_mono_ns <= 0) {
    throw chaos::Rejected("INVALID_ARGUMENT",
                          "Go2 motion requires an absolute monotonic deadline");
  }
  // The issue time is a NEW actual CLOCK_MONOTONIC reading, taken here at
  // publish time. It is never the coordinator's anchor and never a fabricated
  // successor of the previous issue time.
  const std::int64_t issued = driver_deadline::monotonicNowNs();
  if (issued <= 0 || issued >= expires_mono_ns) {
    throw chaos::Rejected("STALE_COMMAND",
                          "Go2 motion deadline has already passed");
  }
  // The output ownership is claimed before the publish, so that a publish
  // that fails after it reached the transport can still be stopped by a
  // retry. The ownership is released only by a successful stop.
  output_owned_ = true;
  // The deadline is published unchanged. A later issue time never extends it.
  publishCommand(values, source_stamp_ns, issued, expires_mono_ns, false);
}

void Go2Adapter::stop() {
  if (output_owned_) {
    publishStop();
    // The ownership is released only after the stop output was actually
    // published. A failing publish keeps it, so a retry can still stop an
    // output that may have reached the transport.
    output_owned_ = false;
  }
}

void Go2Adapter::controlledStop() {
  output_owned_ = true;
  stop();
}

// --- Legacy DJI (read-only) ----------------------------------------------

void DjiLegacyAdapter::validateOptions(const AssetConfig & asset) {
  requireKnownOptions(asset.options, pluginSpec("dji_psdk_legacy").option_names, "DJI");
  const std::string topic =
    asset.options.find("status_topic") != asset.options.end()
      ? requireStringOption(asset.options, "status_topic", "DJI")
      : "/m3td/system/status";
  if (!isAbsoluteTopic(topic)) {
    throw AssetConfigError("DJI status_topic must be a valid absolute ROS topic");
  }
}

DjiLegacyAdapter::DjiLegacyAdapter(rclcpp::Node & node,
                                   const AssetConfig & asset)
    : limits_(asset.limits), mono_s_(chaos::monotonicSeconds) {
  validateOptions(asset);
  const std::string topic =
    asset.options.find("status_topic") != asset.options.end()
      ? requireStringOption(asset.options, "status_topic", "DJI")
      : "/m3td/system/status";
  status_sub_ = node.create_subscription<std_msgs::msg::String>(
    topic, rclcpp::QoS(10),
    [this](const std_msgs::msg::String & message) {
      onStatus(message);
    });
}

void DjiLegacyAdapter::onStatus(const std_msgs::msg::String & message) {
  bool ok = false;
  bool flight_fresh = false;
  if (!parseStatus(message.data, ok, flight_fresh) || !ok) {
    return;
  }
  std::lock_guard<std::mutex> guard(cache_mutex_);
  last_received_s_ = mono_s_();
  flight_fresh_ = flight_fresh;
}

AdapterSnapshot DjiLegacyAdapter::snapshot() {
  std::lock_guard<std::mutex> guard(cache_mutex_);
  const bool fresh =
    last_received_s_ != 0.0 &&
    mono_s_() - last_received_s_ < limits_.state_timeout_s;
  AdapterSnapshot snapshot;
  snapshot.connected = fresh && flight_fresh_;
  snapshot.detail =
    fresh ? "Legacy DJI status received; read-only, pose conversion not implemented"
          : "Waiting for legacy DJI system/status responses; upstream "
            "publishes on status requests";
  return snapshot;
}

std::unique_ptr<Adapter> createAdapter(const std::string & plugin,
                                       rclcpp::Node & node,
                                       const AssetConfig & asset) {
  // The shared registry rejects an unknown plugin, an unknown or missing
  // option and an unsupported capability. It is the same code the
  // --validate-asset preflight uses, so the two can never disagree.
  validateAssetConfig(asset);
  if (plugin == "mock") {
    MockAdapter::validateOptions(asset);
    return std::make_unique<MockAdapter>(asset);
  }
  if (plugin == "lingao_ros2") {
    LingaoAdapter::validateOptions(asset);
    return std::make_unique<LingaoAdapter>(node, asset);
  }
  if (plugin == "unitree_go2") {
    Go2Adapter::validateOptions(asset);
    return std::make_unique<Go2Adapter>(node, asset);
  }
  if (plugin == "dji_psdk_legacy") {
    DjiLegacyAdapter::validateOptions(asset);
    return std::make_unique<DjiLegacyAdapter>(node, asset);
  }
  throw AssetConfigError("Unknown adapter plugin " + plugin);
}

}  // namespace chaos::runtime
