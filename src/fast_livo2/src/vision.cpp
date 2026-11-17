#include "utils/vision.h"

#include <stdexcept>

namespace livo
{

void halfSample(const cv::Mat &in, cv::Mat &out)
{
  if (in.type() != CV_8UC1 || out.type() != CV_8UC1 || out.rows != in.rows / 2 || out.cols != in.cols / 2)
    throw std::invalid_argument("halfSample expects CV_8UC1 input and a half-size CV_8UC1 output");
  for (int r = 0; r < out.rows; ++r)
  {
    const uint8_t *top = in.ptr<uint8_t>(2 * r);
    const uint8_t *bottom = in.ptr<uint8_t>(2 * r + 1);
    uint8_t *dst = out.ptr<uint8_t>(r);
    for (int c = 0; c < out.cols; ++c)
    {
      const int sum = top[2 * c] + top[2 * c + 1] + bottom[2 * c] + bottom[2 * c + 1];
      dst[c] = static_cast<uint8_t>(sum / 4);
    }
  }
}

float shiTomasiScore(const cv::Mat &img, int u, int v)
{
  constexpr int half = 4;
  constexpr int size = 2 * half;
  if (u - half < 1 || u + half >= img.cols - 1 || v - half < 1 || v + half >= img.rows - 1) return 0.0f;

  double gxx = 0.0, gyy = 0.0, gxy = 0.0;
  for (int y = v - half; y < v + half; ++y)
  {
    const uint8_t *row = img.ptr<uint8_t>(y);
    const uint8_t *up = img.ptr<uint8_t>(y - 1);
    const uint8_t *down = img.ptr<uint8_t>(y + 1);
    for (int x = u - half; x < u + half; ++x)
    {
      const double gx = static_cast<double>(row[x + 1]) - row[x - 1];
      const double gy = static_cast<double>(down[x]) - up[x];
      gxx += gx * gx;
      gyy += gy * gy;
      gxy += gx * gy;
    }
  }
  const double norm = 2.0 * size * size;
  gxx /= norm;
  gyy /= norm;
  gxy /= norm;
  const double half_trace = 0.5 * (gxx + gyy);
  const double diff = 0.5 * (gxx - gyy);
  return static_cast<float>(half_trace - std::sqrt(diff * diff + gxy * gxy));
}

} // namespace livo
