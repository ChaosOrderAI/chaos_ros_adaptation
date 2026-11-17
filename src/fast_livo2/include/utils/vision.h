// Image helpers for the visual front end. Replaces the parts of rpg_vikit's
// vision.h that FAST-LIVO2 uses.
#ifndef LIVO_VISION_H
#define LIVO_VISION_H

#include <opencv2/core.hpp>

#include <cmath>
#include <cstdint>

namespace livo
{

/// Bilinear sample of an 8-bit single-channel image. The caller keeps
/// (u, v) at least one pixel inside the right and bottom border.
inline float interpolateMat_8u(const cv::Mat &mat, float u, float v)
{
  const int x = static_cast<int>(std::floor(u));
  const int y = static_cast<int>(std::floor(v));
  const float ax = u - x;
  const float ay = v - y;
  const std::size_t stride = mat.step[0];
  const uint8_t *p = mat.data + y * stride + x;
  return (1.0f - ax) * (1.0f - ay) * p[0] + ax * (1.0f - ay) * p[1] + (1.0f - ax) * ay * p[stride] + ax * ay * p[stride + 1];
}

/// 2x2 box average. out must already be (in.rows / 2) x (in.cols / 2), CV_8U.
void halfSample(const cv::Mat &in, cv::Mat &out);

/// Smaller eigenvalue of the gradient structure tensor over the 8x8 box
/// centred at (u, v). Zero when the box touches the image border.
float shiTomasiScore(const cv::Mat &img, int u, int v);

} // namespace livo

#endif
