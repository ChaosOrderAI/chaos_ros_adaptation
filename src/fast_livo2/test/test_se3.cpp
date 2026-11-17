#include "utils/se3.h"

#include <gtest/gtest.h>

namespace
{

Eigen::Matrix3d rotation(double roll, double pitch, double yaw)
{
  return (Eigen::AngleAxisd(yaw, Eigen::Vector3d::UnitZ()) * Eigen::AngleAxisd(pitch, Eigen::Vector3d::UnitY()) *
          Eigen::AngleAxisd(roll, Eigen::Vector3d::UnitX()))
      .toRotationMatrix();
}

TEST(SE3, ComposeInverseAndApply)
{
  const livo::SE3 a(rotation(0.1, -0.4, 1.2), Eigen::Vector3d(1, 2, 3));
  const livo::SE3 b(rotation(-0.7, 0.2, -2.5), Eigen::Vector3d(-0.5, 0.25, 4));
  const Eigen::Vector3d p(0.3, -1.7, 2.2);

  EXPECT_TRUE(((a * b) * p).isApprox(a * (b * p), 1e-12));
  EXPECT_TRUE((a.inverse() * (a * p)).isApprox(p, 1e-12));
  const livo::SE3 identity = a * a.inverse();
  EXPECT_TRUE(identity.rotation_matrix().isApprox(Eigen::Matrix3d::Identity(), 1e-12));
  EXPECT_LT(identity.translation().norm(), 1e-12);
  // Frame::pos(): camera centre in the world is the inverse translation.
  EXPECT_TRUE(a.inverse().translation().isApprox(-a.rotation_matrix().transpose() * a.translation(), 1e-12));
}

TEST(SE3, NearlyOrthogonalInputIsProjectedOntoSO3)
{
  Eigen::Matrix3d r = rotation(0.3, 0.2, 0.1);
  r(0, 0) += 1e-4;
  const livo::SE3 t(r, Eigen::Vector3d::Zero());
  const Eigen::Matrix3d q = t.rotation_matrix();
  EXPECT_TRUE((q * q.transpose()).isApprox(Eigen::Matrix3d::Identity(), 1e-12));
  EXPECT_NEAR(q.determinant(), 1.0, 1e-12);
}

} // namespace
