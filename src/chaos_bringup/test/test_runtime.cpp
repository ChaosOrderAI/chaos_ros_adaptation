// Contract tests of the no-hardware interface of the runtime.
//
// The runtime owns the validation of an asset and the detection of its
// native backends. The planner asks, the runtime answers. The protocol is a
// JSON document, so an exit code alone is not an answer: a runtime that
// reports something else has not validated the asset.
//
// The tests use a script of their own as the interface, so the contract is
// covered without a ROS installation and without hardware.

#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "chaos_assets/errors.hpp"
#include "chaos_bringup/driver_registry.hpp"
#include "chaos_bringup/runtime.hpp"

using chaos::assets::ConfigurationError;
using chaos::bringup::Json;
using chaos::bringup::RuntimeInterface;
using chaos::bringup::validate_asset_with_runtime;

namespace {

// A script that answers with the given text and exit code.
//
// The script reads its standard input, so the test also proves that the
// asset document reaches the interface.
class FakeRuntime {
 public:
  FakeRuntime(const std::string& name, const std::string& answer, int exit_code = 0) {
    directory_ = std::filesystem::temp_directory_path() /
                 ("chaos-bringup-runtime-" + std::to_string(::getpid()) + "-" + name);
    std::filesystem::create_directories(directory_);
    path_ = (directory_ / "interface").string();
    std::ofstream script(path_);
    script << "#!/bin/sh\n"
           << "cat > " << directory_ << "/input\n"
           << "printf '%s' '" << answer << "'\n"
           << "exit " << exit_code << "\n";
    script.close();
    ::chmod(path_.c_str(), 0755);
  }
  ~FakeRuntime() {
    std::error_code error;
    std::filesystem::remove_all(directory_, error);
  }
  FakeRuntime(const FakeRuntime&) = delete;
  FakeRuntime& operator=(const FakeRuntime&) = delete;

  const std::string& path() const { return path_; }

  // The payload that the interface received.
  std::string received() const {
    std::ifstream stream(directory_ / "input");
    return std::string((std::istreambuf_iterator<char>(stream)),
                        std::istreambuf_iterator<char>());
  }

 private:
  std::filesystem::path directory_;
  std::string path_;
};

Json asset_document() {
  Json asset = Json::object();
  asset["id"] = "dog_001";
  asset["mode"] = "real";
  Json driver = Json::object();
  driver["plugin"] = "unitree_go2";
  asset["driver"] = driver;
  return asset;
}

}  // namespace

TEST(Runtime, AValidAnswerAcceptsTheAsset) {
  const FakeRuntime runtime("valid", "{\"valid\": true}");
  RuntimeInterface interface;
  interface.validate_path = runtime.path();
  EXPECT_NO_THROW(validate_asset_with_runtime(asset_document(), interface));
  // The asset document reached the interface.
  EXPECT_NE(runtime.received().find("unitree_go2"), std::string::npos);
}

TEST(Runtime, ANegativeAnswerRejectsTheAsset) {
  const FakeRuntime runtime("negative", "{\"valid\": false, \"reason\": \"unknown plugin\"}");
  RuntimeInterface interface;
  interface.validate_path = runtime.path();
  try {
    validate_asset_with_runtime(asset_document(), interface);
    FAIL() << "a negative answer was accepted";
  } catch (const ConfigurationError& error) {
    EXPECT_NE(std::string(error.what()).find("unknown plugin"), std::string::npos)
        << error.what();
  }
}

TEST(Runtime, AMalformedAnswerRejectsTheAsset) {
  // The exit code is zero, but the answer is not a JSON object. The asset
  // has not been validated, so the startup has to stop.
  const FakeRuntime runtime("malformed", "not json at all");
  RuntimeInterface interface;
  interface.validate_path = runtime.path();
  EXPECT_THROW(validate_asset_with_runtime(asset_document(), interface), ConfigurationError);
}

