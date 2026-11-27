// Project world-frame 3D LiDAR points onto a thermal image using interpolated
// IMU odometry poses and output RGB + thermal-intensity colored 3D points.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace chaos_sensors
{

struct TimedPose
{
  double stamp_sec{0.0};
  double qx{0.0};
  double qy{0.0};
  double qz{0.0};
  double qw{1.0};
  cv::Vec3d pos{0.0, 0.0, 0.0};
};

struct ThermalProjectorConfig
{
  int width{256};
  int height{192};
  double fx{266.7};
  double fy{266.7};
  double cx{128.0};
  double cy{96.0};
  double distortion[5]{0.0, 0.0, 0.0, 0.0, 0.0};  // k1, k2, p1, p2, k3

  // LiDAR in IMU frame: p_imu = ext_R * p_lidar + ext_T (same as fast_livo2).
  cv::Matx33d ext_R{cv::Matx33d::eye()};
  cv::Vec3d ext_T{-0.011, -0.02329, 0.04412};

  // LiDAR to thermal optical frame: p_ir = Rcl * p_lidar + Pcl (same as fast_livo2).
  cv::Matx33d Rcl{
    0.0, -1.0, 0.0,
    0.0, 0.0, -1.0,
    1.0, 0.0, 0.0};
  cv::Vec3d Pcl{0.0, 0.0, 0.0};

  double min_depth{0.30};
  double max_depth{50.0};
  int border_margin{1};
};

struct ColoredThermalPoint
{
  float x;
  float y;
  float z;
  float rgb;
  float intensity;
};
static_assert(sizeof(ColoredThermalPoint) == 20, "ColoredThermalPoint must be tightly packed");

inline float packRgbFloat(uint8_t r, uint8_t g, uint8_t b)
{
  const uint32_t packed =
    (static_cast<uint32_t>(r) << 16) |
    (static_cast<uint32_t>(g) << 8) |
    static_cast<uint32_t>(b);
  float out = 0.0f;
  std::memcpy(&out, &packed, sizeof(float));
  return out;
}

inline cv::Matx33d quatToRotMat(double qx, double qy, double qz, double qw)
{
  const double n = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
  if (n < 1e-12) {
    return cv::Matx33d::eye();
  }
  qx /= n;
  qy /= n;
  qz /= n;
  qw /= n;

  const double xx = qx * qx;
  const double yy = qy * qy;
  const double zz = qz * qz;
  const double xy = qx * qy;
  const double xz = qx * qz;
  const double yz = qy * qz;
  const double wx = qw * qx;
  const double wy = qw * qy;
  const double wz = qw * qz;

  return cv::Matx33d(
    1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz), 2.0 * (xz + wy),
    2.0 * (xy + wz), 1.0 - 2.0 * (xx + zz), 2.0 * (yz - wx),
    2.0 * (xz - wy), 2.0 * (yz + wx), 1.0 - 2.0 * (xx + yy));
}

