#include "chaos_bringup/components.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "chaos_assets/errors.hpp"
#include "chaos_assets/sensors.hpp"
#include "chaos_assets/yaml.hpp"
#include "chaos_bringup/driver_registry.hpp"
#include "installed_executable.hpp"

namespace chaos::bringup {

namespace {

using chaos::assets::ConfigurationError;

const std::regex parameter_name_re(R"(^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$)");
const std::regex selector_part_re(R"(^[A-Za-z_][A-Za-z0-9_]*$)");

// The enabled sensors of an asset, in the order of the resolved snapshot.
std::vector<const Json*> enabled_sensors(const Json& asset) {
  std::vector<const Json*> result;
  const auto sensors = asset.find("sensors");
  if (sensors == asset.end() || !sensors->is_object()) return result;
  for (auto it = sensors->begin(); it != sensors->end(); ++it) {
    if (it.value().at("enabled").get<bool>()) result.push_back(&it.value());
  }
  return result;
}

// The robot description of an asset, or null when it is disabled or absent.
const Json* enabled_description(const Json& asset) {
  const auto description = asset.find("description");
  if (description == asset.end() || !description->is_object()) return nullptr;
  const auto enabled = description->find("enabled");
  if (enabled != description->end() && !enabled->get<bool>()) return nullptr;
  return &description.value();
}

// The recording policy of an asset, or null when it is disabled or absent.
const Json* enabled_collection(const Json& asset) {
  const auto collection = asset.find("data_collection");
  if (collection == asset.end() || !collection->is_object()) return nullptr;
  const auto enabled = collection->find("enabled");
  if (enabled == collection->end() || !enabled->get<bool>()) return nullptr;
  return &collection.value();
}

// Match a node selector against the name of the managed node.
//
// A selector is a list of patterns: "*" matches one name, "**" matches any
// number of names.
bool selector_matches(const std::vector<std::string>& pattern, const std::vector<std::string>& parts) {
  if (pattern.empty()) return parts.empty();
  if (pattern.front() == "**") {
    if (selector_matches(std::vector<std::string>(pattern.begin() + 1, pattern.end()), parts)) {
      return true;
    }
    if (parts.empty()) return false;
    std::vector<std::string> rest(parts.begin() + 1, parts.end());
    return selector_matches(pattern, rest);
  }
  if (parts.empty()) return false;
  if (pattern.front() != "*" && pattern.front() != parts.front()) return false;
  return selector_matches(std::vector<std::string>(pattern.begin() + 1, pattern.end()),
                          std::vector<std::string>(parts.begin() + 1, parts.end()));
}

// Split a selector into its parts.
//
// The surrounding separators are removed before the split, exactly like the
// reference implementation: a leading "/" must not produce an empty part,
// because an empty part is not a valid node name and would make every
// selector unusable. The inner empty parts of a malformed selector are kept,
// so the caller rejects them.
//
// The result of an empty key is one empty part, which the caller rejects as
// well.
std::vector<std::string> split_selector(const std::string& key) {
  size_t begin = 0;
  size_t end = key.size();
  while (begin < end && key[begin] == '/') ++begin;
  while (end > begin && key[end - 1] == '/') --end;
  std::vector<std::string> parts;
  std::string current;
  for (size_t index = begin; index < end; ++index) {
    if (key[index] == '/') {
      parts.push_back(current);
      current.clear();
    } else {
      current += key[index];
    }
  }
  parts.push_back(current);
  return parts;
}

// Read a file with an upper bound.
//
// The limit is enforced while the file is read: a file that is larger than the
// limit is never loaded completely into memory.
std::string read_bounded_file(const std::string& path, size_t limit,
                              const std::string& limit_message) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw ConfigurationError("cannot read sensor parameter file: " + path);
  }
  std::string content;
  content.reserve(64 * 1024);
  char buffer[8192];
  while (content.size() <= limit) {
    stream.read(buffer, sizeof(buffer));
    const std::streamsize count = stream.gcount();
    if (count <= 0) break;
    content.append(buffer, static_cast<size_t>(count));
  }
  if (stream.bad()) {
    throw ConfigurationError("cannot read sensor parameter file: " + path);
  }
  if (content.size() > limit) {
    throw ConfigurationError(limit_message + path);
  }
  return content;
}

