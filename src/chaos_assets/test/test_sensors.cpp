// Contract tests for the native sensor resolver.

#include <gtest/gtest.h>

#include <string>

#include "chaos_assets/sensors.hpp"

using chaos::assets::ConfigurationError;
using chaos::assets::Json;

namespace {

Json asset_fixture(const std::string& mode) {
  Json asset = Json::object();
  asset["id"] = "unit";
  asset["namespace"] = "/robots/unit";
  asset["mode"] = mode;
  Json frames = Json::object();
  frames["base"] = "unit/base_link";
  frames["odom"] = "unit/odom";
  frames["map"] = "unit/map";
  asset["frames"] = frames;
  return asset;
}

Json imu_sensor(const std::string& mode, const std::string& topic) {
  Json driver = Json::object();
  driver["mode"] = mode;
  if (mode == "node") {
    driver["package"] = "chaos_imu_node";
    driver["executable"] = "imu_node";
  }
  Json stream = Json::object();
  stream["type"] = "sensor_msgs/msg/Imu";
  stream["topic"] = topic;
  Json streams = Json::object();
  streams["imu"] = stream;
  Json sensor = Json::object();
  sensor["kind"] = "imu";
  sensor["driver"] = driver;
  sensor["streams"] = streams;
  return sensor;
}

Json sensor_map(const Json& sensor) {
  Json map = Json::object();
  map["imu"] = sensor;
  return map;
}

}  // namespace

TEST(Sensors, ResolvesNamespacesTopicsAndDefaults) {
  const Json result = chaos::assets::resolve_sensors(Json(), sensor_map(imu_sensor("mock", "data")),
                                                      "/models", "/inventory", asset_fixture("mock"));
  const Json& sensor = result.at("imu");
  EXPECT_EQ(sensor.at("id"), "imu");
  EXPECT_EQ(sensor.at("namespace"), "/robots/unit/sensors/imu");
  EXPECT_EQ(sensor.at("frame_id"), "unit/imu_link");
  EXPECT_EQ(sensor.at("streams").at("imu").at("topic"), "/robots/unit/sensors/imu/data");
  EXPECT_EQ(sensor.at("streams").at("imu").at("qos"), "sensor_data");
  EXPECT_EQ(sensor.at("streams").at("imu").at("rate_hz"), 10.0);
  EXPECT_EQ(sensor.at("streams").at("imu").at("timeout_s"), 2.0);
  EXPECT_TRUE(sensor.at("streams").at("imu").at("monitor").get<bool>());
}

TEST(Sensors, MockAssetsForceMockDrivers) {
  const Json result = chaos::assets::resolve_sensors(
      Json(), sensor_map(imu_sensor("node", "data")), "/models", "/inventory", asset_fixture("mock"));
  EXPECT_EQ(result.at("imu").at("driver").at("mode"), "mock");
  EXPECT_EQ(result.at("imu").at("driver").at("declared_mode"), "node");
}

TEST(Sensors, RealAssetsRejectMockDrivers) {
  EXPECT_THROW(chaos::assets::resolve_sensors(Json(), sensor_map(imu_sensor("mock", "data")),
                                               "/models", "/inventory", asset_fixture("real")),
               ConfigurationError);
}

TEST(Sensors, RejectsUnknownFieldsAndInvalidStreams) {
  Json sensor = imu_sensor("mock", "data");
  sensor["unexpected"] = 1;
  EXPECT_THROW(chaos::assets::resolve_sensors(Json(), sensor_map(sensor), "/models", "/inventory",
                                               asset_fixture("mock")),
               ConfigurationError);

  Json wrong_type = imu_sensor("mock", "data");
  wrong_type["streams"]["imu"]["type"] = "std_msgs/msg/String";
  EXPECT_THROW(chaos::assets::resolve_sensors(Json(), sensor_map(wrong_type), "/models", "/inventory",
                                               asset_fixture("mock")),
               ConfigurationError);

  Json no_stream = imu_sensor("mock", "data");
  no_stream.erase("streams");
  EXPECT_THROW(chaos::assets::resolve_sensors(Json(), sensor_map(no_stream), "/models", "/inventory",
                                               asset_fixture("mock")),
               ConfigurationError);
}

