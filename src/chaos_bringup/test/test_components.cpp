// Contract tests for the native component derivation.
//
// The planner derives the payload of every managed process; the launch glue
// only turns a specification into a ROS action. These tests cover the
// derivation, the summaries, the preflight rules and the validation of a
// sensor parameter file.

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "chaos_bringup/components.hpp"

using chaos::assets::ConfigurationError;
using chaos::bringup::Json;

namespace {

Json asset_with_description() {
  Json asset = Json::object();
  asset["id"] = "unit";
  asset["namespace"] = "/robots/unit";
  asset["mode"] = "mock";
  Json frames = Json::object();
  frames["base"] = "unit/base_link";
  asset["frames"] = frames;
  Json driver = Json::object();
  driver["mode"] = "mock";
  driver["plugin"] = "mock";
  asset["driver"] = driver;
  Json description = Json::object();
  description["enabled"] = true;
  description["xml"] = "<robot name=\"unit\"/>";
  description["file"] = "/models/unit/robot.urdf";
  description["format"] = "urdf";
  description["root"] = "unit/base_link";
  description["frame_prefix"] = "unit/";
  description["sha256"] = std::string(64, 'a');
  description["links"] = Json::array({Json("unit/base_link")});
  description["joints"] = Json::array();
  description["publish_frequency"] = 30.0;
  description["use_sim_time"] = false;
  description["joint_states_topic"] = "/robots/unit/joint_states";
  asset["description"] = description;
  asset["sensors"] = Json::object();
  asset["data_collection"] = Json();
  return asset;
}

}  // namespace

TEST(Components, TheComponentAssetDropsTheExpandedDescription) {
  const Json asset = asset_with_description();
  const Json component = chaos::bringup::component_asset(asset);
  EXPECT_FALSE(component.at("description").contains("xml"));
  // The original snapshot stays unchanged.
  EXPECT_TRUE(asset.at("description").contains("xml"));
  EXPECT_EQ(component.at("id"), "unit");
}

TEST(Components, TheDescriptionSummaryKeepsTheXmlOut) {
  const Json summary = chaos::bringup::description_summary(asset_with_description());
  EXPECT_EQ(summary.at("file"), "/models/unit/robot.urdf");
  EXPECT_EQ(summary.at("link_count"), 1u);
  EXPECT_EQ(summary.at("joint_count"), 0u);
  EXPECT_FALSE(summary.contains("xml"));
}

TEST(Components, ADisabledDescriptionHasNoSummary) {
  Json asset = asset_with_description();
  asset["description"]["enabled"] = false;
  EXPECT_TRUE(chaos::bringup::description_summary(asset).is_null());
}

TEST(Components, TheSensorSummaryOmitsTheDriverParameters) {
  Json asset = asset_with_description();
  Json sensor = Json::object();
  sensor["id"] = "imu";
  sensor["kind"] = "imu";
  sensor["enabled"] = true;
  sensor["namespace"] = "/robots/unit/sensors/imu";
  Json driver = Json::object();
  driver["mode"] = "mock";
  driver["package"] = "chaos_sensors";
  driver["executable"] = "mock_sensor";
  driver["parameters"] = Json::object({{"url", "rtsp://user:secret@camera"}});
  sensor["driver"] = driver;
  Json stream = Json::object();
  stream["type"] = "sensor_msgs/msg/Imu";
  stream["topic"] = "/robots/unit/sensors/imu/data";
  sensor["streams"] = Json::object({{"imu", stream}});
  asset["sensors"] = Json::object({{"imu", sensor}});
  const Json summary = chaos::bringup::sensor_summary(asset);
  ASSERT_EQ(summary.size(), 1u);
  EXPECT_EQ(summary.at(0).at("id"), "imu");
  EXPECT_EQ(summary.at(0).at("streams").at("imu").at("topic"), "/robots/unit/sensors/imu/data");
  // A device URL with credentials must not reach a log.
  EXPECT_EQ(summary.dump().find("secret"), std::string::npos);
}

TEST(Components, TheDescriptionProcessCarriesTheFrozenDocument) {
  const std::vector<chaos::bringup::ProcessSpec> processes =
      chaos::bringup::description_processes(asset_with_description());
  ASSERT_EQ(processes.size(), 1u);
  const auto& spec = processes.front();
  EXPECT_EQ(spec.package, "robot_state_publisher");
  EXPECT_EQ(spec.executable, "robot_state_publisher");
  EXPECT_EQ(spec.node_namespace, "/robots/unit");
  ASSERT_EQ(spec.parameters.size(), 1u);
  EXPECT_EQ(spec.parameters.at(0).at("robot_description"), "<robot name=\"unit\"/>");
  ASSERT_EQ(spec.remappings.size(), 3u);
  EXPECT_EQ(spec.remappings.at(0).first, "joint_states");
}

