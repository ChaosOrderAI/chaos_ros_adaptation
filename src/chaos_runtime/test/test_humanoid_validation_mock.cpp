#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <string>

#include "chaos_core/errors.hpp"
#include "chaos_runtime/adapters/mock_adapter.hpp"
#include "chaos_runtime/asset_validation.hpp"
#include "chaos_runtime/humanoid/booster_backend.hpp"
#include "chaos_runtime/humanoid/options.hpp"

namespace chaos::runtime {
namespace {

AssetConfig realAsset() {
  AssetConfig asset;
  asset.id = "t2";
  asset.model = "booster-t2";
  asset.kind = "humanoid";
  asset.mode = "real";
  asset.name_space = "/t2";
  asset.frames = {"base_link", "odom", "map"};
  asset.capabilities = {"state", "motion.velocity", "motion.stop"};
  asset.plugin = "humanoid";
  asset.options = {{"backend", "booster_t2"}, {"network_interface", "lo"},
    {"dds_domain_id", 0}, {"state_topic", "rt/odom"},
    {"robot_state_topic", "rt/robot_states"}, {"cmd_vel_topic", "/t2/cmd_vel"},
    {"odom_topic", "/t2/odom"}, {"velocity_frame", "body"},
    {"source_odom_frame", "odom"}, {"source_base_frame", "base_link"}};
  return asset;
}

AssetConfig mockAsset() {
  AssetConfig asset;
  asset.kind = "humanoid";
  asset.mode = "mock";
  asset.frames = {"base_link", "odom", "map"};
  asset.capabilities = {"state", "motion.velocity", "motion.stop", "navigation.pose"};
  asset.plugin = "mock";
  asset.options = {{"kind", "humanoid"}};
  return asset;
}

std::string realAssetJson(const std::string & velocity_frame) {
  return std::string(
    R"({"id":"t2","model":"booster-t2","kind":"humanoid","mode":"real",)"
    R"("namespace":"/t2","ros_domain_id":0,)"
    R"("frames":{"base":"base_link","odom":"odom","map":"map"},)"
    R"("capabilities":["state","motion.velocity","motion.stop"],)"
    R"("limits":{"linear_mps":1.0,"angular_rps":1.0,"command_timeout_s":1.0,)"
    R"("state_timeout_s":1.0,"lease_max_s":30.0,"action_timeout_s":120.0},)"
    R"("driver":{"plugin":"humanoid","options":{"backend":"booster_t2",)"
    R"("network_interface":"lo","dds_domain_id":0,"state_topic":"rt/odom",)"
    R"("robot_state_topic":"rt/robot_states","cmd_vel_topic":"/t2/cmd_vel",)"
    R"("odom_topic":"/t2/odom","velocity_frame":")") + velocity_frame +
    R"(","source_odom_frame":"odom","source_base_frame":"base_link"}}})";
}

TEST(HumanoidValidation, RosFreePreflightAcceptsExactRealContract) {
  EXPECT_NO_THROW(validateAssetConfig(realAsset()));
  std::string error;
  EXPECT_TRUE(validateAssetJson(realAssetJson("body"), error)) << error;
  EXPECT_FALSE(validateAssetJson(realAssetJson("odom"), error));
  EXPECT_NE(error.find("velocity_frame"), std::string::npos) << error;
  EXPECT_FALSE(validateAssetJson(realAsset().options.dump(), error));
}

TEST(HumanoidValidation, EveryBackendValueIsCheckedOutsideRosAdapter) {
  auto asset = realAsset();
  asset.options["velocity_frame"] = "odom";
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
  asset = realAsset();
  asset.options["state_topic"] = "rt/odometer_state";
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
  asset = realAsset();
  asset.options["robot_state_topic"] = "rt/other";
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
  asset = realAsset();
  asset.options["backend"] = "unknown";
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
  asset = realAsset();
  asset.options["network_interface"] = "*";
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
}

TEST(HumanoidValidation, RejectsMissingUnknownAndMalformedOptions) {
  auto asset = realAsset();
  asset.options.erase("dds_domain_id");
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
  asset = realAsset();
  asset.options["surprise"] = 1;
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
  asset = realAsset();
  asset.options["dds_domain_id"] = true;
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
  asset = realAsset();
  asset.options["dds_domain_id"] = 233;
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
}

TEST(HumanoidValidation, DeviceTopicsAreConfigurableAndFramesExplicit) {
  auto asset = realAsset();
  asset.options["cmd_vel_topic"] = "/fleet/t2_002/command";
  asset.options["odom_topic"] = "/fleet/t2_002/odometry";
  EXPECT_NO_THROW(validateAssetConfig(asset));
  asset.options["cmd_vel_topic"] = asset.options["odom_topic"];
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
  asset = realAsset();
  asset.options["odom_topic"] = "relative/topic";
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
  asset = realAsset();
  asset.options.erase("source_base_frame");
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
}

TEST(HumanoidValidation, RealKindModeAndCapabilitiesAreRestricted) {
  auto asset = realAsset();
  asset.kind = "quadruped";
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
  asset = realAsset();
  asset.mode = "mock";
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
  asset = realAsset();
  asset.capabilities.insert("navigation.pose");
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
  asset = realAsset();
  asset.capabilities.insert("flight.takeoff");
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
}

TEST(HumanoidMock, PlanarSemanticsNavigationAndUnknownBattery) {
  auto asset = mockAsset();
  EXPECT_NO_THROW(validateAssetConfig(asset));
  MockAdapter mock(asset);
  EXPECT_TRUE(std::isnan(mock.snapshot().battery));
  EXPECT_NO_THROW(mock.setVelocity({0.2, -0.1, 0.0, 0.0, 0.0, 0.3}));
  EXPECT_THROW(mock.setVelocity({0.0, 0.0, 0.1, 0.0, 0.0, 0.0}), chaos::Rejected);
  EXPECT_THROW(mock.setVelocity({0.0, 0.0, 0.0, 0.1, 0.0, 0.0}), chaos::Rejected);
  EXPECT_THROW(mock.setVelocity({0.0, 0.0, 0.0, 0.0, 0.1, 0.0}), chaos::Rejected);
  EXPECT_THROW(mock.startOperation(OperationKind::kTakeoff, {0.0, 0.0, 1.0, 0.0}),
               chaos::Rejected);
  EXPECT_NO_THROW(mock.startOperation(OperationKind::kNavigatePose,
                                      {1.0, 2.0, 0.0, 0.5}));
  EXPECT_THROW(control::validate_pose("map", "map", {0.0, 0.0, 0.1},
    {0.0, 0.0, 0.0, 1.0}, "humanoid", 20.0), chaos::Rejected);
  asset.capabilities.insert("flight.land");
  EXPECT_THROW(validateAssetConfig(asset), AssetConfigError);
}

#if !defined(CHAOS_HUMANOID_WITH_BOOSTER_SDK)
TEST(HumanoidSdkOff, RealBackendRefusesRatherThanFaking) {
  EXPECT_THROW(chaos::humanoid::createBoosterSdkBackend(
    chaos::humanoid::BackendConfig{}, [](const chaos::humanoid::Odometry &) {},
    [](bool) {}), std::runtime_error);
}
#endif

}  // namespace
}  // namespace chaos::runtime
