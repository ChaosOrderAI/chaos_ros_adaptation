#include "utils/camera.h"

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <stdexcept>

namespace livo
{

PinholeCamera::PinholeCamera(int width, int height, double scale, double fx, double fy, double cx, double cy,
                             const std::array<double, 5> &distortion)
    : Camera(static_cast<int>(width * scale), static_cast<int>(height * scale), scale),
      fx_(fx * scale), fy_(fy * scale), cx_(cx * scale), cy_(cy * scale), d_(distortion), distorted_(false)
{
  for (double d : d_) distorted_ = distorted_ || std::abs(d) > 1e-12;
  if (distorted_)
  {
    const cv::Matx33d K(fx_, 0.0, cx_, 0.0, fy_, cy_, 0.0, 0.0, 1.0);
    const cv::Vec<double, 5> D(d_[0], d_[1], d_[2], d_[3], d_[4]);
    cv::initUndistortRectifyMap(K, D, cv::noArray(), K, cv::Size(width_, height_), CV_16SC2, map1_, map2_);
  }
}

Eigen::Vector2d PinholeCamera::distort(const Eigen::Vector2d &xy) const
{
  const double x = xy.x(), y = xy.y();
  const double r2 = x * x + y * y;
  const double radial = 1.0 + r2 * (d_[0] + r2 * (d_[1] + r2 * d_[4]));
  return {x * radial + 2.0 * d_[2] * x * y + d_[3] * (r2 + 2.0 * x * x),
          y * radial + d_[2] * (r2 + 2.0 * y * y) + 2.0 * d_[3] * x * y};
}

Eigen::Vector2d PinholeCamera::planeToPixel(const Eigen::Vector2d &xy) const
{
  const Eigen::Vector2d d = distorted_ ? distort(xy) : xy;
  return {fx_ * d.x() + cx_, fy_ * d.y() + cy_};
}

Eigen::Matrix2d PinholeCamera::distortionJacobian(const Eigen::Vector2d &xy) const
{
  Eigen::Matrix2d jacobian = Eigen::Matrix2d::Identity();
  if (!distorted_) return jacobian;
  const double x = xy.x();
  const double y = xy.y();
  const double r2 = x * x + y * y;
  const double k1 = d_[0], k2 = d_[1], p1 = d_[2], p2 = d_[3], k3 = d_[4];
  // radial = 1 + k1 r2 + k2 r2^2 + k3 r2^3, s = d(radial)/d(r2)
  const double radial = 1.0 + r2 * (k1 + r2 * (k2 + r2 * k3));
  const double s = k1 + 2.0 * k2 * r2 + 3.0 * k3 * r2 * r2;
  const double cross = 2.0 * x * y * s + 2.0 * p1 * x + 2.0 * p2 * y;
  jacobian(0, 0) = radial + 2.0 * x * x * s + 2.0 * p1 * y + 6.0 * p2 * x;
  jacobian(0, 1) = cross;
  jacobian(1, 0) = cross;
  jacobian(1, 1) = radial + 2.0 * y * y * s + 6.0 * p1 * y + 2.0 * p2 * x;
  return jacobian;
}

Eigen::Vector2d PinholeCamera::pixelToPlane(double u, double v) const
{
  const Eigen::Vector2d target((u - cx_) / fx_, (v - cy_) / fy_);
  if (!distorted_) return target;
  // Fixed-point iteration: xy = target - (distort(xy) - xy).
  Eigen::Vector2d xy = target;
  for (int i = 0; i < 20; ++i)
  {
    const Eigen::Vector2d next = target - (distort(xy) - xy);
    const double step = (next - xy).squaredNorm();
    xy = next;
    if (step < 1e-24) break;
  }
  return xy;
}

Eigen::Vector3d PinholeCamera::cam2world(double u, double v) const
{
  const Eigen::Vector2d xy = pixelToPlane(u, v);
  return Eigen::Vector3d(xy.x(), xy.y(), 1.0).normalized();
}

Eigen::Vector2d PinholeCamera::world2cam(const Eigen::Vector3d &xyz) const
{
  return planeToPixel(Eigen::Vector2d(xyz.x() / xyz.z(), xyz.y() / xyz.z()));
}

void PinholeCamera::undistortImage(const cv::Mat &raw, cv::Mat &rectified) const
{
  if (distorted_) cv::remap(raw, rectified, map1_, map2_, cv::INTER_LINEAR);
  else rectified = raw.clone();
}

std::unique_ptr<Camera> makeCamera(const CameraConfig &c)
{
  if (c.model != "Pinhole") throw std::invalid_argument("Unsupported cam_model '" + c.model + "'. Only Pinhole is implemented.");
  if (c.width <= 0 || c.height <= 0 || c.scale <= 0.0 || c.fx <= 0.0 || c.fy <= 0.0)
    throw std::invalid_argument("Camera needs positive cam_width, cam_height, scale, cam_fx and cam_fy.");
  return std::make_unique<PinholeCamera>(c.width, c.height, c.scale, c.fx, c.fy, c.cx, c.cy, c.distortion);
}

} // namespace livo
