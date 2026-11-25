// Estimate the visible -> thermal homography needed before fusion.
//
// Grabs a synchronous-ish pair of images from the two topics, matches SIFT (or
// ORB) features across the two modalities, fits a homography with RANSAC and
// writes it as a ROS 2 parameter file that thermal_fusion_node can consume
// directly:
//
//   ros2 run chaos_thermal_fusion thermal_register_calib --ros-args \
//     -p thermal_topic:=/thermal/image_raw \
//     -p visible_topic:=/visible/image_raw \
//     -p output_file:=/tmp/thermal_homography.yaml
//
//   ros2 run chaos_thermal_fusion thermal_fusion_node --ros-args \
//     --params-file /tmp/thermal_homography.yaml
//
// With the AT600's own halves the calibration should come back close to the
// identity -- that is the sanity check for this tool. A preview image is
// written whenever preview_file is set: thermal in the red channel, warped
// visible in green, so misalignment shows up as red/green fringes.
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <cv_bridge/cv_bridge.hpp>
#include <image_transport/image_transport.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace chaos_thermal_fusion
{

class ThermalRegisterCalib : public rclcpp::Node
{
public:
  ThermalRegisterCalib()
  : rclcpp::Node("thermal_register_calib")
  {
    thermal_topic_ = declare_parameter<std::string>("thermal_topic", "thermal/image_raw");
    visible_topic_ = declare_parameter<std::string>("visible_topic", "visible/image_raw");
    output_file_ = declare_parameter<std::string>("output_file", "");
    preview_file_ = declare_parameter<std::string>("preview_file", "/tmp/thermal_registration.png");
    node_name_ = declare_parameter<std::string>("node_name", "thermal_fusion_node");
    feature_type_ = declare_parameter<std::string>("feature", "sift");
    min_inliers_ = declare_parameter<int>("min_inliers", 12);
    ransac_thresh_ = declare_parameter<double>("ransac_threshold", 3.0);
    ratio_ = declare_parameter<double>("ratio_test", 0.85);
    max_attempts_ = declare_parameter<int>("max_attempts", 30);
    exit_on_success_ = declare_parameter<bool>("exit_on_success", true);

    const auto qos = rclcpp::SensorDataQoS().get_rmw_qos_profile();
    thermal_sub_ = image_transport::create_subscription(
      this, thermal_topic_,
      std::bind(&ThermalRegisterCalib::onThermal, this, std::placeholders::_1),
      "raw", qos);
    visible_sub_ = image_transport::create_subscription(
      this, visible_topic_,
      std::bind(&ThermalRegisterCalib::onVisible, this, std::placeholders::_1),
      "raw", qos);

    timer_ = create_wall_timer(std::chrono::seconds(1), std::bind(&ThermalRegisterCalib::attempt, this));

    RCLCPP_INFO(
      get_logger(), "registering %s -> %s (feature '%s', min_inliers %d)",
      visible_topic_.c_str(), thermal_topic_.c_str(), feature_type_.c_str(), min_inliers_);
  }

private:
  void onThermal(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
  {
    cv_bridge::CvImageConstPtr cv_ptr;
    try {
      cv_ptr = cv_bridge::toCvCopy(msg);
    } catch (const cv_bridge::Exception &) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (cv_ptr->image.depth() == CV_16U) {
      cv_ptr->image.convertTo(thermal_, CV_8U, 255.0 / 65535.0);
    } else if (cv_ptr->image.channels() == 3) {
      cv::cvtColor(cv_ptr->image, thermal_, cv::COLOR_BGR2GRAY);
    } else {
      thermal_ = cv_ptr->image.clone();
    }
    thermal_stamp_ = msg->header.stamp;
  }

  void onVisible(const sensor_msgs::msg::Image::ConstSharedPtr & msg)
  {
    cv_bridge::CvImageConstPtr cv_ptr;
    try {
      cv_ptr = cv_bridge::toCvCopy(msg);
    } catch (const cv_bridge::Exception &) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (cv_ptr->image.channels() == 3) {
      cv::cvtColor(cv_ptr->image, visible_, cv::COLOR_BGR2GRAY);
    } else if (cv_ptr->image.depth() == CV_16U) {
      cv_ptr->image.convertTo(visible_, CV_8U, 255.0 / 65535.0);
    } else {
      visible_ = cv_ptr->image.clone();
    }
    visible_stamp_ = msg->header.stamp;
  }

  void attempt()
  {
    cv::Mat thermal;
    cv::Mat visible;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (thermal_.empty() || visible_.empty()) {
        return;
      }
      if (std::abs((rclcpp::Time(thermal_stamp_) - rclcpp::Time(visible_stamp_)).seconds()) > 1.0) {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000, "pair is not synchronous, waiting");
        return;
      }
      thermal = thermal_.clone();
      visible = visible_.clone();
    }

    ++attempts_;
    if (attempts_ > max_attempts_) {
      RCLCPP_ERROR(get_logger(), "no usable registration after %d attempts", max_attempts_);
      rclcpp::shutdown();
      return;
    }

    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(2.0, cv::Size(8, 8));
    cv::Mat thermal_eq;
    cv::Mat visible_eq;
    clahe->apply(thermal, thermal_eq);
    clahe->apply(visible, visible_eq);

    std::vector<cv::KeyPoint> kp_t;
    std::vector<cv::KeyPoint> kp_v;
    cv::Mat desc_t;
    cv::Mat desc_v;
    bool use_sift = feature_type_ != "orb";
    cv::Ptr<cv::Feature2D> detector;
    int norm = cv::NORM_L2;
    if (use_sift) {
      try {
        detector = cv::SIFT::create(1500);
        detector->detectAndCompute(thermal_eq, cv::noArray(), kp_t, desc_t);
        detector->detectAndCompute(visible_eq, cv::noArray(), kp_v, desc_v);
      } catch (const cv::Exception &) {
        use_sift = false;
      }
    }
    if (!use_sift) {
      detector = cv::ORB::create(1200);
      norm = cv::NORM_HAMMING;
      detector->detectAndCompute(thermal_eq, cv::noArray(), kp_t, desc_t);
      detector->detectAndCompute(visible_eq, cv::noArray(), kp_v, desc_v);
    }

    if (desc_t.empty() || desc_v.empty()) {
      RCLCPP_WARN(get_logger(), "no features (thermal %zu, visible %zu)", kp_t.size(), kp_v.size());
      return;
    }

    cv::BFMatcher matcher(norm);
    std::vector<std::vector<cv::DMatch>> knn;
    matcher.knnMatch(desc_v, desc_t, knn, 2);

    std::vector<cv::Point2f> src;  // visible
    std::vector<cv::Point2f> dst;  // thermal
    for (const auto & pair : knn) {
      if (pair.size() == 2 && pair[0].distance < ratio_ * pair[1].distance) {
        src.push_back(kp_v[pair[0].queryIdx].pt);
        dst.push_back(kp_t[pair[0].trainIdx].pt);
      }
    }
    if (src.size() < 4) {
      RCLCPP_WARN(get_logger(), "only %zu good matches, retrying", src.size());
      return;
    }

    cv::Mat inlier_mask;
    cv::Mat H = cv::findHomography(src, dst, cv::RANSAC, ransac_thresh_, inlier_mask, 2000, 0.995);
    if (H.empty()) {
      RCLCPP_WARN(get_logger(), "findHomography failed on %zu matches", src.size());
      return;
    }

    const int inliers = inlier_mask.empty() ? 0 : cv::countNonZero(inlier_mask);
    RCLCPP_INFO(
      get_logger(), "attempt %d: %zu matches, %d inliers (need %d)",
      attempts_, src.size(), inliers, min_inliers_);

    if (inliers < min_inliers_) {
      return;
    }

    double h[9];
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        h[r * 3 + c] = H.at<double>(r, c);
      }
    }
    if (!std::isfinite(h[8]) || std::abs(h[8]) < 1e-9) {
      RCLCPP_WARN(get_logger(), "degenerate homography, retrying");
      return;
    }

    RCLCPP_INFO(get_logger(), "homography (visible -> thermal):");
    RCLCPP_INFO(
      get_logger(), "  [%8.4f %8.4f %8.3f]\n  [%8.4f %8.4f %8.3f]\n  [%8.6f %8.6f %8.6f]",
      h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8]);

    if (!output_file_.empty()) {
      writeParamsFile(h);
      RCLCPP_INFO(get_logger(), "wrote %s", output_file_.c_str());
    }
    if (!preview_file_.empty()) {
      writePreview(thermal, visible, H);
      RCLCPP_INFO(get_logger(), "wrote preview %s", preview_file_.c_str());
    }

    if (exit_on_success_) {
      RCLCPP_INFO(get_logger(), "registration done");
      rclcpp::shutdown();
    }
  }

  void writeParamsFile(const double h[9]) const
  {
    std::ofstream out(output_file_);
    out << std::fixed << std::setprecision(8);
    out << node_name_ << ":\n";
    out << "  ros__parameters:\n";
    out << "    homography: [";
    for (int i = 0; i < 9; ++i) {
      out << h[i] << (i == 8 ? "" : ", ");
    }
    out << "]\n";
  }

  void writePreview(const cv::Mat & thermal, const cv::Mat & visible, const cv::Mat & H) const
  {
    cv::Mat warped;
    cv::warpPerspective(visible, warped, H, thermal.size(), cv::INTER_LINEAR);
    cv::Mat preview(thermal.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    preview.forEach<cv::Vec3b>(
      [&](cv::Vec3b & px, const int * pos) {
        px[2] = thermal.at<uint8_t>(pos[0], pos[1]);
        px[1] = warped.at<uint8_t>(pos[0], pos[1]);
      });
    cv::imwrite(preview_file_, preview);
  }

  std::string thermal_topic_;
  std::string visible_topic_;
  std::string output_file_;
  std::string preview_file_;
  std::string node_name_;
  std::string feature_type_;
  int min_inliers_{12};
  double ransac_thresh_{3.0};
  double ratio_{0.75};
  int max_attempts_{30};
  bool exit_on_success_{true};
  int attempts_{0};

  std::mutex mutex_;
  cv::Mat thermal_;
  cv::Mat visible_;
  builtin_interfaces::msg::Time thermal_stamp_{};
  builtin_interfaces::msg::Time visible_stamp_{};

  image_transport::Subscriber thermal_sub_;
  image_transport::Subscriber visible_sub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace chaos_thermal_fusion

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<chaos_thermal_fusion::ThermalRegisterCalib>());
  rclcpp::shutdown();
  return 0;
}
