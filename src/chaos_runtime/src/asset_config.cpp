#include "chaos_runtime/asset_config.hpp"

#include <cmath>

namespace chaos::runtime {

namespace {
constexpr std::size_t kMaxAssetBytes = 1024 * 1024;

[[noreturn]] void fail(const std::string & where, const std::string & what) {
  throw AssetConfigError(where + ": " + what);
}

const nlohmann::json & requireObject(const nlohmann::json & object,
                                     const std::string & key,
                                     const std::string & where) {
  auto found = object.find(key);
  if (found == object.end() || !found->is_object()) {
    fail(where, "missing object field '" + key + "'");
  }
  return *found;
}

const nlohmann::json & requireArray(const nlohmann::json & object,
                                    const std::string & key,
                                    const std::string & where) {
  auto found = object.find(key);
  if (found == object.end() || !found->is_array()) {
    fail(where, "missing array field '" + key + "'");
  }
  return *found;
}

std::string requireFrame(const nlohmann::json & object,
                         const std::string & key,
                         const std::string & where) {
  const std::string value = requireString(object, key, where);
  if (value.front() == '/' || value.find(' ') != std::string::npos) {
    fail(where, "invalid frame name '" + value + "'");
  }
  return value;
}
}  // namespace

double requireNumber(const nlohmann::json & object, const std::string & key,
                     double minimum, double maximum,
                     const std::string & where) {
  auto found = object.find(key);
  if (found == object.end()) {
    fail(where, "missing numeric field '" + key + "'");
  }
  if (found->is_boolean() || !found->is_number()) {
    fail(where, "field '" + key + "' must be a number, not a boolean or string");
  }
  const double value = found->get<double>();
  if (!std::isfinite(value) || value < minimum || value > maximum) {
    fail(where, "field '" + key + "' out of range");
  }
  return value;
}

bool requireBool(const nlohmann::json & object, const std::string & key,
                 bool default_value, const std::string & where) {
  auto found = object.find(key);
  if (found == object.end()) {
    return default_value;
  }
  if (!found->is_boolean()) {
    fail(where, "field '" + key + "' must be a boolean");
  }
  return found->get<bool>();
}

std::string requireString(const nlohmann::json & object,
                          const std::string & key,
                          const std::string & where) {
  auto found = object.find(key);
  if (found == object.end() || !found->is_string()) {
    fail(where, "missing string field '" + key + "'");
  }
  const std::string value = found->get<std::string>();
  if (value.empty()) {
    fail(where, "field '" + key + "' must not be empty");
  }
  return value;
}

AssetConfig parseAssetJson(const std::string & raw) {
  if (raw.size() > kMaxAssetBytes) {
    fail("asset_json", "exceeds 1 MiB");
  }
  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(raw, nullptr, /*allow_exceptions=*/true,
                                   /*ignore_comments=*/false);
  } catch (const nlohmann::json::parse_error & error) {
    fail("asset_json", std::string("invalid JSON: ") + error.what());
  }
  if (!parsed.is_object()) {
    fail("asset_json", "must be a JSON object");
  }
  const std::string where = "asset_json";
  AssetConfig asset;
  asset.id = requireString(parsed, "id", where);
  asset.model = requireString(parsed, "model", where);
  if (auto version = parsed.find("model_version"); version != parsed.end()) {
    if (version->is_number_integer()) {
      asset.model_version = std::to_string(version->get<long long>());
    } else if (version->is_string()) {
      asset.model_version = version->get<std::string>();
    } else {
      fail(where, "model_version must be a string or integer");
    }
  }
  if (auto kind = parsed.find("kind"); kind != parsed.end()) {
    asset.kind = requireString(parsed, "kind", where);
  }
  asset.mode = requireString(parsed, "mode", where);
  asset.name_space = requireString(parsed, "namespace", where);
  const auto & frames = requireObject(parsed, "frames", where);
  asset.frames.base = requireFrame(frames, "base", "asset_json.frames");
  asset.frames.odom = requireFrame(frames, "odom", "asset_json.frames");
  asset.frames.map = requireFrame(frames, "map", "asset_json.frames");
  const auto & capabilities = requireArray(parsed, "capabilities", where);
  for (const auto & capability : capabilities) {
    if (!capability.is_string()) {
      fail(where, "capabilities must be strings");
    }
    asset.capabilities.insert(capability.get<std::string>());
  }
  if (auto limits = parsed.find("limits"); limits != parsed.end()) {
    if (!limits->is_object()) {
      fail(where, "limits must be an object");
    }
    std::map<std::string, double> values;
    for (auto item = limits->begin(); item != limits->end(); ++item) {
      values.emplace(item.key(), requireNumber(*limits, item.key(), 1e-9, 1e9,
                                               "asset_json.limits"));
    }
    try {
      asset.limits = control::Limits::fromMap(values);
    } catch (const chaos::Rejected & error) {
      fail("asset_json.limits", error.what());
    }
  }
  const auto & driver = requireObject(parsed, "driver", where);
  asset.plugin = requireString(driver, "plugin", "asset_json.driver");
  if (auto options = driver.find("options"); options != driver.end()) {
    if (!options->is_object()) {
      fail("asset_json.driver", "options must be an object");
    }
    asset.options = *options;
  } else {
    asset.options = nlohmann::json::object();
  }
  if (auto domain = parsed.find("ros_domain_id"); domain != parsed.end()) {
    if (domain->is_boolean() || !domain->is_number_integer()) {
      fail(where, "ros_domain_id must be an integer");
    }
    asset.ros_domain_id = domain->get<std::int64_t>();
  }
  if (auto sensors = parsed.find("sensors"); sensors != parsed.end()) {
    if (!sensors->is_object()) {
      fail(where, "sensors must be an object");
    }
    asset.sensors = *sensors;
  }
  return asset;
}

}  // namespace chaos::runtime
