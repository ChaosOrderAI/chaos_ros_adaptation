// Stamp externally triggered camera frames with the sync board's trigger time.
//
// The camera driver is free to be camera_ros/Aravis, the vendor MVS node, or
// anything else that publishes sensor_msgs/Image: this node sits between the
// driver and the consumers, watches board/trigger, and republishes each frame
// with header.stamp = the most recent trigger edge on its channel plus an
// optional exposure offset.
//
// Why not trust the driver's own stamp? A triggered GigE camera knows when it
// exposed, but most drivers stamp with the host arrival time (or the trigger
// time of the *last* trigger they saw). Pairing here keeps one authoritative
// source: the board's trigger JSON, which carries the edge time in microseconds.
//
// Pairing rules:
//   - only triggers whose device_mask contains the configured channel count;
//   - a frame pairs with the newest trigger if it arrived within max_age_ms of
//     that trigger's host arrival (host_stamp_us), so a dropped trigger or a
//     late frame cannot inherit the next edge;
//   - frames without a fresh trigger follow missing_trigger:
//       drop     (default) do not publish: a frame without a trigger has no
//                trustworthy time and would be indistinguishable from a synced
//                one downstream
//       arrival  publish stamped with the host arrival time (unpaired, visible
//                in diagnostics; only for consumers that can tolerate it)
//       last     publish with the last trigger regardless of age (debugging
//                only, it can be arbitrarily wrong)
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <chaos_interfaces/msg/board_trigger.hpp>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_msgs/msg/key_value.hpp>
#include <image_transport/image_transport.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace chaos_camera_trigger
{

namespace
{
uint32_t channelMask(const std::string & channel)
{
  if (channel == "IMU_2") {return 1u << 1;}
  if (channel == "CAM_1") {return 1u << 2;}
  if (channel == "CAM_2") {return 1u << 3;}
  if (channel == "CAM_3") {return 1u << 4;}
  if (channel == "CAM_4") {return 1u << 5;}
  return 0u;
}

// Pairing ages compare host wall-clock microseconds: trigger host_stamp_us is
// taken from the system clock by the board client, so the ROS clock (which may
// be simulated) must not be mixed into the same subtraction.
int64_t hostUs()
{
  return std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}
}  // namespace

class TriggerPairNode : public rclcpp::Node
{
public:
  TriggerPairNode()
  : rclcpp::Node("trigger_pair")
  {
    trigger_topic_ = declare_parameter<std::string>("trigger_topic", "board/trigger");
    image_topic_ = declare_parameter<std::string>("image_topic", "image_raw");
    camera_info_topic_ = declare_parameter<std::string>("camera_info_topic", "camera_info");
    image_output_topic_ = declare_parameter<std::string>("image_output_topic", "image_synced");
    camera_info_output_topic_ =
      declare_parameter<std::string>("camera_info_output_topic", "camera_info_synced");
    publish_camera_info_ = declare_parameter<bool>("publish_camera_info", true);
    diagnostics_topic_ = declare_parameter<std::string>("diagnostics_topic", "diagnostics");
    const std::string channel = declare_parameter<std::string>("board_channel", "CAM_1");
    max_age_ms_ = declare_parameter<double>("max_age_ms", 50.0);
    exposure_offset_us_ = declare_parameter<int64_t>("exposure_offset_us", 0);
    missing_trigger_ = declare_parameter<std::string>("missing_trigger", "drop");
    expected_period_ms_ = declare_parameter<double>("expected_period_ms", 0.0);
    frame_id_ = declare_parameter<std::string>("frame_id", "");

    channel_mask_ = channelMask(channel);
    if (channel_mask_ == 0u) {
      throw std::runtime_error(
              "board_channel must be IMU_2 or CAM_1..CAM_4, got '" + channel + "'");
    }
    if (missing_trigger_ != "arrival" && missing_trigger_ != "drop" &&
      missing_trigger_ != "last")
    {
      throw std::runtime_error(
              "missing_trigger must be arrival, drop or last, got '" + missing_trigger_ + "'");
    }

    const auto qos = rclcpp::SensorDataQoS().get_rmw_qos_profile();
    trigger_sub_ = create_subscription<chaos_interfaces::msg::BoardTrigger>(
      trigger_topic_, rclcpp::QoS(100),
      std::bind(&TriggerPairNode::onTrigger, this, std::placeholders::_1));
    image_sub_ = image_transport::create_subscription(
      this, image_topic_,
      std::bind(&TriggerPairNode::onImage, this, std::placeholders::_1),
      "raw", qos);
    image_pub_ = image_transport::create_publisher(this, image_output_topic_, qos);
    if (publish_camera_info_ && !camera_info_topic_.empty() &&
      !camera_info_output_topic_.empty())
    {
      info_sub_ = create_subscription<sensor_msgs::msg::CameraInfo>(
        camera_info_topic_, rclcpp::SensorDataQoS(),
        std::bind(&TriggerPairNode::onCameraInfo, this, std::placeholders::_1));
      info_pub_ = create_publisher<sensor_msgs::msg::CameraInfo>(
        camera_info_output_topic_, rclcpp::SensorDataQoS());
    }
    diag_pub_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
      diagnostics_topic_, rclcpp::QoS(10));
    diag_timer_ = create_wall_timer(
      std::chrono::seconds(1), std::bind(&TriggerPairNode::publishDiagnostics, this));

    RCLCPP_INFO(
      get_logger(),
      "pairing %s with %s (channel %s, max_age %.1f ms, missing_trigger %s) -> %s",
      image_topic_.c_str(), trigger_topic_.c_str(), channel.c_str(), max_age_ms_,
      missing_trigger_.c_str(), image_output_topic_.c_str());
  }

private:
  void onTrigger(const chaos_interfaces::msg::BoardTrigger::ConstSharedPtr & msg)
  {
    if ((msg->device_mask & channel_mask_) == 0u) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (have_trigger_ && expected_period_ms_ > 0.0) {
      const int64_t delta_us = static_cast<int64_t>(msg->host_stamp_us) -
        static_cast<int64_t>(last_trigger_.host_stamp_us);
      if (delta_us > static_cast<int64_t>(expected_period_ms_ * 1500.0)) {
        ++trigger_gaps_;
      }
    }
    last_trigger_ = *msg;
    have_trigger_ = true;
    ++trigger_count_;
  }

  void onImage(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
  {
    const int64_t arrival_us = hostUs();

    bool paired = false;
    bool used_last = false;
    int64_t age_us = -1;
    int64_t stamp_us = arrival_us;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (have_trigger_) {
        age_us = arrival_us - static_cast<int64_t>(last_trigger_.host_stamp_us);
        if (age_us >= 0 && age_us <= static_cast<int64_t>(max_age_ms_ * 1000.0)) {
          paired = true;
        } else if (missing_trigger_ == "last") {
          paired = true;
          used_last = true;
        }
        if (paired) {
          stamp_us = static_cast<int64_t>(last_trigger_.board_stamp_us) +
            exposure_offset_us_;
        }
      }
    }

    if (!paired) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        ++unpaired_count_;
      }
      if (missing_trigger_ == "drop") {
        std::lock_guard<std::mutex> lock(mutex_);
        ++dropped_count_;
        return;
      }
    }

    // A paired frame must not be stamped later than it arrived on a host that
    // is roughly NTP-synced; a large residual means the board clock drifted.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (paired) {
        ++paired_count_;
        const int64_t residual = stamp_us - arrival_us;
        if (residual < -max_residual_us_ || residual > max_residual_us_) {
          ++clock_outliers_;
        }
        max_observed_age_us_ = std::max(max_observed_age_us_, age_us);
        last_age_us_ = age_us;
        last_used_last_ = used_last;
      }
      last_image_stamp_us_ = stamp_us;
      last_image_arrival_us_ = arrival_us;
      have_last_image_stamp_ = true;
    }

    sensor_msgs::msg::Image out = *msg;
    out.header.stamp = rclcpp::Time(stamp_us * 1000LL);
    if (!frame_id_.empty()) {
      out.header.frame_id = frame_id_;
    }
    image_pub_.publish(out);
  }

  void onCameraInfo(const sensor_msgs::msg::CameraInfo::ConstSharedPtr & msg)
  {
    if (!info_pub_) {
      return;
    }
    const int64_t arrival_us = hostUs();
    const int64_t max_age_us = static_cast<int64_t>(max_age_ms_ * 1000.0);
    bool paired = false;
    int64_t stamp_us = arrival_us;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      // Pair directly against the trigger in the host clock domain so a small
      // CameraInfo arriving slightly ahead of its large Image still gets the
      // exact same trigger stamp instead of failing against the previous frame.
      if (have_trigger_) {
        const int64_t age_us =
          arrival_us - static_cast<int64_t>(last_trigger_.host_stamp_us);
        if ((age_us >= 0 && age_us <= max_age_us) || missing_trigger_ == "last") {
          paired = true;
          stamp_us = static_cast<int64_t>(last_trigger_.board_stamp_us) +
            exposure_offset_us_;
        }
      }
      if (!paired) {
        ++stale_camera_info_count_;
        if (missing_trigger_ == "drop") {
          return;
        }
        if (have_last_image_stamp_ &&
          (arrival_us - last_image_arrival_us_) >= 0 &&
          (arrival_us - last_image_arrival_us_) <= max_age_us)
        {
          stamp_us = last_image_stamp_us_;
        }
      }
    }
    sensor_msgs::msg::CameraInfo out = *msg;
    out.header.stamp = rclcpp::Time(stamp_us * 1000LL);
    if (!frame_id_.empty()) {
      out.header.frame_id = frame_id_;
    }
    info_pub_->publish(out);
  }

  void publishDiagnostics()
  {
    int64_t paired = 0;
    int64_t unpaired = 0;
    int64_t dropped = 0;
    int64_t triggers = 0;
    int64_t gaps = 0;
    int64_t outliers = 0;
    int64_t stale_info = 0;
    int64_t last_age = -1;
    int64_t max_age = -1;
    bool used_last = false;
    int64_t last_trigger_age_ms = -1;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      paired = paired_count_;
      unpaired = unpaired_count_;
      dropped = dropped_count_;
      triggers = trigger_count_;
      gaps = trigger_gaps_;
      outliers = clock_outliers_;
      stale_info = stale_camera_info_count_;
      last_age = last_age_us_;
      max_age = max_observed_age_us_;
      used_last = last_used_last_;
      if (have_trigger_) {
        const int64_t now_us = hostUs();
        last_trigger_age_ms =
          (now_us - static_cast<int64_t>(last_trigger_.host_stamp_us)) / 1000;
      }
    }

    diagnostic_msgs::msg::DiagnosticArray array;
    array.header.stamp = now();
    diagnostic_msgs::msg::DiagnosticStatus status;
    status.name = "chaos_sensors: trigger pairing";
    status.hardware_id = image_topic_;
    std::string message = "paired";
    uint8_t level = diagnostic_msgs::msg::DiagnosticStatus::OK;
    if (last_trigger_age_ms < 0) {
      level = diagnostic_msgs::msg::DiagnosticStatus::ERROR;
      message = "no trigger on this channel";
    } else if (unpaired > 0 || dropped > 0 || gaps > 0 || outliers > 0) {
      level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
      message = "pairing has gaps or unpaired frames";
    }
    if (used_last) {
      level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
      message = "using stale trigger (missing_trigger=last)";
    }
    status.level = level;
    status.message = message;

    auto add = [&status](const std::string & key, const std::string & value) {
        diagnostic_msgs::msg::KeyValue kv;
        kv.key = key;
        kv.value = value;
        status.values.push_back(kv);
      };
    add("paired_frames", std::to_string(paired));
    add("unpaired_frames", std::to_string(unpaired));
    add("dropped_frames", std::to_string(dropped));
    add("triggers", std::to_string(triggers));
    add("trigger_gaps", std::to_string(gaps));
    add("clock_outliers", std::to_string(outliers));
    add("stale_camera_info", std::to_string(stale_info));
    add("last_age_ms", last_age < 0 ? "n/a" : std::to_string(last_age / 1000.0));
    add("max_age_ms", max_age < 0 ? "n/a" : std::to_string(max_age / 1000.0));
    add("last_trigger_age_ms", std::to_string(last_trigger_age_ms));
    array.status.push_back(status);
    diag_pub_->publish(array);
  }

  std::string trigger_topic_;
  std::string image_topic_;
  std::string camera_info_topic_;
  std::string image_output_topic_;
  std::string camera_info_output_topic_;
  std::string diagnostics_topic_;
  double max_age_ms_{100.0};
  int64_t exposure_offset_us_{0};
  std::string missing_trigger_;
  double expected_period_ms_{0.0};
  std::string frame_id_;
  bool publish_camera_info_{true};
  uint32_t channel_mask_{0};

  std::mutex mutex_;
  chaos_interfaces::msg::BoardTrigger last_trigger_;
  bool have_trigger_{false};
  int64_t trigger_count_{0};
  int64_t trigger_gaps_{0};
  int64_t paired_count_{0};
  int64_t unpaired_count_{0};
  int64_t dropped_count_{0};
  int64_t clock_outliers_{0};
  int64_t stale_camera_info_count_{0};
  int64_t last_age_us_{-1};
  int64_t max_observed_age_us_{-1};
  bool last_used_last_{false};
  int64_t last_image_stamp_us_{0};
  int64_t last_image_arrival_us_{0};
  bool have_last_image_stamp_{false};
  static constexpr int64_t max_residual_us_ = 1000000;  // 1 s

  rclcpp::Subscription<chaos_interfaces::msg::BoardTrigger>::SharedPtr trigger_sub_;
  image_transport::Subscriber image_sub_;
  image_transport::Publisher image_pub_;
  rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr info_sub_;
  rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr info_pub_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diag_pub_;
  rclcpp::TimerBase::SharedPtr diag_timer_;
};

}  // namespace chaos_camera_trigger

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<chaos_camera_trigger::TriggerPairNode>());
  } catch (const std::exception & err) {
    RCLCPP_FATAL(rclcpp::get_logger("trigger_pair"), "%s", err.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