// Validate the parameters of a ROS parameter file.
void validate_parameters(const Json& values, const std::string& source) {
  if (!values.is_object()) {
    throw ConfigurationError("sensor parameter YAML ros__parameters must be a mapping: " + source);
  }
  for (auto it = values.begin(); it != values.end(); ++it) {
    if (!std::regex_match(it.key(), parameter_name_re)) {
      throw ConfigurationError("invalid sensor parameter YAML parameter name: " + it.key());
    }
    if (it.value().is_object()) {
      validate_parameters(it.value(), source);
    } else {
      chaos::assets::validate_ros_parameter(it.value(), "sensor parameter YAML " + source + ": " +
                                                       it.key());
    }
  }
}

}  // namespace

Json component_asset(const Json& asset) {
  Json result = asset;
  const auto description = result.find("description");
  if (description != result.end() && description->is_object()) {
    description.value().erase("xml");
  }
  return result;
}

Json sensor_summary(const Json& asset) {
  Json result = Json::array();
  for (const Json* sensor : enabled_sensors(asset)) {
    Json entry = Json::object();
    entry["id"] = sensor->at("id");
    entry["kind"] = sensor->at("kind");
    entry["mode"] = sensor->at("driver").at("mode");
    entry["namespace"] = sensor->at("namespace");
    auto time_sync = sensor->find("time_sync");
    if (time_sync != sensor->end()) {
      entry["time_sync"] = *time_sync;
    }
    Json streams = Json::object();
    for (auto it = sensor->at("streams").begin(); it != sensor->at("streams").end(); ++it) {
      Json stream = Json::object();
      stream["topic"] = it.value().at("topic");
      stream["type"] = it.value().at("type");
      streams[it.key()] = stream;
    }
    entry["streams"] = streams;
    result.push_back(entry);
  }
  return result;
}

Json description_summary(const Json& asset) {
  const Json* description = enabled_description(asset);
  if (description == nullptr) return Json();
  Json result = Json::object();
  for (const char* key : {"file", "format", "root", "frame_prefix", "sha256"}) {
    result[key] = description->at(key);
  }
  result["link_count"] = description->at("links").size();
  result["joint_count"] = description->at("joints").size();
  return result;
}

Json collection_summary(const Json& asset) {
  const Json* collection = enabled_collection(asset);
  if (collection == nullptr) return Json();
  Json result = Json::object();
  for (const char* key : {"output_dir", "storage_id", "topics", "time_sync",
                          "max_bagfile_duration_s", "max_bagfile_size_mb", "min_free_space_mb"}) {
    result[key] = collection->at(key);
  }
  return result;
}

Json plan_summary(const LaunchPlan& plan) {
  Json result = Json::array();
  for (const Json& item : plan.items()) {
    const Json& asset = item.at("asset");
    Json entry = Json::object();
    entry["id"] = asset.at("id");
    entry["model"] = asset.at("model");
    entry["version"] = asset.at("model_version");
    entry["mode"] = asset.at("mode");
    entry["plugin"] = asset.at("driver").at("plugin");
    entry["namespace"] = asset.at("namespace");
    entry["ros_domain_id"] = asset.at("ros_domain_id");
    entry["capabilities"] = asset.at("capabilities");
    entry["mqtt"] = !item.at("mqtt").is_null();
    const auto board = item.find("board");
    entry["board"] = (board != item.end() && !board->is_null()) ? *board : Json();
    entry["board_owner"] =
        item.contains("board_owner") ? item.at("board_owner") : Json(false);
    entry["sensors"] = sensor_summary(asset);
    entry["description"] = description_summary(asset);
    entry["data_collection"] = collection_summary(asset);
    result.push_back(entry);
  }
  return result;
}

void require_executable(const std::string& package, const std::string& executable) {
  resolve_installed_executable(
      package, executable, {},
      "sensor dependency '" + package + "' is unavailable; build and source its ROS workspace",
      [&](const std::filesystem::path&) {
        return "sensor executable is unavailable: " + package + "/" + executable;
      });
}

