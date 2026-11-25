#include "chaos_assets/inventory.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "chaos_assets/cli.hpp"
#include "chaos_assets/collection.hpp"
#include "chaos_assets/description.hpp"
#include "chaos_assets/json.hpp"
#include "chaos_assets/paths.hpp"
#include "chaos_assets/sensors.hpp"
#include "chaos_assets/yaml.hpp"

namespace chaos::assets {

namespace {

std::regex identifier_re(R"(^[A-Za-z][A-Za-z0-9_]*$)");
std::regex token_re(R"(^[A-Za-z0-9][A-Za-z0-9_.-]*$)");
std::regex namespace_re(R"(^(/[A-Za-z][A-Za-z0-9_]*)+$)");
std::regex frame_re(R"(^[A-Za-z][A-Za-z0-9_/]*$)");

// A literal IPv4 address, checked the same way the sync board client parses it.
std::string ipv4(const Json& value, const std::string& where) {
  const std::string address = text(value, where);
  in_addr parsed{};
  if (::inet_pton(AF_INET, address.c_str(), &parsed) != 1) {
    throw ConfigurationError(where + ": expected an IPv4 address, got '" + address + "'");
  }
  return address;
}

// The default motion and lifetime limits of every asset.
const std::vector<std::string> kDefaultLimits = {"linear_mps",    "angular_rps",  "command_timeout_s",
                                                 "state_timeout_s", "lease_max_s", "action_timeout_s",
                                                 "max_altitude_m"};

Json default_limits() {
  Json result = Json::object();
  result["linear_mps"] = 1.0;
  result["angular_rps"] = 1.0;
  result["command_timeout_s"] = 0.5;
  result["state_timeout_s"] = 2.0;
  result["lease_max_s"] = 30.0;
  result["action_timeout_s"] = 120.0;
  result["max_altitude_m"] = 20.0;
  return result;
}

// Read a file and require a mapping at its root.
Json load_document(const std::string& path) {
  Json data = load_yaml_file(path);
  record(data, path);
  return data;
}

// Validate a capability list: nonempty, unique, token shaped and including
// the mandatory state capability.
std::vector<std::string> capabilities(const Json& value, const std::string& where) {
  if (!value.is_array() || value.empty()) {
    throw ConfigurationError(where + ": expected a nonempty capability list");
  }
  std::vector<std::string> result;
  std::set<std::string> seen;
  for (const Json& item : value) {
    if (!item.is_string() || !std::regex_match(item.get_ref<const std::string&>(), token_re)) {
      throw ConfigurationError(where + ": invalid nonempty string '" +
                               (item.is_string() ? item.get_ref<const std::string&>() : item.dump()) +
                               "'");
    }
    const std::string capability = item.get_ref<const std::string&>();
    if (!seen.insert(capability).second) {
      throw ConfigurationError(where + ": duplicate capabilities");
    }
    result.push_back(capability);
  }
  if (seen.count("state") == 0) {
    throw ConfigurationError(where + ": state capability is required");
  }
  return result;
}

// Merge the model limits with the instance overrides.
Json limits(const Json& base, const Json& overrides, const std::string& where) {
  Json result = default_limits();
  for (const Json* source : {&base, &overrides}) {
    record(*source, where, kDefaultLimits);
    for (auto it = source->begin(); it != source->end(); ++it) {
      const double value = finite_number(it.value(), where + "." + it.key());
      if (value <= 0.0) {
        throw ConfigurationError(where + "." + it.key() + ": expected a positive finite number");
      }
      result[it.key()] = value;
    }
  }
  return result;
}

std::string resolve_path_value(const Json& value, const std::string& base, const std::string& where) {
  return resolve_path(value, base, where);
}

// A real asset whose sensors claim the sync board must have a host board
// block. Without it the board client would never run and the frames would
// silently keep their arrival timestamps.
void validate_board_requirements(const Json& hosts, const std::vector<Json>& assets) {
  for (const Json& asset : assets) {
    if (!asset.at("enabled").get<bool>()) continue;
    if (asset.at("mode").get_ref<const std::string&>() != "real") continue;
    const std::string& host = asset.at("host").get_ref<const std::string&>();
    const bool have_board = hosts.at(host).contains("board");
    for (auto it = asset.at("sensors").begin(); it != asset.at("sensors").end(); ++it) {
      const Json& sensor = it.value();
      if (!sensor.at("enabled").get<bool>()) continue;
      const auto time_sync = sensor.find("time_sync");
      if (time_sync == sensor.end()) continue;
      const std::string source = time_sync->at("source").get_ref<const std::string&>();
      const std::string requires_sync =
          time_sync->at("requires").get_ref<const std::string&>();
      const std::string clock = time_sync->at("clock_domain").get_ref<const std::string&>();
      const std::string channel = time_sync->at("board_channel").get_ref<const std::string&>();
      const bool needs_board = source == "board_trigger" || requires_sync != "none" ||
                               clock == "board" || channel == "LIDAR_1" ||
                               channel == "LIDAR_2";
      if (needs_board && !have_board) {
        throw ConfigurationError(
            asset.at("id").get_ref<const std::string&>() + ".sensors." + it.key() +
            ".time_sync: this sensor needs the sync board, but hosts." + host +
            " has no 'board' block (ip/port)");
      }
    }
  }
}

}  // namespace

Inventory::Inventory(std::string path, Json hosts, std::vector<Json> assets, Json transports)
    : path_(std::move(path)), hosts_(std::move(hosts)), assets_(std::move(assets)),
      transports_(std::move(transports)) {}

Json Inventory::as_dict() const {
  Json result = Json::object();
  result["schema_version"] = 1;
  result["source"] = path_;
  result["hosts"] = hosts_;
  result["assets"] = assets_;
  result["transports"] = transports_;
  return result;
}

std::vector<Json> Inventory::select(const std::string& host_id,
                                     const std::vector<std::string>& asset_ids) const {
  if (hosts_.find(host_id) == hosts_.end()) {
    throw ConfigurationError("unknown host_id: " + host_id);
  }
  std::set<std::string> requested(asset_ids.begin(), asset_ids.end());
  std::set<std::string> enabled_ids;
  std::vector<Json> enabled;
  for (const Json& asset : assets_) {
    if (!asset.at("enabled").get<bool>()) continue;
    if (asset.at("host").get_ref<const std::string&>() != host_id) continue;
    enabled.push_back(asset);
    enabled_ids.insert(asset.at("id").get_ref<const std::string&>());
  }
  std::vector<std::string> unavailable;
  for (const std::string& id : requested) {
    if (enabled_ids.count(id) == 0) unavailable.push_back(id);
  }
  if (!unavailable.empty()) {
    std::sort(unavailable.begin(), unavailable.end());
    std::string names = "[";
    for (size_t index = 0; index < unavailable.size(); ++index) {
      if (index) names += ", ";
      names += "'" + unavailable[index] + "'";
    }
    names += "]";
    throw ConfigurationError("assets unavailable on " + host_id + ": " + names);
  }
  std::vector<Json> selected;
  for (const Json& asset : enabled) {
    if (requested.empty() || requested.count(asset.at("id").get_ref<const std::string&>()) != 0) {
      selected.push_back(asset);
    }
  }
  if (selected.empty()) {
    throw ConfigurationError("no enabled assets selected on " + host_id);
  }
  return selected;
}

Inventory load_inventory(const std::string& path, const std::string& mode) {
  if (!mode.empty() && mode != "mock" && mode != "real") {
    throw ConfigurationError("mode override must be mock or real");
  }
  const std::string absolute =
      std::filesystem::weakly_canonical(std::filesystem::path(expand_user(path))).string();
  const std::string directory = std::filesystem::path(absolute).parent_path().string();
  const Json data = load_document(absolute);
  record(data, "inventory",
         {"schema_version", "hosts", "assets", "transports"});
  schema(data, "inventory");

  Json hosts = record(data.contains("hosts") ? data.at("hosts") : Json(), "hosts");
  if (hosts.empty()) {
    throw ConfigurationError("hosts must not be empty");
  }
  for (auto it = hosts.begin(); it != hosts.end(); ++it) {
    const std::string host = it.key();
    text(host, "host ID", &identifier_re);
    record(it.value(), "hosts." + host, {"ros_domain_id", "board"});
    const auto domain = it.value().find("ros_domain_id");
    int64_t value = 0;
    if (domain == it.value().end() || !int64_value(*domain, &value) || value < 0 || value > 232) {
      throw ConfigurationError("hosts." + host + ".ros_domain_id: expected integer 0..232");
    }
    // An optional sync board of this host. Its presence is the explicit switch
    // for the board client: sensor declarations alone never start hardware.
    auto board = it.value().find("board");
    if (board != it.value().end()) {
      const std::string where = "hosts." + host + ".board";
      record(board.value(), where, {"ip", "port", "local_port", "ptp_period_ms", "publish_imu"});
      Json normalized = Json::object();
      normalized["ip"] = ipv4(board->contains("ip") ? board->at("ip") : Json(), where + ".ip");
      normalized["port"] = integer(board->contains("port") ? board->at("port") : Json(8888),
                                   where + ".port", 1, 65535);
      normalized["local_port"] = integer(
          board->contains("local_port") ? board->at("local_port") : Json(0),
          where + ".local_port", 0, 65535);
      normalized["ptp_period_ms"] = integer(
          board->contains("ptp_period_ms") ? board->at("ptp_period_ms") : Json(100),
          where + ".ptp_period_ms", 1, 10000);
      const Json publish_imu =
          board->contains("publish_imu") ? board->at("publish_imu") : Json(true);
      if (!publish_imu.is_boolean()) {
        throw ConfigurationError(where + ".publish_imu: expected boolean");
      }
      normalized["publish_imu"] = publish_imu;
      it.value()["board"] = normalized;
    }
  }

  Json transports = record(data.contains("transports") ? data.at("transports") : Json(Json::object()),
                           "transports");
  for (auto it = transports.begin(); it != transports.end(); ++it) {
    const std::string name = it.key();
    text(name, "transport profile name", &identifier_re);
    Json& profile = it.value();
    record(profile, "transports." + name, {"executable", "config"});
    const std::string executable = text(
        profile.contains("executable") ? profile.at("executable") : Json("mqtt_transport"),
        "transports." + name + ".executable");
    profile["executable"] = executable;
    if (executable.find('/') != std::string::npos) {
      profile["executable"] = resolve_path_value(profile.at("executable"), directory,
                                                 "transports." + name + ".executable");
    }
    record(profile.contains("config") ? profile.at("config") : Json(),
           "transports." + name + ".config");
    Json& config = profile.at("config");
    const auto mqtt_entry = config.find("mqtt");
    if (mqtt_entry == config.end()) {
      // A profile without an MQTT section stays without one; the empty
      // section is only validated, never added back.
      record(Json(Json::object()), "transports." + name + ".config.mqtt");
      continue;
    }
    Json& mqtt = mqtt_entry.value();
    record(mqtt, "transports." + name + ".config.mqtt");
    if (mqtt.contains("password")) {
      throw ConfigurationError("MQTT passwords must use password_environment, not inline password");
    }
    for (const char* key : {"ca_file", "client_certificate_file", "client_key_file"}) {
      if (mqtt.contains(key)) {
        mqtt[key] = resolve_path_value(mqtt.at(key), directory, std::string("mqtt.") + key);
      }
    }
  }

  const Json& entries = data.contains("assets") ? data.at("assets") : Json();
  if (!entries.is_array() || entries.empty()) {
    throw ConfigurationError("assets: expected a nonempty list");
  }

  std::vector<Json> assets;
  std::set<std::string> ids;
  std::set<std::string> namespaces;
  std::set<std::pair<int64_t, std::string>> frames_seen;
  for (const Json& entry : entries) {
    record(entry, "asset",
           {"id", "model_file", "host", "namespace", "mode", "enabled", "driver", "capabilities",
            "frames", "limits", "transport", "sensors", "description", "data_collection"});
    const std::string asset_id = text(entry.contains("id") ? entry.at("id") : Json(), "asset.id",
                                      &identifier_re);
    if (!ids.insert(asset_id).second) {
      throw ConfigurationError("duplicate asset ID: " + asset_id);
    }
    const Json enabled_value = entry.contains("enabled") ? entry.at("enabled") : Json(true);
    if (!enabled_value.is_boolean()) {
      throw ConfigurationError(asset_id + ".enabled: expected boolean");
    }
    const bool enabled = enabled_value.get<bool>();
    const std::string host = text(entry.contains("host") ? entry.at("host") : Json(),
                                  asset_id + ".host");
    if (hosts.find(host) == hosts.end()) {
      throw ConfigurationError(asset_id + ": unknown host " + host);
    }
    const std::string namespace_ = text(
        entry.contains("namespace") ? entry.at("namespace") : Json("/robots/" + asset_id),
        asset_id + ".namespace", &namespace_re);
    if (!namespaces.insert(namespace_).second) {
      throw ConfigurationError("duplicate namespace: " + namespace_);
    }
    const std::string model_path = resolve_path_value(
        entry.contains("model_file") ? entry.at("model_file") : Json(), directory,
        asset_id + ".model_file");
    const Json model = load_document(model_path);
    record(model, model_path,
           {"schema_version", "model", "model_version", "kind", "capabilities", "backends", "limits",
            "resources", "sensors", "description", "data_collection"});
    for (const Json* declaration : {&model, &entry}) {
      if (declaration->contains("description")) {
        record(declaration->at("description"), asset_id + ".description");
      }
      if (declaration->contains("data_collection")) {
        record(declaration->at("data_collection"), asset_id + ".data_collection");
      }
    }
    schema(model, model_path);
    const std::string model_id = text(model.contains("model") ? model.at("model") : Json(),
                                      "model.model", &token_re);
    const std::string version = text(model.contains("model_version") ? model.at("model_version") : Json(),
                                     "model.model_version", &token_re);
    const std::string kind = text(model.contains("kind") ? model.at("kind") : Json(), "model.kind",
                                  &token_re);
    const std::vector<std::string> supported =
        capabilities(model.contains("capabilities") ? model.at("capabilities") : Json(),
                      model_id + ".capabilities");
    Json supported_json = Json::array();
    for (const std::string& capability : supported) supported_json.push_back(capability);
    std::string selected_mode = mode;
    if (selected_mode.empty()) {
      if (entry.contains("mode")) {
        if (!entry.at("mode").is_string()) {
          throw ConfigurationError(asset_id + ".mode: expected mock or real");
        }
        selected_mode = entry.at("mode").get_ref<const std::string&>();
      } else {
        selected_mode = "mock";
      }
    }
    if (selected_mode != "mock" && selected_mode != "real") {
      throw ConfigurationError(asset_id + ".mode: expected mock or real");
    }
    const Json backends_value =
        model.contains("backends") ? model.at("backends") : Json();
    const Json& backends = record(backends_value, model_id + ".backends", {"mock", "real"});
    if (backends.find(selected_mode) == backends.end()) {
      throw ConfigurationError(model_id + ": no " + selected_mode + " backend");
    }
    const Json backend_value = backends.at(selected_mode);
    const Json& backend = record(backend_value, model_id + "." + selected_mode,
                                 {"plugin", "options", "capabilities"});
    const std::string plugin = text(backend.contains("plugin") ? backend.at("plugin") : Json(),
                                    "driver.plugin", &token_re);
    const std::vector<std::string> backend_caps =
        capabilities(backend.contains("capabilities") ? backend.at("capabilities") : supported_json,
                      "backend.capabilities");
    for (const std::string& capability : backend_caps) {
      if (std::find(supported.begin(), supported.end(), capability) == supported.end()) {
        throw ConfigurationError(model_id + ": backend advertises undeclared capabilities");
      }
    }
    Json backend_caps_json = Json::array();
    for (const std::string& capability : backend_caps) backend_caps_json.push_back(capability);
    const std::vector<std::string> caps =
        capabilities(entry.contains("capabilities") ? entry.at("capabilities") : backend_caps_json,
                     asset_id + ".capabilities");
    std::vector<std::string> unsupported;
    for (const std::string& capability : caps) {
      if (std::find(backend_caps.begin(), backend_caps.end(), capability) == backend_caps.end()) {
        unsupported.push_back(capability);
      }
    }
    if (!unsupported.empty()) {
      std::sort(unsupported.begin(), unsupported.end());
      std::string names = "[";
      for (size_t index = 0; index < unsupported.size(); ++index) {
        if (index) names += ", ";
        names += "'" + unsupported[index] + "'";
      }
      names += "]";
      throw ConfigurationError(asset_id + ": capabilities unsupported by " + selected_mode +
                               " backend: " + names);
    }
    const Json override_value =
        entry.contains("driver") ? entry.at("driver") : Json(Json::object());
    const Json& override = record(override_value, asset_id + ".driver", {"options"});
    const Json options = deep_merge(
        record(backend.contains("options") ? backend.at("options") : Json(Json::object()),
               "backend.options"),
        record(override.contains("options") ? override.at("options") : Json(Json::object()),
               "driver.options"));

    Json frames = Json::object();
    frames["base"] = asset_id + "/base_link";
    frames["odom"] = asset_id + "/odom";
    frames["map"] = asset_id + "/map";
    const Json frame_override_value =
        entry.contains("frames") ? entry.at("frames") : Json(Json::object());
    const Json& frame_overrides =
        record(frame_override_value, asset_id + ".frames", {"base", "odom", "map"});
    for (auto it = frame_overrides.begin(); it != frame_overrides.end(); ++it) {
      frames[it.key()] = it.value();
    }
    const int64_t domain = hosts.at(host).at("ros_domain_id").get<int64_t>();
    for (auto it = frames.begin(); it != frames.end(); ++it) {
      const std::string frame = text(it.value(), asset_id + ".frames." + it.key(), &frame_re);
      if (frame.find("//") != std::string::npos || frame.back() == '/') {
        throw ConfigurationError("invalid frame ID: " + frame);
      }
      // Shared map and odom frames may be supplied by a fleet localization
      // system; body frames stay unique inside a domain.
      if (it.key() == "base") {
        const auto key = std::make_pair(domain, frame);
        if (!frames_seen.insert(key).second) {
          throw ConfigurationError("duplicate base frame in ROS domain: " + frame);
        }
      }
    }

    Json resources = Json::object();
    const Json model_resource_value =
        model.contains("resources") ? model.at("resources") : Json(Json::object());
    const Json& model_resources = record(model_resource_value, "model.resources");
    for (auto it = model_resources.begin(); it != model_resources.end(); ++it) {
      const std::string resource = resolve_path_value(it.value(),
                                                      std::filesystem::path(model_path).parent_path().string(),
                                                      "resources." + it.key());
      if (!std::filesystem::is_regular_file(resource)) {
        throw ConfigurationError("resource does not exist: " + resource);
      }
      resources[it.key()] = resource;
    }

    Json transport = Json();
    if (entry.contains("transport") && !entry.at("transport").is_null()) {
      const Json& value = entry.at("transport");
      if (!value.is_string() || transports.find(value.get_ref<const std::string&>()) == transports.end()) {
        throw ConfigurationError(asset_id + ": unknown transport profile '" + value.dump() + "'");
      }
      transport = value;
    }

    Json asset = Json::object();
    asset["id"] = asset_id;
    asset["model"] = model_id;
    asset["model_version"] = version;
    asset["kind"] = kind;
    asset["host"] = host;
    asset["ros_domain_id"] = domain;
    asset["namespace"] = namespace_;
    asset["enabled"] = enabled;
    asset["mode"] = selected_mode;
    Json driver = Json::object();
    driver["plugin"] = plugin;
    driver["options"] = options;
    asset["driver"] = driver;
    Json capability_list = Json::array();
    for (const std::string& capability : caps) capability_list.push_back(capability);
    asset["capabilities"] = capability_list;
    asset["frames"] = frames;
    asset["limits"] = limits(model.contains("limits") ? model.at("limits") : Json(Json::object()),
                              entry.contains("limits") ? entry.at("limits") : Json(Json::object()),
                              asset_id + ".limits");
    asset["resources"] = resources;
    asset["transport"] = transport;
    asset["model_file"] = model_path;

    const std::string model_directory = std::filesystem::path(model_path).parent_path().string();
    asset["description"] =
        resolve_description(model.contains("description") ? model.at("description") : Json(),
                            entry.contains("description") ? entry.at("description") : Json(),
                            model_directory, directory, asset);
    asset["sensors"] = resolve_sensors(model.contains("sensors") ? model.at("sensors") : Json(),
                                       entry.contains("sensors") ? entry.at("sensors") : Json(),
                                       model_directory, directory, asset);
    asset["data_collection"] =
        resolve_collection(model.contains("data_collection") ? model.at("data_collection") : Json(),
                           entry.contains("data_collection") ? entry.at("data_collection") : Json(),
                           model_directory, directory, asset);
    asset["config_digest"] = config_digest(asset);
    assets.push_back(asset);
  }

  validate_sensor_graph(assets);
  validate_description_graph(assets);
  validate_board_requirements(hosts, assets);
  return Inventory(absolute, hosts, assets, transports);
}

std::string resolve_inventory_json(const std::string& path, const std::string& mode,
                                   const std::string& host) {
  const Inventory inventory = load_inventory(path, mode);
  Json result = inventory.as_dict();
  if (!host.empty()) {
    result["assets"] = inventory.select(host);
  }
  return result.dump(2) + "\n";
}

}  // namespace chaos::assets
