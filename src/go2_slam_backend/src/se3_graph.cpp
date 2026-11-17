#include "go2_slam_backend/se3_graph.hpp"

#include <Eigen/Dense>

#include <cmath>

namespace go2_slam_backend
{
namespace
{

Eigen::Quaterniond normalized(Eigen::Quaterniond q)
{
  q.normalize();
  if (q.w() < 0.0) {
    q.coeffs() *= -1.0;
  }
  return q;
}

Eigen::Matrix<double, 6, 1> factor_error(const Pose & pose_from, const Pose & pose_to, const Pose & measurement)
{
  return pose_log(pose_between(measurement, pose_between(pose_from, pose_to)));
}

}  // namespace

Pose pose_inverse(const Pose & pose)
{
  Pose out;
  out.q = normalized(pose.q.conjugate());
  out.t = -(out.q * pose.t);
  return out;
}

Pose pose_compose(const Pose & a, const Pose & b)
{
  Pose out;
  out.q = normalized(a.q * b.q);
  out.t = a.t + a.q * b.t;
  return out;
}

Pose pose_between(const Pose & from, const Pose & to)
{
  return pose_compose(pose_inverse(from), to);
}

Pose pose_exp(const Eigen::Matrix<double, 6, 1> & xi)
{
  const Eigen::Vector3d omega = xi.head<3>();
  const Eigen::Vector3d rho = xi.tail<3>();
  const double theta = omega.norm();
  Eigen::Quaterniond q = Eigen::Quaterniond::Identity();
  Eigen::Vector3d translation = rho;
  if (theta < 1e-8) {
    q = normalized(Eigen::Quaterniond(1.0, 0.5 * omega.x(), 0.5 * omega.y(), 0.5 * omega.z()));
  } else {
    const Eigen::Vector3d axis = omega / theta;
    q = normalized(Eigen::Quaterniond(Eigen::AngleAxisd(theta, axis)));
    const double a = std::sin(theta) / theta;
    const double b = (1.0 - std::cos(theta)) / theta;
    translation = a * rho + (1.0 - a) * axis.dot(rho) * axis + b * axis.cross(rho);
  }
  return Pose{q, translation};
}

Eigen::Matrix<double, 6, 1> pose_log(const Pose & pose)
{
  Eigen::Quaterniond q = normalized(pose.q);
  Eigen::AngleAxisd aa(q);
  double theta = aa.angle();
  if (theta > M_PI) {
    theta -= 2.0 * M_PI;
  }
  const Eigen::Vector3d omega = theta * aa.axis();
  Eigen::Vector3d rho = pose.t;
  if (std::abs(theta) > 1e-5) {
    const Eigen::Matrix3d skew =
      (Eigen::Matrix3d() <<
      0.0, -omega.z(), omega.y(),
      omega.z(), 0.0, -omega.x(),
      -omega.y(), omega.x(), 0.0).finished();
    const double half_theta = 0.5 * theta;
    const double scale = (1.0 - half_theta * std::sin(theta) / (1.0 - std::cos(theta))) /
      (theta * theta);
    rho = (Eigen::Matrix3d::Identity() - 0.5 * skew + scale * skew * skew) * pose.t;
  }
  Eigen::Matrix<double, 6, 1> xi;
  xi.head<3>() = omega;
  xi.tail<3>() = rho;
  return xi;
}

Pose pose_right_update(const Pose & pose, const Eigen::Matrix<double, 6, 1> & xi)
{
  return pose_compose(pose, pose_exp(xi));
}

bool optimize_pose_graph(
  std::vector<Pose> & poses,
  const std::vector<BetweenFactor> & factors,
  int max_iterations,
  double huber_delta)
{
  if (poses.size() < 2 || factors.empty()) {
    return true;
  }
  const int free_nodes = static_cast<int>(poses.size()) - 1;
  const int dimension = 6 * free_nodes;
  const double epsilon = 1e-6;

  for (int iteration = 0; iteration < max_iterations; ++iteration) {
    Eigen::MatrixXd hessian = Eigen::MatrixXd::Zero(dimension, dimension);
    Eigen::VectorXd gradient = Eigen::VectorXd::Zero(dimension);
    double cost = 0.0;

    for (const BetweenFactor & factor : factors) {
      if (factor.from < 0 || factor.to < 0 ||
        factor.from >= static_cast<int>(poses.size()) ||
        factor.to >= static_cast<int>(poses.size()) ||
        factor.from == factor.to)
      {
        continue;
      }
      const Eigen::Matrix<double, 6, 1> error = factor_error(
        poses[static_cast<std::size_t>(factor.from)],
        poses[static_cast<std::size_t>(factor.to)],
        factor.measurement);
      Eigen::Matrix<double, 6, 6> weight = Eigen::Matrix<double, 6, 6>::Zero();
      weight.block<3, 3>(0, 0).setIdentity();
      weight.block<3, 3>(0, 0) *= factor.rotation_weight;
      weight.block<3, 3>(3, 3).setIdentity();
      weight.block<3, 3>(3, 3) *= factor.translation_weight;

      double robust_scale = 1.0;
      const double residual_norm = std::sqrt(error.dot(weight * error));
      if (factor.robust && residual_norm > huber_delta && residual_norm > 1e-9) {
        robust_scale = huber_delta / residual_norm;
      }
      weight *= robust_scale;
      cost += error.dot(weight * error);

      Eigen::MatrixXd jacobian = Eigen::MatrixXd::Zero(6, dimension);
      for (const int node : {factor.from, factor.to}) {
        if (node == 0) {
          continue;
        }
        const int column = 6 * (node - 1);
        for (int k = 0; k < 6; ++k) {
          Eigen::Matrix<double, 6, 1> step = Eigen::Matrix<double, 6, 1>::Zero();
          step(k) = epsilon;
          std::vector<Pose> perturbed = poses;
          perturbed[static_cast<std::size_t>(node)] = pose_right_update(
            poses[static_cast<std::size_t>(node)], step);
          const Eigen::Matrix<double, 6, 1> perturbed_error = factor_error(
            perturbed[static_cast<std::size_t>(factor.from)],
            perturbed[static_cast<std::size_t>(factor.to)],
            factor.measurement);
          jacobian.col(column + k) = (perturbed_error - error) / epsilon;
        }
      }
      hessian += jacobian.transpose() * weight * jacobian;
      gradient += jacobian.transpose() * weight * error;
    }

    hessian.diagonal().array() += 1e-6;
    const Eigen::VectorXd step = hessian.ldlt().solve(-gradient);
    if (!step.allFinite()) {
      return false;
    }
    if (step.norm() < 1e-8) {
      return true;
    }
    for (int node = 1; node < static_cast<int>(poses.size()); ++node) {
      poses[static_cast<std::size_t>(node)] = pose_right_update(
        poses[static_cast<std::size_t>(node)], step.segment<6>(6 * (node - 1)));
    }
    (void)cost;
    (void)iteration;
  }
  // Still moving after the last iteration: the solve did not converge.
  return false;
}

}  // namespace go2_slam_backend
