// Contract tests for the native launch planner.
//
// The planner owns the selection of the assets, the collision rules between
// them and the MQTT identity. The MQTT adapter configuration is injected, so
// the tests never depend on the transport implementation.

#include <gtest/gtest.h>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "chaos_assets/inventory.hpp"
#include "chaos_bringup/planner.hpp"

using chaos::assets::ConfigurationError;
using chaos::assets::Inventory;
using chaos::bringup::Json;
using chaos::bringup::LaunchPlan;
using chaos::bringup::PlanRequest;

namespace {

std::string share_directory() {
  return ament_index_cpp::get_package_share_directory("chaos_assets");
}

std::string demo_config() { return share_directory() + "/config/demo.yaml"; }

// The MQTT example inventory: its assets reference a transport profile.
std::string mqtt_config() { return share_directory() + "/config/mqtt.example.yaml"; }

// A factory that produces a deterministic adapter configuration.
//
// The shape is the one of the native generator of chaos_mqtt: the planner
// only reads the identity of the asset, so the transport implementation stays
// outside of this package.
chaos::bringup::MqttConfigFactory factory(const std::string& client_id,
                                           const std::string& command_topic = "cmd/topic") {
  return [client_id, command_topic](const Json& asset, const Json&) -> Json {
    Json mqtt = Json::object();
    mqtt["client_id"] = client_id;
    mqtt["command_topic"] = command_topic;
    mqtt["response_topic"] = command_topic + "/response";
    // The native generator emits an empty event topic for a profile without
    // events; it must not be reported as an invalid topic name.
    mqtt["event_topic"] = "";
    mqtt["host"] = "broker.local";
    Json config = Json::object();
    config["mqtt"] = mqtt;
    config["topic_mappings"] = Json::array();
    return config;
  };
}

// A temporary directory that removes itself again.
//
// The collision rules need an inventory on disk, because the planner never
// accepts a pre-resolved asset list.
class TemporaryDirectory {
 public:
  TemporaryDirectory() {
    const char* base = std::getenv("TMPDIR");
    path_ = std::filesystem::path(base == nullptr ? "/tmp" : base) /
            ("chaos-bringup-planner-" + std::to_string(::getpid()));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  TemporaryDirectory(const TemporaryDirectory&) = delete;
  TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

  std::string write(const std::string& name, const std::string& content) const {
    const std::string target = (path_ / name).string();
    std::ofstream stream(target);
    stream << content;
    stream.close();
    return target;
  }

 private:
  std::filesystem::path path_;
};

// An inventory with one Go2 asset in the real mode.
//
// The model file is referenced by an absolute path, so the inventory does not
// depend on the location of the temporary file.
std::string go2_inventory(const TemporaryDirectory& directory, const std::string& second_asset) {
  const std::string model = share_directory() + "/models/unitree_go2/v1/model.yaml";
  return directory.write("assets.yaml",
                         "schema_version: 1\n"
                         "hosts:\n"
                         "  dev:\n"
                         "    ros_domain_id: 87\n"
                         "assets:\n"
                         "  - id: dog_001\n"
                         "    model_file: " + model + "\n"
                         "    host: dev\n"
                         "    mode: real\n"
                         "    frames: {base: dog_001/base_link, odom: dog_001/odom, map: dog_001/map}\n" +
                             second_asset);
}

// A second Go2 asset with the given driver options.
std::string second_go2(const std::string& id, const std::string& options) {
  const std::string model = share_directory() + "/models/unitree_go2/v1/model.yaml";
  return "  - id: " + id + "\n"
         "    model_file: " + model + "\n"
         "    host: dev\n"
         "    mode: real\n"
         "    frames: {base: " + id + "/base_link, odom: " + id + "/odom, map: " + id + "/map}\n"
         "    driver:\n"
         "      options: {" + options + "}\n";
}

// An inventory with one real humanoid asset.
//
// The model file is referenced by an absolute path, so the inventory does not
// depend on the location of the temporary file.
std::string humanoid_inventory(const TemporaryDirectory& directory,
                               const std::string& second_asset) {
  const std::string model = share_directory() + "/models/booster_t2/v1/model.yaml";
  return directory.write("assets.yaml",
                         "schema_version: 1\n"
                         "hosts:\n"
                         "  dev:\n"
                         "    ros_domain_id: 87\n"
                         "assets:\n"
                         "  - id: t2_001\n"
                         "    model_file: " + model + "\n"
                         "    host: dev\n"
                         "    mode: real\n"
                         "    frames: {base: t2_001/base_link, odom: t2_001/odom, map: t2_001/map}\n" +
                             second_asset);
}

// A second humanoid asset with the given driver options.
std::string second_humanoid(const std::string& id, const std::string& options) {
  const std::string model = share_directory() + "/models/booster_t2/v1/model.yaml";
  return "  - id: " + id + "\n"
         "    model_file: " + model + "\n"
         "    host: dev\n"
         "    mode: real\n"
         "    frames: {base: " + id + "/base_link, odom: " + id + "/odom, map: " + id + "/map}\n"
         "    driver:\n"
         "      options: {" + options + "}\n";
}

// Assert that the planner rejects the request for a specific reason.
//
// The reason is part of the contract: a caller has to be able to tell the
// ownership rules apart.
void expect_rejected(const PlanRequest& request, const std::string& reason) {
  try {
    chaos::bringup::prepare_plan(request, nullptr);
    FAIL() << "the planner accepted an invalid request";
  } catch (const ConfigurationError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find(reason), std::string::npos)
        << "unexpected reason: " << message << " (expected \"" << reason << "\")";
  }
}

}  // namespace

