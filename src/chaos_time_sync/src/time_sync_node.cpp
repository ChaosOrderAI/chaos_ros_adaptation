// Host-side client for the InfiniteSense SimpleSensorSync board (V3/V4/MINI).
//
// The vendor SDK is not used: it is GPL-3.0, its colcon build is broken, and
// its Messenger hands raw pointers to callbacks. This node reimplements only
// the documented wire protocol, over UDP:
//
//   host -> board   {"f":"a","a":<host_epoch_us>}        every ptp_period_ms
//   board -> host   {"f":"a","a":<t1>,"b":<t2>}          echo + board rx time
//   board -> host   {"f":"b","a":<t3>}                   board tx time
//   host -> board   {"f":"b","a":<delay>,"b":<offset>}   NTP-style correction
//
//   board -> host   {"f":"t","t":<us>,"s":<mask>}        trigger edge(s)
//   board -> host   {"f":"imu","t":<us>,"d":[ax,ay,az,gx,gy,gz,temp],"q":[w,x,y,z]}
//   board -> host   {"f":"GNGGA"|"GNRMC","d":<nmea>,"pps":<us>,"t":<us>}
//   board -> host   {"f":"log","l":<level>,"msg":<text>}
//
// Trigger mask bits (infinite_sense_core/include/config.h): 0 onboard IMU,
// 1 external IMU, 2..5 CAM1..CAM4, 6 laser, 7 GPS.
//
// The board does not keep time by itself: its UTC comes from the host through
// the exchange above, so every stamp this node emits inherits the host clock
// quality. Enable chrony/NTP on the host.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include <chaos_interfaces/msg/board_trigger.hpp>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <diagnostic_msgs/msg/key_value.hpp>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/imu.hpp>

namespace chaos_time_sync
{

namespace
{
using json = nlohmann::json;
using SteadyClock = std::chrono::steady_clock;

uint64_t hostUs()
{
  return static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}

rclcpp::Time boardTime(uint64_t board_us)
{
  return rclcpp::Time(static_cast<int64_t>(board_us * 1000ULL));
}
}  // namespace

class TimeSyncNode : public rclcpp::Node
{
public:
  TimeSyncNode()
  : rclcpp::Node("chaos_time_sync")
  {
    board_ip_ = declare_parameter<std::string>("board_ip", "192.168.1.188");
    board_port_ = declare_parameter<int>("board_port", 8888);
    local_port_ = declare_parameter<int>("local_port", 0);
    ptp_period_ms_ = declare_parameter<int>("ptp_period_ms", 100);
    publish_imu_ = declare_parameter<bool>("publish_imu", true);
    imu_topic_ = declare_parameter<std::string>("imu_topic", "board/imu");
    trigger_topic_ = declare_parameter<std::string>("trigger_topic", "board/trigger");
    diagnostics_topic_ = declare_parameter<std::string>("diagnostics_topic", "board/diagnostics");
    trigger_frame_id_ = declare_parameter<std::string>("trigger_frame_id", "board");
    imu_frame_id_ = declare_parameter<std::string>("imu_frame_id", "board_imu");
    stale_after_ms_ = declare_parameter<int>("stale_after_ms", 2000);
    clock_warn_us_ = declare_parameter<int64_t>("clock_warn_us", 1000000);

    trigger_pub_ = create_publisher<chaos_interfaces::msg::BoardTrigger>(
      trigger_topic_, rclcpp::QoS(100));
    imu_pub_ = create_publisher<sensor_msgs::msg::Imu>(
      imu_topic_, rclcpp::SensorDataQoS());
    diag_pub_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
      diagnostics_topic_, rclcpp::QoS(10));

    if (!openSocket()) {
      throw std::runtime_error("cannot open UDP socket for " + board_ip_);
    }

    RCLCPP_INFO(
      get_logger(), "board %s:%d, PTP every %d ms, trigger '%s', imu '%s'",
      board_ip_.c_str(), board_port_, ptp_period_ms_, trigger_topic_.c_str(),
      imu_topic_.c_str());

