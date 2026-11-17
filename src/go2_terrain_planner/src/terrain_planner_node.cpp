#include "go2_terrain_planner/elevation_planner.hpp"

#include <Eigen/Geometry>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <nav_msgs/msg/path.hpp>
#include <pcl/filters/voxel_grid.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <tf2/exceptions.h>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <mutex>
#include <string>

namespace go2_terrain_planner
{
namespace
{

Eigen::Isometry3d isometry_from(const geometry_msgs::msg::Transform & transform)
{
  Eigen::Quaterniond rotation(
    transform.rotation.w, transform.rotation.x, transform.rotation.y, transform.rotation.z);
  rotation.normalize();
  Eigen::Isometry3d pose = Eigen::Isometry3d::Identity();
  pose.linear() = rotation.toRotationMatrix();
  pose.translation() = Eigen::Vector3d(
    transform.translation.x, transform.translation.y, transform.translation.z);
  return pose;
}

geometry_msgs::msg::Quaternion orientation_between(
  const Eigen::Vector3d & from, const Eigen::Vector3d & to)
{
  const double horizontal = std::hypot(to.x() - from.x(), to.y() - from.y());
  const double yaw = std::atan2(to.y() - from.y(), to.x() - from.x());
  // Positive pitch about +Y points the nose down, so a rising step is negative.
  const double pitch = horizontal > 1e-6 ? -std::atan2(to.z() - from.z(), horizontal) : 0.0;
  const Eigen::Quaterniond rotation =
    Eigen::AngleAxisd(yaw, Eigen::Vector3d::UnitZ()) *
    Eigen::AngleAxisd(pitch, Eigen::Vector3d::UnitY());
  geometry_msgs::msg::Quaternion message;
  message.w = rotation.w();
  message.x = rotation.x();
  message.y = rotation.y();
  message.z = rotation.z();
  return message;
}

}  // namespace

class TerrainPlannerNode : public rclcpp::Node
{
public:
  TerrainPlannerNode()
  : Node("terrain_planner"),
    buffer_(get_clock()),
    listener_(buffer_),
    map_(0.1, 20.0)
  {
    const std::string cloud_topic = declare_parameter<std::string>("cloud_topic", "/cloud_registered");
    const std::string goal_topic = declare_parameter<std::string>("goal_topic", "/goal_pose");
    const std::string path_topic = declare_parameter<std::string>("path_topic", "/terrain/path");
    const std::string elevation_topic =
      declare_parameter<std::string>("elevation_topic", "/terrain/elevation");
    const std::string route_topic = declare_parameter<std::string>("route_topic", "");
    map_frame_ = declare_parameter<std::string>("map_frame", "map");
    odom_frame_ = declare_parameter<std::string>("odom_frame", "odom");
    route_frame_ = declare_parameter<std::string>("route_frame", odom_frame_);
    base_frame_ = declare_parameter<std::string>("base_frame", "base_link");
    cloud_in_odom_ = declare_parameter<bool>("cloud_in_odom", true);
    max_step_height_ = declare_parameter<double>("max_step_height", 0.16);
    snap_radius_ = declare_parameter<double>("snap_radius", 0.45);
    step_weight_ = declare_parameter<double>("step_weight", 2.0);
    below_robot_ = declare_parameter<double>("below_robot", 3.5);
    above_robot_ = declare_parameter<double>("above_robot", 3.5);
    insert_range_ = declare_parameter<double>("insert_range", 12.0);
    footprint_radius_ = declare_parameter<double>("footprint_radius", 0.22);
    min_stance_drop_ = declare_parameter<double>("min_stance_drop", 0.02);
    max_stance_drop_ = declare_parameter<double>("max_stance_drop", 0.80);
    reproject_translation_ = declare_parameter<double>("reproject_translation", 0.05);
    reproject_yaw_ = declare_parameter<double>("reproject_yaw_deg", 1.0) * M_PI / 180.0;
    leaf_ = static_cast<float>(declare_parameter<double>("voxel_leaf", 0.05));
    body_x_min_ = declare_parameter<double>("body_x_min", -0.45);
    body_x_max_ = declare_parameter<double>("body_x_max", 0.25);
    body_y_ = declare_parameter<double>("body_y", 0.18);
    body_z_min_ = declare_parameter<double>("body_z_min", -0.25);
    body_z_max_ = declare_parameter<double>("body_z_max", 0.35);

    const double resolution = declare_parameter<double>("resolution", 0.1);
    const double half_size = declare_parameter<double>("half_size", 20.0);
    const int min_count = static_cast<int>(declare_parameter<int>("min_count", 3));
    map_ = ElevationMap(resolution, half_size);
    map_.set_min_count(min_count);
    map_.set_obstacle_rise(max_step_height_);

    path_pub_ = create_publisher<nav_msgs::msg::Path>(path_topic, 5);
    if (!route_topic.empty()) {
      // Transient local: a planner that starts after the goal still gets the
      // route, and refreshes below replace the latched sample.
      route_pub_ = create_publisher<nav_msgs::msg::Path>(
        route_topic, rclcpp::QoS(1).reliable().transient_local());
    }
    elevation_pub_ = create_publisher<sensor_msgs::msg::PointCloud2>(elevation_topic, 2);
    cloud_sub_ = create_subscription<sensor_msgs::msg::PointCloud2>(
      cloud_topic, rclcpp::SensorDataQoS(),
      std::bind(&TerrainPlannerNode::on_cloud, this, std::placeholders::_1));
    goal_sub_ = create_subscription<geometry_msgs::msg::PoseStamped>(
      goal_topic, 10,
      std::bind(&TerrainPlannerNode::on_goal, this, std::placeholders::_1));
    const double period = declare_parameter<double>("elevation_period", 1.0);
    elevation_timer_ = create_wall_timer(
      std::chrono::duration<double>(std::max(0.2, period)),
      std::bind(&TerrainPlannerNode::publish_elevation, this));
    if (route_pub_) {
      const double route_period = declare_parameter<double>("route_refresh_period", 1.0);
      route_timer_ = create_wall_timer(
        std::chrono::duration<double>(std::max(0.2, route_period)),
        [this]() { publish_route(false); });
    }
  }

private:
  bool lookup(const std::string & target, const std::string & source, Eigen::Isometry3d & pose)
  {
    try {
      const auto transform = buffer_.lookupTransform(target, source, tf2::TimePointZero);
      pose = isometry_from(transform.transform);
      return true;
    } catch (const tf2::TransformException & error) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 2000, "TF %s -> %s unavailable: %s",
        source.c_str(), target.c_str(), error.what());
      return false;
    }
  }

  void on_cloud(const sensor_msgs::msg::PointCloud2::SharedPtr msg)
  {
    Eigen::Isometry3d map_from_odom = Eigen::Isometry3d::Identity();
    if (cloud_in_odom_ && !lookup(map_frame_, odom_frame_, map_from_odom)) {
      return;
    }
    Eigen::Isometry3d map_from_base = Eigen::Isometry3d::Identity();
    if (!lookup(map_frame_, base_frame_, map_from_base)) {
      return;
    }
    const Eigen::Isometry3d base_from_map = map_from_base.inverse();
    const Eigen::Vector3d robot = map_from_base.translation();

    pcl::PointCloud<pcl::PointXYZ> raw;
    pcl::fromROSMsg(*msg, raw);
    pcl::PointCloud<pcl::PointXYZ> down;
    if (!raw.empty()) {
      pcl::VoxelGrid<pcl::PointXYZ> filter;
      filter.setInputCloud(raw.makeShared());
      filter.setLeafSize(leaf_, leaf_, leaf_);
      filter.filter(down);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (have_map_tf_) {
      const Eigen::Isometry3d delta = map_from_odom * last_map_from_odom_.inverse();
      const double shift = delta.translation().norm();
      const double angle = std::abs(Eigen::AngleAxisd(delta.linear()).angle());
      if (shift > reproject_translation_ || angle > reproject_yaw_) {
        map_.reproject(delta);
      }
    }
    last_map_from_odom_ = map_from_odom;
    have_map_tf_ = true;
    map_.begin_scan();
    for (const auto & point : down.points) {
      if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) {
        continue;
      }
      const Eigen::Vector3d in_map = cloud_in_odom_ ?
        map_from_odom * Eigen::Vector3d(point.x, point.y, point.z) :
        Eigen::Vector3d(point.x, point.y, point.z);
      if ((in_map.head<2>() - robot.head<2>()).norm() > insert_range_) {
        continue;
      }
      if (in_map.z() < robot.z() - below_robot_ || in_map.z() > robot.z() + above_robot_) {
        continue;
      }
      const Eigen::Vector3d in_base = base_from_map * in_map;
      // The box is z-bounded on both sides: without a lower bound it
      // strips the floor under the robot and no stance ever fits.
      const bool inside_body =
        in_base.x() > body_x_min_ && in_base.x() < body_x_max_ &&
        std::abs(in_base.y()) < body_y_ &&
        in_base.z() > body_z_min_ && in_base.z() < body_z_max_;
      if (inside_body) {
        continue;
      }
      map_.insert(in_map);
    }
  }

  void on_goal(const geometry_msgs::msg::PoseStamped::SharedPtr msg)
  {
    Eigen::Vector3d goal(msg->pose.position.x, msg->pose.position.y, msg->pose.position.z);
    if (!msg->header.frame_id.empty() && msg->header.frame_id != map_frame_) {
      Eigen::Isometry3d map_from_goal = Eigen::Isometry3d::Identity();
      if (!lookup(map_frame_, msg->header.frame_id, map_from_goal)) {
        return;
      }
      goal = map_from_goal * goal;
    }
    Eigen::Isometry3d map_from_base = Eigen::Isometry3d::Identity();
    if (!lookup(map_frame_, base_frame_, map_from_base)) {
      return;
    }

    ElevationMap snapshot(0.1, 1.0);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot = map_;
    }
    PlanLimits limits;
    limits.max_step_height = max_step_height_;
    limits.snap_radius = snap_radius_;
    limits.step_weight = step_weight_;
    limits.footprint_radius = footprint_radius_;
    limits.constrain_start_height = true;
    limits.start_z = map_from_base.translation().z();
    limits.min_stance_drop = min_stance_drop_;
    limits.max_stance_drop = max_stance_drop_;
    const SurfacePath planned = snapshot.plan(map_from_base.translation().head<2>(), goal.head<2>(), limits);

    if (!planned.success) {
      RCLCPP_WARN(
        get_logger(),
        "No surface path to (%.2f, %.2f). The step is higher than max_step_height %.2f m, or the surface is unknown.",
        goal.x(), goal.y(), max_step_height_);
      return;
    }

    nav_msgs::msg::Path path;
    path.header.stamp = now();
    path.header.frame_id = map_frame_;
    path.poses.reserve(planned.path.size());
    for (std::size_t i = 0; i < planned.path.size(); ++i) {
      geometry_msgs::msg::PoseStamped pose;
      pose.header = path.header;
      pose.pose.position.x = planned.path[i].x();
      pose.pose.position.y = planned.path[i].y();
      pose.pose.position.z = planned.path[i].z();
      if (i + 1 < planned.path.size()) {
        pose.pose.orientation = orientation_between(planned.path[i], planned.path[i + 1]);
      } else if (i > 0) {
        pose.pose.orientation = path.poses[i - 1].pose.orientation;
      }
      path.poses.push_back(pose);
    }
    path_pub_->publish(path);
    // The local planner works in the continuous odometry frame, so keep the
    // route in map and republish it whenever map -> odom is corrected.
    route_map_path_ = path;
    route_published_ = false;
    publish_route(true);
    RCLCPP_INFO(
      get_logger(), "Surface path with %zu poses, goal height %.2f m. This node does not command the robot.",
      planned.path.size(), planned.path.back().z());
  }

  void publish_route(bool force)
  {
    if (!route_pub_ || route_map_path_.poses.empty()) {
      return;
    }
    Eigen::Isometry3d route_from_map = Eigen::Isometry3d::Identity();
    if (!lookup(route_frame_, map_frame_, route_from_map)) {
      return;
    }
    if (!force && route_published_) {
      const Eigen::Isometry3d correction = route_from_map * published_route_from_map_.inverse();
      if (correction.translation().norm() < reproject_translation_ &&
        std::abs(Eigen::AngleAxisd(correction.linear()).angle()) < reproject_yaw_)
      {
        return;
      }
    }

    nav_msgs::msg::Path route;
    route.header.stamp = now();
    route.header.frame_id = route_frame_;
    route.poses.reserve(route_map_path_.poses.size());
    const Eigen::Quaterniond route_rot(route_from_map.linear());
    for (const auto & pose : route_map_path_.poses) {
      geometry_msgs::msg::PoseStamped out;
      out.header = route.header;
      const Eigen::Vector3d p = route_from_map * Eigen::Vector3d(
        pose.pose.position.x, pose.pose.position.y, pose.pose.position.z);
      const Eigen::Quaterniond q = route_rot * Eigen::Quaterniond(
        pose.pose.orientation.w, pose.pose.orientation.x,
        pose.pose.orientation.y, pose.pose.orientation.z);
      out.pose.position.x = p.x();
      out.pose.position.y = p.y();
      out.pose.position.z = p.z();
      out.pose.orientation.w = q.w();
      out.pose.orientation.x = q.x();
      out.pose.orientation.y = q.y();
      out.pose.orientation.z = q.z();
      route.poses.push_back(out);
    }
    route_pub_->publish(route);
    const bool refreshed = route_published_;
    published_route_from_map_ = route_from_map;
    route_published_ = true;
    if (refreshed) {
      RCLCPP_INFO(get_logger(), "Route refreshed after a map -> odom correction");
    }
  }

  void publish_elevation()
  {
    ElevationMap snapshot(0.1, 1.0);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot = map_;
    }
    pcl::PointCloud<pcl::PointXYZ> cloud;
    snapshot.visit_surfaces([&cloud](double x, double y, double z) {
        cloud.emplace_back(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
      });
    sensor_msgs::msg::PointCloud2 message;
    pcl::toROSMsg(cloud, message);
    message.header.stamp = now();
    message.header.frame_id = map_frame_;
    elevation_pub_->publish(message);
  }

  std::string map_frame_;
  std::string odom_frame_;
  std::string route_frame_;
  std::string base_frame_;
  bool cloud_in_odom_{true};
  double max_step_height_{0.16};
  double snap_radius_{0.45};
  double step_weight_{2.0};
  double below_robot_{3.5};
  double above_robot_{3.5};
  double insert_range_{12.0};
  double footprint_radius_{0.22};
  double min_stance_drop_{0.02};
  double max_stance_drop_{0.80};
  double reproject_translation_{0.05};
  double reproject_yaw_{0.0};
  Eigen::Isometry3d last_map_from_odom_{Eigen::Isometry3d::Identity()};
  bool have_map_tf_{false};
  float leaf_{0.05F};
  double body_x_min_{-0.45};
  double body_x_max_{0.25};
  double body_y_{0.18};
  double body_z_min_{-0.25};
  double body_z_max_{0.35};
  ElevationMap map_;
  std::mutex mutex_;
  tf2_ros::Buffer buffer_;
  tf2_ros::TransformListener listener_;
  // Last successful route in the map frame; republished in odom when the
  // backend corrects map -> odom after a loop closure.
  nav_msgs::msg::Path route_map_path_;
  Eigen::Isometry3d published_route_from_map_{Eigen::Isometry3d::Identity()};
  bool route_published_{false};
  rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr path_pub_;
  rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr route_pub_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr elevation_pub_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr cloud_sub_;
  rclcpp::Subscription<geometry_msgs::msg::PoseStamped>::SharedPtr goal_sub_;
  rclcpp::TimerBase::SharedPtr elevation_timer_;
  rclcpp::TimerBase::SharedPtr route_timer_;
};

}  // namespace go2_terrain_planner

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<go2_terrain_planner::TerrainPlannerNode>());
  rclcpp::shutdown();
  return 0;
}
