#include "chaos_runtime/humanoid/process_guard.hpp"

#include <cerrno>
#include <cstdlib>
#include <mutex>
#include <unistd.h>

namespace chaos::humanoid {
namespace {

constexpr char kMagic[] = {'C', 'H', 'S', '1'};
constexpr char kRelease = '\x01';
int session_fd = -1;
std::mutex session_mutex;

bool sameText(const char * value, const char * expected) {
  if (value == nullptr || expected == nullptr) {
    return false;
  }
  return std::string(value) == expected;
}

void appendLe32(std::string & out, std::int32_t value) {
  const auto bits = static_cast<std::uint32_t>(value);
  out.push_back(static_cast<char>(bits & 0xffu));
  out.push_back(static_cast<char>((bits >> 8) & 0xffu));
  out.push_back(static_cast<char>((bits >> 16) & 0xffu));
  out.push_back(static_cast<char>((bits >> 24) & 0xffu));
}

bool readLe32(const std::string & bytes, std::size_t offset, std::int32_t & value) {
  if (offset + 4 > bytes.size()) {
    return false;
  }
  const auto bits = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) |
    (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8) |
    (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16) |
    (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3])) << 24);
  value = static_cast<std::int32_t>(bits);
  return true;
}

bool writeAll(int fd, const std::string & bytes) {
  std::size_t written = 0;
  while (written < bytes.size()) {
    const ssize_t result = ::write(fd, bytes.data() + written, bytes.size() - written);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    written += static_cast<std::size_t>(result);
  }
  return true;
}

}  // namespace

bool localhostDiscovery(const char * localhost_only, const char * discovery_range) {
  if (sameText(localhost_only, "0")) {
    return false;
  }
  if (sameText(localhost_only, "1")) {
    return true;
  }
  return sameText(discovery_range, "LOCALHOST");
}

bool localhostRosDiscovery() {
  return localhostDiscovery(std::getenv("ROS_LOCALHOST_ONLY"),
                            std::getenv("ROS_AUTOMATIC_DISCOVERY_RANGE"));
}

std::string encodeVendorSession(std::int32_t domain, const std::string & interface_name) {
  if (domain < 0 || domain > 232 || interface_name.empty() || interface_name.size() > 15) {
    return {};
  }
  std::string out(kMagic, kMagic + 4);
  appendLe32(out, domain);
  out.push_back(static_cast<char>(interface_name.size()));
  out.append(interface_name);
  return out;
}

std::string vendorSessionReleaseByte() {
  return std::string(1, kRelease);
}

VendorSessionRecord parseVendorSession(const std::string & bytes) {
  VendorSessionRecord record;
  if (bytes.size() < 9 || bytes.compare(0, 4, kMagic, 4) != 0) {
    return record;
  }
  std::int32_t domain = 0;
  if (!readLe32(bytes, 4, domain)) {
    return record;
  }
  const auto length = static_cast<unsigned char>(bytes[8]);
  if (length == 0 || length > 15) {
    return record;
  }
  const std::size_t framed = 9u + length;
  if (bytes.size() != framed && bytes.size() != framed + 1) {
    return record;
  }
  if (bytes.size() == framed + 1 && bytes.back() != kRelease) {
    return record;
  }
  record.interface_name.assign(bytes, 9, length);
  record.domain = domain;
  record.released = bytes.size() == framed + 1;
  record.active = !encodeVendorSession(domain, record.interface_name).empty();
  if (!record.active) {
    record = VendorSessionRecord{};
  }
  return record;
}

bool emergencyZeroRequired(const VendorSessionRecord & record) {
  return record.active && !record.released;
}

void armVendorSessionChannel(int fd) {
  std::lock_guard<std::mutex> guard(session_mutex);
  session_fd = fd;
}

void publishVendorSession(std::int32_t domain, const std::string & interface_name) {
  const std::string bytes = encodeVendorSession(domain, interface_name);
  if (bytes.empty()) {
    return;
  }
  std::lock_guard<std::mutex> guard(session_mutex);
  if (session_fd >= 0) {
    writeAll(session_fd, bytes);
  }
}

void publishVendorSessionRelease() {
  std::lock_guard<std::mutex> guard(session_mutex);
  if (session_fd >= 0) {
    writeAll(session_fd, vendorSessionReleaseByte());
  }
}

}  // namespace chaos::humanoid