TEST(Sensors, RejectsAStreamWithoutAPrimaryType) {
  Json camera = Json::object();
  camera["kind"] = "camera";
  Json driver = Json::object();
  driver["mode"] = "mock";
  camera["driver"] = driver;
  Json stream = Json::object();
  stream["type"] = "sensor_msgs/msg/Imu";
  stream["topic"] = "imu";
  Json streams = Json::object();
  streams["imu"] = stream;
  camera["streams"] = streams;
  EXPECT_THROW(chaos::assets::resolve_sensors(Json(), sensor_map(camera), "/models", "/inventory",
                                               asset_fixture("mock")),
               ConfigurationError);
}

TEST(Sensors, RejectsAMountingTransformOntoItself) {
  Json sensor = imu_sensor("mock", "data");
  Json transform = Json::object();
  transform["parent"] = "unit/imu_link";
  transform["xyz"] = Json::array({0, 0, 0.1});
  transform["rpy"] = Json::array({0, 0, 0});
  sensor["transform"] = transform;
  EXPECT_THROW(chaos::assets::resolve_sensors(Json(), sensor_map(sensor), "/models", "/inventory",
                                               asset_fixture("mock")),
               ConfigurationError);
}

TEST(Sensors, RejectsDuplicateFramesInOneDomain) {
  Json first = Json::object();
  first["id"] = "a";
  first["enabled"] = true;
  first["ros_domain_id"] = 87;
  first["frames"] = Json::object({{"base", "a/base_link"}});
  first["sensors"] = Json::object();

  Json second = Json::object();
  second["id"] = "b";
  second["enabled"] = true;
  second["ros_domain_id"] = 87;
  second["frames"] = Json::object({{"base", "b/base_link"}});
  second["sensors"] = Json::object();

  Json sensor = Json::object();
  sensor["id"] = "imu";
  sensor["enabled"] = true;
  sensor["frame_id"] = "a/base_link";
  sensor["driver"] = Json::object({{"mode", "mock"}});
  Json stream = Json::object();
  stream["type"] = "sensor_msgs/msg/Imu";
  stream["topic"] = "/imu";
  stream["frame_id"] = "a/base_link";
  sensor["streams"] = Json::object({{"imu", stream}});
  second["sensors"] = Json::object({{"imu", sensor}});

  EXPECT_THROW(chaos::assets::validate_sensor_graph({first, second}), ConfigurationError);
}

TEST(Sensors, RejectsDuplicateOutputTopicsInOneDomain) {
  Json first = Json::object();
  first["id"] = "a";
  first["enabled"] = true;
  first["ros_domain_id"] = 87;
  first["frames"] = Json::object({{"base", "a/base_link"}});
  first["sensors"] = Json::object();

  Json second = Json::object();
  second["id"] = "b";
  second["enabled"] = true;
  second["ros_domain_id"] = 87;
  second["frames"] = Json::object({{"base", "b/base_link"}});
  second["sensors"] = Json::object();

  auto sensor = [](const std::string& id, const std::string& frame) {
    Json value = Json::object();
    value["id"] = id;
    value["enabled"] = true;
    value["frame_id"] = frame;
    value["driver"] = Json::object({{"mode", "mock"}});
    Json stream = Json::object();
    stream["type"] = "sensor_msgs/msg/Imu";
    stream["topic"] = "/shared/imu";
    stream["frame_id"] = frame;
    value["streams"] = Json::object({{"imu", stream}});
    return value;
  };
  first["sensors"] = Json::object({{"imu", sensor("imu", "a/imu_link")}});
  second["sensors"] = Json::object({{"imu", sensor("imu", "b/imu_link")}});

  EXPECT_THROW(chaos::assets::validate_sensor_graph({first, second}), ConfigurationError);
}

TEST(Sensors, ResolvesRelativeTopicNames) {
  EXPECT_EQ(chaos::assets::resolve_topic(Json("data"), "/robots/unit", "field"),
            "/robots/unit/data");
  EXPECT_EQ(chaos::assets::resolve_topic(Json("/absolute"), "/robots/unit", "field"), "/absolute");
}
