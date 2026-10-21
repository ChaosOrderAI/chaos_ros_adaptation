// The lossless image sink of the export.
//
// The pixels are the original bytes of the ROS image: the row padding of the
// message is not part of the array, the byte order of the message is kept and
// no pixel is converted, resized or normalised. The array is written in the
// NumPy format version 1.0, so a consumer reads it with
// numpy.load(..., allow_pickle=False).
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include <sensor_msgs/msg/image.hpp>

namespace chaos::data {

// The pixel layout of an image encoding.
struct ImageLayout {
  // False when the encoding is not supported.
  bool supported = false;
  // The number of channels of one pixel.
  std::size_t channels = 0;
  // The size of one channel in bytes.
  std::size_t itemsize = 0;
  // The NumPy type code of one channel: "u1", "u2" or "f4".
  std::string code;
};

// Writes images as NumPy array files.
class NpyWriter {
public:
  // The layout of an encoding: rgb8, bgr8, mono8, 16UC1 and 32FC1.
  static ImageLayout layout(const std::string & encoding);

  // Writes the pixels of the image.
  //
  // The file is created exclusively, so an existing file is never replaced.
  // The header declares the byte order of the message, so a big endian
  // image stays big endian in the array.
  static void write(const std::filesystem::path & path,
                    const sensor_msgs::msg::Image & image);
};

}  // namespace chaos::data