TEST(Planner, TheDemoPlanSelectsTwoIndependentAssets) {
  PlanRequest request;
  request.inventory_path = demo_config();
  request.host_id = "dev";
  const LaunchPlan plan = chaos::bringup::prepare_plan(request, nullptr);
  ASSERT_EQ(plan.size(), 2u);
  const std::vector<Json> items = plan.items();
  EXPECT_NE(items.at(0).at("asset").at("namespace"), items.at(1).at("asset").at("namespace"));
  for (const Json& item : items) {
    EXPECT_TRUE(item.at("mqtt").is_null());
  }
}

TEST(Planner, AnUnknownHostIsRejected) {
  PlanRequest request;
  request.inventory_path = demo_config();
  request.host_id = "other";
  EXPECT_THROW(chaos::bringup::prepare_plan(request, nullptr), ConfigurationError);
}

TEST(Planner, AnUnavailableAssetIsRejected) {
  PlanRequest request;
  request.inventory_path = demo_config();
  request.host_id = "dev";
  request.asset_ids = {"drone_001"};
  // The drone is enabled in the demo inventory.
  EXPECT_NO_THROW(chaos::bringup::prepare_plan(request, nullptr));
  request.asset_ids = {"absent_001"};
  EXPECT_THROW(chaos::bringup::prepare_plan(request, nullptr), ConfigurationError);
}

TEST(Planner, ThePlanIsImmutableDespiteMutatingReturnedData) {
  PlanRequest request;
  request.inventory_path = demo_config();
  request.host_id = "dev";
  const LaunchPlan plan = chaos::bringup::prepare_plan(request, nullptr);
  Json first = plan.at(0);
  first.at("asset").at("driver")["plugin"] = "replaced";
  EXPECT_EQ(plan.at(0).at("asset").at("driver").at("plugin"), "mock");
}

TEST(Planner, TheMqttConfigurationIsOptional) {
  PlanRequest request;
  request.inventory_path = mqtt_config();
  request.host_id = "dev";
  request.enable_mqtt = true;
  // Without a producer the planner refuses to plan instead of silently
  // dropping the MQTT support.
  EXPECT_THROW(chaos::bringup::prepare_plan(request, nullptr), ConfigurationError);
}

