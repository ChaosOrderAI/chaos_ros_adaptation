#include "chaos_assets/paths.hpp"

#include <filesystem>
#include <regex>

#include <ament_index_cpp/get_package_share_directory.hpp>

namespace chaos::assets {

namespace {

std::regex package_name_re(R"(^[a-z][a-z0-9_]*$)");

bool valid_package_uri(const std::string& value, std::string& package, std::string& relative) {
  // package://netloc/path — reject query/fragment, empty path, escaping.
  if (value.rfind("package://", 0) != 0) return false;
  std::string rest = value.substr(std::string("package://").size());
  const size_t slash = rest.find('/');
  if (slash == std::string::npos) return false;
  package = rest.substr(0, slash);
  relative = rest.substr(slash + 1);
  if (!std::regex_match(package, package_name_re)) return false;
  if (relative.empty() || relative.find('?') != std::string::npos ||
      relative.find('#') != std::string::npos) {
    return false;
  }
  std::filesystem::path rel(relative);
  for (const auto& part : rel) {
    if (part == "..") return false;
  }
  return true;
}

}  // namespace

std::string resolve_path(const Json& value, const std::string& base, const std::string& where) {
  const std::string& raw = text(value, where);
  if (raw.rfind("package://", 0) == 0) {
    std::string package, relative;
    if (!valid_package_uri(raw, package, relative)) {
      throw ConfigurationError(where + ": invalid package resource URI '" + raw + "'");
    }
    std::string share;
    try {
      share = ament_index_cpp::get_package_share_directory(package);
    } catch (const std::exception&) {
      throw ConfigurationError(where + ": package '" + package +
                               "' is unavailable; source the installed ROS workspace");
    }
    return std::filesystem::weakly_canonical(std::filesystem::path(share) / relative).string();
  }
  std::filesystem::path path(raw);
  std::filesystem::path joined = path.is_absolute() ? path : std::filesystem::path(base) / path;
  std::error_code ec;
  std::string resolved = std::filesystem::weakly_canonical(joined, ec).string();
  return ec ? joined.lexically_normal().string() : resolved;
}

}  // namespace chaos::assets