inline bool interpolatePose(
  const std::deque<TimedPose> & poses,
  double query_sec,
  double max_extrap_sec,
  TimedPose & out)
{
  if (poses.empty()) {
    return false;
  }
  if (query_sec <= poses.front().stamp_sec) {
    if (poses.front().stamp_sec - query_sec > max_extrap_sec) {
      return false;
    }
    out = poses.front();
    out.stamp_sec = query_sec;
    return true;
  }
  if (query_sec >= poses.back().stamp_sec) {
    if (query_sec - poses.back().stamp_sec > max_extrap_sec) {
      return false;
    }
    out = poses.back();
    out.stamp_sec = query_sec;
    return true;
  }

  auto it = std::lower_bound(
    poses.begin(), poses.end(), query_sec,
    [](const TimedPose & p, double t) {return p.stamp_sec < t;});
  if (it == poses.begin()) {
    out = *it;
    return true;
  }
  const TimedPose & p1 = *it;
  const TimedPose & p0 = *(it - 1);
  const double dt = p1.stamp_sec - p0.stamp_sec;
  if (dt <= 1e-9) {
    out = p1;
    return true;
  }
  const double alpha = std::clamp((query_sec - p0.stamp_sec) / dt, 0.0, 1.0);

  out.stamp_sec = query_sec;
  out.pos = p0.pos * (1.0 - alpha) + p1.pos * alpha;

  // Shortest-path quaternion SLERP.
  double q1x = p1.qx, q1y = p1.qy, q1z = p1.qz, q1w = p1.qw;
  double dot = p0.qx * q1x + p0.qy * q1y + p0.qz * q1z + p0.qw * q1w;
  if (dot < 0.0) {
    dot = -dot;
    q1x = -q1x;
    q1y = -q1y;
    q1z = -q1z;
    q1w = -q1w;
  }

  double s0 = 1.0 - alpha;
  double s1 = alpha;
  if (dot < 0.9995) {
    const double theta = std::acos(std::clamp(dot, -1.0, 1.0));
    const double sin_theta = std::sin(theta);
    if (sin_theta > 1e-9) {
      s0 = std::sin((1.0 - alpha) * theta) / sin_theta;
      s1 = std::sin(alpha * theta) / sin_theta;
    }
  }
  out.qx = s0 * p0.qx + s1 * q1x;
  out.qy = s0 * p0.qy + s1 * q1y;
  out.qz = s0 * p0.qz + s1 * q1z;
  out.qw = s0 * p0.qw + s1 * q1w;
  const double norm = std::sqrt(
    out.qx * out.qx + out.qy * out.qy + out.qz * out.qz + out.qw * out.qw);
  if (norm > 1e-12) {
    out.qx /= norm;
    out.qy /= norm;
    out.qz /= norm;
    out.qw /= norm;
  }
  return true;
}

