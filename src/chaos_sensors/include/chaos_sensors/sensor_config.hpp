// Sensor configuration parsing at the node trust boundary. Port of
// chaos_sensors/config.py with the same limits and strict typing: JSON
// booleans are rejected for numeric fields, unknown stream types and
// relative topics are refused, and the payload is capped at 1 MiB.
//
// Units: rate_hz is publications per second, timeout_s is the freshness
// timeout in seconds.
//
// Clocks: the configuration carries no clock. The node publishes with the
// ROS clock and stamps the messages with the acquisition time of the
// simulated sample, which is the ROS clock reading at publication.
//
// Threading: the parsed configuration is an immutable value, copied into the
// node at construction time.
//
// Errors: every rejection throws SensorConfigError naming the offending
// field; a failed parse leaves no partially filled configuration behind.
#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace chaos::sensors {

class SensorConfigError : public std::runtime_error {
public:
  explicit SensorConfigError(const std::string & message)
      : std::runtime_error(message) {}
};

enum class StreamType { kImage, kCameraInfo, kPointCloud2, kLaserScan, kImu };

struct StreamConfig {
  std::string id;
  StreamType type;
  std::string topic;       // absolute, resolved by the inventory
  std::string frame_id;
  std::string qos = "sensor_data";
  double rate_hz = 10.0;
  double timeout_s = 2.0;
  bool monitor = true;
};

// Where a sensor's message stamps come from. The inventory normalizes the
// block; the monitor re-validates it at its trust boundary.
struct TimeSyncConfig {
  bool present = false;
  std::string source;        // board_trigger | device_timestamp | host_arrival
  std::string board_channel; // CAM_1..CAM_4 | IMU_2 | LIDAR_1 | LIDAR_2 | none
  std::string clock_domain;  // host_utc | board | device
  std::string requires;      // none | gps_sync | ptp
  std::string image_stream;  // stream paired with the trigger (board_trigger cameras)
  double trigger_hz = 0.0;   // 0 = not declared
  double max_age_ms = 50.0;
  int64_t exposure_offset_us = 0;
};

struct SensorConfig {
  std::string id;
  std::string kind;
  std::string name_space;
  std::string frame_id;
  bool enabled = true;
  std::string driver_mode;
  TimeSyncConfig time_sync;
  std::map<std::string, StreamConfig> streams;
};

// Parses one sensor entry (the mock_sensor `sensor_json` parameter).
SensorConfig parseSensorJson(const std::string & raw);

// Parses every enabled sensor of an asset (the monitor `asset_json`).
// The asset namespace is returned separately: the monitor endpoints are
// derived from the asset namespace, never from a per-sensor namespace.
std::vector<SensorConfig> parseAssetSensorsJson(const std::string & raw,
                                                std::string & asset_id_out,
                                                std::string & namespace_out);

// Allowlisted public description, matching public_description() in
// config.py: driver credentials never leave the node.
std::string publicDescription(const std::string & asset_id,
                              const std::vector<SensorConfig> & sensors);

std::string streamTypeName(StreamType type);

}  // namespace chaos::sensors