void check_output_directory(const std::string& path) {
  std::filesystem::path candidate(path);
  while (true) {
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(candidate, error);
    if (error || !std::filesystem::exists(status)) {
      if (candidate == candidate.parent_path()) {
        throw ConfigurationError("data collection output has no existing ancestor: " + path);
      }
      candidate = candidate.parent_path();
      continue;
    }
    if (!std::filesystem::is_directory(status)) {
      throw ConfigurationError("data collection output ancestor is not a directory: " +
                               candidate.string());
    }
    if (::access(candidate.c_str(), W_OK | X_OK) != 0) {
      throw ConfigurationError("data collection output ancestor is not writable: " +
                               candidate.string());
    }
    return;
  }
}

void validate_parameter_file(const std::string& content, const std::string& node_name,
                             const std::string& source) {
  const Json parsed = chaos::assets::load_yaml_string(content, "sensor parameter YAML " + source);
  if (!parsed.is_object() || parsed.empty()) {
    throw ConfigurationError("sensor parameter YAML must be a nonempty ROS mapping: " + source);
  }
  const std::vector<std::string> node_parts = split_selector(node_name);
  std::vector<bool> found;
  // Walk the selector tree and collect the parameters of every selector that
  // matches the managed node.
  std::function<void(const Json&, std::vector<std::string>)> visit =
      [&](const Json& tree, std::vector<std::string> path) {
        if (!tree.is_object()) {
          throw ConfigurationError(
              "sensor parameter YAML requires ros__parameters under a node selector: " + source);
        }
        for (auto it = tree.begin(); it != tree.end(); ++it) {
          if (it.key() == "ros__parameters") {
            if (path.empty()) {
              throw ConfigurationError("sensor parameter YAML requires a node selector: " + source);
            }
            validate_parameters(it.value(), source);
            found.push_back(selector_matches(path, node_parts));
            continue;
          }
          const std::vector<std::string> parts = split_selector(it.key());
          bool valid = !it.key().empty() && it.key().find("//") == std::string::npos &&
                       it.key().back() != '/';
          for (const std::string& part : parts) {
            if (part != "*" && part != "**" && !std::regex_match(part, selector_part_re)) {
              valid = false;
            }
          }
          if (!valid) {
            throw ConfigurationError("invalid sensor parameter YAML node selector: " + it.key());
          }
          std::vector<std::string> next = path;
          next.insert(next.end(), parts.begin(), parts.end());
          visit(it.value(), next);
        }
      };
  visit(parsed, {});
  if (std::none_of(found.begin(), found.end(), [](bool value) { return value; })) {
    throw ConfigurationError("sensor parameter YAML has no selector matching " + node_name + ": " +
                             source + "; use /** or the full node name");
  }
}

Json snapshot_parameter_files(const LaunchPlan& plan, const std::string& root) {
  Json result = Json::object();
  for (const Json& item : plan.items()) {
    const Json& asset = item.at("asset");
    const std::string asset_id = asset.at("id").get_ref<const std::string&>();
    for (const Json* sensor : enabled_sensors(asset)) {
      const Json& driver = sensor->at("driver");
      if (driver.at("mode").get_ref<const std::string&>() != "node") continue;
      Json paths = Json::array();
      const auto files = driver.find("parameter_files");
      if (files == driver.end()) continue;
      size_t index = 0;
      for (auto it = files->begin(); it != files->end(); ++it, ++index) {
        const std::string source = it.value().get_ref<const std::string&>();
        // The file is read with a bound, so a huge file never reaches the
        // memory of the launcher.
        const std::string content =
            read_bounded_file(source, 1024u * 1024u, "sensor parameter file exceeds 1 MiB: ");
        const std::string node_name = sensor->at("namespace").get_ref<const std::string&>() +
                                      "/driver";
        validate_parameter_file(content, node_name, source);
        const std::string target = root + "/" + asset_id + "-" +
                                   sensor->at("id").get_ref<const std::string&>() + "-" +
                                   std::to_string(index) + ".yaml";
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        if (!output) {
          throw ConfigurationError("cannot write the sensor parameter snapshot: " + target);
        }
        output << content;
        output.flush();
        if (!output) {
          throw ConfigurationError("cannot write the sensor parameter snapshot: " + target);
        }
        output.close();
        if (!output) {
          throw ConfigurationError("cannot close the sensor parameter snapshot: " + target);
        }
        // The snapshot holds the private parameters of a driver, so it is
        // restricted to the owner. A failure is reported instead of being
        // ignored, because a readable snapshot would leak the configuration.
        if (::chmod(target.c_str(), S_IRUSR | S_IWUSR) != 0) {
          throw ConfigurationError(
              "cannot restrict the permissions of the sensor parameter snapshot: " + target);
        }
        paths.push_back(target);
      }
      result[asset_id + "/" + sensor->at("id").get_ref<const std::string&>()] = paths;
    }
  }
  return result;
}

