// Split the IRay AT600 dual-spectrum frame into its two halves.
//
// The module exposes a single UVC mode: 512x192 YUYV at 25 fps. The device
// itself paints two 256x192 pictures side by side -- a thermal image on the
// left and a visible-light image on the right, factory-registered to the same
// optical axis. This node takes the Y plane of the YUYV frame, cuts it at
// split_ratio and publishes each half as its own mono8 image topic.
//
// Capture goes through raw V4L2 rather than OpenCV so the frame carries the
// kernel's buffer timestamp (uvcvideo tags buffers CLOCK_MONOTONIC). That is
// the closest available proxy for the exposure moment on a free-running UVC
// module: the AT600 has no external trigger input, so it cannot take part in
// the sync board's hardware trigger chain. Stamp source is selectable:
//   v4l2       (default) buffer timestamp mapped onto ROS time
//   arrival    node time at dequeue (the old behaviour, for comparison)
// exposure_offset_us shifts the v4l2 stamp by a constant (e.g. half the
// exposure) once it is measured.
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <cv_bridge/cv_bridge.hpp>
#include <opencv2/imgproc.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

namespace chaos_sensors
{

class At600SplitNode : public rclcpp::Node
{
public:
  At600SplitNode()
  : rclcpp::Node("at600_split_node")
  {
    device_ = declare_parameter<std::string>(
      "device", "/dev/v4l/by-id/usb-IRay_AT600-video-index0");
    width_ = declare_parameter<int>("width", 512);
    height_ = declare_parameter<int>("height", 192);
    fps_ = declare_parameter<int>("fps", 25);
    split_ratio_ = declare_parameter<double>("split_ratio", 0.5);
    thermal_topic_ = declare_parameter<std::string>("thermal_topic", "thermal/image_raw");
    visible_topic_ = declare_parameter<std::string>("visible_topic", "visible/image_raw");
    thermal_frame_id_ = declare_parameter<std::string>("thermal_frame_id", "thermal_frame");
    visible_frame_id_ = declare_parameter<std::string>("visible_frame_id", "camera_front");
    publish_visible_ = declare_parameter<bool>("publish_visible", true);
    buffer_count_ = declare_parameter<int>("buffer_count", 4);
    stamp_source_ = declare_parameter<std::string>("stamp_source", "v4l2");
    exposure_offset_us_ = declare_parameter<int64_t>("exposure_offset_us", 0);

    const auto qos = rclcpp::SensorDataQoS();
    if (!thermal_topic_.empty()) {
      thermal_pub_ = create_publisher<sensor_msgs::msg::Image>(thermal_topic_, qos);
    }
    if (publish_visible_ && !visible_topic_.empty()) {
      visible_pub_ = create_publisher<sensor_msgs::msg::Image>(visible_topic_, qos);
    }

    if (!openDevice()) {
      // openDevice may have failed after the fd, mappings or stream were set
      // up; release them before the constructor throws (the destructor does
      // not run for a partially constructed object).
      closeDevice();
      throw std::runtime_error("cannot open video device " + device_);
    }
    RCLCPP_INFO(
      get_logger(), "AT600 %s: %dx%d @ %d fps (%u buffers, stamp=%s) -> thermal '%s', visible '%s'",
      device_.c_str(), width_, height_, fps_, static_cast<unsigned>(buffer_count_),
      stamp_source_.c_str(), thermal_topic_.c_str(), visible_topic_.c_str());

    capture_thread_ = std::thread(&At600SplitNode::captureLoop, this);
  }

  ~At600SplitNode() override
  {
    stop_ = true;
    if (capture_thread_.joinable()) {
      capture_thread_.join();
    }
    closeDevice();
  }

private:
  bool openDevice()
  {
    fd_ = ::open(device_.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
      RCLCPP_ERROR(get_logger(), "open %s: %s", device_.c_str(), std::strerror(errno));
      return false;
    }

    v4l2_capability cap{};
    if (::ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0 ||
      !(cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) ||
      !(cap.capabilities & V4L2_CAP_STREAMING))
    {
      RCLCPP_ERROR(get_logger(), "%s is not a streaming capture device", device_.c_str());
      return false;
    }

    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = static_cast<uint32_t>(width_);
    fmt.fmt.pix.height = static_cast<uint32_t>(height_);
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (::ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
      RCLCPP_ERROR(get_logger(), "VIDIOC_S_FMT: %s", std::strerror(errno));
      return false;
    }
    width_ = static_cast<int>(fmt.fmt.pix.width);
    height_ = static_cast<int>(fmt.fmt.pix.height);
    bytes_per_line_ = fmt.fmt.pix.bytesperline;
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
      RCLCPP_ERROR(get_logger(), "device refused YUYV");
      return false;
    }

    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = static_cast<uint32_t>(std::max(1, fps_));
    ::ioctl(fd_, VIDIOC_S_PARM, &parm);  // best effort

    v4l2_requestbuffers req{};
    req.count = static_cast<uint32_t>(std::max(2, buffer_count_));
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (::ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
      RCLCPP_ERROR(get_logger(), "VIDIOC_REQBUFS: %s", std::strerror(errno));
      return false;
    }