inline void projectWorldCloudToThermal(
  const std::vector<cv::Vec3f> & world_points,
  const TimedPose & pose_at_thermal,
  const ThermalProjectorConfig & cfg,
  const cv::Mat & color_bgr,
  const cv::Mat & intensity_u8,
  std::vector<ColoredThermalPoint> & out_points)
{
  out_points.clear();
  if (world_points.empty() || color_bgr.empty() || color_bgr.type() != CV_8UC3) {
    return;
  }
  const int img_w = color_bgr.cols;
  const int img_h = color_bgr.rows;
  if (img_w <= 2 || img_h <= 2) {
    return;
  }
  const bool has_intensity =
    !intensity_u8.empty() && intensity_u8.type() == CV_8UC1 &&
    intensity_u8.cols == img_w && intensity_u8.rows == img_h;

  // Scale intrinsics transparently if the runtime image size differs from cfg.
  const double sx = (cfg.width > 0) ? (static_cast<double>(img_w) / cfg.width) : 1.0;
  const double sy = (cfg.height > 0) ? (static_cast<double>(img_h) / cfg.height) : 1.0;
  const double fx = cfg.fx * sx;
  const double fy = cfg.fy * sy;
  const double cx = cfg.cx * sx;
  const double cy = cfg.cy * sy;

  // Compose world -> IMU(t_ir) -> LiDAR -> thermal optical frame once per scan.
  const cv::Matx33d R_wi = quatToRotMat(
    pose_at_thermal.qx, pose_at_thermal.qy, pose_at_thermal.qz, pose_at_thermal.qw);
  const cv::Matx33d R_iw = R_wi.t();
  const cv::Vec3d t_iw = -(R_iw * pose_at_thermal.pos);

  const cv::Matx33d R_ci = cfg.Rcl * cfg.ext_R.t();
  const cv::Vec3d t_ci = cfg.Pcl - R_ci * cfg.ext_T;

  const cv::Matx33d R_cw = R_ci * R_iw;
  const cv::Vec3d t_cw = R_ci * t_iw + t_ci;

  const int margin = std::max(0, cfg.border_margin);
  const double min_u = static_cast<double>(margin);
  const double min_v = static_cast<double>(margin);
  const double max_u = static_cast<double>(img_w - 1 - margin) - 1e-4;
  const double max_v = static_cast<double>(img_h - 1 - margin) - 1e-4;

  const double d0 = cfg.distortion[0];
  const double d1 = cfg.distortion[1];
  const double d2 = cfg.distortion[2];
  const double d3 = cfg.distortion[3];
  const double d4 = cfg.distortion[4];
  const bool has_dist =
    (std::abs(d0) + std::abs(d1) + std::abs(d2) + std::abs(d3) + std::abs(d4)) > 1e-12;

  out_points.reserve(world_points.size() / 3);
  for (const auto & pw_f : world_points) {
    if (!std::isfinite(pw_f[0]) || !std::isfinite(pw_f[1]) || !std::isfinite(pw_f[2])) {
      continue;
    }
    const cv::Vec3d pw(pw_f[0], pw_f[1], pw_f[2]);
    const cv::Vec3d pc = R_cw * pw + t_cw;
    if (pc[2] < cfg.min_depth || pc[2] > cfg.max_depth) {
      continue;
    }

    const double xn = pc[0] / pc[2];
    const double yn = pc[1] / pc[2];
    double xd = xn;
    double yd = yn;
    if (has_dist) {
      const double r2 = xn * xn + yn * yn;
      const double r4 = r2 * r2;
      const double r6 = r4 * r2;
      const double radial = 1.0 + d0 * r2 + d1 * r4 + d4 * r6;
      xd = xn * radial + 2.0 * d2 * xn * yn + d3 * (r2 + 2.0 * xn * xn);
      yd = yn * radial + d2 * (r2 + 2.0 * yn * yn) + 2.0 * d3 * xn * yn;
    }

    const double u = fx * xd + cx;
    const double v = fy * yd + cy;
    if (u < min_u || u >= max_u || v < min_v || v >= max_v) {
      continue;
    }

    const int u0 = static_cast<int>(u);
    const int v0 = static_cast<int>(v);
    const float du = static_cast<float>(u - u0);
    const float dv = static_cast<float>(v - v0);
    const float w00 = (1.0f - du) * (1.0f - dv);
    const float w10 = du * (1.0f - dv);
    const float w01 = (1.0f - du) * dv;
    const float w11 = du * dv;

    const cv::Vec3b & c00 = color_bgr.at<cv::Vec3b>(v0, u0);
    const cv::Vec3b & c10 = color_bgr.at<cv::Vec3b>(v0, u0 + 1);
    const cv::Vec3b & c01 = color_bgr.at<cv::Vec3b>(v0 + 1, u0);
    const cv::Vec3b & c11 = color_bgr.at<cv::Vec3b>(v0 + 1, u0 + 1);

    const auto b = static_cast<uint8_t>(
      std::clamp(w00 * c00[0] + w10 * c10[0] + w01 * c01[0] + w11 * c11[0], 0.0f, 255.0f));
    const auto g = static_cast<uint8_t>(
      std::clamp(w00 * c00[1] + w10 * c10[1] + w01 * c01[1] + w11 * c11[1], 0.0f, 255.0f));
    const auto r = static_cast<uint8_t>(
      std::clamp(w00 * c00[2] + w10 * c10[2] + w01 * c01[2] + w11 * c11[2], 0.0f, 255.0f));

    float therm_val = 0.0f;
    if (has_intensity) {
      therm_val =
        w00 * intensity_u8.at<uint8_t>(v0, u0) +
        w10 * intensity_u8.at<uint8_t>(v0, u0 + 1) +
        w01 * intensity_u8.at<uint8_t>(v0 + 1, u0) +
        w11 * intensity_u8.at<uint8_t>(v0 + 1, u0 + 1);
    }

    out_points.push_back(
      ColoredThermalPoint{pw_f[0], pw_f[1], pw_f[2], packRgbFloat(r, g, b), therm_val});
  }
}

}  // namespace chaos_sensors
