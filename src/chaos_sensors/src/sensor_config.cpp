#include "chaos_sensors/sensor_config.hpp"

#include <cmath>

namespace chaos::sensors {

namespace {
constexpr std::size_t kMaxJsonBytes = 1024 * 1024;

[[noreturn]] void fail(const std::string & where, const std::string & what) {
  throw SensorConfigError(where + ": " + what);
}

nlohmann::json parseObject(const std::string & raw, const std::string & where) {
  if (raw.size() > kMaxJsonBytes) {
    fail(where, "configuration exceeds 1 MiB");
  }
  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(raw, nullptr, true, false);
  } catch (const nlohmann::json::parse_error & error) {
    fail(where, std::string("not valid JSON: ") + error.what());
  }
  if (!parsed.is_object()) {
    fail(where, "configuration must be an object");
  }
  return parsed;
}

double requireRange(const nlohmann::json & object, const std::string & key,
                    double default_value, double minimum, double maximum,
                    const std::string & where) {
  auto found = object.find(key);
  if (found == object.end()) {
    return default_value;
  }
  if (found->is_boolean() || !found->is_number()) {
    fail(where, key + " must be a number, not a boolean or string");
  }
  const double value = found->get<double>();
  if (!std::isfinite(value) || value < minimum || value > maximum) {
    fail(where, key + " is out of range");
  }
  return value;
}

bool requireMonitorFlag(const nlohmann::json & object,
                        const std::string & where) {
  auto found = object.find("monitor");
  if (found == object.end()) {
    return true;
  }
  if (!found->is_boolean()) {
    fail(where, "monitor flag must be a boolean");
  }
  return found->get<bool>();
}

std::string requireNonEmptyString(const nlohmann::json & object,
                                  const std::string & key,
                                  const std::string & where) {
  auto found = object.find(key);
  if (found == object.end() || !found->is_string() ||
      found->get<std::string>().empty()) {
    fail(where, key + " must be a nonempty string");
  }
  return found->get<std::string>();
}

StreamType parseType(const std::string & type, const std::string & where) {
  if (type == "sensor_msgs/msg/Image") return StreamType::kImage;
  if (type == "sensor_msgs/msg/CameraInfo") return StreamType::kCameraInfo;
  if (type == "sensor_msgs/msg/PointCloud2") return StreamType::kPointCloud2;
  if (type == "sensor_msgs/msg/LaserScan") return StreamType::kLaserScan;
  if (type == "sensor_msgs/msg/Imu") return StreamType::kImu;
  fail(where, "unsupported message type " + type);
}

std::map<std::string, StreamConfig> parseStreams(const nlohmann::json & sensor,
                                                 const std::string & where) {
  auto streams = sensor.find("streams");
  if (streams == sensor.end()) {
    return {};
  }
  if (!streams->is_object() || streams->size() > 32) {
    fail(where, "streams must be an object with at most 32 entries");
  }
  std::map<std::string, StreamConfig> result;
  for (auto item = streams->begin(); item != streams->end(); ++item) {
    const std::string stream_where = where + ".streams." + item.key();
    if (!item.value().is_object()) {
      fail(stream_where, "stream entry must be an object");
    }
    const auto & value = item.value();
    StreamConfig stream;
    stream.id = item.key();
    stream.type = parseType(requireNonEmptyString(value, "type", stream_where),
                            stream_where);
    stream.topic =
      requireNonEmptyString(value, "topic", stream_where);
    if (stream.topic.front() != '/') {
      fail(stream_where, "topic must be absolute");
    }
    stream.frame_id =
      requireNonEmptyString(value, "frame_id", stream_where);
    auto qos = value.find("qos");
    if (qos != value.end()) {
      if (!qos->is_string() ||
          (qos->get<std::string>() != "sensor_data" &&
           qos->get<std::string>() != "reliable")) {
        fail(stream_where, "invalid QoS");
      }
      stream.qos = qos->get<std::string>();
    }
    stream.monitor = requireMonitorFlag(value, stream_where);
    stream.rate_hz = requireRange(value, "rate_hz", 10.0, 0.01, 1000.0,
                                  stream_where);
    stream.timeout_s = requireRange(value, "timeout_s", 2.0, 0.001, 300.0,
                                    stream_where);
    result.emplace(item.key(), std::move(stream));
  }
  return result;
}

SensorConfig parseSensor(const std::string & sensor_id,
                         const nlohmann::json & sensor,
                         const std::string & name_space,
                         const std::string & where) {
  if (!sensor.is_object()) {
    fail(where, "sensor entry must be an object");
  }
  SensorConfig config;
  config.id = sensor_id;
  config.kind = requireNonEmptyString(sensor, "kind", where);
  config.name_space = name_space.empty()
                        ? requireNonEmptyString(sensor, "namespace", where)
                        : name_space;
  config.frame_id = requireNonEmptyString(sensor, "frame_id", where);
  auto enabled = sensor.find("enabled");
  if (enabled != sensor.end()) {
    if (!enabled->is_boolean()) {
      fail(where, "enabled must be a boolean");
    }
    config.enabled = enabled->get<bool>();
  }
  auto driver = sensor.find("driver");
  if (driver == sensor.end() || !driver->is_object()) {
    fail(where, "driver must be an object");
  }
  auto mode = driver->find("mode");
  if (mode == driver->end() || !mode->is_string()) {
    fail(where, "driver.mode must be a string");
  }
  config.driver_mode = mode->get<std::string>();
  config.streams = parseStreams(sensor, where);
  return config;
}
}  // namespace

