// Contract tests for the native robot description resolver.
//
// The document is parsed with libxml2, validated structurally and checked
// with the native urdfdom parser. No child process is started for the URDF
// validation itself.

#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <ament_index_cpp/get_package_share_directory.hpp>

#include "chaos_assets/description.hpp"
#include "chaos_assets/process.hpp"

using chaos::assets::ConfigurationError;
using chaos::assets::Json;

namespace {

const char* kSimpleUrdf = R"(<robot name="test_robot">
  <link name="base_link"/><link name="imu_link"/>
  <joint name="imu_mount" type="fixed">
    <parent link="base_link"/><child link="imu_link"/>
    <origin xyz="0 0 0.2" rpy="0 0 0"/>
  </joint>
</robot>)";

class TempDirectory {
 public:
  TempDirectory() {
    path_ = std::filesystem::temp_directory_path() /
            ("chaos-assets-description-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
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

Json asset_fixture() {
  Json asset = Json::object();
  asset["id"] = "unit";
  asset["namespace"] = "/robots/unit";
  Json frames = Json::object();
  frames["base"] = "unit/base_link";
  frames["odom"] = "unit/odom";
  frames["map"] = "unit/map";
  asset["frames"] = frames;
  asset["mode"] = "mock";
  return asset;
}

Json description_declaration(const std::string& file) {
  Json declaration = Json::object();
  declaration["file"] = file;
  return declaration;
}

// Restores an environment variable when the scope ends.
class EnvironmentGuard {
 public:
  EnvironmentGuard(std::string name, const std::string& value) : name_(std::move(name)) {
    const char* previous = std::getenv(name_.c_str());
    if (previous != nullptr) {
      previous_ = previous;
      had_previous_ = true;
    }
    setenv(name_.c_str(), value.c_str(), 1);
  }
  ~EnvironmentGuard() {
    if (had_previous_) {
      setenv(name_.c_str(), previous_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  std::string previous_;
  bool had_previous_ = false;
};

// True when the third-party xacro package can be imported.
bool xacro_is_available() {
  const chaos::assets::ProcessResult result =
      chaos::assets::run_bounded({"python3", "-c", "import xacro"}, "probe",
                                  std::chrono::milliseconds(10000), 4096);
  return result.exit_code == 0;
}

}  // namespace

TEST(Description, ResolvesLinksJointsAndDigests) {
  const TempDirectory directory;
  const std::string urdf = directory.write("robot.urdf", kSimpleUrdf);
  const Json result = chaos::assets::resolve_description(Json(), description_declaration(urdf),
                                                         directory.path(), directory.path(),
                                                         asset_fixture());
  EXPECT_TRUE(result.at("enabled").get<bool>());
  EXPECT_EQ(result.at("format"), "urdf");
  EXPECT_EQ(result.at("root"), "unit/base_link");
  EXPECT_EQ(result.at("links"), Json::array({Json("unit/base_link"), Json("unit/imu_link")}));
  EXPECT_EQ(result.at("joints").size(), 1u);
  EXPECT_EQ(result.at("joints").at(0).at("parent"), "unit/base_link");
  EXPECT_EQ(result.at("joints").at(0).at("child"), "unit/imu_link");
  EXPECT_EQ(result.at("sha256").get_ref<const std::string&>().size(), 64u);
  EXPECT_EQ(result.at("source_sha256").get_ref<const std::string&>().size(), 64u);
  EXPECT_EQ(result.at("publish_frequency"), 30.0);
  EXPECT_EQ(result.at("joint_states_topic"), "/robots/unit/joint_states");
}

TEST(Description, DisabledDescriptionIsReported) {
  const TempDirectory directory;
  Json declaration = description_declaration(directory.write("robot.urdf", kSimpleUrdf));
  declaration["enabled"] = false;
  const Json result = chaos::assets::resolve_description(Json(), declaration, directory.path(),
                                                         directory.path(), asset_fixture());
  EXPECT_EQ(result, Json::object({{"enabled", false}}));
}

TEST(Description, RejectsDoctypeAndEntityDeclarations) {
  const TempDirectory directory;
  const std::string urdf = directory.write(
      "robot.urdf",
      std::string("<!DOCTYPE robot [<!ENTITY xxe SYSTEM \"file:///etc/passwd\">]>\n") + kSimpleUrdf);
  EXPECT_THROW(chaos::assets::resolve_description(Json(), description_declaration(urdf),
                                                   directory.path(), directory.path(), asset_fixture()),
               ConfigurationError);
}

TEST(Description, RejectsUnexpandedXacroNamespaces) {
  const TempDirectory directory;
  const std::string urdf = directory.write(
      "robot.urdf",
      "<robot xmlns:xacro=\"http://www.ros.org/wiki/xacro\" name=\"x\">"
      "<link name=\"base_link\"/><xacro:arg name=\"a\"/></robot>");
  EXPECT_THROW(chaos::assets::resolve_description(Json(), description_declaration(urdf),
                                                   directory.path(), directory.path(), asset_fixture()),
               ConfigurationError);
}

TEST(Description, RejectsMalformedXml) {
  const TempDirectory directory;
  const std::string urdf = directory.write("robot.urdf", "<robot name=\"x\"><link name=\"a\"></robot>");
  EXPECT_THROW(chaos::assets::resolve_description(Json(), description_declaration(urdf),
                                                   directory.path(), directory.path(), asset_fixture()),
               ConfigurationError);
}

TEST(Description, RejectsInvalidControlCharacters) {
  const TempDirectory directory;
  std::string content = kSimpleUrdf;
  const size_t newline = content.find('\n');
  content.replace(newline, 1, std::string("\x01"));
  const std::string urdf = directory.write("robot.urdf", content);
  EXPECT_THROW(chaos::assets::resolve_description(Json(), description_declaration(urdf),
                                                   directory.path(), directory.path(), asset_fixture()),
               ConfigurationError);
}

TEST(Description, RejectsDuplicateAttributes) {
  const TempDirectory directory;
  const std::string urdf = directory.write(
      "robot.urdf", "<robot name=\"x\" name=\"y\"><link name=\"a\"/></robot>");
  EXPECT_THROW(chaos::assets::resolve_description(Json(), description_declaration(urdf),
                                                   directory.path(), directory.path(), asset_fixture()),
               ConfigurationError);
}

TEST(Description, RejectsStructurallyInvalidTrees) {
  const TempDirectory directory;
  const char* documents[] = {
      // Two roots.
      "<robot name=\"x\"><link name=\"a\"/><link name=\"b\"/></robot>",
      // A joint that references an unknown link.
      "<robot name=\"x\"><link name=\"a\"/><joint name=\"j\" type=\"fixed\">"
      "<parent link=\"a\"/><child link=\"ghost\"/></joint></robot>",
      // A transform cycle.
      "<robot name=\"x\"><link name=\"a\"/><link name=\"b\"/>"
      "<joint name=\"j1\" type=\"fixed\"><parent link=\"a\"/><child link=\"b\"/></joint>"
      "<joint name=\"j2\" type=\"fixed\"><parent link=\"b\"/><child link=\"a\"/></joint></robot>",
      // An unsupported joint type.
      "<robot name=\"x\"><link name=\"a\"/><link name=\"b\"/>"
      "<joint name=\"j\" type=\"floating\"><parent link=\"a\"/><child link=\"b\"/></joint></robot>",
      // A missing link.
      "<robot name=\"x\"></robot>",
  };
  for (const char* document : documents) {
    const std::string urdf = directory.write("robot.urdf", document);
    EXPECT_THROW(chaos::assets::resolve_description(Json(), description_declaration(urdf),
                                                     directory.path(), directory.path(),
                                                     asset_fixture()),
                 ConfigurationError)
        << document;
  }
}

TEST(Description, RejectsARootFrameThatIsNotTheAssetBase) {
  const TempDirectory directory;
  const std::string urdf = directory.write("robot.urdf", kSimpleUrdf);
  Json asset = asset_fixture();
  asset["frames"]["base"] = "unit/other_link";
  EXPECT_THROW(chaos::assets::resolve_description(Json(), description_declaration(urdf),
                                                   directory.path(), directory.path(), asset),
               ConfigurationError);
}

TEST(Description, ResolvesMeshResourcesToLocalUris) {
  const TempDirectory directory;
  const std::string mesh = directory.write("body.stl", "solid\nendsolid\n");
  const std::string urdf = directory.write(
      "robot.urdf",
      std::string("<robot name=\"x\"><link name=\"base_link\"><visual><geometry>"
                  "<mesh filename=\"") +
          mesh + "\"/></geometry></visual></link></robot>");
  const Json result = chaos::assets::resolve_description(Json(), description_declaration(urdf),
                                                         directory.path(), directory.path(),
                                                         asset_fixture());
  EXPECT_NE(result.at("xml").get_ref<const std::string&>().find("file://"), std::string::npos);
}

TEST(Description, RejectsMissingMeshResources) {
  const TempDirectory directory;
  const std::string urdf = directory.write(
      "robot.urdf",
      "<robot name=\"x\"><link name=\"base_link\"><visual><geometry>"
      "<mesh filename=\"" + directory.path() + "/absent.stl\"/></geometry></visual></link></robot>");
  EXPECT_THROW(chaos::assets::resolve_description(Json(), description_declaration(urdf),
                                                   directory.path(), directory.path(), asset_fixture()),
               ConfigurationError);
}

TEST(Description, RejectsRemoteResourceUris) {
  const TempDirectory directory;
  const std::string urdf = directory.write(
      "robot.urdf",
      "<robot name=\"x\"><link name=\"base_link\"><visual><geometry>"
      "<mesh filename=\"https://example.com/body.stl\"/></geometry></visual></link></robot>");
  EXPECT_THROW(chaos::assets::resolve_description(Json(), description_declaration(urdf),
                                                   directory.path(), directory.path(), asset_fixture()),
               ConfigurationError);
}

TEST(Description, RejectsArgumentsForPlainUrdf) {
  const TempDirectory directory;
  Json declaration = description_declaration(directory.write("robot.urdf", kSimpleUrdf));
  Json arguments = Json::object();
  arguments["length"] = 1;
  declaration["arguments"] = arguments;
  EXPECT_THROW(chaos::assets::resolve_description(Json(), declaration, directory.path(),
                                                   directory.path(), asset_fixture()),
               ConfigurationError);
}

TEST(Description, XacroExpansionFailuresAreReported) {
  const TempDirectory directory;
  const std::string urdf = directory.write(
      "robot.urdf",
      "<robot xmlns:xacro=\"http://www.ros.org/wiki/xacro\" name=\"x\">"
      "<xacro:arg name=\"a\" default=\"1\"/><link name=\"base_link\"/></robot>");
  Json declaration = description_declaration(urdf);
  declaration["format"] = "xacro";

  // A bridge that fails must surface its diagnostic to the caller.
  const std::string failing_bridge = directory.write("failing_bridge.py",
                                                     "#!/usr/bin/env python3\n"
                                                     "import sys\n"
                                                     "sys.stderr.write('expansion refused\\n')\n"
                                                     "sys.exit(3)\n");
  EnvironmentGuard guard("CHAOS_XACRO_BRIDGE", failing_bridge);
  try {
    chaos::assets::resolve_description(Json(), declaration, directory.path(), directory.path(),
                                       asset_fixture());
    ADD_FAILURE() << "a failing xacro expansion must be reported";
  } catch (const ConfigurationError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("expansion failed"), std::string::npos) << message;
    EXPECT_NE(message.find("expansion refused"), std::string::npos) << message;
  }
}

TEST(Description, XacroWithoutTheXacroPackageFailsWithAClearError) {
  if (xacro_is_available()) {
    GTEST_SKIP() << "the xacro package is installed; the missing-package path cannot be exercised";
  }
  const TempDirectory directory;
  const std::string urdf = directory.write(
      "robot.urdf",
      "<robot xmlns:xacro=\"http://www.ros.org/wiki/xacro\" name=\"x\">"
      "<xacro:arg name=\"a\" default=\"1\"/><link name=\"base_link\"/></robot>");
  Json declaration = description_declaration(urdf);
  declaration["format"] = "xacro";
  try {
    chaos::assets::resolve_description(Json(), declaration, directory.path(), directory.path(),
                                       asset_fixture());
    ADD_FAILURE() << "the xacro expansion must fail without the xacro package";
  } catch (const ConfigurationError& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("expansion failed"), std::string::npos) << message;
  }
}
