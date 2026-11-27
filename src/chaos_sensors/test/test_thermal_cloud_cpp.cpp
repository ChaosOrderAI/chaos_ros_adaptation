#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

#include <gtest/gtest.h>

#include "chaos_sensors/thermal_cloud_projector.hpp"

namespace chaos_sensors
{

TEST(ThermalCloudProjector, InterpolatesTranslationAndShortestPathSlerp)
{
  std::deque<TimedPose> poses;
  TimedPose p0;
  p0.stamp_sec = 10.0;
  p0.qw = 1.0;
  p0.pos = cv::Vec3d(0.0, 0.0, 0.0);

  // 90 deg rotation around Z, negated quaternion (-q represents the same SO(3) rotation).
  const double half = M_PI / 4.0;
  TimedPose p1;
  p1.stamp_sec = 11.0;
  p1.qz = -std::sin(half);
  p1.qw = -std::cos(half);
  p1.pos = cv::Vec3d(2.0, 4.0, -1.0);

  poses.push_back(p0);
  poses.push_back(p1);

  TimedPose mid;
  ASSERT_TRUE(interpolatePose(poses, 10.5, 0.2, mid));
  EXPECT_NEAR(mid.pos[0], 1.0, 1e-6);
  EXPECT_NEAR(mid.pos[1], 2.0, 1e-6);
  EXPECT_NEAR(mid.pos[2], -0.5, 1e-6);

  // Midpoint rotation must be +45 deg around Z: unit X maps to (sqrt(2)/2, sqrt(2)/2, 0).
  const cv::Matx33d R = quatToRotMat(mid.qx, mid.qy, mid.qz, mid.qw);
  const cv::Vec3d v = R * cv::Vec3d(1.0, 0.0, 0.0);
  EXPECT_NEAR(v[0], std::sqrt(0.5), 1e-4);
  EXPECT_NEAR(v[1], std::sqrt(0.5), 1e-4);
  EXPECT_NEAR(v[2], 0.0, 1e-6);

  // Rejects queries beyond max_extrap_sec.
  TimedPose out_of_range;
  EXPECT_FALSE(interpolatePose(poses, 11.5, 0.2, out_of_range));
}

TEST(ThermalCloudProjector, ProjectsWorldPointToOpticalCenterAndPacksRgb)
{
  ThermalProjectorConfig cfg;
  cfg.width = 256;
  cfg.height = 192;
  cfg.fx = 260.0;
  cfg.fy = 260.0;
  cfg.cx = 128.0;
  cfg.cy = 96.0;
  cfg.ext_R = cv::Matx33d::eye();
  cfg.ext_T = cv::Vec3d(0.0, 0.0, 0.0);
  // Standard forward optical frame: X_cam = -Y_lid, Y_cam = -Z_lid, Z_cam = +X_lid.
  cfg.Rcl = cv::Matx33d(
    0.0, -1.0, 0.0,
    0.0, 0.0, -1.0,
    1.0, 0.0, 0.0);
  cfg.Pcl = cv::Vec3d(0.0, 0.0, 0.0);

  cv::Mat color(192, 256, CV_8UC3, cv::Scalar(10, 20, 30));
  cv::Mat intensity(192, 256, CV_8UC1, cv::Scalar(40));
  // Paint a hot pixel block around optical center (128, 96): BGR = (12, 134, 250), therm = 220.
  cv::rectangle(color, cv::Rect(127, 95, 3, 3), cv::Scalar(12, 134, 250), cv::FILLED);
  cv::rectangle(intensity, cv::Rect(127, 95, 3, 3), cv::Scalar(220), cv::FILLED);

  TimedPose pose;
  pose.stamp_sec = 1.0;
  pose.qw = 1.0;
  pose.pos = cv::Vec3d(1.0, 0.0, 0.0);  // IMU is at X_w = 1.0

  // Point at (3.0, 0.0, 0.0) in world -> (2.0, 0.0, 0.0) in LiDAR -> (0, 0, 2.0) in IR optical!
  // Point behind sensor at (-1.0, 0.0, 0.0) must be culled.
  std::vector<cv::Vec3f> pts = {
    cv::Vec3f(3.0f, 0.0f, 0.0f),
    cv::Vec3f(-1.0f, 0.0f, 0.0f)};

  std::vector<ColoredThermalPoint> out;
  projectWorldCloudToThermal(pts, pose, cfg, color, intensity, out);

  ASSERT_EQ(out.size(), 1u);
  EXPECT_FLOAT_EQ(out[0].x, 3.0f);
  EXPECT_FLOAT_EQ(out[0].y, 0.0f);
  EXPECT_FLOAT_EQ(out[0].z, 0.0f);
  EXPECT_NEAR(out[0].intensity, 220.0f, 1e-3f);

  uint32_t rgb_bits = 0;
  std::memcpy(&rgb_bits, &out[0].rgb, sizeof(uint32_t));
  EXPECT_EQ((rgb_bits >> 16) & 0xFFu, 250u);  // R
  EXPECT_EQ((rgb_bits >> 8) & 0xFFu, 134u);   // G
  EXPECT_EQ(rgb_bits & 0xFFu, 12u);           // B
}

}  // namespace chaos_sensors
