#include "chaos_assets/collection.hpp"

#include <filesystem>
#include <regex>
#include <set>
#include <vector>

#include "chaos_assets/paths.hpp"
#include "chaos_assets/sensors.hpp"

namespace chaos::assets {

namespace {

std::regex topic_re(R"(^(/[A-Za-z_][A-Za-z0-9_]*)+$)");

const std::vector<std::string> kScalarFields = {"enabled", "output_dir", "storage_id", "include_state",
                                                "include_sensors", "include_tf", "max_bagfile_duration_s",
                                                "max_bagfile_size_mb", "max_cache_size_mb",
                                                "min_free_space_mb", "time_sync", "topics"};

Json defaults() {
  Json value = Json::object();
  value["enabled"] = false;
  value["output_dir"] = "/var/lib/chaos/recordings";
  value["storage_id"] = "mcap";
  value["include_state"] = true;
  value["include_sensors"] = true;
  value["include_tf"] = true;
  value["max_bagfile_duration_s"] = 300;
  value["max_bagfile_size_mb"] = 1024;
  value["max_cache_size_mb"] = 64;
  value["min_free_space_mb"] = 1024;
  Json timing = Json::object();
  timing["require_sync"] = true;
  timing["max_uncertainty_ms"] = 10.0;
  timing["max_reference_age_s"] = 120.0;
  timing["poll_interval_s"] = 1.0;
  timing["probe_timeout_s"] = 0.5;
  timing["stable_samples"] = 3;
  timing["jump_threshold_ms"] = 100.0;
  value["time_sync"] = timing;
  return value;
}

const std::vector<std::string>& time_sync_fields() {
  static const std::vector<std::string> fields = {"require_sync",        "max_uncertainty_ms",
                                                  "max_reference_age_s", "poll_interval_s",
                                                  "probe_timeout_s",     "stable_samples",
                                                  "jump_threshold_ms"};
  return fields;
}

}  // namespace

Json resolve_collection(const Json& model, const Json& instance, const std::string& model_dir,
                        const std::string& instance_dir, const Json& asset) {
  if (model.is_null() && instance.is_null()) return nullptr;
  const std::string where = asset.at("id").get_ref<const std::string&>() + ".data_collection";
  for (const Json* declaration : {&model, &instance}) {
    if (declaration->is_null()) continue;
    record(*declaration, where, kScalarFields);
    auto timing = declaration->find("time_sync");
    if (timing != declaration->end()) {
      record(*timing, where + ".time_sync", time_sync_fields());
    }
  }
  Json result = defaults();
  if (!model.is_null()) result = deep_merge(result, model);
  if (!instance.is_null()) result = deep_merge(result, instance);

  for (const char* name : {"enabled", "include_state", "include_sensors", "include_tf"}) {
    if (!result.at(name).is_boolean()) {
      throw ConfigurationError(where + "." + name + ": expected boolean");
    }
  }
  const Json& storage = result.at("storage_id");
  if (!storage.is_string() ||
      (storage.get_ref<const std::string&>() != "mcap" && storage.get_ref<const std::string&>() != "sqlite3")) {
    throw ConfigurationError(where + ".storage_id: expected mcap or sqlite3");
  }
  const Json& output = result.at("output_dir");
  if (!output.is_string() || output.get_ref<const std::string&>().empty() ||
      output.get_ref<const std::string&>().find('\0') != std::string::npos ||
      output.get_ref<const std::string&>().find("://") != std::string::npos) {
    throw ConfigurationError(where + ".output_dir: expected a local directory path");
  }
  const bool instance_has_output =
      !instance.is_null() && instance.contains("output_dir");
  const std::string& base = instance_has_output ? instance_dir : model_dir;
  std::filesystem::path output_path(expand_user(output.get_ref<const std::string&>()));
  std::filesystem::path joined =
      output_path.is_absolute() ? output_path : std::filesystem::path(base) / output_path;
  std::error_code ec;
  std::string resolved = std::filesystem::weakly_canonical(joined, ec).string();
  result["output_dir"] = ec ? joined.lexically_normal().string() : resolved;

  struct Range {
    const char* name;
    int64_t low;
    int64_t high;
  };
  for (const Range& range : {Range{"max_bagfile_duration_s", 0, 86400},
                             Range{"max_bagfile_size_mb", 0, 1048576},
                             Range{"max_cache_size_mb", 1, 2048},
                             Range{"min_free_space_mb", 1, 1048576}}) {
    integer(result.at(range.name), where + "." + range.name, range.low, range.high);
  }
  if (result.at("max_bagfile_duration_s").get<int64_t>() == 0 &&
      result.at("max_bagfile_size_mb").get<int64_t>() == 0) {
    throw ConfigurationError(where + ": at least one bag splitting limit must be positive");
  }
  if (result.at("min_free_space_mb").get<int64_t>() < result.at("max_cache_size_mb").get<int64_t>()) {
    throw ConfigurationError(where + ": free-space reserve must cover the recorder cache");
  }

  Json& timing = result["time_sync"];
  if (!timing.at("require_sync").is_boolean()) {
    throw ConfigurationError(where + ".time_sync.require_sync: expected boolean");
  }
  struct TimingRange {
    const char* name;
    double low;
    double high;
  };
  for (const TimingRange& range :
       {TimingRange{"max_uncertainty_ms", 0.001, 1000.0},
        TimingRange{"max_reference_age_s", 1.0, 3600.0},
        TimingRange{"poll_interval_s", 0.1, 10.0},
        TimingRange{"probe_timeout_s", 0.01, 5.0},
        TimingRange{"jump_threshold_ms", 1.0, 10000.0}}) {
    timing[range.name] = positive(timing.at(range.name), where + ".time_sync." + range.name, range.low,
                                  range.high);
  }
  if (timing.at("probe_timeout_s").get<double>() > timing.at("poll_interval_s").get<double>()) {
    throw ConfigurationError(where + ": clock probe timeout must not exceed polling interval");
  }
  integer(timing.at("stable_samples"), where + ".time_sync.stable_samples", 1, 100);

  const auto description = asset.find("description");
  if (result.at("enabled").get<bool>() && description != asset.end() && !description->is_null() &&
      description->contains("enabled") && description->at("enabled").get<bool>() &&
      description->contains("use_sim_time") && description->at("use_sim_time").get<bool>()) {
    throw ConfigurationError(where + ": collection currently requires system time, not use_sim_time");
  }
  if (result.at("enabled").get<bool>()) {
    const auto sensors = asset.find("sensors");
    if (sensors != asset.end()) {
      for (const auto& item : sensors->items()) {
        const Json& sensor = item.value();
        const auto driver = sensor.find("driver");
        if (driver == sensor.end()) continue;
        bool sim_time_enabled = false;
        auto parameters = driver->find("parameters");
        if (parameters != driver->end()) {
          auto use_sim_time = parameters->find("use_sim_time");
          sim_time_enabled =
              use_sim_time != parameters->end() && use_sim_time->is_boolean() && use_sim_time->get<bool>();
        }
        if (sensor.at("enabled").get<bool>() &&
            driver->at("mode").get_ref<const std::string&>() == "node" && sim_time_enabled) {
          throw ConfigurationError(where + ": managed sensor use_sim_time must be false during collection");
        }
      }
    }
  }

  const std::string namespace_ = asset.at("namespace").get_ref<const std::string&>();
  std::vector<std::string> resolved_topics;
  if (result.contains("topics")) {
    const Json& topics = result.at("topics");
    if (!topics.is_array() || topics.empty()) {
      throw ConfigurationError(where + ".topics: expected a nonempty list");
    }
    std::set<std::string> seen;
    for (const auto& item : topics) {
      if (!item.is_string()) {
        throw ConfigurationError(where + ".topics: expected topic strings");
      }
      std::string topic = item.get_ref<const std::string&>();
      if (topic[0] != '/') topic = namespace_ + "/" + topic;
      if (!std::regex_match(topic, topic_re)) {
        throw ConfigurationError(where + ": invalid exact ROS topic '" + topic + "'");
      }
      if (!seen.insert(topic).second) {
        throw ConfigurationError(where + ": duplicate collection topic " + topic);
      }
      resolved_topics.push_back(topic);
    }
  } else {
    if (result.at("include_state").get<bool>()) {
      for (const char* name : {"state", "command_results", "control_trace"}) {
        resolved_topics.push_back(namespace_ + "/" + name);
      }
    }
    const auto sensors = asset.find("sensors");
    bool any_enabled = false;
    if (sensors != asset.end()) {
      for (const auto& item : sensors->items()) {
        const Json& sensor = item.value();
        if (!sensor.at("enabled").get<bool>()) continue;
        any_enabled = true;
        for (const auto& stream : sensor.at("streams").items()) {
          resolved_topics.push_back(stream.value().at("topic").get<std::string>());
        }
      }
    }
    if (result.at("include_sensors").get<bool>() && any_enabled) {
      resolved_topics.push_back(namespace_ + "/sensors/description");
      resolved_topics.push_back(namespace_ + "/sensors/diagnostics");
    }
    if (result.at("include_tf").get<bool>()) {
      resolved_topics.push_back("/tf");
      resolved_topics.push_back("/tf_static");
    }
    if (description != asset.end() && !description->is_null() && description->contains("enabled") &&
        description->at("enabled").get<bool>()) {
      resolved_topics.push_back(namespace_ + "/robot_description");
      resolved_topics.push_back(description->at("joint_states_topic").get<std::string>());
    }
  }
  resolved_topics.push_back(namespace_ + "/data_collection/status");
  std::vector<std::string> unique;
  std::set<std::string> seen;
  for (const auto& topic : resolved_topics) {
    if (seen.insert(topic).second) unique.push_back(topic);
  }
  if (unique.size() > 256) {
    throw ConfigurationError(where + ": at most 256 topics can be recorded");
  }
  result["topics"] = Json::array();
  for (const auto& topic : unique) result["topics"].push_back(topic);
  return result;
}

}  // namespace chaos::assets
