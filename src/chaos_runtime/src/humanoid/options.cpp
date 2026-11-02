#include "chaos_runtime/humanoid/options.hpp"

#include <string>
#include <regex>

#include "chaos_runtime/asset_validation.hpp"

namespace chaos::runtime {
namespace {

std::string stringOption(const nlohmann::json & options, const char * name) {
  const auto found = options.find(name);
  if (found == options.end() || !found->is_string()) {
    throw AssetConfigError(std::string("humanoid requires string option ") + name);
  }
  const std::string value = found->get<std::string>();
  if (value.empty()) {
    throw AssetConfigError(std::string("humanoid option must not be empty: ") + name);
  }
  return value;
}

void requireValue(const nlohmann::json & options, const char * name,
                  const char * expected) {
  if (stringOption(options, name) != expected) {
    throw AssetConfigError(std::string("humanoid ") + name + " must be " + expected);
  }
}

}  // namespace

void validateHumanoidOptions(const AssetConfig & asset) {
  if (asset.kind != "humanoid") {
    throw AssetConfigError("humanoid plugin requires kind humanoid");
  }
  if (asset.mode != "real") {
    throw AssetConfigError("humanoid plugin requires real mode; use the mock plugin for mock mode");
  }
  requireValue(asset.options, "backend", "booster_t2");
  requireValue(asset.options, "state_topic", "rt/odom");
  requireValue(asset.options, "robot_state_topic", "rt/robot_states");
  static const std::regex topic_pattern("^(/[A-Za-z_][A-Za-z0-9_]*)+$");
  const std::string command = stringOption(asset.options, "cmd_vel_topic");
  const std::string odometry = stringOption(asset.options, "odom_topic");
  if (!std::regex_match(command, topic_pattern) ||
      !std::regex_match(odometry, topic_pattern) || command == odometry) {
    throw AssetConfigError("humanoid cmd_vel_topic and odom_topic must be distinct absolute ROS topics");
  }
  static const std::regex frame_pattern("^[A-Za-z_][A-Za-z0-9_]*(/[A-Za-z_][A-Za-z0-9_]*)*$");
  const std::string source_odom = stringOption(asset.options, "source_odom_frame");
  const std::string source_base = stringOption(asset.options, "source_base_frame");
  if (!std::regex_match(source_odom, frame_pattern) ||
      !std::regex_match(source_base, frame_pattern) || source_odom == source_base) {
    throw AssetConfigError("humanoid source frames must be distinct valid frame names");
  }
  requireValue(asset.options, "velocity_frame", "body");

  const std::string interface = stringOption(asset.options, "network_interface");
  if (interface.size() > 15 || interface == "0.0.0.0") {
    throw AssetConfigError("humanoid network_interface must be an explicit NIC name or IPv4 address");
  }
  for (char ch : interface) {
    const bool valid = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
      (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == ':' || ch == '-';
    if (!valid) {
      throw AssetConfigError("humanoid network_interface contains invalid characters");
    }
  }
  const auto domain = asset.options.find("dds_domain_id");
  if (domain == asset.options.end() || !domain->is_number_integer()) {
    throw AssetConfigError("humanoid dds_domain_id must be an integer from 0 to 232");
  }
  const auto value = domain->get<std::int64_t>();
  if (value < 0 || value > 232) {
    throw AssetConfigError("humanoid dds_domain_id must be an integer from 0 to 232");
  }
  if (asset.capabilities.count("navigation.pose") != 0 ||
      asset.capabilities.count("flight.takeoff") != 0 ||
      asset.capabilities.count("flight.land") != 0) {
    throw AssetConfigError("real humanoid supports only state, motion.velocity and motion.stop");
  }
}

}  // namespace chaos::runtime
