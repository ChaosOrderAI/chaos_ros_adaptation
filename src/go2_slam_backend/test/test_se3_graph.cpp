#include "go2_slam_backend/se3_graph.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace
{

go2_slam_backend::Pose translation(double x, double y, double z = 0.0)
{
  go2_slam_backend::Pose pose;
  pose.t = Eigen::Vector3d(x, y, z);
  return pose;
}

}  // namespace

TEST(Se3Graph, ExpAndLogRoundTrip)
{
  Eigen::Matrix<double, 6, 1> xi;
  xi << 0.2, -0.1, 0.4, 0.3, -0.2, 0.1;
  const auto recovered = go2_slam_backend::pose_log(go2_slam_backend::pose_exp(xi));
  EXPECT_NEAR(recovered(0), xi(0), 1e-6);
  EXPECT_NEAR(recovered(1), xi(1), 1e-6);
  EXPECT_NEAR(recovered(2), xi(2), 1e-6);
  EXPECT_NEAR(recovered(3), xi(3), 1e-6);
  EXPECT_NEAR(recovered(4), xi(4), 1e-6);
  EXPECT_NEAR(recovered(5), xi(5), 1e-6);
}

TEST(Se3Graph, OdomChainPullsPerturbedPose)
{
  std::vector<go2_slam_backend::Pose> poses{translation(0.0, 0.0), translation(4.0, -2.0)};
  go2_slam_backend::BetweenFactor factor;
  factor.from = 0;
  factor.to = 1;
  factor.measurement = translation(1.0, 0.0);
  factor.rotation_weight = 10.0;
  factor.translation_weight = 10.0;
  ASSERT_TRUE(go2_slam_backend::optimize_pose_graph(poses, {factor}));
  EXPECT_NEAR(poses[1].t.x(), 1.0, 1e-3);
  EXPECT_NEAR(poses[1].t.y(), 0.0, 1e-3);
  EXPECT_NEAR(poses[1].t.z(), 0.0, 1e-3);
}

TEST(Se3Graph, LoopFactorClosesDriftedChain)
{
  std::vector<go2_slam_backend::Pose> poses{
    translation(0.0, 0.0), translation(1.0, 0.0), translation(2.0, 0.0)};
  std::vector<go2_slam_backend::BetweenFactor> factors(3);
  factors[0].from = 0;
  factors[0].to = 1;
  factors[0].measurement = translation(1.0, 0.0);
  factors[1].from = 1;
  factors[1].to = 2;
  factors[1].measurement = translation(1.0, 0.0);
  factors[2].from = 2;
  factors[2].to = 0;
  factors[2].measurement = translation(0.0, 0.0);
  factors[2].robust = true;
  for (auto & factor : factors) {
    factor.rotation_weight = 1.0;
    factor.translation_weight = 1.0;
  }
  factors[2].rotation_weight = 40.0;
  factors[2].translation_weight = 40.0;
  ASSERT_TRUE(go2_slam_backend::optimize_pose_graph(poses, factors));
  EXPECT_LT(poses[2].t.norm(), 0.5);
}