void preflight(const LaunchPlan& plan, const PreflightOptions& options) {
  std::set<std::pair<std::string, std::string>> dependencies;
  // The facts that are needed after the collection loop are copied out of the
  // plan. A pointer into a plan item would keep a document alive only as long
  // as the item, and the items are handed out detached from the plan, so the
  // collected data owns its values.
  std::vector<std::string> collection_storage_ids;
  std::vector<std::string> collection_output_dirs;
  std::vector<std::string> driver_plugins;
  // The plan items are held in a local variable, so every reference into them
  // stays valid for the whole function.
  const std::vector<Json> items = plan.items();
  for (const Json& item : items) {
    const Json& asset = item.at("asset");
    const std::vector<const Json*> sensors = enabled_sensors(asset);
    if (!sensors.empty()) dependencies.insert({"chaos_sensors", "sensor_monitor"});
    for (const Json* sensor : sensors) {
      const Json& driver = sensor->at("driver");
      const std::string mode = driver.at("mode").get_ref<const std::string&>();
      if (mode == "mock") {
        dependencies.insert({"chaos_sensors", "mock_sensor"});
      } else if (mode == "node") {
        dependencies.insert({driver.at("package").get_ref<const std::string&>(),
                             driver.at("executable").get_ref<const std::string&>()});
      }
      if (sensor->find("transform") != sensor->end()) {
        dependencies.insert({"tf2_ros", "static_transform_publisher"});
      }
    }
    const auto board = item.find("board");
    if (board != item.end() && !board->is_null()) {
      if (item.contains("board_owner") && item.at("board_owner").get<bool>()) {
        dependencies.insert({"chaos_sensors", "chaos_time_sync_node"});
      }
      for (const Json* sensor : sensors) {
        const auto time_sync = sensor->find("time_sync");
        if (time_sync == sensor->end()) continue;
        if (sensor->at("kind").get_ref<const std::string&>() == "camera" &&
          time_sync->at("source").get_ref<const std::string&>() == "board_trigger")
        {
          dependencies.insert({"chaos_sensors", "trigger_pair_node"});
        }
      }
    }
    if (enabled_description(asset) != nullptr) {
      dependencies.insert({"robot_state_publisher", "robot_state_publisher"});
    }
    const Json* collection = enabled_collection(asset);
    if (collection != nullptr) {
      collection_storage_ids.push_back(
          collection->at("storage_id").get_ref<const std::string&>());
      collection_output_dirs.push_back(
          collection->at("output_dir").get_ref<const std::string&>());
      dependencies.insert({"chaos_data", "collector"});
    }
    if (asset.at("mode").get_ref<const std::string&>() == "real") {
      // Every real asset with a managed driver needs the native backend of
      // its plugin in the build. The registry knows which plugins have a
      // managed driver, so a further vendor plugin is covered by its entry.
      const std::string plugin = asset.at("driver").at("plugin").get_ref<const std::string&>();
      if (driver_metadata_for(plugin) != nullptr) driver_plugins.push_back(plugin);
    }
  }
  // The runtime validates every selected asset before a single process action
  // exists. The rules for the plugin, the capabilities and the watchdog of an
  // asset belong to the runtime, so the planner only asks.
  if (options.validate_asset) {
    for (const Json& item : items) {
      options.validate_asset(item.at("asset"));
    }
  }
  for (const auto& dependency : dependencies) {
    if (options.executable_available) {
      if (!options.executable_available(dependency.first, dependency.second)) {
        throw ConfigurationError("sensor executable is unavailable: " + dependency.first + "/" +
                                 dependency.second);
      }
    } else {
      require_executable(dependency.first, dependency.second);
    }
  }
  // The native backend of a driver plugin is a property of the build. The
  // runtime owns the detection; the planner only asks. A real asset without a
  // reported backend fails loudly, so a mock build can never enter the real
  // mode by accident.
  for (const std::string& plugin : driver_plugins) {
    if (!options.backend_available || !options.backend_available(plugin)) {
      throw ConfigurationError(
          "the native backend of the driver plugin '" + plugin +
          "' is not part of this build; the runtime must report the available backends");
    }
  }
  for (size_t index = 0; index < collection_storage_ids.size(); ++index) {
    const std::string& storage_id = collection_storage_ids[index];
    if (std::find(options.registered_writers.begin(), options.registered_writers.end(),
                  storage_id) == options.registered_writers.end()) {
      throw ConfigurationError("data collection storage plugin is unavailable: " + storage_id);
    }
    if (options.check_files) {
      check_output_directory(collection_output_dirs[index]);
    }
  }
}

