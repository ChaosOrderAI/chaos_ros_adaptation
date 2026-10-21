// ROS-free tests of the shared asset rules.
//
// These are the rules the runtime startup path and the --validate-asset
// preflight both use, so they are tested once, here, instead of being
// duplicated per language.
#include <gtest/gtest.h>

#include <string>

#include "chaos_runtime/asset_validation.hpp"

namespace chaos::runtime {
namespace {

std::string assetJson(const std::string & plugin, const std::string & kind,
                      const std::string & capabilities,
                      const std::string & options) {
  return std::string(R"({"id":"a","model":"m","kind":")") + kind +
         R"(","mode":"mock","namespace":"/a","ros_domain_id":196,)"
         R"("frames":{"base":"base_link","odom":"odom","map":"map"},)"
         R"("capabilities":[)" + capabilities + R"(],)"
         R"("limits":{"linear_mps":1.0,"angular_rps":1.0,"command_timeout_s":0.5,)"
         R"("state_timeout_s":2.0,"lease_max_s":30.0,"action_timeout_s":120.0},)"
         R"("driver":{"plugin":")" + plugin + R"(","options":{)" + options +
         R"(}}})";
}

TEST(AssetValidation, AcceptsAMockGroundAsset) {
  std::string error;
  EXPECT_TRUE(validateAssetJson(
    assetJson("mock", "ugv", R"("state","motion.velocity","motion.stop")",
              R"("kind":"ugv")"),
    error)) << error;
}

TEST(AssetValidation, RejectsAnUnknownPlugin) {
  std::string error;
  EXPECT_FALSE(validateAssetJson(
    assetJson("not_a_plugin", "ugv", R"("state")", R"("kind":"ugv")"),
    error));
  EXPECT_NE(error.find("Unknown adapter plugin"), std::string::npos) << error;
}

TEST(AssetValidation, RejectsAnUnknownOption) {
  std::string error;
  EXPECT_FALSE(validateAssetJson(
    assetJson("mock", "ugv", R"("state")", R"("kind":"ugv","bogus":1)"),
    error));
  EXPECT_NE(error.find("Unsupported mock options"), std::string::npos)
    << error;
}

TEST(AssetValidation, RejectsAnUnsupportedCapability) {
  std::string error;
  EXPECT_FALSE(validateAssetJson(
    assetJson("lingao_ros2", "ugv", R"("state","navigation.pose")",
              R"("odom_topic":"/a/odom","cmd_vel_topic":"/a/cmd")"),
    error));
  EXPECT_NE(error.find("does not support"), std::string::npos) << error;
}

TEST(AssetValidation, MockGroundKindDropsTheFlightCapabilities) {
  std::string error;
  // A ground kind must not claim a flight action.
  EXPECT_FALSE(validateAssetJson(
    assetJson("mock", "ugv", R"("state","flight.takeoff")",
              R"("kind":"ugv")"),
    error));
  EXPECT_NE(error.find("flight.takeoff"), std::string::npos) << error;
  // The same capability is valid for an air kind.
  EXPECT_TRUE(validateAssetJson(
    assetJson("mock", "uav", R"("state","flight.takeoff")",
              R"("kind":"uav")"),
    error)) << error;
}

TEST(AssetValidation, Go2RequiresEveryExplicitOption) {
  std::string error;
  EXPECT_FALSE(validateAssetJson(
    assetJson("unitree_go2", "quadruped", R"("state")", R"("odom_topic":"/a/odom")"),
    error));
  EXPECT_NE(error.find("requires explicit options"), std::string::npos)
    << error;
}

TEST(AssetValidation, MalformedJsonIsRejectedWithAReason) {
  std::string error;
  EXPECT_FALSE(validateAssetJson("{not json", error));
  EXPECT_FALSE(error.empty());
}

TEST(AssetValidation, AbsoluteTopicRule) {
  EXPECT_TRUE(isAbsoluteTopic("/a/b_c"));
  EXPECT_TRUE(isAbsoluteTopic("/status"));
  EXPECT_FALSE(isAbsoluteTopic("relative/topic"));
  EXPECT_FALSE(isAbsoluteTopic("/a//b"));
  EXPECT_FALSE(isAbsoluteTopic("/a/b-c"));
  EXPECT_FALSE(isAbsoluteTopic("/"));
  EXPECT_FALSE(isAbsoluteTopic(""));
}

}  // namespace
}  // namespace chaos::runtime
