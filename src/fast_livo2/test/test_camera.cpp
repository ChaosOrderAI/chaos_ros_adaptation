#include "utils/camera.h"
#include "utils/vision.h"

#include <gtest/gtest.h>
#include <opencv2/calib3d.hpp>

#include <stdexcept>
#include <vector>

namespace
{

// The upstream camera_pinhole.yaml for the Avia handheld rig.
livo::PinholeCamera avia_camera(double scale)
{
  return livo::PinholeCamera(1280, 1024, scale, 1293.56944, 1293.3155, 626.91359, 522.799224, {-0.076160, 0.123001, -0.00113, 0.000251, 0.0});
}

TEST(PinholeCamera, ScaleShrinksResolutionAndIntrinsics)
{
  const auto cam = avia_camera(0.5);
  EXPECT_EQ(cam.width(), 640);
  EXPECT_EQ(cam.height(), 512);
  EXPECT_DOUBLE_EQ(cam.scale(), 0.5);
  EXPECT_DOUBLE_EQ(cam.fx(), 1293.56944 * 0.5);
  EXPECT_DOUBLE_EQ(cam.cy(), 522.799224 * 0.5);
}

TEST(PinholeCamera, ProjectionMatchesOpenCv)
{
  const auto cam = avia_camera(1.0);
  const cv::Matx33d K(cam.fx(), 0, cam.cx(), 0, cam.fy(), cam.cy(), 0, 0, 1);
  const cv::Vec<double, 5> D(-0.076160, 0.123001, -0.00113, 0.000251, 0.0);
  const std::vector<cv::Point3d> points{{0.3, -0.2, 2.0}, {-1.1, 0.7, 3.0}, {0.0, 0.0, 1.0}, {0.9, 0.6, 1.5}};
  std::vector<cv::Point2d> expected;
  cv::projectPoints(points, cv::Vec3d::zeros(), cv::Vec3d::zeros(), K, D, expected);
  for (size_t i = 0; i < points.size(); ++i)
  {
    const Eigen::Vector2d px = cam.world2cam(Eigen::Vector3d(points[i].x, points[i].y, points[i].z));
    EXPECT_NEAR(px.x(), expected[i].x, 1e-6);
    EXPECT_NEAR(px.y(), expected[i].y, 1e-6);
  }
}

TEST(PinholeCamera, DistortionJacobianMatchesFiniteDifference)
{
  const auto cam = avia_camera(1.0);
  const double eps = 1e-6;
  const std::vector<Eigen::Vector2d> points{
    {0.0, 0.0}, {0.2, -0.1}, {-0.5, 0.35}, {0.8, 0.6}};
  for (const auto & xy : points)
  {
    const Eigen::Matrix2d J = cam.distortionJacobian(xy);
    const Eigen::Vector2d px = cam.planeToPixel(xy);
    const Eigen::Vector2d px_dx = cam.planeToPixel(xy + Eigen::Vector2d(eps, 0.0));
    const Eigen::Vector2d px_dy = cam.planeToPixel(xy + Eigen::Vector2d(0.0, eps));
    EXPECT_NEAR(cam.fx() * J(0, 0), (px_dx.x() - px.x()) / eps, 1e-3);
    EXPECT_NEAR(cam.fy() * J(1, 0), (px_dx.y() - px.y()) / eps, 1e-3);
    EXPECT_NEAR(cam.fx() * J(0, 1), (px_dy.x() - px.x()) / eps, 1e-3);
    EXPECT_NEAR(cam.fy() * J(1, 1), (px_dy.y() - px.y()) / eps, 1e-3);
  }
}

TEST(PinholeCamera, DistortionJacobianIsIdentityWithoutDistortion)
{
  const livo::PinholeCamera cam(640, 480, 1.0, 500, 510, 320, 240);
  EXPECT_TRUE(cam.distortionJacobian(Eigen::Vector2d(0.3, -0.2)).isIdentity(0.0));
}

TEST(PinholeCamera, UnprojectInvertsProjection)
{
  const auto cam = avia_camera(0.5);
  for (double u = 10; u < cam.width(); u += 90)
  {
    for (double v = 10; v < cam.height(); v += 70)
    {
      const Eigen::Vector3d bearing = cam.cam2world(u, v);
      EXPECT_NEAR(bearing.norm(), 1.0, 1e-12);
      const Eigen::Vector2d px = cam.world2cam(bearing * 4.2);
      EXPECT_NEAR(px.x(), u, 1e-6);
      EXPECT_NEAR(px.y(), v, 1e-6);
    }
  }
}

TEST(PinholeCamera, NoDistortionIsPlainPinhole)
{
  const livo::PinholeCamera cam(640, 480, 1.0, 500, 510, 320, 240);
  EXPECT_FALSE(cam.distorted());
  const Eigen::Vector2d px = cam.world2cam(Eigen::Vector3d(0.2, -0.1, 2.0));
  EXPECT_DOUBLE_EQ(px.x(), 500 * 0.1 + 320);
  EXPECT_DOUBLE_EQ(px.y(), 510 * -0.05 + 240);
  EXPECT_TRUE(cam.isInFrame(Eigen::Vector2i(0, 0)));
  EXPECT_FALSE(cam.isInFrame(Eigen::Vector2i(640, 10)));
  EXPECT_FALSE(cam.isInFrame(Eigen::Vector2i(2, 10), 3));
}

TEST(CameraFactory, RejectsUnknownModelAndMissingIntrinsics)
{
  livo::CameraConfig config;
  config.width = 640;
  config.height = 480;
  config.fx = config.fy = 500;
  EXPECT_NO_THROW(livo::makeCamera(config));
  config.model = "EquidistantCamera";
  EXPECT_THROW(livo::makeCamera(config), std::invalid_argument);
  config.model = "Pinhole";
  config.fx = 0;
  EXPECT_THROW(livo::makeCamera(config), std::invalid_argument);
}

TEST(Vision, BilinearInterpolation)
{
  cv::Mat img = (cv::Mat_<uint8_t>(2, 2) << 0, 100, 50, 150);
  EXPECT_FLOAT_EQ(livo::interpolateMat_8u(img, 0.0f, 0.0f), 0.0f);
  EXPECT_FLOAT_EQ(livo::interpolateMat_8u(img, 0.5f, 0.0f), 50.0f);
  EXPECT_FLOAT_EQ(livo::interpolateMat_8u(img, 0.5f, 0.5f), 75.0f);
  EXPECT_FLOAT_EQ(livo::interpolateMat_8u(img, 0.25f, 1.0f), 75.0f);
}

TEST(Vision, HalfSampleAveragesBlocks)
{
  cv::Mat in = (cv::Mat_<uint8_t>(2, 4) << 1, 2, 10, 20, 3, 4, 30, 41);
  cv::Mat out(1, 2, CV_8U);
  livo::halfSample(in, out);
  EXPECT_EQ(out.at<uint8_t>(0, 0), 2);  // 10 / 4, truncated
  EXPECT_EQ(out.at<uint8_t>(0, 1), 25); // 101 / 4, truncated
}

TEST(Vision, ShiTomasiPrefersCornersOverEdgesAndFlat)
{
  cv::Mat flat(40, 40, CV_8U, cv::Scalar(80));
  cv::Mat edge = flat.clone();
  edge(cv::Rect(20, 0, 20, 40)).setTo(200);
  cv::Mat corner = flat.clone();
  corner(cv::Rect(20, 20, 20, 20)).setTo(200);
  EXPECT_FLOAT_EQ(livo::shiTomasiScore(flat, 20, 20), 0.0f);
  EXPECT_NEAR(livo::shiTomasiScore(edge, 20, 20), 0.0f, 1e-3);
  EXPECT_GT(livo::shiTomasiScore(corner, 20, 20), 100.0f);
  EXPECT_FLOAT_EQ(livo::shiTomasiScore(corner, 3, 20), 0.0f);
}

} // namespace