std::vector<ProcessSpec> driver_processes(const Json& asset) {
  std::vector<ProcessSpec> result;
  if (asset.at("mode").get_ref<const std::string&>() != "real") return result;
  const std::string plugin = asset.at("driver").at("plugin").get_ref<const std::string&>();
  const DriverMetadata* metadata = driver_metadata_for(plugin);
  // A plugin without a managed driver has no process of its own; the mock
  // backend of an asset is served by the coordinator.
  if (metadata == nullptr) return result;
  ProcessSpec spec;
  spec.label = std::string(metadata->label) + " " + asset.at("id").get_ref<const std::string&>();
  spec.package = "chaos_runtime";
  spec.executable = metadata->driver_executable;
  spec.name = metadata->driver_name;
  spec.node_namespace = asset.at("namespace").get_ref<const std::string&>();
  Json parameters = Json::array();
  Json entry = Json::object();
  // The runtime receives the asset as one JSON document; the payload is
  // serialized here so that the launch glue only has to mark its type.
  entry["asset_json"] = asset.dump();
  parameters.push_back(entry);
  spec.parameters = parameters;
  result.push_back(spec);
  return result;
}

std::vector<ProcessSpec> description_processes(const Json& asset) {
  std::vector<ProcessSpec> result;
  const Json* description = enabled_description(asset);
  if (description == nullptr) return result;
  ProcessSpec spec;
  spec.label = "Robot description " + asset.at("id").get_ref<const std::string&>();
  spec.package = "robot_state_publisher";
  spec.executable = "robot_state_publisher";
  spec.name = "robot_state_publisher";
  spec.node_namespace = asset.at("namespace").get_ref<const std::string&>();
  Json parameters = Json::array();
  Json entry = Json::object();
  entry["robot_description"] = description->at("xml");
  entry["frame_prefix"] = description->at("frame_prefix");
  entry["publish_frequency"] = description->at("publish_frequency");
  entry["use_sim_time"] = description->at("use_sim_time");
  parameters.push_back(entry);
  spec.parameters = parameters;
  spec.remappings.emplace_back("joint_states",
                                description->at("joint_states_topic").get_ref<const std::string&>());
  spec.remappings.emplace_back("tf", "/tf");
  spec.remappings.emplace_back("tf_static", "/tf_static");
  result.push_back(spec);
  return result;
}

