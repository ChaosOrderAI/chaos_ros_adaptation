// Fuse a thermal image with a visible-light image.
//
// The node is deliberately source-agnostic: it subscribes to any two image
// topics. Out of the box those are the two halves split by at600_split_node,
// but the same node works unchanged once the visible source becomes a
// Hikvision camera (USB or RTSP -> image topic) -- only the topic name moves.
//
// Fusion modes:
//   blend          alpha * colormap(thermal) + (1 - alpha) * CLAHE(visible)
//   detail         colormap(thermal) with the visible image's high-frequency
//                  detail added on top (keeps the thermal palette readable)
//   side_by_side   colormap(thermal) | visible, for calibration/debugging
//   thermal_color  colormap only, useful when the visible stream is absent
//
// Registration: when the visible source is not already pixel-aligned with the
// thermal camera (a separately mounted Hikvision, for instance), pass the
// 3x3 homography produced by thermal_register_calib through the `homography`
// parameter (row-major, visible pixel -> thermal pixel). It is applied with
// warpPerspective before fusion; without it the node only resizes.
#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <cv_bridge/cv_bridge.hpp>
#include <image_transport/image_transport.hpp>
#include <opencv2/imgproc.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace chaos_thermal_fusion
{

namespace
{

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

// Stretch an 8-bit image between two percentiles so one hot pixel cannot
// wash out the whole palette.
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

// Only the luminance of the visible stream reaches the fusion, so every
// incoming encoding is normalized to one 8-bit plane here instead of being
// expanded to BGR and collapsed again per frame.
void toGray(const cv::Mat & src, const std::string & encoding, cv::Mat & gray)
{
  if ((src.depth() == CV_8U) && (src.channels() == 1)) {
    gray = src;
  } else if (encoding == "bgr8") {
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
  } else if (encoding == "rgb8") {
    cv::cvtColor(src, gray, cv::COLOR_RGB2GRAY);
  } else if ((src.depth() == CV_16U) || encoding == "mono16") {
    src.convertTo(gray, CV_8U, 255.0 / 65535.0);
  } else if (src.channels() == 3) {
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
  } else {
    gray = src;
  }
}

}  // namespace

class ThermalFusionNode : public rclcpp::Node
{
public:
  ThermalFusionNode()
  : rclcpp::Node("thermal_fusion_node")
  {
    thermal_topic_ = declare_parameter<std::string>("thermal_topic", "thermal/image_raw");
    visible_topic_ = declare_parameter<std::string>("visible_topic", "visible/image_raw");
    output_topic_ = declare_parameter<std::string>("output_topic", "thermal/image_fused");
    output_frame_id_ = declare_parameter<std::string>("output_frame_id", "thermal_frame");

    mode_ = declare_parameter<std::string>("mode", "blend");
    alpha_ = declare_parameter<double>("alpha", 0.6);
    colormap_ = colormapId(declare_parameter<std::string>("colormap", "inferno"));
    thermal_min_ = declare_parameter<double>("thermal_min", 0.0);
    thermal_max_ = declare_parameter<double>("thermal_max", 0.0);
    low_pct_ = declare_parameter<double>("auto_low_percentile", 2.0);
    high_pct_ = declare_parameter<double>("auto_high_percentile", 98.0);
    clahe_clip_ = declare_parameter<double>("clahe_clip_limit", 2.0);
    clahe_grid_ = declare_parameter<int>("clahe_grid", 8);
    detail_gain_ = declare_parameter<double>("detail_gain", 1.0);
    align_to_ = declare_parameter<std::string>("align_to", "thermal");
    publish_rate_hz_ = declare_parameter<double>("publish_rate_hz", 25.0);

    const auto homography = declare_parameter<std::vector<double>>(
      "homography", std::vector<double>{});
    if (homography.size() == 9) {
      homography_ = cv::Mat(3, 3, CV_64F);
      for (int i = 0; i < 9; ++i) {
        homography_.at<double>(i / 3, i % 3) = homography[i];
      }
      RCLCPP_INFO(get_logger(), "visible -> thermal homography loaded, warping before fusion");
    } else if (!homography.empty()) {
      RCLCPP_WARN(
        get_logger(), "homography needs 9 values (row-major), got %zu; ignoring",
        homography.size());
    }

    if (mode_ != "blend" && mode_ != "detail" && mode_ != "side_by_side" &&
      mode_ != "thermal_color")
    {
      RCLCPP_WARN(
        get_logger(), "unknown mode '%s', falling back to 'blend'", mode_.c_str());
      mode_ = "blend";
    }

    const auto qos = rclcpp::SensorDataQoS().get_rmw_qos_profile();
    clahe_ = cv::createCLAHE(
      clahe_clip_, cv::Size(std::max(1, clahe_grid_), std::max(1, clahe_grid_)));
    thermal_sub_ = image_transport::create_subscription(
      this, thermal_topic_,
      std::bind(&ThermalFusionNode::onThermal, this, std::placeholders::_1),
      "raw", qos);
    visible_sub_ = image_transport::create_subscription(
      this, visible_topic_,
      std::bind(&ThermalFusionNode::onVisible, this, std::placeholders::_1),
      "raw", qos);
    pub_ = image_transport::create_publisher(this, output_topic_, qos);

    const auto period = std::chrono::duration<double>(1.0 / std::max(1.0, publish_rate_hz_));
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      std::bind(&ThermalFusionNode::tick, this));

    RCLCPP_INFO(
      get_logger(), "fusion %s + %s -> %s (mode '%s', align_to '%s')",
      thermal_topic_.c_str(), visible_topic_.c_str(), output_topic_.c_str(),
      mode_.c_str(), align_to_.c_str());
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
    std::lock_guard<std::mutex> lock(mutex_);
    thermal_ = cv_ptr->image.clone();
    thermal_encoding_ = cv_ptr->encoding;
    thermal_stamp_ = msg->header.stamp;
  }

  void onVisible(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
  {
    cv_bridge::CvImageConstPtr cv_ptr;
    try {
      cv_ptr = cv_bridge::toCvCopy(msg);
    } catch (const cv_bridge::Exception & err) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 2000, "visible decode failed: %s", err.what());
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    toGray(cv_ptr->image, cv_ptr->encoding, visible_gray_);
    visible_stamp_ = msg->header.stamp;
  }

  void tick()
  {
    cv::Mat thermal;
    cv::Mat visible;
    std::string thermal_encoding;
    rclcpp::Time stamp;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (thermal_.empty()) {
        return;
      }
      const bool needs_visible = mode_ != "thermal_color";
      if (needs_visible && visible_gray_.empty()) {
        return;
      }
      // Move the latest frames out instead of cloning them; a callback that
      // runs before the next tick simply allocates a fresh buffer.
      thermal = std::move(thermal_);
      if (needs_visible) {
        visible = std::move(visible_gray_);
      }
      thermal_encoding = thermal_encoding_;
      stamp = thermal_stamp_;
    }
    if (pub_.getNumSubscribers() == 0) {
      return;
    }

    cv::Mat thermal_gray;
    if (thermal_encoding == "mono16" || thermal.depth() == CV_16U) {
      thermal.convertTo(thermal_gray, CV_8U, 255.0 / 65535.0);
    } else if (thermal.channels() == 3) {
      cv::cvtColor(thermal, thermal_gray, cv::COLOR_BGR2GRAY);
    } else {
      thermal_gray = thermal;
    }

    cv::Mat thermal8;
    if (thermal_min_ < thermal_max_) {
      const double scale = 255.0 / (thermal_max_ - thermal_min_);
      thermal_gray.convertTo(thermal8, CV_8U, scale, -thermal_min_ * scale);
    } else {
      percentileStretch(thermal_gray, low_pct_, high_pct_, thermal8);
    }

    cv::Mat color;
    cv::applyColorMap(thermal8, color, colormap_);

    if (mode_ == "thermal_color" || visible.empty()) {
      publish(color, stamp);
      return;
    }

    if (!homography_.empty()) {
      cv::Mat warped;
      cv::warpPerspective(
        visible, warped, homography_, thermal8.size(), cv::INTER_LINEAR,
        cv::BORDER_CONSTANT, cv::Scalar(0));
      visible = warped;
    } else if (align_to_ == "thermal") {
      if (visible.size() != thermal8.size()) {
        cv::resize(visible, visible, thermal8.size(), 0, 0, cv::INTER_LINEAR);
      }
    } else if (visible.size() != thermal8.size()) {
      cv::resize(color, color, visible.size(), 0, 0, cv::INTER_LINEAR);
    }

    cv::Mat visible_detail;
    clahe_->apply(visible, visible_detail);
    cv::Mat visible_clahe;
    cv::cvtColor(visible_detail, visible_clahe, cv::COLOR_GRAY2BGR);

    cv::Mat fused;
    if (mode_ == "side_by_side") {
      cv::hconcat(color, visible_clahe, fused);
    } else if (mode_ == "detail") {
      cv::Mat blurred;
      cv::GaussianBlur(visible_detail, blurred, cv::Size(0, 0), 2.0);
      cv::Mat detail;
      cv::subtract(visible_detail, blurred, detail);
      cv::Mat detail_bgr;
      cv::cvtColor(detail, detail_bgr, cv::COLOR_GRAY2BGR);
      fused = color + detail_bgr * detail_gain_;
    } else {
      cv::addWeighted(color, alpha_, visible_clahe, 1.0 - alpha_, 0.0, fused);
    }

    publish(fused, stamp);
  }

  void publish(const cv::Mat & image, const rclcpp::Time & stamp)
  {
    std_msgs::msg::Header header;
    header.stamp = stamp;
    header.frame_id = output_frame_id_;
    pub_.publish(*cv_bridge::CvImage(header, "bgr8", image).toImageMsg());
  }

  std::string thermal_topic_;
  std::string visible_topic_;
  std::string output_topic_;
  std::string output_frame_id_;
  std::string mode_;
  double alpha_{0.6};
  int colormap_{cv::COLORMAP_INFERNO};
  double thermal_min_{0.0};
  double thermal_max_{0.0};
  double low_pct_{2.0};
  double high_pct_{98.0};
  double clahe_clip_{2.0};
  int clahe_grid_{8};
  double detail_gain_{1.0};
  std::string align_to_;
  double publish_rate_hz_{25.0};
  cv::Mat homography_;  // visible pixel -> thermal pixel, empty when unregistered

  std::mutex mutex_;
  cv::Mat thermal_;
  std::string thermal_encoding_;
  rclcpp::Time thermal_stamp_{0, 0, RCL_ROS_TIME};
  cv::Mat visible_gray_;
  rclcpp::Time visible_stamp_{0, 0, RCL_ROS_TIME};

  cv::Ptr<cv::CLAHE> clahe_;

  image_transport::Subscriber thermal_sub_;
  image_transport::Subscriber visible_sub_;
  image_transport::Publisher pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace chaos_thermal_fusion

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<chaos_thermal_fusion::ThermalFusionNode>());
  rclcpp::shutdown();
  return 0;
}