TEST(Components, AMockAssetHasNoDriverProcess) {
  EXPECT_TRUE(chaos::bringup::driver_processes(asset_with_description()).empty());
}

TEST(Components, ARealGo2AssetHasADriverProcess) {
  Json asset = asset_with_description();
  asset["mode"] = "real";
  asset["driver"]["plugin"] = "unitree_go2";
  const std::vector<chaos::bringup::ProcessSpec> processes =
      chaos::bringup::driver_processes(asset);
  ASSERT_EQ(processes.size(), 1u);
  EXPECT_EQ(processes.front().executable, "go2_driver");
}

TEST(Components, TheSensorProcessCarriesTheStaticTransform) {
  Json asset = asset_with_description();
  Json sensor = Json::object();
  sensor["id"] = "imu";
  sensor["kind"] = "imu";
  sensor["enabled"] = true;
  sensor["frame_id"] = "unit/imu_link";
  sensor["namespace"] = "/robots/unit/sensors/imu";
  Json driver = Json::object();
  driver["mode"] = "mock";
  sensor["driver"] = driver;
  Json transform = Json::object();
  transform["parent"] = "unit/base_link";
  transform["xyz"] = Json::array({0.0, 0.0, 0.1});
  transform["rpy"] = Json::array({0.0, 0.0, 0.0});
  sensor["transform"] = transform;
  Json stream = Json::object();
  stream["type"] = "sensor_msgs/msg/Imu";
  stream["topic"] = "/robots/unit/sensors/imu/data";
  sensor["streams"] = Json::object({{"imu", stream}});
  asset["sensors"] = Json::object({{"imu", sensor}});
  const std::vector<chaos::bringup::ProcessSpec> processes =
      chaos::bringup::sensor_processes(asset, Json::object());
  // The sensor node, the static transform and the sensor monitor.
  ASSERT_EQ(processes.size(), 3u);
  EXPECT_EQ(processes.at(0).executable, "mock_sensor");
  EXPECT_EQ(processes.at(1).executable, "static_transform_publisher");
  EXPECT_EQ(processes.at(2).executable, "sensor_monitor");
  // The transform is passed as six named values, followed by the frame pair.
  const Json& arguments = processes.at(1).arguments;
  ASSERT_EQ(arguments.size(), 16u);
  EXPECT_EQ(arguments.at(0), "--x");
  EXPECT_EQ(arguments.at(1), "0.0");
  EXPECT_EQ(arguments.at(2), "--y");
  EXPECT_EQ(arguments.at(3), "0.0");
  EXPECT_EQ(arguments.at(4), "--z");
  EXPECT_EQ(arguments.at(5), "0.1");
  EXPECT_EQ(arguments.at(6), "--roll");
  EXPECT_EQ(arguments.at(10), "--yaw");
  EXPECT_EQ(arguments.at(11), "0.0");
  EXPECT_EQ(arguments.at(12), "--frame-id");
  EXPECT_EQ(arguments.at(13), "unit/base_link");
  EXPECT_EQ(arguments.at(14), "--child-frame-id");
  EXPECT_EQ(arguments.at(15), "unit/imu_link");
}

TEST(Components, AParameterFileNeedsASelectorThatMatchesTheDriver) {
  // The full node name, a wildcard and a recursive wildcard are the
  // selectors of a managed sensor driver.
  for (const char* selector : {"/**", "/robots/*/sensors/**/driver",
                               "/robots/unit/sensors/imu/driver"}) {
    const std::string content =
        std::string(selector) + ":\n  ros__parameters:\n    rate: 10\n    name: unit\n";
    EXPECT_NO_THROW(chaos::bringup::validate_parameter_file(
        content, "/robots/unit/sensors/imu/driver", "unit.yaml"))
        << selector;
  }
  // A selector for another node is rejected, so a vendor file for a
  // different node is never silently ignored.
  const std::string other = "other:\n  ros__parameters:\n    rate: 10\n";
  EXPECT_THROW(chaos::bringup::validate_parameter_file(other, "/robots/unit/sensors/imu/driver",
                                                        "unit.yaml"),
               ConfigurationError);
  const std::string invalid = "driver:\n  ros__parameters:\n    1bad: 10\n";
  EXPECT_THROW(chaos::bringup::validate_parameter_file(invalid, "/robots/unit/sensors/imu/driver",
                                                        "unit.yaml"),
               ConfigurationError);
}

TEST(Components, ANestedNamespaceParameterFileIsAccepted) {
  const std::string content =
      "/robots:\n"
      "  unit:\n"
      "    sensors:\n"
      "      imu:\n"
      "        driver:\n"
      "          ros__parameters:\n"
      "            camera:\n"
      "              enabled: true\n";
  EXPECT_NO_THROW(chaos::bringup::validate_parameter_file(
      content, "/robots/unit/sensors/imu/driver", "unit.yaml"));
}