TEST(Planner, TheMqttIdentityIsGeneratedPerAsset) {
  PlanRequest request;
  request.inventory_path = mqtt_config();
  request.host_id = "dev";
  request.enable_mqtt = true;
  request.check_files = false;
  // The producer is called once per asset, so every asset gets its own
  // identity.
  std::vector<std::string> assets;
  auto counting = [&assets](const Json& asset, const Json&) -> Json {
    const std::string id = asset.at("id").get_ref<const std::string&>();
    assets.push_back(id);
    Json mqtt = Json::object();
    mqtt["client_id"] = "chaos-" + id;
    mqtt["command_topic"] = "robots/" + id + "/commands";
    mqtt["response_topic"] = "robots/" + id + "/responses";
    mqtt["event_topic"] = "";
    mqtt["host"] = "broker.local";
    Json config = Json::object();
    config["mqtt"] = mqtt;
    config["topic_mappings"] = Json::array();
    return config;
  };
  const LaunchPlan plan = chaos::bringup::prepare_plan(request, counting);
  EXPECT_EQ(plan.size(), 2u);
  EXPECT_EQ(assets.size(), 2u);
  for (const Json& item : plan.items()) {
    EXPECT_FALSE(item.at("mqtt").is_null());
    EXPECT_EQ(item.at("mqtt").at("config").at("mqtt").at("host"), "broker.local");
  }
}

TEST(Planner, ADuplicateMqttClientIdIsRejected) {
  PlanRequest request;
  request.inventory_path = mqtt_config();
  request.host_id = "dev";
  request.enable_mqtt = true;
  request.check_files = false;
  EXPECT_THROW(chaos::bringup::prepare_plan(request, factory("shared-client")), ConfigurationError);
}

TEST(Planner, ADuplicateMqttTopicIsRejected) {
  PlanRequest request;
  request.inventory_path = mqtt_config();
  request.host_id = "dev";
  request.enable_mqtt = true;
  request.check_files = false;
  EXPECT_THROW(chaos::bringup::prepare_plan(request, factory("client", "shared/topic")),
               ConfigurationError);
}

TEST(Planner, AMqttTopicWithAWildcardIsRejected) {
  PlanRequest request;
  request.inventory_path = mqtt_config();
  request.host_id = "dev";
  request.enable_mqtt = true;
  request.check_files = false;
  EXPECT_THROW(chaos::bringup::prepare_plan(request, factory("client", "shared/#")),
               ConfigurationError);
}

TEST(Planner, TheEdgePlanRequiresOneRealAsset) {
  EXPECT_THROW(chaos::bringup::prepare_edge_plan("relative/path.yaml", "unit", false, false, nullptr),
               ConfigurationError);
  // The demo inventory has two hosts and a mock backend.
  EXPECT_THROW(chaos::bringup::prepare_edge_plan(demo_config(), "vehicle_001", false, false, nullptr),
               ConfigurationError);
}

TEST(Planner, TheMqttExecutableOfAnExplicitPathIsKept) {
  const std::string binary = "/bin/sh";
  EXPECT_EQ(chaos::bringup::resolve_mqtt_executable(binary), binary);
  EXPECT_THROW(chaos::bringup::resolve_mqtt_executable("/nonexistent/transport"),
               ConfigurationError);
}

TEST(Planner, TwoRealAssetsCannotOwnOneVelocityOutput) {
  const TemporaryDirectory directory;
  // The vehicle model has no DDS connection of its own, so the velocity
  // output is the only ownership that can collide here.
  const std::string model = share_directory() + "/models/lingao_vehicle/v1/model.yaml";
  const std::string path = directory.write(
      "assets.yaml",
      "schema_version: 1\n"
      "hosts:\n"
      "  dev:\n"
      "    ros_domain_id: 87\n"
      "assets:\n"
      "  - id: vehicle_001\n"
      "    model_file: " + model + "\n"
      "    host: dev\n"
      "    mode: real\n"
      "  - id: vehicle_002\n"
      "    model_file: " + model + "\n"
      "    host: dev\n"
      "    mode: real\n");
  PlanRequest request;
  request.inventory_path = path;
  request.host_id = "dev";
  // Both assets keep the velocity topic of the model, so both would publish
  // on the same output.
  expect_rejected(request, "same velocity output");
}

TEST(Planner, TwoGo2AssetsCannotOwnOneDdsConnection) {
  const TemporaryDirectory directory;
  const std::string path = go2_inventory(directory, second_go2("dog_002", "network_interface: eno1"));
  PlanRequest request;
  request.inventory_path = path;
  request.host_id = "dev";
  // The filter selects one asset, but the ownership of the DDS connection is
  // a property of the deployment and is checked for every asset.
  request.asset_ids = {"dog_001"};
  expect_rejected(request, "same DDS connection");
}