    buffers_.resize(req.count);
    lengths_.resize(req.count);
    for (uint32_t i = 0; i < req.count; ++i) {
      v4l2_buffer buf{};
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = i;
      if (::ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
        RCLCPP_ERROR(get_logger(), "VIDIOC_QUERYBUF %u: %s", i, std::strerror(errno));
        return false;
      }
      lengths_[i] = buf.length;
      buffers_[i] = ::mmap(
        nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);
      if (buffers_[i] == MAP_FAILED) {
        RCLCPP_ERROR(get_logger(), "mmap %u: %s", i, std::strerror(errno));
        return false;
      }
      if (::ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        RCLCPP_ERROR(get_logger(), "VIDIOC_QBUF %u: %s", i, std::strerror(errno));
        return false;
      }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (::ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
      RCLCPP_ERROR(get_logger(), "VIDIOC_STREAMON: %s", std::strerror(errno));
      return false;
    }
    streaming_ = true;
    return true;
  }

  void closeDevice()
  {
    if (fd_ < 0) {
      return;
    }
    if (streaming_) {
      v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      ::ioctl(fd_, VIDIOC_STREAMOFF, &type);
      streaming_ = false;
    }
    for (size_t i = 0; i < buffers_.size(); ++i) {
      if (buffers_[i] != MAP_FAILED && buffers_[i] != nullptr) {
        ::munmap(buffers_[i], lengths_[i]);
      }
    }
    buffers_.clear();
    lengths_.clear();
    ::close(fd_);
    fd_ = -1;
  }

  void captureLoop()
  {
    cv::Mat mono;
    while (!stop_ && rclcpp::ok()) {
      pollfd pfd{fd_, POLLIN, 0};
      const int ready = ::poll(&pfd, 1, 200);
      if (ready < 0 && errno == EINTR) {
        continue;
      }
      if (ready <= 0 || !(pfd.revents & POLLIN)) {
        continue;
      }

      v4l2_buffer buf{};
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      if (::ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        if (errno != EAGAIN) {
          RCLCPP_WARN_THROTTLE(
            get_logger(), *get_clock(), 2000, "VIDIOC_DQBUF: %s", std::strerror(errno));
        }
        continue;
      }
      if (buf.index < buffers_.size()) {
        processFrame(buf, mono);
      }
      if (::ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 2000, "VIDIOC_QBUF: %s", std::strerror(errno));
      }
    }
  }

  void processFrame(const v4l2_buffer & buf, cv::Mat & mono)
  {
    // YUYV packs Y at the even byte of each pair; extractChannel keeps the
    // thermal half bit-exact and reuses mono's buffer across frames.
    const cv::Mat packed(
      height_, width_, CV_8UC2,
      reinterpret_cast<void *>(static_cast<uint8_t *>(buffers_[buf.index])),
      bytes_per_line_);
    cv::extractChannel(packed, mono, 0);
    if (mono.cols < 2) {
      return;
    }

    const int split = std::clamp(
      static_cast<int>(std::lround(mono.cols * split_ratio_)), 1, mono.cols - 1);
    const cv::Mat thermal = mono(cv::Rect(0, 0, split, mono.rows));
    const cv::Mat visible = mono(cv::Rect(split, 0, mono.cols - split, mono.rows));

    const rclcpp::Time stamp = frameStamp(buf);
    if (thermal_pub_ && thermal_pub_->get_subscription_count() > 0) {
      publish(thermal_pub_, thermal, thermal_frame_id_, stamp);
    }
    if (visible_pub_ && visible_pub_->get_subscription_count() > 0) {
      publish(visible_pub_, visible, visible_frame_id_, stamp);
    }
  }

  rclcpp::Time frameStamp(const v4l2_buffer & buf) const
  {
    if (stamp_source_ == "arrival") {
      return now();
    }
    const auto & tv = buf.timestamp;
    const int64_t ns = static_cast<int64_t>(tv.tv_sec) * 1000000000LL +
      static_cast<int64_t>(tv.tv_usec) * 1000LL;
    rclcpp::Time stamp;
    if ((buf.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC) {
      // uvcvideo stamps buffers on CLOCK_MONOTONIC. Anchor that to the host
      // wall clock (not the ROS clock, which may be simulated) with a single
      // sample pair, so the frame keeps its kernel timestamp.
      const int64_t steady_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
      const int64_t system_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
      stamp = rclcpp::Time(system_ns + (ns - steady_ns));
    } else {
      stamp = rclcpp::Time(ns);
    }
    if (exposure_offset_us_ != 0) {
      stamp = stamp + rclcpp::Duration::from_nanoseconds(exposure_offset_us_ * 1000);
    }
    return stamp;
  }

  void publish(
    const rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr & pub,
    const cv::Mat & image, const std::string & frame_id, const rclcpp::Time & stamp)
  {
    std_msgs::msg::Header header;
    header.stamp = stamp;
    header.frame_id = frame_id;
    pub->publish(*cv_bridge::CvImage(header, "mono8", image).toImageMsg());
  }

  std::string device_;
  int width_{512};
  int height_{192};
  int fps_{25};
  double split_ratio_{0.5};
  std::string thermal_topic_;
  std::string visible_topic_;
  std::string thermal_frame_id_;
  std::string visible_frame_id_;
  bool publish_visible_{true};
  int buffer_count_{4};
  std::string stamp_source_;
  int64_t exposure_offset_us_{0};

  int fd_{-1};
  bool streaming_{false};
  uint32_t bytes_per_line_{0};
  std::vector<void *> buffers_;
  std::vector<size_t> lengths_;
  std::atomic<bool> stop_{false};
  std::thread capture_thread_;

  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr thermal_pub_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr visible_pub_;
};

}  // namespace chaos_sensors

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<chaos_sensors::At600SplitNode>());
  } catch (const std::exception & err) {
    RCLCPP_FATAL(rclcpp::get_logger("at600_split_node"), "%s", err.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
