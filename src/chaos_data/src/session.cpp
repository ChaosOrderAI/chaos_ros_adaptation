// Durable collection session metadata without driver credentials.
#include "chaos_data/session.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <random>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unistd.h>

#include "chaos_core/clock.hpp"
#include "chaos_core/time.hpp"

namespace chaos::data {
namespace {

constexpr std::int64_t kNs = 1'000'000'000;

// [A-Za-z][A-Za-z0-9_]* — validated without <regex> to keep this hot-path
// free of locale/allocation surprises.
bool rosSafeId(const std::string & id) {
  if (id.empty() || !(std::isalpha(static_cast<unsigned char>(id[0])) != 0)) {
    return false;
  }
  for (const char c : id) {
    const bool ok = std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

std::string utcStamp(std::int64_t utc_ns) {
  const std::time_t seconds = static_cast<std::time_t>(utc_ns / kNs);
  std::tm tm{};
  gmtime_r(&seconds, &tm);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%S", &tm);
  return buffer;
}

std::string randomHex() {
  std::array<unsigned char, 16> bytes{};
  std::random_device source;
  for (auto & byte : bytes) {
    byte = static_cast<unsigned char>(source());
  }
  // RFC 4122 version 4 shape; uniqueness comes from 122 random bits.
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
  static const char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(32);
  for (const unsigned char byte : bytes) {
    result += digits[byte >> 4];
    result += digits[byte & 0x0f];
  }
  return result;
}

// Test-injected session tags must stay a single safe path component.
bool safeUniqueTag(const std::string & tag) {
  if (tag.empty() || tag.size() > 64) {
    return false;
  }
  for (const char c : tag) {
    const bool ok = std::isalnum(static_cast<unsigned char>(c)) != 0 ||
                    c == '-' || c == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

void fsyncDirectory(const std::filesystem::path & directory) {
  const int fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY);
  if (fd < 0) {
    throw std::system_error(errno, std::generic_category(),
                            "open session directory");
  }
  try {
    if (::fsync(fd) != 0) {
      throw std::system_error(errno, std::generic_category(),
                              "fsync session directory");
    }
  } catch (...) {
    ::close(fd);
    throw;
  }
  ::close(fd);
}

}  // namespace

void atomicJsonReplace(const std::filesystem::path & path,
                       const nlohmann::json & data) {
  const std::string payload = data.dump(2) + "\n";
  const std::string temporary =
    (path.parent_path() / (".manifest-" + randomHex().substr(0, 16))).string();
  const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) {
    throw std::system_error(errno, std::generic_category(),
                            "create temporary manifest");
  }
  try {
    std::size_t written = 0;
    while (written < payload.size()) {
      const ssize_t result =
        ::write(fd, payload.data() + written, payload.size() - written);
      if (result < 0) {
        if (errno == EINTR) {
          continue;
        }
        throw std::system_error(errno, std::generic_category(),
                                "write temporary manifest");
      }
      written += static_cast<std::size_t>(result);
    }
    if (::fsync(fd) != 0) {
      throw std::system_error(errno, std::generic_category(),
                              "fsync temporary manifest");
    }
  } catch (...) {
    ::close(fd);
    ::unlink(temporary.c_str());
    throw;
  }
  ::close(fd);
  try {
    std::filesystem::rename(temporary, path);
  } catch (...) {
    ::unlink(temporary.c_str());
    throw;
  }
  fsyncDirectory(path.parent_path());
}

nlohmann::json publicAsset(const nlohmann::json & asset) {
  nlohmann::json result = nlohmann::json::object();
  for (const char * key :
       {"id", "namespace", "kind", "model", "model_version", "mode",
        "config_digest", "frames"}) {
    if (asset.contains(key)) {
      result[key] = asset.at(key);
    }
  }
  nlohmann::json sensors = nlohmann::json::array();
  if (asset.contains("sensors") && asset.at("sensors").is_object()) {
    for (const auto & [sensor_id, sensor] : asset.at("sensors").items()) {
      nlohmann::json entry;
      entry["id"] = sensor_id;
      if (sensor.contains("kind")) {
        entry["kind"] = sensor.at("kind");
      }
      nlohmann::json streams = nlohmann::json::object();
      if (sensor.contains("streams") && sensor.at("streams").is_object()) {
        for (const auto & [key, stream] : sensor.at("streams").items()) {
          nlohmann::json reduced;
          for (const char * field : {"topic", "type", "frame_id"}) {
            if (stream.contains(field)) {
              reduced[field] = stream.at(field);
            }
          }
          streams[key] = std::move(reduced);
        }
      }
      entry["streams"] = std::move(streams);
      sensors.push_back(std::move(entry));
    }
  }
  result["sensors"] = std::move(sensors);
  if (asset.contains("description") && asset.at("description").is_object()) {
    const auto & description = asset.at("description");
    if (!description.empty()) {
      nlohmann::json reduced = nlohmann::json::object();
      for (const char * key :
           {"enabled", "format", "frame_prefix", "root", "sha256",
            "source_sha256"}) {
        if (description.contains(key)) {
          reduced[key] = description.at(key);
        }
      }
      result["description"] = std::move(reduced);
    }
  }
  return result;
}

Session::Session(const nlohmann::json & asset, const nlohmann::json & config,
                 std::int64_t wall_ns, std::int64_t monotonic_ns,
                 const std::string & unique_tag) {
  const std::string asset_id = asset.at("id").get<std::string>();
  if (!rosSafeId(asset_id)) {
    throw std::invalid_argument("session asset id must be ROS-safe");
  }
  const std::int64_t utc_ns =
    wall_ns >= 0 ? wall_ns : chaos::systemNs();
  start_steady_ns_ =
    monotonic_ns >= 0 ? monotonic_ns : chaos::monotonicNs();
  const std::string tag = unique_tag.empty() ? randomHex() : unique_tag;
  if (!safeUniqueTag(tag)) {
    throw std::invalid_argument("session unique tag must match [A-Za-z0-9_-]{1,64}");
  }
  path_ = std::filesystem::path(config.at("output_dir").get<std::string>()) /
          asset_id / (utcStamp(utc_ns) + "_" + tag);
  std::error_code error;
  std::filesystem::create_directories(path_.parent_path(), error);
  if (error) {
    throw std::system_error(
      error, "create session parent directory " + path_.parent_path().string());
  }
  {
    // mkdir mode 0700, must not overwrite an existing session directory.
    const int rc = ::mkdir(path_.c_str(), 0700);
    if (rc != 0) {
      throw std::system_error(errno, std::generic_category(),
                              "create session directory " + path_.string());
    }
  }
  std::filesystem::create_directory(path_ / "segments", error);
  if (error) {
    std::filesystem::remove_all(path_);
    throw std::system_error(error, "create segments directory");
  }
  manifest_path_ = path_ / "manifest.json";

  manifest_ = {
    {"schema_version", 1},
    {"session_id", path_.filename().string()},
    {"status", "running"},
    {"started_utc_ns", utc_ns},
    {"started_monotonic_ns", start_steady_ns_},
    {"asset", publicAsset(asset)},
    {"recording", nlohmann::json::object()},
    {"timestamps",
     {{"bag_receive_time",
       "native rosbag RMW received timestamp (system clock)"},
      {"bag_publish_time",
       "MCAP stores RMW source timestamp; may be zero or unavailable"},
      {"message_header", "original publisher stamp, never rewritten"},
      {"publisher_header_clock_domain",
       "unverified; depends on the sensor driver"},
      {"collector_clock_domain", "system"},
      {"monitor_intervals", "monotonic clock"},
      {"clock_evidence", "time_sync.jsonl"}}},
    {"segments", nlohmann::json::array()},
  };
  for (const char * key :
       {"storage_id", "topics", "max_bagfile_size_mb", "max_bagfile_duration_s",
        "max_cache_size_mb", "min_free_space_mb", "time_sync"}) {
    if (config.contains(key)) {
      manifest_["recording"][key] = config.at(key);
    }
  }

  try {
    writeManifest();
    journal_fd_ =
      ::open((path_ / "time_sync.jsonl").c_str(), O_WRONLY | O_CREAT | O_EXCL,
             0600);
    if (journal_fd_ < 0) {
      throw std::system_error(errno, std::generic_category(),
                              "create clock journal");
    }
  } catch (...) {
    if (journal_fd_ >= 0) {
      ::close(journal_fd_);
      journal_fd_ = -1;
    }
    throw;
  }
}

Session::~Session() noexcept {
  // Never finalize here: an unfinished session must keep its `running`
  // manifest as interruption evidence.
  if (journal_fd_ >= 0) {
    ::close(journal_fd_);
    journal_fd_ = -1;
  }
}

void Session::writeManifest() { atomicJsonReplace(manifest_path_, manifest_); }

void Session::writeJournal(std::string_view line) {
  std::size_t written = 0;
  while (written < line.size()) {
    const ssize_t result =
      ::write(journal_fd_, line.data() + written, line.size() - written);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::system_error(errno, std::generic_category(),
                              "write clock journal");
    }
    written += static_cast<std::size_t>(result);
  }
}

std::uintmax_t Session::freeBytes() const {
  return std::filesystem::space(path_).free;
}

void Session::append(const nlohmann::json & event) {
  if (finalized_) {
    throw std::runtime_error("session already finalized");
  }
  writeJournal(event.dump());
  writeJournal("\n");
  if (::fsync(journal_fd_) != 0) {
    throw std::system_error(errno, std::generic_category(),
                            "fsync clock journal");
  }
}

std::filesystem::path Session::beginSegment(const nlohmann::json & clock,
                                            std::int64_t wall_ns,
                                            std::int64_t monotonic_ns) {
  if (finalized_) {
    throw std::runtime_error("session already finalized");
  }
  for (const auto & row : manifest_.at("segments")) {
    if (row.at("status") == "recording") {
      throw std::runtime_error("previous segment is still recording");
    }
  }
  const int index = static_cast<int>(manifest_.at("segments").size()) + 1;
  const std::string relative =
    "segments/bag_" + std::string(4 - std::to_string(index).size(), '0') +
    std::to_string(index);
  manifest_.at("segments").push_back(nlohmann::json{
    {"index", index},
    {"relative_path", relative},
    {"status", "recording"},
    {"started_utc_ns",
     wall_ns >= 0 ? wall_ns : chaos::systemNs()},
    {"started_monotonic_ns",
     monotonic_ns >= 0 ? monotonic_ns : chaos::monotonicNs()},
    {"clock_at_start", clock},
  });
  writeManifest();
  return path_ / relative;
}

bool Session::endSegment(const std::string & reason, bool failed,
                         const nlohmann::json & suspect_interval,
                         std::int64_t wall_ns, std::int64_t monotonic_ns) {
  if (manifest_.at("segments").empty()) {
    throw std::runtime_error("there is no active segment");
  }
  nlohmann::json & row = manifest_.at("segments").back();
  if (row.at("status") != "recording") {
    throw std::runtime_error("there is no active segment");
  }
  std::string final_reason = reason;
  if (!std::filesystem::is_regular_file(path_ / row.at("relative_path").get<std::string>() /
                                        "metadata.yaml")) {
    failed = true;
    final_reason += "; bag metadata missing after recorder shutdown";
  }
  row["status"] = failed ? "failed" : "closed";
  row["reason"] = final_reason;
  row["finished_utc_ns"] =
    wall_ns >= 0 ? wall_ns : chaos::systemNs();
  row["finished_monotonic_ns"] =
    monotonic_ns >= 0 ? monotonic_ns : chaos::monotonicNs();
  row["suspect_interval"] = suspect_interval;
  writeManifest();
  return !failed;
}

void Session::recordStartupCheck(int index, const nlohmann::json & evidence) {
  manifest_.at("segments").at(static_cast<std::size_t>(index - 1))["startup_clock_check"] =
    evidence;
  writeManifest();
}

void Session::finish(const std::string & status, const std::string & reason,
                     const nlohmann::json & summary, std::int64_t wall_ns,
                     std::int64_t monotonic_ns) {
  if (finalized_) {
    return;
  }
  if (status != "completed" && status != "failed") {
    throw std::invalid_argument(
      "final status must be completed or failed");
  }
  std::string final_status = status;
  std::string final_reason = reason;
  if (status == "completed") {
    for (const auto & row : manifest_.at("segments")) {
      if (row.at("status") != "closed") {
        final_status = "failed";
        final_reason = "one or more recording segments did not close cleanly";
        break;
      }
    }
  }
  const std::int64_t now =
    monotonic_ns >= 0 ? monotonic_ns : chaos::monotonicNs();
  manifest_["status"] = final_status;
  manifest_["reason"] = final_reason;
  manifest_["finished_utc_ns"] =
    wall_ns >= 0 ? wall_ns : chaos::systemNs();
  manifest_["duration_monotonic_s"] =
    static_cast<double>(std::max<std::int64_t>(0, now - start_steady_ns_)) / 1e9;
  manifest_["summary"] = summary.is_null() ? nlohmann::json::object() : summary;
  try {
    if (::fsync(journal_fd_) != 0) {
      throw std::system_error(errno, std::generic_category(),
                              "fsync clock journal");
    }
    writeManifest();
  } catch (...) {
    ::close(journal_fd_);
    journal_fd_ = -1;
    finalized_ = true;
    throw;
  }
  ::close(journal_fd_);
  journal_fd_ = -1;
  finalized_ = true;
}

}  // namespace chaos::data