TEST(Components, AMalformedParameterFileIsRejected) {
  for (const char* content : {"[", "[]", "", "serial_number: \"00123\"",
                              "orbbec_camera_node:\n  ros__parameters:\n    serial_number: \"1\"",
                              "/**:\n  ros__parameters:\n    frequency: .inf"}) {
    try {
      chaos::bringup::validate_parameter_file(content, "/robots/unit/sensors/imu/driver",
                                              "unit.yaml");
      FAIL() << "a malformed parameter file was accepted: " << content;
    } catch (const ConfigurationError& error) {
      EXPECT_NE(std::string(error.what()).find("parameter YAML"), std::string::npos)
          << error.what();
    }
  }
}

TEST(Components, ADuplicateParameterKeyIsRejected) {
  const std::string content = "/**:\n  ros__parameters:\n    serial: \"one\"\n    serial: \"two\"\n";
  EXPECT_THROW(chaos::bringup::validate_parameter_file(content, "/robots/unit/sensors/imu/driver",
                                                        "unit.yaml"),
               ConfigurationError);
}

TEST(Components, AnOutputDirectoryNeedsAnExistingWritableAncestor) {
  EXPECT_NO_THROW(chaos::bringup::check_output_directory("/tmp"));
  EXPECT_THROW(chaos::bringup::check_output_directory("/nonexistent-root/recordings"),
               ConfigurationError);
}

namespace {

// An asset with a real Go2 driver and an enabled recorder.
//
// The preflight of this asset touches every collected fact of a plan: the
// driver backend, the storage plugin and the output directory. The values are
// read after the plan items have been iterated, so this case also covers the
// lifetime of the collected data.
Json asset_with_go2_and_collection(const std::string& output_dir) {
  Json asset = asset_with_description();
  asset["mode"] = "real";
  asset["driver"]["plugin"] = "unitree_go2";
  Json collection = Json::object();
  collection["enabled"] = true;
  collection["output_dir"] = output_dir;
  collection["storage_id"] = "sqlite3";
  collection["topics"] = Json::array();
  collection["time_sync"] = false;
  collection["max_bagfile_duration_s"] = 60.0;
  collection["max_bagfile_size_mb"] = 512.0;
  collection["min_free_space_mb"] = 1024.0;
  asset["data_collection"] = collection;
  return asset;
}

// A plan with one asset.
chaos::bringup::LaunchPlan plan_of(const Json& asset) {
  Json item = Json::object();
  item["asset"] = asset;
  item["mqtt"] = Json();
  return chaos::bringup::LaunchPlan::from_items({item});
}

}  // namespace

TEST(Components, ThePreflightCollectsTheFactsOfEveryAsset) {
  const Json asset = asset_with_go2_and_collection("/tmp");
  chaos::bringup::PreflightOptions options;
  options.executable_available = [](const std::string&, const std::string&) { return true; };
  options.registered_writers = {"sqlite3"};
  options.backend_available = [](const std::string& plugin) { return plugin == "unitree_go2"; };
  options.check_files = true;
  EXPECT_NO_THROW(chaos::bringup::preflight(plan_of(asset), options));
}

TEST(Components, AMissingDriverBackendIsRejected) {
  const Json asset = asset_with_go2_and_collection("/tmp");
  chaos::bringup::PreflightOptions options;
  options.executable_available = [](const std::string&, const std::string&) { return true; };
  options.registered_writers = {"sqlite3"};
  // A mock build must never enter the real mode: the backend of the plugin
  // has to be reported by the runtime.
  options.backend_available = [](const std::string&) { return false; };
  EXPECT_THROW(chaos::bringup::preflight(plan_of(asset), options), ConfigurationError);
  // Without a reported backend the preflight is just as strict.
  chaos::bringup::PreflightOptions without;
  without.executable_available = [](const std::string&, const std::string&) { return true; };
  without.registered_writers = {"sqlite3"};
  EXPECT_THROW(chaos::bringup::preflight(plan_of(asset), without), ConfigurationError);
}

TEST(Components, AnUnavailableStoragePluginIsRejected) {
  const Json asset = asset_with_go2_and_collection("/tmp");
  chaos::bringup::PreflightOptions options;
  options.executable_available = [](const std::string&, const std::string&) { return true; };
  options.registered_writers = {"mcap"};
  options.backend_available = [](const std::string&) { return true; };
  EXPECT_THROW(chaos::bringup::preflight(plan_of(asset), options), ConfigurationError);
}

TEST(Components, AnUnavailableExecutableIsRejected) {
  const Json asset = asset_with_go2_and_collection("/tmp");
  chaos::bringup::PreflightOptions options;
  options.executable_available = [](const std::string&, const std::string&) { return false; };
  options.registered_writers = {"sqlite3"};
  options.backend_available = [](const std::string&) { return true; };
  EXPECT_THROW(chaos::bringup::preflight(plan_of(asset), options), ConfigurationError);
}
