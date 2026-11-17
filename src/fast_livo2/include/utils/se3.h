// Rigid transform used by the visual front end. Replaces the subset of the
// non-templated Sophus::SE3 that FAST-LIVO2 uses.
#ifndef LIVO_SE3_H
#define LIVO_SE3_H

#include <Eigen/Core>
#include <Eigen/Geometry>

namespace livo
{

class SE3
{
public:
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  SE3() : rotation_(Eigen::Matrix3d::Identity()), translation_(Eigen::Vector3d::Zero()) {}
  SE3(const Eigen::Matrix3d &rotation, const Eigen::Vector3d &translation)
      : rotation_(project(rotation)), translation_(translation)
  {
  }

  SE3 inverse() const
  {
    const Eigen::Matrix3d rt = rotation_.transpose();
    SE3 out;
    out.rotation_ = rt;
    out.translation_ = -(rt * translation_);
    return out;
  }

  SE3 operator*(const SE3 &other) const
  {
    SE3 out;
    out.rotation_ = rotation_ * other.rotation_;
    out.translation_ = rotation_ * other.translation_ + translation_;
    return out;
  }

  Eigen::Vector3d operator*(const Eigen::Vector3d &point) const { return rotation_ * point + translation_; }

  // By value, as in Sophus: callers use these on temporaries.
  Eigen::Matrix3d rotation_matrix() const { return rotation_; }
  Eigen::Vector3d translation() const { return translation_; }

private:
  // Sophus stores a unit quaternion, so a slightly non-orthogonal input is
  // snapped back onto SO(3). Keep that behaviour.
  static Eigen::Matrix3d project(const Eigen::Matrix3d &rotation)
  {
    Eigen::Quaterniond q(rotation);
    q.normalize();
    return q.toRotationMatrix();
  }

  Eigen::Matrix3d rotation_;
  Eigen::Vector3d translation_;
};

} // namespace livo

#endif
