#include "go2_slam_backend/se3_graph.hpp"

#include <geometry_msgs/msg/transform_stamped.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <nav_msgs/msg/path.hpp>
#include <pcl/common/transforms.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/registration/icp.h>
#include <pcl_conversions/pcl_conversions.h>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <tf2/exceptions.h>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2_ros/transform_listener.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace go2_slam_backend
{
namespace
{

using Cloud = pcl::PointCloud<pcl::PointXYZ>;

struct RingKey
{
  std::vector<float> height;
};

struct Keyframe
{
  Pose odom;
  Cloud::Ptr cloud;
  RingKey ring;
};

double yaw_of(const Pose & pose)
{
  const auto & q = pose.q;
  return std::atan2(
    2.0 * (q.w() * q.z() + q.x() * q.y()),
    1.0 - 2.0 * (q.y() * q.y() + q.z() * q.z()));
}

double wrap_angle(double angle)
{
  while (angle > M_PI) {
    angle -= 2.0 * M_PI;
  }
  while (angle < -M_PI) {
    angle += 2.0 * M_PI;
  }
  return angle;
}

Pose pose_from_odometry(const nav_msgs::msg::Odometry & msg)
{
  Pose pose;
  pose.t = Eigen::Vector3d(
    msg.pose.pose.position.x, msg.pose.pose.position.y, msg.pose.pose.position.z);
  pose.q = Eigen::Quaterniond(
    msg.pose.pose.orientation.w, msg.pose.pose.orientation.x,
    msg.pose.pose.orientation.y, msg.pose.pose.orientation.z);
  pose.q.normalize();
  return pose;
}

Cloud::Ptr voxel_down(const Cloud::Ptr & input, float leaf)
{
  auto output = Cloud::Ptr(new Cloud());
  if (!input || input->empty()) {
    return output;
  }
  pcl::VoxelGrid<pcl::PointXYZ> filter;
  filter.setInputCloud(input);
  filter.setLeafSize(leaf, leaf, leaf);
  filter.filter(*output);
  return output;
}

Cloud::Ptr to_body(const Cloud & input, const Pose & odom_pose, bool cloud_in_odom)
{
  auto output = Cloud::Ptr(new Cloud());
  if (!cloud_in_odom) {
    *output = input;
    return output;
  }
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  const Pose body_from_odom = pose_inverse(odom_pose);
  const Eigen::Matrix3f rotation = body_from_odom.q.toRotationMatrix().cast<float>();
  transform.block<3, 3>(0, 0) = rotation;
  transform.block<3, 1>(0, 3) = body_from_odom.t.cast<float>();
  pcl::transformPointCloud(input, *output, transform);
  return output;
}

RingKey make_ring(const Cloud & cloud, int bins, double min_range, double max_range)
{
  RingKey key;
  key.height.assign(static_cast<std::size_t>(bins), 0.0F);
  std::vector<int> counts(static_cast<std::size_t>(bins), 0);
  for (const auto & point : cloud.points) {
    const double range = std::hypot(point.x, point.y);
    if (range < min_range || range > max_range) {
      continue;
    }
    const double azimuth = std::atan2(point.y, point.x);
    int bin = static_cast<int>(std::floor((azimuth + M_PI) / (2.0 * M_PI) * bins));
    bin = std::clamp(bin, 0, bins - 1);
    const auto index = static_cast<std::size_t>(bin);
    key.height[index] = std::max(key.height[index], point.z);
    counts[index] += 1;
  }
  for (int i = 0; i < bins; ++i) {
    if (counts[static_cast<std::size_t>(i)] == 0) {
      key.height[static_cast<std::size_t>(i)] = 0.0F;
    }
  }
  return key;
}

double ring_similarity(const RingKey & query, const RingKey & candidate, int & best_shift)
{
  const int bins = static_cast<int>(query.height.size());
  best_shift = 0;
  if (bins == 0 || candidate.height.size() != query.height.size()) {
    return -1.0;
  }
  auto norm_of = [](const std::vector<float> & values, int shift) {
      double sum = 0.0;
      const int bins_local = static_cast<int>(values.size());
      for (int i = 0; i < bins_local; ++i) {
        const float value = values[static_cast<std::size_t>((i + shift) % bins_local)];
        sum += static_cast<double>(value) * value;
      }
      return std::sqrt(sum);
    };
  const double query_norm = norm_of(query.height, 0);
  double best = -1.0;
  for (int shift = 0; shift < bins; ++shift) {
    double dot = 0.0;
    for (int i = 0; i < bins; ++i) {
      dot += static_cast<double>(query.height[static_cast<std::size_t>(i)]) *
        candidate.height[static_cast<std::size_t>((i + shift) % bins)];
    }
    const double candidate_norm = norm_of(candidate.height, shift);
    const double similarity = query_norm > 1e-3 && candidate_norm > 1e-3 ?
      dot / (query_norm * candidate_norm) : -1.0;
    if (similarity > best) {
      best = similarity;
      best_shift = shift;
    }
  }
  return best;
}

bool register_loop(
  const Cloud & source, const Cloud & target, const Pose & initial, Pose & measurement,
  double & fitness, double max_correspondence, int max_iterations)
{
  if (source.size() < 80 || target.size() < 80) {
    return false;
  }
  pcl::IterativeClosestPoint<pcl::PointXYZ, pcl::PointXYZ> icp;
  Cloud::Ptr source_ptr(new Cloud(source));
  Cloud::Ptr target_ptr(new Cloud(target));
  icp.setInputSource(source_ptr);
  icp.setInputTarget(target_ptr);
  icp.setMaxCorrespondenceDistance(max_correspondence);
  icp.setMaximumIterations(max_iterations);
  icp.setTransformationEpsilon(1e-6);
  Eigen::Matrix4f guess = Eigen::Matrix4f::Identity();
  guess.block<3, 3>(0, 0) = initial.q.toRotationMatrix().cast<float>();
  guess.block<3, 1>(0, 3) = initial.t.cast<float>();
  Cloud aligned;
  icp.align(aligned, guess);
  if (!icp.hasConverged()) {
    return false;
  }
  fitness = icp.getFitnessScore();
  const Eigen::Matrix4f result = icp.getFinalTransformation();
  measurement.q = Eigen::Quaterniond(result.block<3, 3>(0, 0).cast<double>());
  measurement.q.normalize();
  measurement.t = result.block<3, 1>(0, 3).cast<double>();
  return true;
}

class OccupancyMemory
{
public:
  OccupancyMemory(double resolution, double half_size)
  : resolution_(resolution),
    origin_(-half_size, -half_size),
    width_(static_cast<int>(std::ceil(2.0 * half_size / resolution))),
    height_(width_),
    cells_(static_cast<std::size_t>(width_ * height_), -1)
  {
  }

  void clear()
  {
    std::fill(cells_.begin(), cells_.end(), -1);
  }

  void paint(
    const Pose & pose_map, const Cloud & cloud, double min_z, double max_z, double base_to_ground)
  {
    const Eigen::Matrix3d rotation = pose_map.q.toRotationMatrix();
    for (const auto & point : cloud.points) {
      const Eigen::Vector3d body(point.x, point.y, point.z);
      const Eigen::Vector3d world = rotation * body + pose_map.t;
      // Body z is relative to the IMU. base_to_ground shifts that onto the
      // local floor so min_z/max_z mean height above the ground, not the IMU.
      const double z_above_ground = static_cast<double>(point.z) + base_to_ground;
      if (z_above_ground < min_z || z_above_ground > max_z) {
        continue;
      }
      mark(world.x(), world.y());
    }
  }

  nav_msgs::msg::OccupancyGrid message(const rclcpp::Time & stamp) const
  {
    nav_msgs::msg::OccupancyGrid grid;
    grid.header.stamp = stamp;
    grid.header.frame_id = "map";
    grid.info.resolution = static_cast<float>(resolution_);
    grid.info.width = static_cast<uint32_t>(width_);
    grid.info.height = static_cast<uint32_t>(height_);
    grid.info.origin.position.x = origin_.x();
    grid.info.origin.position.y = origin_.y();
    grid.info.origin.orientation.w = 1.0;
    grid.data = cells_;
    return grid;
  }

private:
  void mark(double x, double y)
  {
    const int column = static_cast<int>(std::floor((x - origin_.x()) / resolution_));
    const int row = static_cast<int>(std::floor((y - origin_.y()) / resolution_));
    if (column < 0 || row < 0 || column >= width_ || row >= height_) {
      return;
    }
    cells_[static_cast<std::size_t>(row * width_ + column)] = 100;
  }

  double resolution_;
  Eigen::Vector2d origin_;
  int width_;
  int height_;
  std::vector<int8_t> cells_;
};

}  // namespace

class SlamBackendNode : public rclcpp::Node
{
public:
  SlamBackendNode()
  : Node("slam_backend"),
    tf_buffer_(get_clock()),
    tf_listener_(tf_buffer_),
    map_(declare_parameter<double>("grid_resolution", 0.1),
      declare_parameter<double>("grid_half_size", 40.0))
  {
    odom_topic_ = declare_parameter<std::string>("odom_topic", "/aft_mapped_to_init");
    cloud_topic_ = declare_parameter<std::string>("cloud_topic", "/cloud_registered");
    cloud_in_odom_ = declare_parameter<bool>("cloud_in_odom", true);
    publish_frontend_tf_ = declare_parameter<bool>("publish_frontend_tf", true);
    keyframe_distance_ = declare_parameter<double>("keyframe_distance", 0.6);
    keyframe_yaw_ = declare_parameter<double>("keyframe_yaw", 0.35);
    min_loop_gap_ = declare_parameter<int>("min_loop_gap", 20);
    ring_bins_ = declare_parameter<int>("ring_bins", 60);
    ring_accept_ = declare_parameter<double>("ring_similarity", 0.86);
    ring_ambiguity_ = declare_parameter<double>("ring_ambiguity", 0.04);
    icp_fitness_ = declare_parameter<double>("icp_fitness", 0.20);
    icp_max_correction_ = declare_parameter<double>("icp_max_correction", 5.0);
    voxel_leaf_ = static_cast<float>(declare_parameter<double>("voxel_leaf", 0.4));
    odom_rotation_weight_ = declare_parameter<double>("odom_rotation_weight", 20.0);
    odom_translation_weight_ = declare_parameter<double>("odom_translation_weight", 20.0);
    loop_rotation_weight_ = declare_parameter<double>("loop_rotation_weight", 5.0);
    loop_translation_weight_ = declare_parameter<double>("loop_translation_weight", 5.0);
    obstacle_min_z_ = declare_parameter<double>("obstacle_min_z", 0.15);
    obstacle_max_z_ = declare_parameter<double>("obstacle_max_z", 1.6);
    // base_link / IMU height above the local floor. Placeholder until measured.
    base_to_ground_ = declare_parameter<double>("base_to_ground", 0.35);
    publish_body_pose_ = declare_parameter<bool>("publish_body_pose", true);
    body_pose_topic_ = declare_parameter<std::string>("body_pose_topic", "/body_pose");
    base_frame_ = declare_parameter<std::string>("base_frame", "base_link");
    body_pose_frame_ = declare_parameter<std::string>("body_pose_frame", "trunk");

    odom_pub_ = create_publisher<nav_msgs::msg::Odometry>("/odom", 20);
    body_pose_pub_ = create_publisher<nav_msgs::msg::Odometry>(body_pose_topic_, 20);
    path_pub_ = create_publisher<nav_msgs::msg::Path>("/slam/path", 5);
    grid_pub_ = create_publisher<nav_msgs::msg::OccupancyGrid>(
      "/slam/occupancy", rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable());
    tf_broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);

    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      odom_topic_, 50, std::bind(&SlamBackendNode::on_odometry, this, std::placeholders::_1));
    cloud_sub_ = create_subscription<sensor_msgs::msg::PointCloud2>(
      cloud_topic_, 5, std::bind(&SlamBackendNode::on_cloud, this, std::placeholders::_1));
    tf_timer_ = create_wall_timer(
      std::chrono::milliseconds(50), std::bind(&SlamBackendNode::publish_tf, this));
    grid_timer_ = create_wall_timer(
      std::chrono::seconds(1), std::bind(&SlamBackendNode::publish_grid, this));

    map_from_odom_ = Pose{};
    RCLCPP_INFO(
      get_logger(),
      "SLAM backend listening to odometry '%s' and cloud '%s'. Frontend state is not written back.",
      odom_topic_.c_str(), cloud_topic_.c_str());
  }