    rx_thread_ = std::thread(&TimeSyncNode::rxLoop, this);
    diag_timer_ = create_wall_timer(
      std::chrono::seconds(1), std::bind(&TimeSyncNode::publishDiagnostics, this));
  }

  ~TimeSyncNode() override
  {
    stop_ = true;
    if (rx_thread_.joinable()) {
      rx_thread_.join();
    }
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

private:
  bool openSocket()
  {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
      return false;
    }
    int reuse = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(static_cast<uint16_t>(local_port_));
    if (::bind(fd_, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
      RCLCPP_FATAL(get_logger(), "bind failed: %s", std::strerror(errno));
      return false;
    }

    std::memset(&board_addr_, 0, sizeof(board_addr_));
    board_addr_.sin_family = AF_INET;
    board_addr_.sin_port = htons(static_cast<uint16_t>(board_port_));
    if (::inet_pton(AF_INET, board_ip_.c_str(), &board_addr_.sin_addr) != 1) {
      RCLCPP_FATAL(get_logger(), "invalid board_ip '%s'", board_ip_.c_str());
      return false;
    }
    return true;
  }

  void sendJson(const json & message) const
  {
    const std::string payload = message.dump() + "\n";
    ::sendto(
      fd_, payload.data(), payload.size(), 0,
      reinterpret_cast<const sockaddr *>(&board_addr_), sizeof(board_addr_));
  }

  void rxLoop()
  {
    auto next_ptp = SteadyClock::now();
    char buffer[65536];
    while (!stop_ && rclcpp::ok()) {
      const auto now = SteadyClock::now();
      if (now >= next_ptp) {
        sendJson({{"f", "a"}, {"a", hostUs()}});
        next_ptp = now + std::chrono::milliseconds(std::max(1, ptp_period_ms_));
      }

      pollfd pfd{fd_, POLLIN, 0};
      const int ready = ::poll(&pfd, 1, 20);
      if (ready <= 0 || !(pfd.revents & POLLIN)) {
        continue;
      }

      sockaddr_in from{};
      socklen_t from_len = sizeof(from);
      const ssize_t size = ::recvfrom(
        fd_, buffer, sizeof(buffer) - 1, 0,
        reinterpret_cast<sockaddr *>(&from), &from_len);
      if (size <= 0) {
        continue;
      }
      buffer[size] = '\0';
      handleDatagram(std::string(buffer, static_cast<size_t>(size)));
    }
  }

  void handleDatagram(const std::string & data)
  {
    size_t start = 0;
    while (start < data.size()) {
      size_t end = data.find('\n', start);
      if (end == std::string::npos) {
        end = data.size();
      }
      const std::string line = data.substr(start, end - start);
      start = end + 1;
      if (line.empty()) {
        continue;
      }
      json message = json::parse(line, nullptr, false);
      if (message.is_discarded() || !message.is_object()) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++parse_errors_;
        continue;
      }
      handleMessage(message);
    }
  }

