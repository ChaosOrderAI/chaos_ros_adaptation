// Contract tests for the native inventory engine.
//
// The cases mirror the behaviour of the original Python suite: the same
// configuration must be accepted, and the same malformed configuration must
// be rejected with the same error category.

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <ament_index_cpp/get_package_share_directory.hpp>

#include "chaos_assets/inventory.hpp"
#include "chaos_assets/json.hpp"

using chaos::assets::ConfigurationError;
using chaos::assets::Inventory;
using chaos::assets::Json;

namespace {

// The configuration directory of the installed package.
std::string share_directory() {
  return ament_index_cpp::get_package_share_directory("chaos_assets");
}

std::string demo_config() { return share_directory() + "/config/demo.yaml"; }

// A temporary directory that removes itself.
class TempDirectory {
 public:
  TempDirectory() {
    path_ = std::filesystem::temp_directory_path() /
            ("chaos-assets-test-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(path_);
  }
  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  const std::string& path() const { return path_; }

  std::string write(const std::string& name, const std::string& content) const {
    const std::string full = path_ + "/" + name;
    std::ofstream stream(full);
    stream << content;
    stream.close();
    return full;
  }

 private:
  static int counter;
  std::string path_;
};

int TempDirectory::counter = 0;

const char* kInventoryTemplate = R"(schema_version: 1
hosts:
  dev:
    ros_domain_id: 87
assets:
  - id: vehicle_001
    model_file: %s
    host: dev
    mode: mock
)";

std::string write_inventory(const TempDirectory& directory, const std::string& body) {
  const std::string model = share_directory() + "/models/lingao_vehicle/v1/model.yaml";
  std::string content;
  for (size_t position = 0; position < body.size();) {
    const size_t marker = body.find("%s", position);
    if (marker == std::string::npos) {
      content.append(body, position, std::string::npos);
      break;
    }
    content.append(body, position, marker - position);
    content += model;
    position = marker + 2;
  }
  return directory.write("assets.yaml", content);
}

}  // namespace

TEST(Inventory, DemoInventoryResolvesTwoAssets) {
  const Inventory inventory = chaos::assets::load_inventory(demo_config());
  EXPECT_EQ(inventory.select("dev").size(), 2u);
  for (const Json& asset : inventory.assets()) {
    EXPECT_EQ(asset.at("driver").at("plugin"), "mock");
  }
  EXPECT_NE(inventory.assets()[0].at("config_digest"), inventory.assets()[1].at("config_digest"));
}

TEST(Inventory, RealModeSelectsTheRealBackend) {
  const Inventory inventory = chaos::assets::load_inventory(demo_config(), "real");
  const Json& drone = inventory.assets()[1];
  EXPECT_EQ(drone.at("kind"), "uav");
  EXPECT_EQ(drone.at("capabilities"), Json::array({Json("state")}));
  EXPECT_EQ(drone.at("driver").at("plugin"), "dji_psdk_legacy");
  const Inventory mock = chaos::assets::load_inventory(demo_config());
  EXPECT_NE(mock.assets()[1].at("config_digest"), drone.at("config_digest"));
}

TEST(Inventory, ModeOverrideMustBeMockOrReal) {
  EXPECT_THROW(chaos::assets::load_inventory(demo_config(), "virtual"), ConfigurationError);
}

TEST(Inventory, RejectsMalformedInventories) {
  const TempDirectory directory;
  const struct Case {
    const char* body;
    const char* message;
  } cases[] = {
      {"schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\nassets:\n"
       "  - id: vehicle_001\n    model_file: %s\n    host: dev\n    mode: mock\n"
       "  - id: vehicle_001\n    model_file: %s\n    host: dev\n    mode: mock\n",
       "duplicate asset"},
      {"schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\nassets:\n"
       "  - id: vehicle_001\n    model_file: %s\n    host: dev\n    namespace: /bad-name\n",
       "namespace"},
      {"schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\nassets:\n"
       "  - id: vehicle_001\n    model_file: %s\n    host: missing\n",
       "unknown host"},
      {"schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\nassets:\n"
       "  - id: vehicle_001\n    model_file: %s\n    host: dev\n    limits: {linear_mps: .nan}\n",
       "finite"},
      {"schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\nassets:\n"
       "  - id: vehicle_001\n    model_file: %s\n    host: dev\n    limits: {linear_mps: true}\n",
       "finite"},
      {"schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\nassets:\n"
       "  - id: vehicle_001\n    model_file: %s\n    host: dev\n    limits: {linear_mps: -1}\n",
       "finite"},
      {"schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\nassets:\n"
       "  - id: vehicle_001\n    model_file: %s\n    host: dev\n    limts: {}\n",
       "unknown fields"},
      {"schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\nassets:\n"
       "  - id: vehicle_001\n    model_file: %s\n    host: dev\n    capabilities: [state, flight.takeoff]\n",
       "unsupported"},
      {"schema_version: 1\nhosts:\n  dev: {ros_domain_id: true}\nassets:\n"
       "  - id: vehicle_001\n    model_file: %s\n    host: dev\n",
       "integer"},
      {"schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\nassets:\n"
       "  - id: vehicle_001\n    model_file: %s\n    host: dev\n    transport: missing\n",
       "unknown transport"},
      {"schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\nassets:\n"
       "  - id: vehicle_001\n    model_file: %s\n    host: dev\n    enabled: 'false'\n",
       "boolean"},
  };
  for (const Case& item : cases) {
    const std::string path = write_inventory(directory, item.body);
    try {
      chaos::assets::load_inventory(path);
      ADD_FAILURE() << "expected a rejection for: " << item.body;
    } catch (const ConfigurationError& error) {
      const std::string message = error.what();
      EXPECT_NE(message.find(item.message), std::string::npos)
          << "message '" << message << "' does not mention '" << item.message << "'";
    }
  }
}