std::vector<ProcessSpec> sensor_processes(const Json& asset, const Json& parameter_files) {
  std::vector<ProcessSpec> result;
  const std::string asset_id = asset.at("id").get_ref<const std::string&>();
  for (const Json* sensor : enabled_sensors(asset)) {
    const Json& driver = sensor->at("driver");
    const std::string mode = driver.at("mode").get_ref<const std::string&>();
    const std::string sensor_id = sensor->at("id").get_ref<const std::string&>();
    if (mode == "mock" || mode == "node") {
      ProcessSpec spec;
      spec.label = "Sensor " + asset_id + "/" + sensor_id;
      spec.package = mode == "mock" ? "chaos_sensors" : driver.at("package").get_ref<const std::string&>();
      spec.executable = mode == "mock" ? "mock_sensor" : driver.at("executable").get_ref<const std::string&>();
      spec.name = "driver";
      spec.node_namespace = sensor->at("namespace").get_ref<const std::string&>();
      Json parameters = Json::array();
      if (mode == "mock") {
        Json entry = Json::object();
        entry["sensor_json"] = sensor->dump();
        parameters.push_back(entry);
      } else {
        const std::string key = asset_id + "/" + sensor_id;
        const auto files = parameter_files.find(key);
        if (files != parameter_files.end()) {
          for (const Json& file : files.value()) {
            Json entry = Json::object();
            entry["__parameter_file"] = file;
            parameters.push_back(entry);
          }
        }
        const auto values = driver.find("parameters");
        if (values != driver.end()) {
          for (auto it = values->begin(); it != values->end(); ++it) {
            Json entry = Json::object();
            entry[it.key()] = it.value();
            parameters.push_back(entry);
          }
        }
        const auto remappings = driver.find("remappings");
        if (remappings != driver.end()) {
          for (auto it = remappings->begin(); it != remappings->end(); ++it) {
            spec.remappings.emplace_back(it.key(), it.value().get_ref<const std::string&>());
          }
        }
      }
      spec.parameters = parameters;
      result.push_back(spec);
    }
    const auto transform = sensor->find("transform");
    if (transform != sensor->end()) {
      ProcessSpec spec;
      spec.label = "Sensor transform " + asset_id + "/" + sensor_id;
      spec.package = "tf2_ros";
      spec.executable = "static_transform_publisher";
      spec.name = sensor_id + "_static_tf";
      spec.node_namespace = asset.at("namespace").get_ref<const std::string&>();
      Json arguments = Json::array();
      const Json& xyz = transform.value().at("xyz");
      const Json& rpy = transform.value().at("rpy");
      const char* const names[] = {"x", "y", "z", "roll", "pitch", "yaw"};
      for (size_t index = 0; index < 6; ++index) {
        const double value = index < 3 ? xyz.at(index).get<double>() : rpy.at(index - 3).get<double>();
        arguments.push_back(std::string("--") + names[index]);
        arguments.push_back(chaos::assets::py_repr_float(value));
      }
      arguments.push_back("--frame-id");
      arguments.push_back(transform.value().at("parent"));
      arguments.push_back("--child-frame-id");
      arguments.push_back(sensor->at("frame_id"));
      spec.arguments = arguments;
      result.push_back(spec);
    }
  }
  if (!enabled_sensors(asset).empty()) {
    ProcessSpec spec;
    spec.label = "Sensor monitor " + asset_id;
    spec.package = "chaos_sensors";
    spec.executable = "sensor_monitor";
    spec.name = "sensor_monitor";
    spec.node_namespace = asset.at("namespace").get_ref<const std::string&>();
    Json parameters = Json::array();
    Json entry = Json::object();
    // The asset is passed as one JSON document: a ROS parameter cannot hold
    // the nested document of an asset with null values, and the monitor
    // parses the document itself.
    entry["asset_json"] = asset.dump();
    parameters.push_back(entry);
    spec.parameters = parameters;
    result.push_back(spec);
  }
  return result;
}