TEST(Runtime, AnAnswerWithoutTheValidFieldRejectsTheAsset) {
  const FakeRuntime runtime("incomplete", "{\"status\": \"ok\"}");
  RuntimeInterface interface;
  interface.validate_path = runtime.path();
  EXPECT_THROW(validate_asset_with_runtime(asset_document(), interface), ConfigurationError);
}

TEST(Runtime, AFailingInterfaceRejectsTheAsset) {
  const FakeRuntime runtime("failing", "{\"valid\": true}", 3);
  RuntimeInterface interface;
  interface.validate_path = runtime.path();
  EXPECT_THROW(validate_asset_with_runtime(asset_document(), interface), ConfigurationError);
}

TEST(Runtime, AMissingInterfaceRejectsTheAsset) {
  RuntimeInterface interface;
  interface.validate_path = "/nonexistent/robot_runtime";
  EXPECT_THROW(validate_asset_with_runtime(asset_document(), interface), ConfigurationError);
}

TEST(Runtime, TheHumanoidBackendIsReportedByItsOwnDriver) {
  const FakeRuntime booster("booster", "{\"booster_sdk\": true}");
  const FakeRuntime unitree("unitree-key", "{\"unitree_sdk2\": true}");
  RuntimeInterface interface;
  interface.capabilities_path = booster.path();
  // The humanoid plugin is reported by its own driver, under its own key.
  EXPECT_TRUE(chaos::bringup::native_backend_available("humanoid", interface));
  // The key of the Go2 driver is not the key of the humanoid plugin.
  interface.capabilities_path = unitree.path();
  EXPECT_FALSE(chaos::bringup::native_backend_available("humanoid", interface));
}

TEST(Runtime, TheGo2BackendKeepsItsOwnKey) {
  const FakeRuntime unitree("go2-key", "{\"unitree_sdk2\": true}");
  const FakeRuntime booster("booster-key", "{\"booster_sdk\": true}");
  RuntimeInterface interface;
  interface.capabilities_path = unitree.path();
  EXPECT_TRUE(chaos::bringup::native_backend_available("unitree_go2", interface));
  interface.capabilities_path = booster.path();
  EXPECT_FALSE(chaos::bringup::native_backend_available("unitree_go2", interface));
}

TEST(Runtime, TheDriverRegistryKnowsTheReportingExecutables) {
  const chaos::bringup::DriverMetadata* humanoid = chaos::bringup::driver_metadata_for("humanoid");
  ASSERT_NE(humanoid, nullptr);
  EXPECT_STREQ(humanoid->capabilities_executable, "humanoid_driver");
  EXPECT_STREQ(humanoid->capability_key, "booster_sdk");
  EXPECT_EQ(humanoid->driver_executable, std::string("humanoid_driver"));
  // The vendor topics of the humanoid are unsuffixed, so the DDS domain
  // belongs to one humanoid.
  EXPECT_EQ(humanoid->dds_ownership, chaos::bringup::DdsOwnership::kDomainEndpoint);

  const chaos::bringup::DriverMetadata* go2 = chaos::bringup::driver_metadata_for("unitree_go2");
  ASSERT_NE(go2, nullptr);
  EXPECT_STREQ(go2->capabilities_executable, "go2_driver");
  EXPECT_STREQ(go2->capability_key, "unitree_sdk2");
  EXPECT_EQ(go2->dds_ownership, chaos::bringup::DdsOwnership::kHostEndpoint);

  // The interface of the runtime keeps the default of the Go2 driver.
  const RuntimeInterface interface;
  EXPECT_EQ(interface.capabilities_executable, std::string("go2_driver"));
  EXPECT_EQ(chaos::bringup::capability_of_plugin("humanoid"), std::string("booster_sdk"));
  EXPECT_EQ(chaos::bringup::capability_of_plugin("unitree_go2"), std::string("unitree_sdk2"));
}