TEST(Inventory, RejectsDuplicateYamlKeys) {
  const TempDirectory directory;
  const std::string path = directory.write("duplicate.yaml", "schema_version: 1\nschema_version: 1\n");
  EXPECT_THROW(chaos::assets::load_inventory(path), ConfigurationError);
}

TEST(Inventory, HostFilterAndDisabledAssets) {
  const TempDirectory directory;
  const std::string path = write_inventory(
      directory,
      "schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\nassets:\n"
      "  - id: vehicle_001\n    model_file: %s\n    host: dev\n    mode: mock\n"
      "  - id: drone_001\n    model_file: %s\n    host: dev\n    mode: mock\n    enabled: false\n");
  const Inventory inventory = chaos::assets::load_inventory(path);
  EXPECT_EQ(inventory.select("dev").size(), 1u);
  EXPECT_THROW(inventory.select("dev", {"drone_001"}), ConfigurationError);
  EXPECT_THROW(inventory.select("other"), ConfigurationError);
}

TEST(Inventory, InstanceOptionsOverrideModelOptions) {
  const TempDirectory directory;
  const std::string path = write_inventory(
      directory,
      "schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\nassets:\n"
      "  - id: vehicle_001\n    model_file: %s\n    host: dev\n    mode: real\n"
      "    driver: {options: {odom_topic: /custom/odom}}\n");
  const Inventory inventory = chaos::assets::load_inventory(path);
  EXPECT_EQ(inventory.assets()[0].at("driver").at("options").at("odom_topic"), "/custom/odom");
  EXPECT_EQ(inventory.assets()[0].at("driver").at("options").at("cmd_vel_topic"), "/cmd_vel");
}

TEST(Inventory, SelectReturnsDetachedData) {
  const Inventory inventory = chaos::assets::load_inventory(demo_config());
  Json selected = inventory.select("dev");
  selected[0]["limits"]["linear_mps"] = 100;
  EXPECT_NE(inventory.assets()[0].at("limits").at("linear_mps"), 100);
}

TEST(Inventory, InvalidTransportExecutableIsNotDefaulted) {
  const TempDirectory directory;
  for (const char* value : {"null", "''", "123", "[]"}) {
    const std::string body =
        std::string("schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\ntransports:\n"
                    "  mqtt: {executable: ") +
        value + ", config: {}}\nassets:\n  - id: vehicle_001\n    model_file: %s\n    host: dev\n";
    const std::string path = write_inventory(directory, body);
    EXPECT_THROW(chaos::assets::load_inventory(path), ConfigurationError) << value;
  }
}

TEST(Inventory, CustomTransportExecutableStaysRelativeToTheInventory) {
  const TempDirectory directory;
  const std::string path = write_inventory(
      directory,
      "schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\ntransports:\n"
      "  mqtt: {executable: ./bin/custom_bridge, config: {}}\nassets:\n"
      "  - id: vehicle_001\n    model_file: %s\n    host: dev\n");
  const Inventory inventory = chaos::assets::load_inventory(path);
  EXPECT_EQ(inventory.transports().at("mqtt").at("executable"),
            directory.path() + "/bin/custom_bridge");
}

TEST(Inventory, PackageUriMustStayInsideThePackage) {
  const TempDirectory directory;
  for (const char* uri : {"package://chaos_assets/../outside.yaml", "package://chaos_assets/",
                          "package://chaos_assets/model.yaml?override=true",
                          "package://invalid-package/model.yaml", "package://[invalid/model.yaml"}) {
    const std::string body = std::string("schema_version: 1\nhosts:\n  dev: {ros_domain_id: 87}\n"
                                         "assets:\n  - id: vehicle_001\n    model_file: '") +
                             uri + "'\n    host: dev\n";
    const std::string path = write_inventory(directory, body);
    EXPECT_THROW(chaos::assets::load_inventory(path), ConfigurationError) << uri;
  }
}

TEST(Inventory, DigestIsStableAcrossLoads) {
  const Inventory first = chaos::assets::load_inventory(demo_config());
  const Inventory second = chaos::assets::load_inventory(demo_config());
  EXPECT_EQ(first.assets()[0].at("config_digest"), second.assets()[0].at("config_digest"));
}

TEST(Inventory, ResolvedInventoryCarriesTheDocumentedFields) {
  const Inventory inventory = chaos::assets::load_inventory(demo_config());
  const Json document = inventory.as_dict();
  EXPECT_EQ(document.at("schema_version"), 1);
  EXPECT_TRUE(document.at("source").is_string());
  EXPECT_TRUE(document.contains("hosts"));
  EXPECT_TRUE(document.contains("assets"));
  EXPECT_TRUE(document.contains("transports"));
  const Json& asset = document.at("assets").at(0);
  for (const char* field : {"id", "model", "model_version", "kind", "host", "ros_domain_id",
                            "namespace", "enabled", "mode", "driver", "capabilities", "frames",
                            "limits", "resources", "transport", "model_file", "description",
                            "sensors", "data_collection", "config_digest"}) {
    EXPECT_TRUE(asset.contains(field)) << field;
  }
}
