// 3D Thermal-LIVO point cloud coloring node.
//
// Decouples odometry estimation from thermal coloring: FAST-LIVO2 runs on the
// LiDAR + hardware-triggered visible camera to produce accurate world-frame
// scans (/cloud_registered) and poses (/aft_mapped_to_init), while this node
// time-aligns free-running thermal frames (e.g. IRay AT600 /thermal/image_raw),
// interpolates the IMU pose at the exact thermal frame timestamp, projects the
// 3D scan onto the thermal image plane, and publishes a PointXYZRGBI cloud
// (/cloud_registered_thermal) plus a false-color thermal preview image.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <cv_bridge/cv_bridge.hpp>
#include <image_transport/image_transport.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <opencv2/imgproc.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <sensor_msgs/msg/point_field.hpp>

#include "chaos_sensors/thermal_cloud_projector.hpp"

namespace chaos_sensors
{

namespace
{

double stampToSec(const builtin_interfaces::msg::Time & s)
{
  return static_cast<double>(s.sec) + static_cast<double>(s.nanosec) * 1e-9;
}

int colormapId(const std::string & name)
{
  if (name == "jet") {return cv::COLORMAP_JET;}
  if (name == "hot") {return cv::COLORMAP_HOT;}
  if (name == "bone") {return cv::COLORMAP_BONE;}
  if (name == "rainbow") {return cv::COLORMAP_RAINBOW;}
  if (name == "turbo") {return cv::COLORMAP_TURBO;}
  if (name == "magma") {return cv::COLORMAP_MAGMA;}
  return cv::COLORMAP_INFERNO;
}

void percentileStretch(const cv::Mat & src, double low_pct, double high_pct, cv::Mat & dst)
{
  int hist_size = 256;
  float range[] = {0.f, 256.f};
  const float * ranges[] = {range};
  cv::Mat hist;
  cv::calcHist(&src, 1, nullptr, cv::Mat(), hist, 1, &hist_size, ranges);

  const double total = static_cast<double>(src.total());
  double low = 0.0;
  double high = 255.0;
  double acc = 0.0;
  for (int i = 0; i < hist_size; ++i) {
    acc += hist.at<float>(i);
    if (acc >= total * low_pct / 100.0) {low = i; break;}
  }
  acc = 0.0;
  for (int i = 0; i < hist_size; ++i) {
    acc += hist.at<float>(i);
    if (acc >= total * high_pct / 100.0) {high = i; break;}
  }
  if (high - low < 1.0) {
    high = low + 1.0;
  }
  const double scale = 255.0 / (high - low);
  src.convertTo(dst, CV_8U, scale, -low * scale);
}

struct ThermalFrame
{
  double stamp_sec{0.0};
  builtin_interfaces::msg::Time stamp{};
  cv::Mat color_bgr;
  cv::Mat intensity_u8;
};

}  // namespace

class ThermalCloudNode : public rclcpp::Node
{
public:
  ThermalCloudNode()
  : rclcpp::Node("thermal_cloud_node")
  {
    thermal_topic_ = declare_parameter<std::string>("thermal_topic", "thermal/image_raw");
    odom_topic_ = declare_parameter<std::string>("odom_topic", "/aft_mapped_to_init");
    cloud_topic_ = declare_parameter<std::string>("cloud_topic", "/cloud_registered");
    output_cloud_topic_ =
      declare_parameter<std::string>("output_cloud_topic", "/cloud_registered_thermal");
    output_image_topic_ =
      declare_parameter<std::string>("output_image_topic", "/thermal/image_colored");

    colormap_ = colormapId(declare_parameter<std::string>("colormap", "inferno"));
    thermal_min_ = declare_parameter<double>("thermal_min", 0.0);
    thermal_max_ = declare_parameter<double>("thermal_max", 0.0);
    low_pct_ = declare_parameter<double>("auto_low_percentile", 2.0);
    high_pct_ = declare_parameter<double>("auto_high_percentile", 98.0);
    min_thermal_intensity_ = declare_parameter<double>("min_thermal_intensity", 0.0);
    max_sync_diff_sec_ = declare_parameter<double>("max_sync_diff_sec", 0.20);
    thermal_time_offset_sec_ = declare_parameter<double>("thermal_time_offset_sec", 0.0);

    cfg_.width = declare_parameter<int>("cam_width", 256);
    cfg_.height = declare_parameter<int>("cam_height", 192);
    cfg_.fx = declare_parameter<double>("cam_fx", 266.7);
    cfg_.fy = declare_parameter<double>("cam_fy", 266.7);
    cfg_.cx = declare_parameter<double>("cam_cx", 128.0);
    cfg_.cy = declare_parameter<double>("cam_cy", 96.0);
    for (int i = 0; i < 5; ++i) {
      cfg_.distortion[i] = declare_parameter<double>("cam_d" + std::to_string(i), 0.0);
    }
    cfg_.min_depth = declare_parameter<double>("min_depth", 0.35);
    cfg_.max_depth = declare_parameter<double>("max_depth", 40.0);
    cfg_.border_margin = declare_parameter<int>("border_margin", 1);

    const auto ext_t = declare_parameter<std::vector<double>>(
      "extrinsic_T", std::vector<double>{-0.011, -0.02329, 0.04412});
    const auto ext_r = declare_parameter<std::vector<double>>(
      "extrinsic_R", std::vector<double>{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0});
    const auto pcl = declare_parameter<std::vector<double>>(
      "Pcl", std::vector<double>{0.0, 0.0, 0.0});
    const auto rcl = declare_parameter<std::vector<double>>(
      "Rcl", std::vector<double>{0.0, -1.0, 0.0, 0.0, 0.0, -1.0, 1.0, 0.0, 0.0});

    if (ext_t.size() != 3 || ext_r.size() != 9 || pcl.size() != 3 || rcl.size() != 9) {
      throw std::runtime_error(
              "extrinsic_T/Pcl require 3 elements and extrinsic_R/Rcl require 9 elements");
    }
    cfg_.ext_T = cv::Vec3d(ext_t[0], ext_t[1], ext_t[2]);
    cfg_.Pcl = cv::Vec3d(pcl[0], pcl[1], pcl[2]);
    for (int i = 0; i < 9; ++i) {
      cfg_.ext_R(i / 3, i % 3) = ext_r[i];
      cfg_.Rcl(i / 3, i % 3) = rcl[i];
    }

    const auto sensor_qos = rclcpp::SensorDataQoS().get_rmw_qos_profile();
    thermal_sub_ = image_transport::create_subscription(
      this, thermal_topic_,
      std::bind(&ThermalCloudNode::onThermal, this, std::placeholders::_1),
      "raw", sensor_qos);
    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      odom_topic_, rclcpp::QoS(100),
      std::bind(&ThermalCloudNode::onOdom, this, std::placeholders::_1));
    cloud_sub_ = create_subscription<sensor_msgs::msg::PointCloud2>(
      cloud_topic_, rclcpp::SensorDataQoS(),
      std::bind(&ThermalCloudNode::onCloud, this, std::placeholders::_1));

