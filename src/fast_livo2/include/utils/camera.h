// Camera models for the visual front end. Replaces the parts of rpg_vikit
// (vk::AbstractCamera, vk::PinholeCamera) that FAST-LIVO2 uses.
#ifndef LIVO_CAMERA_H
#define LIVO_CAMERA_H

#include <Eigen/Core>
#include <opencv2/core.hpp>

#include <array>
#include <memory>
#include <string>

namespace livo
{

class Camera
{
public:
  Camera(int width, int height, double scale) : width_(width), height_(height), scale_(scale) {}
  virtual ~Camera() = default;

  /// Pixel to unit-length bearing vector in the camera frame.
  virtual Eigen::Vector3d cam2world(double u, double v) const = 0;
  Eigen::Vector3d cam2world(const Eigen::Vector2d &px) const { return cam2world(px.x(), px.y()); }

  /// Camera-frame point to pixel.
  virtual Eigen::Vector2d world2cam(const Eigen::Vector3d &xyz) const = 0;

  virtual double fx() const = 0;
  virtual double fy() const = 0;
  virtual double cx() const = 0;
  virtual double cy() const = 0;

  int width() const { return width_; }
  int height() const { return height_; }
  /// Factor between the configured resolution and the one used internally.
  double scale() const { return scale_; }

  bool isInFrame(const Eigen::Vector2i &px, int border = 0) const
  {
    return px.x() >= border && px.x() < width_ - border && px.y() >= border && px.y() < height_ - border;
  }

protected:
  int width_;
  int height_;
  double scale_;
};

/// Pinhole projection with OpenCV radial-tangential distortion
/// (k1, k2, p1, p2, k3). Intrinsics and resolution are given at the native
/// resolution and multiplied by scale.
class PinholeCamera : public Camera
{
public:
  PinholeCamera(int width, int height, double scale, double fx, double fy, double cx, double cy,
                const std::array<double, 5> &distortion = {0.0, 0.0, 0.0, 0.0, 0.0});

  Eigen::Vector3d cam2world(double u, double v) const override;
  Eigen::Vector2d world2cam(const Eigen::Vector3d &xyz) const override;

  /// Normalized image plane (x/z, y/z) to pixel.
  Eigen::Vector2d planeToPixel(const Eigen::Vector2d &xy) const;
  /// Pixel to normalized image plane, inverting the distortion iteratively.
  Eigen::Vector2d pixelToPlane(double u, double v) const;
  /// Derivative of distort(xy) with respect to xy. Identity without distortion.
  Eigen::Matrix2d distortionJacobian(const Eigen::Vector2d &xy) const;

  void undistortImage(const cv::Mat &raw, cv::Mat &rectified) const;

  double fx() const override { return fx_; }
  double fy() const override { return fy_; }
  double cx() const override { return cx_; }
  double cy() const override { return cy_; }
  bool distorted() const { return distorted_; }

private:
  Eigen::Vector2d distort(const Eigen::Vector2d &xy) const;

  double fx_, fy_, cx_, cy_;
  std::array<double, 5> d_;
  bool distorted_;
  cv::Mat map1_, map2_;
};

struct CameraConfig
{
  std::string model{"Pinhole"};
  int width{0};
  int height{0};
  double scale{1.0};
  double fx{0.0}, fy{0.0}, cx{0.0}, cy{0.0};
  std::array<double, 5> distortion{0.0, 0.0, 0.0, 0.0, 0.0};
};

/// Throws std::invalid_argument for an unknown model or bad intrinsics.
std::unique_ptr<Camera> makeCamera(const CameraConfig &config);

} // namespace livo

#endif
