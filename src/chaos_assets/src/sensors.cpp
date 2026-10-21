#include "chaos_assets/sensors.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <utility>

#include "chaos_assets/paths.hpp"

namespace chaos::assets {

const std::regex kFrameRe(R"(^[A-Za-z_][A-Za-z0-9_]*(?:/[A-Za-z_][A-Za-z0-9_]*)*$)");

namespace {

std::regex identifier_re(R"(^[A-Za-z][A-Za-z0-9_]*$)");
std::regex package_re(R"(^[a-z][a-z0-9_]*$)");
std::regex executable_re(R"(^[A-Za-z0-9_][A-Za-z0-9_.-]*$)");
std::regex parameter_re(R"(^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$)");
std::regex ros_name_re(R"(^/?[A-Za-z_][A-Za-z0-9_]*(?:/[A-Za-z_][A-Za-z0-9_]*)*$)");

const std::set<std::string> kMessageTypes = {
    "sensor_msgs/msg/Image",     "sensor_msgs/msg/CameraInfo", "sensor_msgs/msg/PointCloud2",
    "sensor_msgs/msg/LaserScan", "sensor_msgs/msg/Imu"};

const std::map<std::string, std::set<std::string>> kPrimaryTypes = {
    {"camera", {"sensor_msgs/msg/Image", "sensor_msgs/msg/CameraInfo", "sensor_msgs/msg/PointCloud2"}},
    {"lidar", {"sensor_msgs/msg/PointCloud2", "sensor_msgs/msg/LaserScan"}},
    {"imu", {"sensor_msgs/msg/Imu"}},
};

const std::vector<std::string> kSensorFields = {"kind", "enabled", "frame_id", "driver", "streams", "transform"};
const std::vector<std::string> kDriverFields = {"mode", "package", "executable", "parameters",
                                                "parameter_files", "remappings"};
const std::vector<std::string> kStreamFields = {"type", "topic", "frame_id", "qos", "rate_hz",
                                                "timeout_s", "monitor"};

std::string resolve_topic_impl(const Json& value, const std::string& namespace_, const std::string& where) {
  const std::string& topic = text(value, where, &ros_name_re);
  return topic[0] == '/' ? topic : namespace_ + "/" + topic;
}

// ROS scalar validation: bool/string/int64/finite-double, or a nonempty
// homogeneous array of one of those scalar kinds.
void parameter_value(const Json& value, const std::string& where) {
  auto scalar = [&](const Json& item) {
    if (item.is_boolean() || item.is_string()) return;
    if (item.is_number_integer()) return;  // int64 range enforced at YAML load
    if (item.is_number_float() && std::isfinite(item.get<double>())) return;
    throw ConfigurationError(where + ": expected a ROS scalar or homogeneous scalar array with finite numbers");
  };
  if (value.is_array()) {
    if (value.empty()) {
      throw ConfigurationError(where + ": expected a nonempty homogeneous ROS scalar array");
    }
    const Json& first = value.at(0);
    for (const auto& item : value) {
      if (item.type() != first.type()) {
        throw ConfigurationError(where + ": expected a nonempty homogeneous ROS scalar array");
      }
    }
    for (const auto& item : value) scalar(item);
  } else {
    scalar(value);
  }
}

// Resolve parameter file paths before model and instance dicts merge, so a
// relative path always refers to its own declaring file.
Json declarations(const Json& value, const std::string& base_path, const std::string& where) {
  Json result = record(value, where);
  for (auto it = result.begin(); it != result.end(); ++it) {
    const std::string& sensor_id = text(Json(it.key()), where + " sensor ID", &identifier_re);
    const std::string prefix = where + "." + sensor_id;
    Json& declaration = it.value();
    record(declaration, prefix, kSensorFields);
    if (Json* driver = declaration.find("driver")) {
      record(*driver, prefix + ".driver", kDriverFields);
      if (Json* files = driver->find("parameter_files")) {
        if (!files->is_array()) {
          throw ConfigurationError(prefix + ".driver.parameter_files: expected a list");
        }
        Json resolved = Json::array();
        for (const auto& file : *files) {
          const std::string path = resolve_path(file, base_path, prefix + ".driver.parameter_files");
          if (!std::filesystem::is_regular_file(path)) {
            throw ConfigurationError("sensor parameter file does not exist: " + path);
          }
          resolved.push_back(path);
        }
        (*driver)["parameter_files"] = resolved;
      }
    }
    if (Json* streams = declaration.find("streams")) {
      for (auto sit = streams->begin(); sit != streams->end(); ++sit) {
        text(Json(sit.key()), prefix + " stream ID", &identifier_re);
        record(sit.value(), prefix + ".streams." + sit.key(), kStreamFields);
      }
    }
    if (Json* transform = declaration.find("transform")) {
      record(*transform, prefix + ".transform", {"parent", "xyz", "rpy"});
    }
  }
  return result;
}

Json driver(const Json& value, const std::string& namespace_, const std::string& asset_mode,
            bool enabled, const std::string& where) {
  Json result = record(value, where, kDriverFields);
  const Json* mode_value = result.find("mode");
  if (mode_value == nullptr || !mode_value->is_string()) {
    throw ConfigurationError(where + ".mode: expected mock, node or attach");
  }
  const std::string& mode = mode_value->get_ref<const std::string&>();
  if (mode != "mock" && mode != "node" && mode != "attach") {
    throw ConfigurationError(where + ".mode: expected mock, node or attach");
  }
  if (mode == "attach" && result.size() != 1) {
    throw ConfigurationError(where + ": attach drivers accept only mode");
  }
  if (mode == "node" || result.contains("package")) {
    text(result.contains("package") ? result.at("package") : Json(), where + ".package", &package_re);
  }
  if (mode == "node" || result.contains("executable")) {
    text(result.contains("executable") ? result.at("executable") : Json(), where + ".executable",
         &executable_re);
  }
  if (Json* parameters = result.find("parameters")) {
    for (auto it = parameters->begin(); it != parameters->end(); ++it) {
      text(Json(it.key()), where + ".parameters key", &parameter_re);
      parameter_value(it.value(), where + ".parameters." + it.key());
    }
  }
  if (Json* remappings = result.find("remappings")) {
    record(*remappings, where + ".remappings");
    for (auto it = remappings->begin(); it != remappings->end(); ++it) {
      const std::string& source = text(Json(it.key()), where + ".remappings source", &ros_name_re);
      if (source == "__ns" || source == "__node" || source == "__name") {
        throw ConfigurationError(where + ".remappings: node and namespace remapping is not a topic mapping");
      }
      it.value() = resolve_topic_impl(it.value(), namespace_, where + ".remappings." + source);
    }
  }
  if (enabled && asset_mode == "real" && mode == "mock") {
    throw ConfigurationError(where + ": enabled sensors on a real asset cannot use a mock driver");
  }
  result["declared_mode"] = mode;
  if (asset_mode == "mock") {
    result["mode"] = "mock";
  }
  return result;
}

std::vector<double> vector3(const Json& value, const std::string& where) {
  if (!value.is_array() || value.size() != 3) {
    throw ConfigurationError(where + ": expected exactly three finite numbers");
  }
  std::vector<double> result;
  for (const auto& item : value) {
    if (item.is_boolean() || !item.is_number()) {
      throw ConfigurationError(where + ": expected exactly three finite numbers");
    }
    const double number = item.is_number_integer() ? static_cast<double>(item.get<int64_t>())
                                                   : item.get<double>();
    if (!std::isfinite(number)) {
      throw ConfigurationError(where + ": expected exactly three finite numbers");
    }
    result.push_back(number);
  }
  return result;
}

}  // namespace

std::string resolve_topic(const Json& value, const std::string& namespace_, const std::string& where) {
  return resolve_topic_impl(value, namespace_, where);
}

Json resolve_sensors(const Json& model_sensors, const Json& instance_sensors,
                     const std::string& model_directory, const std::string& inventory_directory,
                     const Json& asset) {
  const std::string& asset_id = asset.at("id").get_ref<const std::string&>();
  const std::string where = asset_id + ".sensors";
  Json model = declarations(model_sensors.is_null() ? Json(Json::object()) : model_sensors,
                            model_directory, where + " (model)");
  Json instance = declarations(instance_sensors.is_null() ? Json(Json::object()) : instance_sensors,
                               inventory_directory, where);
  // An explicit backend mode change replaces inherited options, so a
  // node -> attach override cannot inherit an executable.
  for (auto it = instance.begin(); it != instance.end(); ++it) {
    const Json* override_driver = it.value().find("driver");
    if (override_driver == nullptr) continue;
    const Json* override_mode = override_driver->find("mode");
    auto mit = model.find(it.key());
    if (override_mode == nullptr || mit == model.end()) continue;
    const Json* inherited_driver = mit.value().find("driver");
    if (inherited_driver == nullptr) continue;
    const Json* inherited_mode = inherited_driver->find("mode");
    if (inherited_mode != nullptr && *override_mode != *inherited_mode) {
      mit.value()["driver"] = Json(Json::object());
    }
  }
  Json result = nlohmann::ordered_json::parse(model.dump());
  result.merge_patch(instance);
  // Deep merge for nested dicts (driver options, streams), like the original
  // _merge: mapping + mapping merges recursively, anything else replaces.
  // merge_patch already implements exactly that JSON-merge semantic.
  if (result.size() > 64) {
    throw ConfigurationError(where + ": expected at most 64 sensors per asset");
  }
  for (auto it = result.begin(); it != result.end(); ++it) {
    const std::string& sensor_id = it.key();
    const std::string prefix = where + "." + sensor_id;
    Json& sensor = it.value();

    const Json* kind = sensor.find("kind");
    if (kind == nullptr || !kind->is_string() || kPrimaryTypes.find(kind->get_ref<const std::string&>()) == kPrimaryTypes.end()) {
      throw ConfigurationError(prefix + ".kind: expected camera, lidar or imu");
    }
    const std::string& kind_string = kind->get_ref<const std::string&>();
    Json enabled = sensor.contains("enabled") ? sensor.at("enabled") : Json(true);
    if (!enabled.is_boolean()) {
      throw ConfigurationError(prefix + ".enabled: expected boolean");
    }
    const std::string& namespace_ = asset.at("namespace").get_ref<const std::string&>() + "/sensors/" + sensor_id;
    const std::string frame =
        text(sensor.contains("frame_id") ? sensor.at("frame_id") : Json(asset_id + "/" + sensor_id + "_link"),
             prefix + ".frame_id", &kFrameRe);
    sensor["id"] = sensor_id;
    sensor["enabled"] = enabled;
    sensor["namespace"] = namespace_;
    sensor["frame_id"] = frame;
    sensor["driver"] = driver(sensor.contains("driver") ? sensor.at("driver") : Json(Json::object()),
                              namespace_, asset.at("mode").get_ref<const std::string&>(),
                              enabled.get<bool>(), prefix + ".driver");
    Json* streams = sensor.find("streams");
    if (streams == nullptr || streams->is_null()) {
      throw ConfigurationError(prefix + ".streams: expected at least one stream");
    }
    record(*streams, prefix + ".streams");
    if (streams->empty()) {
      throw ConfigurationError(prefix + ".streams: expected at least one stream");
    }
    if (streams->size() > 32) {
      throw ConfigurationError(prefix + ".streams: expected at most 32 streams per sensor");
    }
    bool has_primary = false;
    for (auto sit = streams->begin(); sit != streams->end(); ++sit) {
      const std::string stream_where = prefix + ".streams." + sit.key();
      Json& stream = sit.value();
      const Json* type = stream.find("type");
      if (type == nullptr || !type->is_string() ||
          kMessageTypes.find(type->get_ref<const std::string&>()) == kMessageTypes.end()) {
        throw ConfigurationError(stream_where + ".type: unsupported sensor message type");
      }
      if (kPrimaryTypes.at(kind_string).count(type->get_ref<const std::string&>()) > 0) {
        has_primary = true;
      }
      stream["topic"] = resolve_topic_impl(
          stream.contains("topic") ? stream.at("topic") : Json(), namespace_, stream_where + ".topic");
      stream["frame_id"] = text(stream.contains("frame_id") ? stream.at("frame_id") : Json(frame),
                                stream_where + ".frame_id", &kFrameRe);
      Json qos = stream.contains("qos") ? stream.at("qos") : Json("sensor_data");
      if (!qos.is_string() || (qos.get_ref<const std::string&>() != "sensor_data" &&
                               qos.get_ref<const std::string&>() != "reliable")) {
        throw ConfigurationError(stream_where + ".qos: expected sensor_data or reliable");
      }
      stream["qos"] = qos;
      Json monitor = stream.contains("monitor") ? stream.at("monitor") : Json(true);
      if (!monitor.is_boolean()) {
        throw ConfigurationError(stream_where + ".monitor: expected boolean");
      }
      stream["monitor"] = monitor;
      stream["rate_hz"] = positive(stream.contains("rate_hz") ? stream.at("rate_hz") : Json(10.0),
                                   stream_where + ".rate_hz", 0.01, 1000);
      stream["timeout_s"] = positive(stream.contains("timeout_s") ? stream.at("timeout_s") : Json(2.0),
                                     stream_where + ".timeout_s", 0.001, 300);
    }
    if (!has_primary) {
      throw ConfigurationError(prefix + ": no primary stream for sensor kind " + kind_string);
    }
    if (Json* transform = sensor.find("transform")) {
      const std::string parent = text(
          transform->contains("parent") ? transform->at("parent")
                                        : Json(asset.at("frames").at("base").get_ref<const std::string&>()),
          prefix + ".transform.parent", &kFrameRe);
      if (parent == frame) {
        throw ConfigurationError(prefix + ".transform: parent and child frames must differ");
      }
      Json normalized = Json::object();
      normalized["parent"] = parent;
      const std::vector<double> xyz = vector3(transform->contains("xyz") ? transform->at("xyz") : Json(),
                                              prefix + ".transform.xyz");
      const std::vector<double> rpy = vector3(transform->contains("rpy") ? transform->at("rpy") : Json(),
                                              prefix + ".transform.rpy");
      normalized["xyz"] = Json::array({xyz[0], xyz[1], xyz[2]});
      normalized["rpy"] = Json::array({rpy[0], rpy[1], rpy[2]});
      sensor["transform"] = normalized;
    }
  }
  return result;
}

void validate_sensor_graph(const std::vector<Json>& assets) {
  using Domain = int64_t;
  std::map<std::pair<Domain, std::string>, std::string> frames;
  std::map<std::pair<Domain, std::string>, std::pair<std::string, bool>> topics;
  std::map<std::pair<Domain, std::string>, std::string> transforms;

  for (const Json& asset : assets) {
    if (!asset.at("enabled").get<bool>()) continue;
    const Domain domain = asset.at("ros_domain_id").get<int64_t>();
    for (auto it = asset.at("frames").begin(); it != asset.at("frames").end(); ++it) {
      frames.emplace(std::make_pair(domain, it.value().get<std::string>()),
                     asset.at("id").get_ref<const std::string&>() + ".frames." + it.key());
    }
  }
  for (const Json& asset : assets) {
    if (!asset.at("enabled").get<bool>()) continue;
    const Domain domain = asset.at("ros_domain_id").get<int64_t>();
    for (const auto& item : asset.at("sensors").items()) {
      const Json& sensor = item.value();
      if (!sensor.at("enabled").get<bool>()) continue;
      const std::string owner = asset.at("id").get_ref<const std::string&>() + ".sensors." + item.key();
      std::set<std::string> sensor_frames = {sensor.at("frame_id").get<std::string>()};
      for (const auto& stream : sensor.at("streams").items()) {
        sensor_frames.insert(stream.value().at("frame_id").get<std::string>());
      }
      for (const auto& frame : sensor_frames) {
        const auto key = std::make_pair(domain, frame);
        auto found = frames.find(key);
        if (found != frames.end()) {
          throw ConfigurationError("duplicate sensor frame in ROS domain " + std::to_string(domain) +
                                   ": " + frame + " (" + found->second + ", " + owner + ")");
        }
        frames[key] = owner;
      }
      const Json* transform = sensor.find("transform");
      if (transform != nullptr) {
        transforms[std::make_pair(domain, sensor.at("frame_id").get<std::string>())] =
            transform->at("parent").get<std::string>();
      }
      const bool managed = sensor.at("driver").at("mode") == "mock" ||
                           sensor.at("driver").at("mode") == "node";
      for (const auto& stream : sensor.at("streams").items()) {
        const auto key = std::make_pair(domain, stream.value().at("topic").get<std::string>());
        const std::string stream_owner = owner + ".streams." + stream.key();
        auto found = topics.find(key);
        if (found != topics.end() && found->second.first != stream.value().at("type").get<std::string>()) {
          throw ConfigurationError("inconsistent sensor topic types in ROS domain " +
                                   std::to_string(domain) + ": " + key.second);
        }
        if (managed && found != topics.end() && found->second.second) {
          throw ConfigurationError("duplicate sensor output topic in ROS domain " +
                                   std::to_string(domain) + ": " + key.second);
        }
        if (found == topics.end()) {
          topics[key] = {stream.value().at("type").get<std::string>(), managed};
        } else if (managed) {
          found->second.second = true;
        }
      }
    }
  }
  for (const auto& item : transforms) {
    std::set<std::string> seen;
    std::string frame = item.first.second;
    while (true) {
      auto found = transforms.find(std::make_pair(item.first.first, frame));
      if (found == transforms.end()) break;
      if (!seen.insert(frame).second) {
        throw ConfigurationError("sensor transform cycle in ROS domain " +
                                 std::to_string(item.first.first) + ": " + frame);
      }
      frame = found->second;
    }
  }
}

}  // namespace chaos::assets