    cloud_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>(
      output_cloud_topic_, rclcpp::QoS(20));
    if (!output_image_topic_.empty()) {
      image_pub_ = create_publisher<sensor_msgs::msg::Image>(
        output_image_topic_, rclcpp::QoS(5));
    }

    RCLCPP_INFO(
      get_logger(),
      "3D thermal projection: cloud '%s' + odom '%s' + thermal '%s' (%dx%d, fx=%.1f) -> '%s'",
      cloud_topic_.c_str(), odom_topic_.c_str(), thermal_topic_.c_str(),
      cfg_.width, cfg_.height, cfg_.fx, output_cloud_topic_.c_str());
  }

private:
  void onThermal(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
  {
    cv_bridge::CvImageConstPtr cv_ptr;
    try {
      cv_ptr = cv_bridge::toCvCopy(msg);
    } catch (const cv_bridge::Exception & err) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 2000, "thermal decode failed: %s", err.what());
      return;
    }

    const cv::Mat & raw = cv_ptr->image;
    if (raw.empty()) {
      return;
    }

    ThermalFrame frame;
    frame.stamp = msg->header.stamp;
    frame.stamp_sec = stampToSec(msg->header.stamp) + thermal_time_offset_sec_;

    if (raw.channels() == 3) {
      if (cv_ptr->encoding == "rgb8") {
        cv::cvtColor(raw, frame.color_bgr, cv::COLOR_RGB2BGR);
      } else {
        frame.color_bgr = raw.clone();
      }
      cv::cvtColor(frame.color_bgr, frame.intensity_u8, cv::COLOR_BGR2GRAY);
    } else {
      cv::Mat gray8;
      if (raw.depth() == CV_16U || cv_ptr->encoding == "mono16") {
        raw.convertTo(gray8, CV_8U, 255.0 / 65535.0);
      } else if (raw.depth() == CV_8U) {
        gray8 = raw;
      } else {
        raw.convertTo(gray8, CV_8U);
      }
      if (thermal_min_ < thermal_max_) {
        const double scale = 255.0 / (thermal_max_ - thermal_min_);
        gray8.convertTo(frame.intensity_u8, CV_8U, scale, -thermal_min_ * scale);
      } else {
        percentileStretch(gray8, low_pct_, high_pct_, frame.intensity_u8);
      }
      cv::applyColorMap(frame.intensity_u8, frame.color_bgr, colormap_);
    }

