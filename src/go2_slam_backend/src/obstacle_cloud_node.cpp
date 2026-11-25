#include <pcl/filters/voxel_grid.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl_conversions/pcl_conversions.h>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_listener.h>
#include <tf2_sensor_msgs/tf2_sensor_msgs.hpp>

#include <cmath>
#include <functional>
#include <string>

class ObstacleCloudNode : public rclcpp::Node
{
public:
  ObstacleCloudNode()
  : Node("obstacle_cloud"),
    buffer_(get_clock()),
    listener_(buffer_)
  {
    input_topic_ = declare_parameter<std::string>("input_topic", "/livox/lidar");
    output_topic_ = declare_parameter<std::string>("output_topic", "/navigation/obstacle_cloud");
    base_frame_ = declare_parameter<std::string>("base_frame", "base_link");
    min_z_ = declare_parameter<double>("min_z", 0.15);
    max_z_ = declare_parameter<double>("max_z", 1.2);
    min_range_ = declare_parameter<double>("min_range", 0.3);
    max_range_ = declare_parameter<double>("max_range", 8.0);
    body_x_min_ = declare_parameter<double>("body_x_min", -0.45);
    body_x_max_ = declare_parameter<double>("body_x_max", 0.25);
    body_y_ = declare_parameter<double>("body_y", 0.18);
    body_z_max_ = declare_parameter<double>("body_z_max", 0.35);
    leaf_ = static_cast<float>(declare_parameter<double>("voxel_leaf", 0.08));
    // base_link is the IMU pose, so z = 0 is not the ground.
    base_to_ground_ = declare_parameter<double>("base_to_ground", 0.35);

    publisher_ = create_publisher<sensor_msgs::msg::PointCloud2>(output_topic_, 5);
    subscription_ = create_subscription<sensor_msgs::msg::PointCloud2>(
      input_topic_, rclcpp::SensorDataQoS(),
      std::bind(&ObstacleCloudNode::on_cloud, this, std::placeholders::_1));
  }

private:
  void on_cloud(const sensor_msgs::msg::PointCloud2::SharedPtr msg)
  {
    sensor_msgs::msg::PointCloud2 transformed;
    try {
      transformed = buffer_.transform(*msg, base_frame_, tf2::durationFromSec(0.05));
    } catch (const tf2::TransformException & error) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "Obstacle cloud skipped: %s", error.what());
      return;
    }

    pcl::PointCloud<pcl::PointXYZ> raw;
    pcl::PointCloud<pcl::PointXYZ> kept;
    pcl::fromROSMsg(transformed, raw);
    kept.reserve(raw.size());
    for (const auto & point : raw.points) {
      const double range = std::hypot(point.x, point.y);
      const bool inside_body =
        point.x > body_x_min_ && point.x < body_x_max_ &&
        std::abs(point.y) < body_y_ && point.z < body_z_max_;
      const double z_above_ground = point.z + base_to_ground_;
      if (inside_body || range < min_range_ || range > max_range_ ||
        z_above_ground < min_z_ || z_above_ground > max_z_)
      {
        continue;
      }
      kept.push_back(point);
    }

    pcl::PointCloud<pcl::PointXYZ> down;
    if (!kept.empty()) {
      pcl::VoxelGrid<pcl::PointXYZ> filter;
      auto input = kept.makeShared();
      filter.setInputCloud(input);
      filter.setLeafSize(leaf_, leaf_, leaf_);
      filter.filter(down);
    }

    sensor_msgs::msg::PointCloud2 output;
    pcl::toROSMsg(down, output);
    output.header.stamp = msg->header.stamp;
    output.header.frame_id = base_frame_;
    publisher_->publish(output);
  }

  std::string input_topic_;
  std::string output_topic_;
  std::string base_frame_;
  double min_z_{0.15};
  double max_z_{1.2};
  double min_range_{0.3};
  double max_range_{8.0};
  double body_x_min_{-0.45};
  double body_x_max_{0.25};
  double body_y_{0.18};
  double body_z_max_{0.35};
  float leaf_{0.08F};
  double base_to_ground_{0.35};
  tf2_ros::Buffer buffer_;
  tf2_ros::TransformListener listener_;
  rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr publisher_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr subscription_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ObstacleCloudNode>());
  rclcpp::shutdown();
  return 0;
}
