#include "preprocess.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>

namespace
{

#pragma pack(push, 1)
// Byte layout that livox_ros_driver2 copies into PointCloud2 (LivoxPointXyzrtlt).
struct DriverPoint
{
  float x, y, z, reflectivity;
  uint8_t tag, line;
  double timestamp;
};
#pragma pack(pop)

sensor_msgs::msg::PointCloud2::ConstSharedPtr livox_cloud(uint64_t base_ns, const std::vector<DriverPoint> &points)
{
  auto cloud = std::make_shared<sensor_msgs::msg::PointCloud2>();
  cloud->header.stamp.sec = static_cast<int32_t>(base_ns / 1000000000ULL);
  cloud->header.stamp.nanosec = static_cast<uint32_t>(base_ns % 1000000000ULL);
  cloud->header.frame_id = "livox_frame";
  cloud->height = 1;
  cloud->width = points.size();
  const std::vector<std::pair<std::string, std::pair<uint32_t, uint8_t>>> fields{
      {"x", {0, sensor_msgs::msg::PointField::FLOAT32}},         {"y", {4, sensor_msgs::msg::PointField::FLOAT32}},
      {"z", {8, sensor_msgs::msg::PointField::FLOAT32}},         {"intensity", {12, sensor_msgs::msg::PointField::FLOAT32}},
      {"tag", {16, sensor_msgs::msg::PointField::UINT8}},        {"line", {17, sensor_msgs::msg::PointField::UINT8}},
      {"timestamp", {18, sensor_msgs::msg::PointField::FLOAT64}}};
  for (const auto &[name, layout] : fields)
  {
    sensor_msgs::msg::PointField field;
    field.name = name;
    field.offset = layout.first;
    field.datatype = layout.second;
    field.count = 1;
    cloud->fields.push_back(field);
  }
  cloud->point_step = sizeof(DriverPoint);
  cloud->row_step = cloud->point_step * cloud->width;
  cloud->is_dense = true;
  cloud->data.resize(cloud->row_step);
  std::memcpy(cloud->data.data(), points.data(), cloud->data.size());
  return cloud;
}

TEST(Preprocess, LivoxPointCloud2OffsetsAreMillisecondsFromHeader)
{
  static_assert(sizeof(DriverPoint) == 26, "driver layout");
  const uint64_t base = 1700000000123456789ULL;
  const auto cloud = livox_cloud(base, {
                                           {1.0f, 0.0f, 0.0f, 10.0f, 0x10, 0, static_cast<double>(base)},
                                           {2.0f, 0.0f, 0.0f, 20.0f, 0x10, 1, static_cast<double>(base + 500000)},
                                           {0.1f, 0.0f, 0.0f, 30.0f, 0x10, 2, static_cast<double>(base + 600000)}, // inside blind
                                           {3.0f, 1.0f, 0.0f, 40.0f, 0x10, 5, static_cast<double>(base + 700000)}, // line >= scan_line
                                           {0.0f, 4.0f, 1.0f, 50.0f, 0x10, 3, static_cast<double>(base + 900000)},
                                       });
  Preprocess pre;
  pre.lidar_type = LIVOX;
  pre.N_SCANS = 4;
  pre.point_filter_num = 1;
  pre.blind = 0.5;
  pre.blind_sqr = pre.blind * pre.blind;
  PointCloudXYZI::Ptr out(new PointCloudXYZI());
  pre.process(cloud, out);

  ASSERT_EQ(out->size(), 3u);
  EXPECT_FLOAT_EQ(out->points[0].x, 1.0f);
  EXPECT_NEAR(out->points[0].curvature, 0.0f, 1e-3);
  EXPECT_FLOAT_EQ(out->points[0].intensity, 10.0f);
  EXPECT_NEAR(out->points[1].curvature, 0.5f, 1e-3);
  EXPECT_FLOAT_EQ(out->points[2].y, 4.0f);
  EXPECT_NEAR(out->points[2].curvature, 0.9f, 1e-3);
  EXPECT_FLOAT_EQ(out->points[2].intensity, 50.0f);
}

} // namespace
