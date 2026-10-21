// Contract tests for path and package URI resolution.

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include <ament_index_cpp/get_package_share_directory.hpp>

#include "chaos_assets/paths.hpp"

using chaos::assets::ConfigurationError;
using chaos::assets::Json;

namespace {

std::string resolve(const std::string& value, const std::string& base) {
  return chaos::assets::resolve_path(Json(value), base, "field");
}

}  // namespace

TEST(Paths, RelativePathsResolveAgainstTheirDeclaringDirectory) {
  const std::string resolved = resolve("models/robot.yaml", "/tmp/declaring");
  EXPECT_EQ(resolved, "/tmp/declaring/models/robot.yaml");
}

TEST(Paths, AbsolutePathsAreKept) {
  EXPECT_EQ(resolve("/opt/robot.yaml", "/tmp/declaring"), "/opt/robot.yaml");
}

TEST(Paths, PackageUriKeepsItsPackageRelativeSemantics) {
  // The package must exist in the ament index for this check.
  const std::string share = ament_index_cpp::get_package_share_directory("chaos_assets");
  const std::string resolved =
      resolve("package://chaos_assets/models/lingao_vehicle/v1/model.yaml", "/tmp");
  EXPECT_EQ(resolved, std::filesystem::weakly_canonical(share + "/models/lingao_vehicle/v1/model.yaml").string());
}

TEST(Paths, PackageUriRejectsEscapingAndMalformedReferences) {
  EXPECT_THROW(resolve("package://chaos_assets/../outside.yaml", "/tmp"), ConfigurationError);
  EXPECT_THROW(resolve("package://chaos_assets/", "/tmp"), ConfigurationError);
  EXPECT_THROW(resolve("package://chaos_assets///", "/tmp"), ConfigurationError);
  EXPECT_THROW(resolve("package://chaos_assets/model.yaml?override=true", "/tmp"), ConfigurationError);
  EXPECT_THROW(resolve("package://chaos_assets/model.yaml#fragment", "/tmp"), ConfigurationError);
  EXPECT_THROW(resolve("package://invalid-package/model.yaml", "/tmp"), ConfigurationError);
  EXPECT_THROW(resolve("package://[invalid/model.yaml", "/tmp"), ConfigurationError);
  EXPECT_THROW(resolve("package://chaos_assets", "/tmp"), ConfigurationError);
}

TEST(Paths, RepeatedSlashesStayInsideThePackage) {
  // A crafted URI must never escape the package share directory.
  const std::string share = ament_index_cpp::get_package_share_directory("chaos_assets");
  const std::string resolved = resolve("package://chaos_assets//etc/passwd", "/tmp");
  EXPECT_EQ(resolved, std::filesystem::weakly_canonical(share + "/etc/passwd").string());
  EXPECT_EQ(resolved.rfind(share, 0), 0u);
  EXPECT_NE(resolved.find("/etc/passwd"), std::string::npos);
}
