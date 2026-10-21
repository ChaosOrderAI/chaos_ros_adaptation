// The source provenance of an episode.
//
// The export may only read a session that is proven to be the one the
// profile names: the manifest, the clock journal and the metadata of the
// bag are validated, and the files that are read are hashed so a change
// during the export is detected.
//
// This is an internal module of the export library: the header is not
// installed, so the public interface of the package stays small.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "chaos_data/export.hpp"

namespace chaos::data {

struct SourceFile {
  std::filesystem::path path;
  std::string relative;
  std::uint64_t size = 0;
  std::int64_t mtime_ns = 0;
  std::string sha256;
};

struct SourceEpisode {
  const EpisodeSpec * episode = nullptr;
  nlohmann::json manifest;
  nlohmann::json segment;
  std::filesystem::path bag;
  std::string storage_id;
  int epoch = 0;
  bool synthetic = false;
  bool time_verified = false;
  std::vector<SourceFile> files;
};

// A regular file inside a directory, without a symbolic link in the way.
std::filesystem::path containedFile(const std::filesystem::path & path,
                                    const std::filesystem::path & root);

// Validates the manifest, the clock journal and the bag metadata of one
// episode and collects the files that have to stay unchanged.
SourceEpisode validateSource(const EpisodeSpec & episode, const Profile & profile);

}  // namespace chaos::data