    if (image_pub_ && image_pub_->get_subscription_count() > 0) {
      std_msgs::msg::Header hdr = msg->header;
      image_pub_->publish(*cv_bridge::CvImage(hdr, "bgr8", frame.color_bgr).toImageMsg());
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!thermal_buf_.empty() && frame.stamp_sec < thermal_buf_.back().stamp_sec) {
      thermal_buf_.clear();
    }
    thermal_buf_.push_back(std::move(frame));
    while (thermal_buf_.size() > 50) {
      thermal_buf_.pop_front();
    }
  }

  void onOdom(const nav_msgs::msg::Odometry::ConstSharedPtr & msg)
  {
    TimedPose pose;
    pose.stamp_sec = stampToSec(msg->header.stamp);
    pose.qx = msg->pose.pose.orientation.x;
    pose.qy = msg->pose.pose.orientation.y;
    pose.qz = msg->pose.pose.orientation.z;
    pose.qw = msg->pose.pose.orientation.w;
    pose.pos = cv::Vec3d(
      msg->pose.pose.position.x,
      msg->pose.pose.position.y,
      msg->pose.pose.position.z);

    std::lock_guard<std::mutex> lock(mutex_);
    if (!odom_buf_.empty() && pose.stamp_sec < odom_buf_.back().stamp_sec) {
      odom_buf_.clear();
    }
    odom_buf_.push_back(pose);
    while (odom_buf_.size() > 200) {
      odom_buf_.pop_front();
    }
  }

  static int findFloat32FieldOffset(
    const sensor_msgs::msg::PointCloud2 & msg, const std::string & name)
  {
    for (const auto & f : msg.fields) {
      if (f.name == name && f.datatype == sensor_msgs::msg::PointField::FLOAT32) {
        return static_cast<int>(f.offset);
      }
    }
    return -1;
  }

  void onCloud(const sensor_msgs::msg::PointCloud2::ConstSharedPtr & msg)
  {
    if (cloud_pub_->get_subscription_count() == 0) {
      return;
    }
    const size_t num_pts = static_cast<size_t>(msg->width) * static_cast<size_t>(msg->height);
    if (num_pts == 0 || msg->point_step < 12 || msg->data.size() < num_pts * msg->point_step) {
      return;
    }
    const int off_x = findFloat32FieldOffset(*msg, "x");
    const int off_y = findFloat32FieldOffset(*msg, "y");
    const int off_z = findFloat32FieldOffset(*msg, "z");
    if (off_x < 0 || off_y < 0 || off_z < 0 ||
      static_cast<uint32_t>(std::max({off_x, off_y, off_z}) + 4) > msg->point_step)
    {
      return;
    }

    const double cloud_sec = stampToSec(msg->header.stamp);
    ThermalFrame best_frame;
    TimedPose pose_at_ir;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (thermal_buf_.empty() || odom_buf_.empty()) {
        return;
      }
      double best_dt = 1e9;
      size_t best_idx = 0;
      for (size_t i = 0; i < thermal_buf_.size(); ++i) {
        const double dt = std::abs(thermal_buf_[i].stamp_sec - cloud_sec);
        if (dt < best_dt) {
          best_dt = dt;
          best_idx = i;
        }
      }
      if (best_dt > max_sync_diff_sec_) {
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 3000,
          "thermal/cloud stamp gap %.3f s exceeds max_sync_diff_sec %.3f s",
          best_dt, max_sync_diff_sec_);
        return;
      }
      best_frame = thermal_buf_[best_idx];
      if (!interpolatePose(odom_buf_, best_frame.stamp_sec, max_sync_diff_sec_, pose_at_ir)) {
        return;
      }
    }

    world_pts_buf_.clear();
    world_pts_buf_.reserve(num_pts);
    const uint8_t * ptr = msg->data.data();
    for (size_t i = 0; i < num_pts; ++i, ptr += msg->point_step) {
      float x = 0.0f, y = 0.0f, z = 0.0f;
      std::memcpy(&x, ptr + off_x, sizeof(float));
      std::memcpy(&y, ptr + off_y, sizeof(float));
      std::memcpy(&z, ptr + off_z, sizeof(float));
      world_pts_buf_.emplace_back(x, y, z);
    }

    projectWorldCloudToThermal(
      world_pts_buf_, pose_at_ir, cfg_,
      best_frame.color_bgr, best_frame.intensity_u8,
      colored_pts_buf_);

    if (min_thermal_intensity_ > 0.0 && !colored_pts_buf_.empty()) {
      colored_pts_buf_.erase(
        std::remove_if(
          colored_pts_buf_.begin(), colored_pts_buf_.end(),
          [this](const ColoredThermalPoint & p) {
            return p.intensity < static_cast<float>(min_thermal_intensity_);
          }),
        colored_pts_buf_.end());
    }

    if (colored_pts_buf_.empty()) {
      return;
    }

    sensor_msgs::msg::PointCloud2 out;
    out.header = msg->header;
    out.height = 1;
    out.width = static_cast<uint32_t>(colored_pts_buf_.size());
    out.is_bigendian = false;
    out.is_dense = true;
    out.point_step = sizeof(ColoredThermalPoint);
    out.row_step = out.point_step * out.width;

    out.fields.resize(5);
    const char * names[5] = {"x", "y", "z", "rgb", "intensity"};
    for (int i = 0; i < 5; ++i) {
      out.fields[i].name = names[i];
      out.fields[i].offset = static_cast<uint32_t>(i * 4);
      out.fields[i].datatype = sensor_msgs::msg::PointField::FLOAT32;
      out.fields[i].count = 1;
    }

    out.data.resize(out.row_step);
    std::memcpy(out.data.data(), colored_pts_buf_.data(), out.row_step);
    cloud_pub_->publish(out);
  }

  std::string thermal_topic_;
  std::string odom_topic_;
  std::string cloud_topic_;
  std::string output_cloud_topic_;
  std::string output_image_topic_;

  int colormap_{cv::COLORMAP_INFERNO};
  double thermal_min_{0.0};
  double thermal_max_{0.0};
  double low_pct_{2.0};
  double high_pct_{98.0};
  double min_thermal_intensity_{0.0};
  double max_sync_diff_sec_{0.20};
  double thermal_time_offset_sec_{0.0};
  ThermalProjectorConfig cfg_;

  std::mutex mutex_;
  std::deque<ThermalFrame> thermal_buf_;
  std::deque<TimedPose> odom_buf_;
  std::vector<cv::Vec3f> world_pts_buf_;
  std::vector<ColoredThermalPoint> colored_pts_buf_;

  image_transport::Subscriber thermal_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_sub_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_pub_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_pub_;
};

}  // namespace chaos_sensors

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<chaos_sensors::ThermalCloudNode>());
  } catch (const std::exception & err) {
    RCLCPP_FATAL(rclcpp::get_logger("thermal_cloud_node"), "%s", err.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
