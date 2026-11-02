// Process-local guards for the humanoid driver. No ROS and no vendor SDK.
// The session record is an optional pipe from the driver child to a parent
// that can still send a zero after the child is gone.
#pragma once

#include <cstdint>
#include <string>

namespace chaos::humanoid {

// ROS_LOCALHOST_ONLY=0 wins over a LOCALHOST range and is not local.
// ROS_LOCALHOST_ONLY=1 is local. Otherwise the range must be exactly LOCALHOST.
bool localhostDiscovery(const char * localhost_only, const char * discovery_range);
bool localhostRosDiscovery();

struct VendorSessionRecord {
  bool active = false;
  bool released = false;
  std::int32_t domain = 0;
  std::string interface_name;
};

// Framed record, optionally followed by one release byte. A truncated or
// inconsistent buffer is inactive so a parent never guesses an interface.
VendorSessionRecord parseVendorSession(const std::string & bytes);
bool emergencyZeroRequired(const VendorSessionRecord & record);
std::string encodeVendorSession(std::int32_t domain, const std::string & interface_name);
std::string vendorSessionReleaseByte();

// No-op unless the driver process armed a write end. Writes are one record
// then, on a clean hand-off, one release byte.
void armVendorSessionChannel(int fd);
void publishVendorSession(std::int32_t domain, const std::string & interface_name);
void publishVendorSessionRelease();

}  // namespace chaos::humanoid