  void handleMessage(const json & message)
  {
    // Any well-formed datagram proves the link is alive, including log lines
    // and PTP replies that carry no usable four-timestamp pair.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_rx_ = SteadyClock::now();
    }
    const std::string func = message.value("f", "");
    if (func == "a") {
      handlePtpRequest(message);
    } else if (func == "b") {
      handlePtpResponse(message);
    } else if (func == "t") {
      handleTrigger(message);
    } else if (func == "imu") {
      handleImu(message);
    } else if (func == "GNGGA" || func == "GNRMC") {
      std::lock_guard<std::mutex> lock(mutex_);
      ++gps_count_;
      last_rx_ = SteadyClock::now();
    } else if (func == "log") {
      RCLCPP_INFO(get_logger(), "board: %s", message.value("msg", "").c_str());
    } else {
      std::lock_guard<std::mutex> lock(mutex_);
      last_rx_ = SteadyClock::now();
    }
  }

  void handlePtpRequest(const json & message)
  {
    // Board echoes our stray timestamp as t1 and reports its receive time t2.
    if (!message.contains("a") || !message.contains("b")) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ptp_t1_us_ = message.at("a").get<uint64_t>();
    ptp_t2_us_ = message.at("b").get<uint64_t>();
    have_t1_t2_ = true;
    last_rx_ = SteadyClock::now();
  }

  void handlePtpResponse(const json & message)
  {
    const uint64_t t3 = message.value("a", uint64_t{0});
    const uint64_t t4 = hostUs();
    int64_t delay = 0;
    int64_t offset = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_rx_ = SteadyClock::now();
      if (!have_t1_t2_) {
        return;
      }
      delay = static_cast<int64_t>(
        (static_cast<__int128>(t4) - t3 + ptp_t2_us_ - ptp_t1_us_) / 2);
      offset = static_cast<int64_t>(
        (static_cast<__int128>(ptp_t2_us_) - ptp_t1_us_ - t4 + t3) / 2);
      have_t1_t2_ = false;
      ptp_delay_us_ = delay;
      ptp_offset_us_ = offset;
      ++ptp_exchanges_;
    }
    sendJson({{"f", "b"}, {"a", delay}, {"b", offset}});
  }

  void handleTrigger(const json & message)
  {
    if (!message.contains("t")) {
      return;
    }
    const uint64_t board_us = message.at("t").get<uint64_t>();
    const uint32_t mask = message.value("s", 0u);
    const uint64_t host_us = hostUs();

    chaos_interfaces::msg::BoardTrigger out;
    out.header.stamp = boardTime(board_us);
    out.header.frame_id = trigger_frame_id_;
    out.device_mask = mask;
    out.seq = ++trigger_seq_;
    out.board_stamp_us = board_us;
    out.host_stamp_us = host_us;
    trigger_pub_->publish(out);

    std::lock_guard<std::mutex> lock(mutex_);
    ++trigger_count_;
    last_rx_ = SteadyClock::now();
    last_trigger_ = SteadyClock::now();
    clock_skew_us_ = static_cast<int64_t>(board_us) - static_cast<int64_t>(host_us);
    last_trigger_mask_ = mask;
  }

  void handleImu(const json & message)
  {
    if (!message.contains("t") || !message.contains("d")) {
      return;
    }
    sensor_msgs::msg::Imu imu;
    imu.header.stamp = boardTime(message.at("t").get<uint64_t>());
    imu.header.frame_id = imu_frame_id_;
    const auto & d = message.at("d");
    if (d.size() >= 7) {
      imu.linear_acceleration.x = d[0].get<double>();
      imu.linear_acceleration.y = d[1].get<double>();
      imu.linear_acceleration.z = d[2].get<double>();
      imu.angular_velocity.x = d[3].get<double>();
      imu.angular_velocity.y = d[4].get<double>();
      imu.angular_velocity.z = d[5].get<double>();
      // d[6] is the die temperature; sensor_msgs/Imu has no field for it.
    }
    if (message.contains("q") && message.at("q").size() >= 4) {
      const auto & q = message.at("q");
      imu.orientation.w = q[0].get<double>();
      imu.orientation.x = q[1].get<double>();
      imu.orientation.y = q[2].get<double>();
      imu.orientation.z = q[3].get<double>();
    } else {
      imu.orientation_covariance[0] = -1.0;  // no attitude solution available
    }
    imu.linear_acceleration_covariance[0] = -1.0;
    imu.angular_velocity_covariance[0] = -1.0;
    if (publish_imu_) {
      imu_pub_->publish(imu);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ++imu_count_;
    last_rx_ = SteadyClock::now();
  }

  void publishDiagnostics()
  {
    int64_t age_ms = -1;
    int64_t trigger_age_ms = -1;
    int64_t ptp_offset = 0;
    int64_t ptp_delay = 0;
    int64_t clock_skew = 0;
    uint64_t ptp_exchanges = 0;
    uint64_t triggers = 0;
    uint64_t imus = 0;
    uint64_t gps = 0;
    uint64_t errors = 0;
    uint32_t last_mask = 0;
    bool have_rx = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto now = SteadyClock::now();
      if (last_rx_.time_since_epoch().count() != 0) {
        have_rx = true;
        age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_rx_).count();
      }
      if (last_trigger_.time_since_epoch().count() != 0) {
        trigger_age_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - last_trigger_).count();
      }
      ptp_offset = ptp_offset_us_;
      ptp_delay = ptp_delay_us_;
      clock_skew = clock_skew_us_;
      ptp_exchanges = ptp_exchanges_;
      triggers = trigger_count_;
      imus = imu_count_;
      gps = gps_count_;
      errors = parse_errors_;
      last_mask = last_trigger_mask_;
    }

    diagnostic_msgs::msg::DiagnosticArray array;
    array.header.stamp = now();
    diagnostic_msgs::msg::DiagnosticStatus status;
    status.name = "chaos_time_sync: board";
    status.hardware_id = board_ip_ + ":" + std::to_string(board_port_);
    const bool stale = !have_rx || age_ms > stale_after_ms_;
    const bool clock_off = std::llabs(clock_skew) > clock_warn_us_;
    status.level = stale ? diagnostic_msgs::msg::DiagnosticStatus::ERROR :
      (clock_off ? diagnostic_msgs::msg::DiagnosticStatus::WARN :
      diagnostic_msgs::msg::DiagnosticStatus::OK);
    status.message = stale ? "no datagrams from board" :
      (clock_off ? "board clock differs from host" : "ok");

    auto add = [&status](const std::string & key, const std::string & value) {
        diagnostic_msgs::msg::KeyValue kv;
        kv.key = key;
        kv.value = value;
        status.values.push_back(kv);
      };
    add("link", stale ? "down" : "up");
    add("last_rx_age_ms", std::to_string(age_ms));
    add("ptp_exchanges", std::to_string(ptp_exchanges));
    add("ptp_offset_us", std::to_string(ptp_offset));
    add("ptp_delay_us", std::to_string(ptp_delay));
    add("trigger_count", std::to_string(triggers));
    add("trigger_age_ms", std::to_string(trigger_age_ms));
    add("last_trigger_mask", std::to_string(last_mask));
    add("clock_skew_us", std::to_string(clock_skew));
    add("imu_count", std::to_string(imus));
    add("gps_count", std::to_string(gps));
    add("parse_errors", std::to_string(errors));
    array.status.push_back(status);
    diag_pub_->publish(array);
  }

  std::string board_ip_;
  int board_port_{8888};
  int local_port_{0};
  int ptp_period_ms_{100};
  bool publish_imu_{true};
  std::string imu_topic_;
  std::string trigger_topic_;
  std::string diagnostics_topic_;
  std::string trigger_frame_id_;
  std::string imu_frame_id_;
  int stale_after_ms_{2000};
  int64_t clock_warn_us_{1000000};

  int fd_{-1};
  sockaddr_in board_addr_{};
  std::atomic<bool> stop_{false};
  std::thread rx_thread_;
  rclcpp::TimerBase::SharedPtr diag_timer_;

  rclcpp::Publisher<chaos_interfaces::msg::BoardTrigger>::SharedPtr trigger_pub_;
  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diag_pub_;

  std::mutex mutex_;
  uint64_t ptp_t1_us_{0};
  uint64_t ptp_t2_us_{0};
  bool have_t1_t2_{false};
  int64_t ptp_delay_us_{0};
  int64_t ptp_offset_us_{0};
  int64_t clock_skew_us_{0};
  uint64_t ptp_exchanges_{0};
  uint64_t trigger_count_{0};
  uint64_t imu_count_{0};
  uint64_t gps_count_{0};
  uint64_t parse_errors_{0};
  uint32_t last_trigger_mask_{0};
  SteadyClock::time_point last_rx_{};
  SteadyClock::time_point last_trigger_{};
  std::atomic<uint32_t> trigger_seq_{0};
};

}  // namespace chaos_time_sync

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  try {
    auto node = std::make_shared<chaos_time_sync::TimeSyncNode>();
    rclcpp::spin(node);
  } catch (const std::exception & err) {
    RCLCPP_FATAL(rclcpp::get_logger("chaos_time_sync"), "%s", err.what());
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}
