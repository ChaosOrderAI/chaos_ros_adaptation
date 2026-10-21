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
