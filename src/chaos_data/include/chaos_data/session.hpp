// Durable collection session metadata without driver credentials.
//
// A manifest left `running` indicates interruption without clean finalization.
// A `completed` manifest means shutdown flushed rosbag metadata; it is not a
// claim that clock quality or every selected topic was continuously available.
#pragma once

#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

namespace chaos::data {

// Writes a JSON document with crash durability: temp file + fsync + atomic
// rename + directory fsync. The rename is atomic for readers; a failure
// after the rename (e.g. directory fsync) cannot roll the rename back, so
// callers must still surface the I/O error instead of claiming success.
// Throws std::system_error on I/O failure.
void atomicJsonReplace(const std::filesystem::path & path,
                       const nlohmann::json & data);

// Extracts the deliberately small public asset allowlist, never the full
// launch/driver configuration.
nlohmann::json publicAsset(const nlohmann::json & asset);

// One non-overwriting run containing independently readable clock segments.
class Session {
public:
  // wall_ns/monotonic_ns are injectable for tests; unique_tag overrides the
  // random session suffix (collision tests) and is restricted to
  // [A-Za-z0-9_-]{1,64} so it can never escape the session parent directory.
  // Throws std::invalid_argument on an unsafe asset id or tag and
  // std::system_error when the session directory already exists.
  Session(const nlohmann::json & asset, const nlohmann::json & config,
          std::int64_t wall_ns = -1, std::int64_t monotonic_ns = -1,
          const std::string & unique_tag = "");

  // Releasing the journal fd never finalizes metadata: an unfinished session
  // stays `running` in its manifest, which is exactly the interruption
  // evidence readers rely on.
  ~Session() noexcept;

  Session(const Session &) = delete;
  Session & operator=(const Session &) = delete;

  const std::filesystem::path & path() const { return path_; }
  const nlohmann::json & manifest() const { return manifest_; }
  std::string sessionId() const { return manifest_.at("session_id"); }

  std::uintmax_t freeBytes() const;

  // Appends one clock/event line, flushing and fsyncing immediately.
  void append(const nlohmann::json & event);

  // Registers a new recording segment. Returns the bag directory path, which
  // does not exist yet; the native recorder creates its own storage.
  std::filesystem::path beginSegment(const nlohmann::json & clock,
                                     std::int64_t wall_ns = -1,
                                     std::int64_t monotonic_ns = -1);

  // Marks the active segment closed, or failed when metadata is missing or
  // failed is set. Returns false when the segment did not finalize cleanly.
  bool endSegment(const std::string & reason, bool failed = false,
                  const nlohmann::json & suspect_interval = nullptr,
                  std::int64_t wall_ns = -1,
                  std::int64_t monotonic_ns = -1);

  void recordStartupCheck(int index, const nlohmann::json & evidence);

  // Finalizes the manifest and clock journal. `completed` is downgraded to
  // `failed` when any segment did not close cleanly. No-op once finalized.
  void finish(const std::string & status, const std::string & reason,
              const nlohmann::json & summary = nullptr,
              std::int64_t wall_ns = -1, std::int64_t monotonic_ns = -1);

private:
  void writeManifest();
  void writeJournal(std::string_view line);

  std::filesystem::path path_;
  std::filesystem::path manifest_path_;
  int journal_fd_ = -1;
  nlohmann::json manifest_;
  std::int64_t start_steady_ns_ = 0;
  bool finalized_ = false;
};

}  // namespace chaos::data