TEST(Planner, TwoGo2AssetsCannotPublishTheSameOdometry) {
  const TemporaryDirectory directory;
  const std::string path =
      go2_inventory(directory, second_go2("dog_002", "network_interface: eno2, odom_topic: /go2/odom"));
  PlanRequest request;
  request.inventory_path = path;
  request.host_id = "dev";
  request.asset_ids = {"dog_001"};
  expect_rejected(request, "same odometry output");
}

TEST(Planner, TheGo2OdometryCannotConflictWithAVelocityOutput) {
  const TemporaryDirectory directory;
  const std::string path = go2_inventory(
      directory, second_go2("dog_002", "network_interface: eno2, cmd_vel_topic: /go2/odom, odom_topic: /dog2/odom"));
  PlanRequest request;
  request.inventory_path = path;
  request.host_id = "dev";
  expect_rejected(request, "odometry output conflicts");
}

TEST(Planner, TwoRealHumanoidAssetsCannotShareOneDdsDomain) {
  const TemporaryDirectory directory;
  // The two assets use different ROS topics, so only the vendor DDS domain
  // can collide. The vendor topics of the SDK are unsuffixed, so the domain
  // belongs to one humanoid.
  const std::string path = humanoid_inventory(
      directory, second_humanoid("t2_002", "cmd_vel_topic: /t2_002/cmd_vel, odom_topic: /t2_002/odom"));
  PlanRequest request;
  request.inventory_path = path;
  request.host_id = "dev";
  expect_rejected(request, "same DDS connection");
}

TEST(Planner, TwoHumanoidAssetsOnDifferentHostsCannotShareOneDdsDomain) {
  const TemporaryDirectory directory;
  const std::string model = share_directory() + "/models/booster_t2/v1/model.yaml";
  // The hosts differ and the ROS domains differ, so nothing but the vendor
  // DDS domain can collide. The host is not part of the identity of the
  // endpoint of a humanoid.
  const std::string path = directory.write(
      "assets.yaml",
      "schema_version: 1\n"
      "hosts:\n"
      "  dev_a:\n"
      "    ros_domain_id: 87\n"
      "  dev_b:\n"
      "    ros_domain_id: 88\n"
      "assets:\n"
      "  - id: t2_001\n"
      "    model_file: " + model + "\n"
      "    host: dev_a\n"
      "    mode: real\n"
      "    frames: {base: t2_001/base_link, odom: t2_001/odom, map: t2_001/map}\n"
      "    driver:\n"
      "      options: {cmd_vel_topic: /t2_001/cmd_vel, odom_topic: /t2_001/odom}\n"
      "  - id: t2_002\n"
      "    model_file: " + model + "\n"
      "    host: dev_b\n"
      "    mode: real\n"
      "    frames: {base: t2_002/base_link, odom: t2_002/odom, map: t2_002/map}\n"
      "    driver:\n"
      "      options: {cmd_vel_topic: /t2_002/cmd_vel, odom_topic: /t2_002/odom}\n");
  PlanRequest request;
  request.inventory_path = path;
  request.host_id = "dev_a";
  expect_rejected(request, "same DDS connection");
}

TEST(Planner, TwoRealHumanoidAssetsCannotPublishTheSameOdometry) {
  const TemporaryDirectory directory;
  // The DDS domains differ, so the odometry output is the only ownership
  // that can collide here.
  const std::string path = humanoid_inventory(
      directory, second_humanoid("t2_002", "dds_domain_id: 1, cmd_vel_topic: /t2_002/cmd_vel, odom_topic: /t2/odom"));
  PlanRequest request;
  request.inventory_path = path;
  request.host_id = "dev";
  expect_rejected(request, "same odometry output");
}

TEST(Planner, TheHumanoidOdometryCannotConflictWithAVelocityOutput) {
  const TemporaryDirectory directory;
  const std::string path = humanoid_inventory(
      directory, second_humanoid("t2_002", "dds_domain_id: 1, cmd_vel_topic: /t2/odom, odom_topic: /t2_002/odom"));
  PlanRequest request;
  request.inventory_path = path;
  request.host_id = "dev";
  expect_rejected(request, "odometry output conflicts");
}