private:
  void on_odometry(const nav_msgs::msg::Odometry::SharedPtr msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    latest_odom_ = pose_from_odometry(*msg);
    latest_odom_stamp_ = msg->header.stamp;
    have_odom_ = true;
    publish_navigation_odometry(*msg);
    consider_keyframe();
  }

  void on_cloud(const sensor_msgs::msg::PointCloud2::SharedPtr msg)
  {
    Cloud raw;
    pcl::fromROSMsg(*msg, raw);
    std::lock_guard<std::mutex> lock(mutex_);
    latest_cloud_.reset(new Cloud(raw));
    latest_cloud_stamp_ = msg->header.stamp;
    have_cloud_ = true;
  }

  void publish_navigation_odometry(const nav_msgs::msg::Odometry & input)
  {
    nav_msgs::msg::Odometry output = input;
    output.header.frame_id = "odom";
    output.child_frame_id = "base_link";
    odom_pub_->publish(output);
    if (publish_body_pose_) {
      publish_body_pose(output);
    }
  }

  // scan_planner's collision model and the closed-loop controller expect the
  // body centre (trunk), not the IMU. The trunk offset comes from the URDF, via
  // TF, so the robot description stays the single source of truth.
  void publish_body_pose(const nav_msgs::msg::Odometry & base_odom)
  {
    geometry_msgs::msg::TransformStamped transform;
    try {
      transform = tf_buffer_.lookupTransform(base_frame_, body_pose_frame_, tf2::TimePointZero);
    } catch (const tf2::TransformException & error) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "No TF %s -> %s yet; %s not published: %s", base_frame_.c_str(),
        body_pose_frame_.c_str(), body_pose_topic_.c_str(), error.what());
      return;
    }

    Pose base_from_body;
    base_from_body.t = Eigen::Vector3d(
      transform.transform.translation.x, transform.transform.translation.y,
      transform.transform.translation.z);
    base_from_body.q = Eigen::Quaterniond(
      transform.transform.rotation.w, transform.transform.rotation.x,
      transform.transform.rotation.y, transform.transform.rotation.z);
    base_from_body.q.normalize();

    const Pose odom_from_base = pose_from_odometry(base_odom);
    const Pose odom_from_body = pose_compose(odom_from_base, base_from_body);

    nav_msgs::msg::Odometry body = base_odom;
    body.child_frame_id = body_pose_frame_;
    body.pose.pose.position.x = odom_from_body.t.x();
    body.pose.pose.position.y = odom_from_body.t.y();
    body.pose.pose.position.z = odom_from_body.t.z();
    body.pose.pose.orientation.w = odom_from_body.q.w();
    body.pose.pose.orientation.x = odom_from_body.q.x();
    body.pose.pose.orientation.y = odom_from_body.q.y();
    body.pose.pose.orientation.z = odom_from_body.q.z();

    // The input twist is the velocity of the base_link origin. Move it to the
    // trunk origin and express it there, so consumers can rotate it with the
    // body orientation.
    const Eigen::Vector3d v_base(
      base_odom.twist.twist.linear.x, base_odom.twist.twist.linear.y,
      base_odom.twist.twist.linear.z);
    const Eigen::Vector3d w_base(
      base_odom.twist.twist.angular.x, base_odom.twist.twist.angular.y,
      base_odom.twist.twist.angular.z);
    const Eigen::Vector3d v_body =
      base_from_body.q.conjugate() * (v_base + w_base.cross(base_from_body.t));
    const Eigen::Vector3d w_body = base_from_body.q.conjugate() * w_base;
    body.twist.twist.linear.x = v_body.x();
    body.twist.twist.linear.y = v_body.y();
    body.twist.twist.linear.z = v_body.z();
    body.twist.twist.angular.x = w_body.x();
    body.twist.twist.angular.y = w_body.y();
    body.twist.twist.angular.z = w_body.z();

    body_pose_pub_->publish(body);
  }

  void consider_keyframe()
  {
    if (!have_odom_ || !have_cloud_ || !latest_cloud_) {
      return;
    }
    const double cloud_dt = std::abs((rclcpp::Time(latest_odom_stamp_) - rclcpp::Time(latest_cloud_stamp_)).seconds());
    if (cloud_dt > 0.2) {
      return;
    }
    if (!keyframes_.empty()) {
      const Pose delta = pose_between(keyframes_.back().odom, latest_odom_);
      const double moved = delta.t.norm();
      const double turned = std::abs(wrap_angle(yaw_of(delta)));
      if (moved < keyframe_distance_ && turned < keyframe_yaw_) {
        return;
      }
    }

    Keyframe frame;
    frame.odom = latest_odom_;
    frame.cloud = voxel_down(to_body(*latest_cloud_, latest_odom_, cloud_in_odom_), voxel_leaf_);
    frame.ring = make_ring(*frame.cloud, ring_bins_, 0.8, 25.0);
    const Pose map_pose = pose_compose(map_from_odom_, frame.odom);
    keyframes_.push_back(frame);
    poses_.push_back(map_pose);
    if (poses_.size() >= 2) {
      BetweenFactor factor;
      factor.from = static_cast<int>(poses_.size()) - 2;
      factor.to = static_cast<int>(poses_.size()) - 1;
      factor.measurement = pose_between(keyframes_[factor.from].odom, keyframes_[factor.to].odom);
      factor.rotation_weight = odom_rotation_weight_;
      factor.translation_weight = odom_translation_weight_;
      factors_.push_back(factor);
    }
    try_loop(static_cast<int>(keyframes_.size()) - 1);
    paint_keyframe(static_cast<int>(keyframes_.size()) - 1);
    publish_path();
    RCLCPP_INFO(get_logger(), "Keyframe %zu, loops %d", keyframes_.size() - 1, loop_count_);
  }

  void try_loop(int query)
  {
    if (query < min_loop_gap_) {
      return;
    }
    int best_index = -1;
    int second_index = -1;
    double best_score = -1.0;
    double second_score = -1.0;
    int best_shift = 0;
    for (int candidate = 0; candidate <= query - min_loop_gap_; ++candidate) {
      int shift = 0;
      const double score = ring_similarity(
        keyframes_[static_cast<std::size_t>(query)].ring,
        keyframes_[static_cast<std::size_t>(candidate)].ring, shift);
      if (score > best_score) {
        second_score = best_score;
        second_index = best_index;
        best_score = score;
        best_index = candidate;
        best_shift = shift;
      } else if (score > second_score) {
        second_score = score;
        second_index = candidate;
      }
    }
    (void)second_index;
    if (best_index < 0 || best_score < ring_accept_) {
      return;
    }
    if (second_score > 0.0 && best_score - second_score < ring_ambiguity_) {
      RCLCPP_INFO(get_logger(), "Rejected ambiguous loop candidate %.3f / %.3f", best_score, second_score);
      return;
    }

    Pose initial = pose_between(poses_[static_cast<std::size_t>(best_index)], poses_[static_cast<std::size_t>(query)]);
    // The ring shift is the absolute relative yaw, and initial.q already has
    // the pose-graph yaw. Apply only the residual so a good yaw is not doubled.
    const double ring_yaw =
      2.0 * M_PI * static_cast<double>(best_shift) / static_cast<double>(ring_bins_);
    const double residual = wrap_angle(ring_yaw - yaw_of(initial));
    initial.q = Eigen::Quaterniond(Eigen::AngleAxisd(residual, Eigen::Vector3d::UnitZ())) * initial.q;
    initial.q.normalize();

    Pose measurement;
    double fitness = std::numeric_limits<double>::infinity();
    if (!register_loop(
        *keyframes_[static_cast<std::size_t>(query)].cloud,
        *keyframes_[static_cast<std::size_t>(best_index)].cloud,
        initial, measurement, fitness, 1.5, 40))
    {
      return;
    }
    if (fitness > icp_fitness_ || (measurement.t - initial.t).norm() > icp_max_correction_) {
      RCLCPP_INFO(get_logger(), "Rejected ICP loop fitness %.3f", fitness);
      return;
    }

    BetweenFactor loop;
    loop.from = best_index;
    loop.to = query;
    loop.measurement = measurement;
    loop.rotation_weight = loop_rotation_weight_;
    loop.translation_weight = loop_translation_weight_;
    loop.robust = true;
    factors_.push_back(loop);
    const std::vector<Pose> poses_backup = poses_;
    if (!optimize_pose_graph(poses_, factors_)) {
      poses_ = poses_backup;
      factors_.pop_back();
      RCLCPP_WARN(get_logger(), "Pose-graph solve failed; loop was discarded");
      return;
    }
    map_from_odom_ = pose_compose(
      poses_.back(), pose_inverse(keyframes_.back().odom));
    ++loop_count_;
    rebuild_grid();
    RCLCPP_INFO(
      get_logger(), "Accepted loop %d -> %d, fitness %.3f, similarity %.3f",
      best_index, query, fitness, best_score);
  }

  void paint_keyframe(int index)
  {
    map_.paint(
      poses_[static_cast<std::size_t>(index)],
      *keyframes_[static_cast<std::size_t>(index)].cloud,
      obstacle_min_z_, obstacle_max_z_, base_to_ground_);
  }

  void rebuild_grid()
  {
    map_.clear();
    for (int i = 0; i < static_cast<int>(keyframes_.size()); ++i) {
      paint_keyframe(i);
    }
  }

  void publish_path()
  {
    nav_msgs::msg::Path path;
    path.header.stamp = latest_odom_stamp_;
    path.header.frame_id = "map";
    path.poses.reserve(poses_.size());
    for (const Pose & pose : poses_) {
      geometry_msgs::msg::PoseStamped stamped;
      stamped.header = path.header;
      stamped.pose.position.x = pose.t.x();
      stamped.pose.position.y = pose.t.y();
      stamped.pose.position.z = pose.t.z();
      stamped.pose.orientation.w = pose.q.w();
      stamped.pose.orientation.x = pose.q.x();
      stamped.pose.orientation.y = pose.q.y();
      stamped.pose.orientation.z = pose.q.z();
      path.poses.push_back(stamped);
    }
    path_pub_->publish(path);
  }

  void publish_grid()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (keyframes_.empty()) {
      return;
    }
    grid_pub_->publish(map_.message(now()));
  }

  void publish_tf()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const rclcpp::Time stamp = have_odom_ ? rclcpp::Time(latest_odom_stamp_) : now();
    geometry_msgs::msg::TransformStamped map_to_odom;
    map_to_odom.header.stamp = stamp;
    map_to_odom.header.frame_id = "map";
    map_to_odom.child_frame_id = "odom";
    map_to_odom.transform.translation.x = map_from_odom_.t.x();
    map_to_odom.transform.translation.y = map_from_odom_.t.y();
    map_to_odom.transform.translation.z = map_from_odom_.t.z();
    map_to_odom.transform.rotation.w = map_from_odom_.q.w();
    map_to_odom.transform.rotation.x = map_from_odom_.q.x();
    map_to_odom.transform.rotation.y = map_from_odom_.q.y();
    map_to_odom.transform.rotation.z = map_from_odom_.q.z();
    tf_broadcaster_->sendTransform(map_to_odom);

    if (!publish_frontend_tf_ || !have_odom_) {
      return;
    }
    geometry_msgs::msg::TransformStamped odom_to_base;
    odom_to_base.header.stamp = stamp;
    odom_to_base.header.frame_id = "odom";
    odom_to_base.child_frame_id = "base_link";
    odom_to_base.transform.translation.x = latest_odom_.t.x();
    odom_to_base.transform.translation.y = latest_odom_.t.y();
    odom_to_base.transform.translation.z = latest_odom_.t.z();
    odom_to_base.transform.rotation.w = latest_odom_.q.w();
    odom_to_base.transform.rotation.x = latest_odom_.q.x();
    odom_to_base.transform.rotation.y = latest_odom_.q.y();
    odom_to_base.transform.rotation.z = latest_odom_.q.z();
    tf_broadcaster_->sendTransform(odom_to_base);
  }

  tf2_ros::Buffer tf_buffer_;
  tf2_ros::TransformListener tf_listener_;
  OccupancyMemory map_;
  std::string odom_topic_;
  std::string cloud_topic_;
  bool cloud_in_odom_{true};
  bool publish_frontend_tf_{true};
  double keyframe_distance_{0.6};
  double keyframe_yaw_{0.35};
  int min_loop_gap_{20};
  int ring_bins_{60};
  double ring_accept_{0.86};
  double ring_ambiguity_{0.04};
  double icp_fitness_{0.2};
  double icp_max_correction_{5.0};
  float voxel_leaf_{0.4F};
  double odom_rotation_weight_{20.0};
  double odom_translation_weight_{20.0};
  double loop_rotation_weight_{5.0};
  double loop_translation_weight_{5.0};
  double obstacle_min_z_{0.15};
  double obstacle_max_z_{1.6};
  double base_to_ground_{0.35};
  bool publish_body_pose_{true};
  std::string body_pose_topic_{"/body_pose"};
  std::string base_frame_{"base_link"};
  std::string body_pose_frame_{"trunk"};

  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr body_pose_pub_;
  rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr path_pub_;
  rclcpp::Publisher<nav_msgs::msg::OccupancyGrid>::SharedPtr grid_pub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_sub_;
  rclcpp::TimerBase::SharedPtr tf_timer_;
  rclcpp::TimerBase::SharedPtr grid_timer_;
  std::unique_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;

  std::mutex mutex_;
  Pose latest_odom_;
  Pose map_from_odom_;
  builtin_interfaces::msg::Time latest_odom_stamp_;
  builtin_interfaces::msg::Time latest_cloud_stamp_;
  Cloud::Ptr latest_cloud_;
  bool have_odom_{false};
  bool have_cloud_{false};
  std::vector<Keyframe> keyframes_;
  std::vector<Pose> poses_;
  std::vector<BetweenFactor> factors_;
  int loop_count_{0};
};

}  // namespace go2_slam_backend

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<go2_slam_backend::SlamBackendNode>());
  rclcpp::shutdown();
  return 0;
}
