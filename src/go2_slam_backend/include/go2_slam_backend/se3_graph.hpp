#ifndef GO2_SLAM_BACKEND__SE3_GRAPH_HPP_
#define GO2_SLAM_BACKEND__SE3_GRAPH_HPP_

#include <Eigen/Geometry>
#include <vector>

namespace go2_slam_backend
{

struct Pose
{
  Eigen::Quaterniond q{1.0, 0.0, 0.0, 0.0};
  Eigen::Vector3d t{Eigen::Vector3d::Zero()};
};

struct BetweenFactor
{
  int from{0};
  int to{0};
  Pose measurement;
  double rotation_weight{1.0};
  double translation_weight{1.0};
  bool robust{false};
};

Pose pose_inverse(const Pose & pose);
Pose pose_compose(const Pose & a, const Pose & b);
Pose pose_between(const Pose & from, const Pose & to);
Pose pose_exp(const Eigen::Matrix<double, 6, 1> & xi);
Eigen::Matrix<double, 6, 1> pose_log(const Pose & pose);
Pose pose_right_update(const Pose & pose, const Eigen::Matrix<double, 6, 1> & xi);

// Gauss-Newton on SE(3). Node 0 stays fixed. Loop factors may use a Huber weight.
// Returns false when the linear system cannot be solved.
bool optimize_pose_graph(
  std::vector<Pose> & poses,
  const std::vector<BetweenFactor> & factors,
  int max_iterations = 15,
  double huber_delta = 0.5);

}  // namespace go2_slam_backend

#endif  // GO2_SLAM_BACKEND__SE3_GRAPH_HPP_