std::string streamTypeName(StreamType type) {
  switch (type) {
    case StreamType::kImage: return "sensor_msgs/msg/Image";
    case StreamType::kCameraInfo: return "sensor_msgs/msg/CameraInfo";
    case StreamType::kPointCloud2: return "sensor_msgs/msg/PointCloud2";
    case StreamType::kLaserScan: return "sensor_msgs/msg/LaserScan";
    case StreamType::kImu: return "sensor_msgs/msg/Imu";
  }
  return "unknown";
}

SensorConfig parseSensorJson(const std::string & raw) {
  const nlohmann::json parsed = parseObject(raw, "sensor_json");
  SensorConfig sensor = parseSensor(
    parsed.contains("id") && parsed["id"].is_string()
      ? parsed["id"].get<std::string>()
      : "sensor",
    parsed, "", "sensor_json");
  if (sensor.driver_mode != "mock") {
    fail("sensor_json", "mock_sensor requires driver.mode=mock");
  }
  return sensor;
}

std::vector<SensorConfig> parseAssetSensorsJson(const std::string & raw,
                                                std::string & asset_id_out,
                                                std::string & namespace_out) {
  const nlohmann::json parsed = parseObject(raw, "asset_json");
  asset_id_out = requireNonEmptyString(parsed, "id", "asset_json");
  namespace_out = requireNonEmptyString(parsed, "namespace", "asset_json");
  auto sensors = parsed.find("sensors");
  std::vector<SensorConfig> result;
  if (sensors == parsed.end()) {
    return result;
  }
  if (!sensors->is_object() || sensors->size() > 64) {
    fail("asset_json", "sensors must be an object with at most 64 entries");
  }
  for (auto item = sensors->begin(); item != sensors->end(); ++item) {
    const std::string where = "asset_json.sensors." + item.key();
    SensorConfig sensor =
      parseSensor(item.key(), item.value(), "", where);
    if (!sensor.enabled) {
      continue;
    }
    result.push_back(std::move(sensor));
  }
  return result;
}

std::string publicDescription(const std::string & asset_id,
                              const std::vector<SensorConfig> & sensors) {
  nlohmann::json catalog = nlohmann::json::object();
  for (const SensorConfig & sensor : sensors) {
    nlohmann::json entry;
    entry["id"] = sensor.id;
    entry["kind"] = sensor.kind;
    entry["namespace"] = sensor.name_space;
    entry["frame_id"] = sensor.frame_id;
    entry["driver"] = {{"mode", sensor.driver_mode}};
    nlohmann::json streams = nlohmann::json::object();
    for (const auto & [stream_id, stream] : sensor.streams) {
      streams[stream_id] = {
        {"type", streamTypeName(stream.type)},
        {"topic", stream.topic},
        {"frame_id", stream.frame_id},
        {"qos", stream.qos},
        {"rate_hz", stream.rate_hz},
        {"timeout_s", stream.timeout_s},
        {"monitor", stream.monitor},
      };
    }
    entry["streams"] = std::move(streams);
    catalog[sensor.id] = std::move(entry);
  }
  nlohmann::json description = {
    {"schema_version", 1},
    {"asset_id", asset_id},
    {"sensors", std::move(catalog)},
  };
  return description.dump(-1, ' ', false, nlohmann::json::error_handler_t::strict);
}

}  // namespace chaos::sensors
