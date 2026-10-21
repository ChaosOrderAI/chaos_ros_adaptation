// The lossless image sink of the export.
#include "chaos_data/npy.hpp"

#include <array>
#include <cstdio>
#include <stdexcept>
#include <vector>

#include "chaos_data/export.hpp"
#include "chaos_data/file_io.hpp"

namespace chaos::data {

ImageLayout NpyWriter::layout(const std::string & encoding)
{
  ImageLayout result;
  if (encoding == "rgb8" || encoding == "bgr8") {
    result.supported = true;
    result.channels = 3;
    result.itemsize = 1;
    result.code = "u1";
  } else if (encoding == "mono8") {
    result.supported = true;
    result.channels = 1;
    result.itemsize = 1;
    result.code = "u1";
  } else if (encoding == "16UC1") {
    result.supported = true;
    result.channels = 1;
    result.itemsize = 2;
    result.code = "u2";
  } else if (encoding == "32FC1") {
    result.supported = true;
    result.channels = 1;
    result.itemsize = 4;
    result.code = "f4";
  }
  return result;
}

void NpyWriter::write(const std::filesystem::path & path,
                      const sensor_msgs::msg::Image & image)
{
  const ImageLayout pixels = NpyWriter::layout(image.encoding);
  if (!pixels.supported) {
    throw std::runtime_error("unsupported image encoding: " + image.encoding);
  }
  if (image.height == 0 || image.height > 32768 || image.width == 0 ||
      image.width > 32768) {
    throw std::runtime_error(
      "the image height and width must be in [1, 32768]");
  }
  if (image.is_bigendian != 0 && image.is_bigendian != 1) {
    throw std::runtime_error("the image endian flag is invalid");
  }
  // The row of the message may be padded, so it can be longer than its
  // pixels.
  const std::size_t row_bytes =
    static_cast<std::size_t>(image.width) * pixels.channels * pixels.itemsize;
  if (image.step < row_bytes) {
    throw std::runtime_error("the image row step is shorter than its pixels");
  }
  const std::size_t size =
    static_cast<std::size_t>(image.height) * static_cast<std::size_t>(image.step);
  if (size > kMaxMessageBytes || image.data.size() != size) {
    throw std::runtime_error(
      "the image payload size does not match height*step or exceeds 64 MiB");
  }

  // The header of the NumPy format version 1.0.
  const std::string descr =
    std::string(1, image.is_bigendian ? '>' : '<') + pixels.code;
  std::string shape = "(" + std::to_string(image.height) + ", " +
    std::to_string(image.width);
  if (pixels.channels > 1) {
    shape += ", " + std::to_string(pixels.channels);
  }
  shape += ")";
  std::string header = "{'descr': '" + descr +
    "', 'fortran_order': False, 'shape': " + shape + ", }";
  // The header is padded with spaces so that the data starts at a 64 byte
  // boundary, and the header ends with a newline.
  const std::size_t prefix = 10;
  std::size_t padding = 64 - ((prefix + header.size() + 1) % 64);
  if (padding == 64) {
    padding = 0;
  }
  header.append(padding, ' ');
  header.push_back('\n');

  ExclusiveFile file(path);
  const std::array<std::uint8_t, 6> magic = {0x93, 'N', 'U', 'M', 'P', 'Y'};
  file.write(reinterpret_cast<const char *>(magic.data()), magic.size());
  const std::array<char, 2> version = {1, 0};
  file.write(version.data(), version.size());
  const std::uint16_t header_length = static_cast<std::uint16_t>(header.size());
  const std::array<char, 2> length_bytes = {
    static_cast<char>(header_length & 0xffU),
    static_cast<char>((header_length >> 8) & 0xffU)};
  file.write(length_bytes.data(), length_bytes.size());
  file.write(header);
  // The row padding of the ROS image is not part of the array.
  for (std::uint32_t row = 0; row < image.height; ++row) {
    const char * begin = reinterpret_cast<const char *>(
      image.data.data() + static_cast<std::size_t>(row) * image.step);
    file.write(begin, row_bytes);
  }
  file.close();
}

}  // namespace chaos::data