std::vector<ProcessSpec> time_sync_processes(const Json& item) {
  std::vector<ProcessSpec> result;
  if (!item.contains("asset")) return result;
  const Json& asset = item.at("asset");
  if (asset.at("mode").get_ref<const std::string&>() != "real") return result;
  const auto board = item.find("board");
  if (board == item.end() || board->is_null()) return result;

  const std::string asset_id = asset.at("id").get_ref<const std::string&>();
  const std::string host = asset.at("host").get_ref<const std::string&>();

  const bool owner = item.contains("board_owner") && item.at("board_owner").get<bool>();
  if (owner) {
    // One client per host: the board's UTC comes from this process, so a
    // second one on the same host would fight over the same UDP peer.
    ProcessSpec spec;
    spec.label = "Time sync board " + host;
    spec.package = "chaos_sensors";
    spec.executable = "chaos_time_sync_node";
    spec.name = "chaos_time_sync";
    spec.node_namespace = "/";
    Json entry = Json::object();
    entry["board_ip"] = board->at("ip");
    entry["board_port"] = board->at("port");
    entry["local_port"] = board->at("local_port");
    entry["ptp_period_ms"] = board->at("ptp_period_ms");
    entry["publish_imu"] = board->at("publish_imu");
    // Absolute topics: the pairing processes live in sensor namespaces and
    // must reach the same board streams.
    entry["trigger_topic"] = "/board/trigger";
    entry["imu_topic"] = "/board/imu";
    entry["diagnostics_topic"] = "/board/diagnostics";
    Json parameters = Json::array();
    parameters.push_back(entry);
    spec.parameters = parameters;
    result.push_back(spec);
  }

  for (const Json* sensor : enabled_sensors(asset)) {
    if (sensor->at("kind").get_ref<const std::string&>() != "camera") continue;
    const auto time_sync = sensor->find("time_sync");
    if (time_sync == sensor->end()) continue;
    if (time_sync->at("source").get_ref<const std::string&>() != "board_trigger") continue;

    const Json& streams = sensor->at("streams");
    const Json* image = nullptr;
    const Json* info = nullptr;
    // The declaration names the stream to pair when a camera has more than
    // one; otherwise fall back to the only Image stream.
    const auto selected = time_sync->find("image_stream");
    if (selected != time_sync->end()) {
      const std::string& name = selected->get_ref<const std::string&>();
      const auto stream = streams.find(name);
      if (stream != streams.end()) image = &stream.value();
    }
    for (auto it = streams.begin(); it != streams.end(); ++it) {
      const std::string& type = it.value().at("type").get_ref<const std::string&>();
      if (info == nullptr && type == "sensor_msgs/msg/CameraInfo") info = &it.value();
      if (image == nullptr && type == "sensor_msgs/msg/Image") image = &it.value();
    }
    if (image == nullptr) continue;  // load-time validation rejects this

    const std::string sensor_id = sensor->at("id").get_ref<const std::string&>();
    const std::string ns = sensor->at("namespace").get_ref<const std::string&>();

    ProcessSpec spec;
    spec.label = "Camera trigger " + asset_id + "/" + sensor_id;
    spec.package = "chaos_sensors";
    spec.executable = "trigger_pair_node";
    spec.name = "trigger_pair";
    spec.node_namespace = ns;
    Json entry = Json::object();
    entry["trigger_topic"] = "/board/trigger";
    entry["image_topic"] = image->at("topic");
    entry["image_output_topic"] = ns + "/image_synced";
    entry["diagnostics_topic"] = ns + "/diagnostics";
    entry["board_channel"] = time_sync->at("board_channel");
    entry["max_age_ms"] = time_sync->at("max_age_ms");
    entry["exposure_offset_us"] = time_sync->at("exposure_offset_us");
    const auto trigger_hz = time_sync->find("trigger_hz");
    entry["expected_period_ms"] =
        (trigger_hz != time_sync->end() && trigger_hz->get<double>() > 0.0)
            ? Json(1000.0 / trigger_hz->get<double>())
            : Json(0.0);
    if (info != nullptr) {
      entry["publish_camera_info"] = true;
      entry["camera_info_topic"] = info->at("topic");
      entry["camera_info_output_topic"] = ns + "/camera_info_synced";
    } else {
      entry["publish_camera_info"] = false;
      entry["camera_info_topic"] = "";
      entry["camera_info_output_topic"] = "";
    }
    Json parameters = Json::array();
    parameters.push_back(entry);
    spec.parameters = parameters;
    result.push_back(spec);
  }
  return result;
}

std::vector<ProcessSpec> collection_processes(const Json& asset) {
  std::vector<ProcessSpec> result;
  if (enabled_collection(asset) == nullptr) return result;
  ProcessSpec spec;
  spec.label = "Data collection " + asset.at("id").get_ref<const std::string&>();
  spec.package = "chaos_data";
  spec.executable = "collector";
  spec.name = "data_collector";
  spec.node_namespace = asset.at("namespace").get_ref<const std::string&>();
  Json parameters = Json::array();
  Json entry = Json::object();
  entry["asset_json"] = component_asset(asset).dump();
  entry["use_sim_time"] = false;
  parameters.push_back(entry);
  spec.parameters = parameters;
  result.push_back(spec);
  return result;
}

}  // namespace chaos::bringup
