// Contract tests for the native recording policy resolver.

#include <gtest/gtest.h>

#include <string>

#include "chaos_assets/collection.hpp"

using chaos::assets::ConfigurationError;
using chaos::assets::Json;

namespace {

Json asset_fixture() {
  Json asset = Json::object();
  asset["id"] = "unit";
  asset["namespace"] = "/robots/unit";
  Json frames = Json::object();
  frames["base"] = "unit/base_link";
  asset["frames"] = frames;
  Json description = Json::object();
  description["enabled"] = true;
  description["use_sim_time"] = false;
  description["joint_states_topic"] = "/robots/unit/joint_states";
  asset["description"] = description;
  Json sensors = Json::object();
  Json sensor = Json::object();
  sensor["enabled"] = true;
  sensor["driver"] = Json::object({{"mode", "mock"}});
  Json stream = Json::object();
  stream["topic"] = "/robots/unit/sensors/imu/data";
  sensor["streams"] = Json::object({{"imu", stream}});
  sensors["imu"] = sensor;
  asset["sensors"] = sensors;
  return asset;
}

}  // namespace

TEST(Collection, AppliesTheDocumentedDefaults) {
  const Json result = chaos::assets::resolve_collection(Json(Json::object()), Json(), "/models",
                                                        "/inventory", asset_fixture());
  EXPECT_FALSE(result.at("enabled").get<bool>());
  EXPECT_EQ(result.at("storage_id"), "mcap");
  EXPECT_EQ(result.at("max_bagfile_duration_s"), 300);
  EXPECT_EQ(result.at("max_cache_size_mb"), 64);
  EXPECT_EQ(result.at("min_free_space_mb"), 1024);
  EXPECT_TRUE(result.at("topics").is_array());
  EXPECT_NE(result.at("topics").get<std::vector<std::string>>().size(), 0u);
}

TEST(Collection, ReturnsNullWithoutAnyDeclaration) {
  const Json result = chaos::assets::resolve_collection(Json(), Json(), "/models", "/inventory",
                                                        asset_fixture());
  EXPECT_TRUE(result.is_null());
}

TEST(Collection, ResolvesTheOutputDirectoryAgainstTheDeclaringFile) {
  Json declaration = Json::object();
  declaration["output_dir"] = "recordings";
  const Json result = chaos::assets::resolve_collection(Json(Json::object()), declaration, "/models",
                                                        "/inventory", asset_fixture());
  EXPECT_EQ(result.at("output_dir"), "/inventory/recordings");
}

TEST(Collection, RejectsARemoteOutputDirectory) {
  Json declaration = Json::object();
  declaration["output_dir"] = "s3://bucket/recordings";
  EXPECT_THROW(chaos::assets::resolve_collection(Json(Json::object()), declaration, "/models", "/inventory",
                                                 asset_fixture()),
               ConfigurationError);
}

TEST(Collection, RejectsNonBooleanFlags) {
  Json declaration = Json::object();
  declaration["enabled"] = "true";
  EXPECT_THROW(chaos::assets::resolve_collection(Json(Json::object()), declaration, "/models", "/inventory",
                                                 asset_fixture()),
               ConfigurationError);
}

TEST(Collection, RejectsAnUnsupportedStorage) {
  Json declaration = Json::object();
  declaration["storage_id"] = "rosbag1";
  EXPECT_THROW(chaos::assets::resolve_collection(Json(Json::object()), declaration, "/models", "/inventory",
                                                 asset_fixture()),
               ConfigurationError);
}

TEST(Collection, RequiresAtLeastOneSplittingLimit) {
  Json declaration = Json::object();
  declaration["max_bagfile_duration_s"] = 0;
  declaration["max_bagfile_size_mb"] = 0;
  EXPECT_THROW(chaos::assets::resolve_collection(Json(Json::object()), declaration, "/models", "/inventory",
                                                 asset_fixture()),
               ConfigurationError);
}

TEST(Collection, RequiresTheFreeSpaceReserveToCoverTheCache) {
  Json declaration = Json::object();
  declaration["max_cache_size_mb"] = 2048;
  declaration["min_free_space_mb"] = 1024;
  EXPECT_THROW(chaos::assets::resolve_collection(Json(Json::object()), declaration, "/models", "/inventory",
                                                 asset_fixture()),
               ConfigurationError);
}

TEST(Collection, RejectsAProbeTimeoutAboveThePollInterval) {
  Json declaration = Json::object();
  Json timing = Json::object();
  timing["probe_timeout_s"] = 2.0;
  timing["poll_interval_s"] = 1.0;
  declaration["time_sync"] = timing;
  EXPECT_THROW(chaos::assets::resolve_collection(Json(Json::object()), declaration, "/models", "/inventory",
                                                 asset_fixture()),
               ConfigurationError);
}

TEST(Collection, RejectsCollectionWithSimulatedTime) {
  Json asset = asset_fixture();
  asset["description"]["use_sim_time"] = true;
  Json declaration = Json::object();
  declaration["enabled"] = true;
  EXPECT_THROW(chaos::assets::resolve_collection(Json(Json::object()), declaration, "/models", "/inventory", asset),
               ConfigurationError);
}

TEST(Collection, ResolvesExplicitTopicsAndRejectsDuplicates) {
  Json declaration = Json::object();
  declaration["topics"] = Json::array({Json("state"), Json("/absolute/topic")});
  const Json result = chaos::assets::resolve_collection(Json(Json::object()), declaration, "/models",
                                                        "/inventory", asset_fixture());
  const std::vector<std::string> topics = result.at("topics").get<std::vector<std::string>>();
  EXPECT_NE(std::find(topics.begin(), topics.end(), "/robots/unit/state"), topics.end());
  EXPECT_NE(std::find(topics.begin(), topics.end(), "/absolute/topic"), topics.end());

  Json duplicated = Json::object();
  duplicated["topics"] = Json::array({Json("state"), Json("/robots/unit/state")});
  EXPECT_THROW(chaos::assets::resolve_collection(Json(Json::object()), duplicated, "/models", "/inventory",
                                                 asset_fixture()),
               ConfigurationError);
}
