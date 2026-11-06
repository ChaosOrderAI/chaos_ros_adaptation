// The source provenance of an episode.
//
// The module proves that the session directory is the one the profile names:
// the manifest is complete, the clock evidence belongs to the same session
// and the same clock epoch, the bag metadata is readable with the shared
// YAML engine, and every file that is read is a regular file inside the
// session. The digests of those files are kept, so a source that changes
// while it is read is detected before the dataset is published.
#include "export_source.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include <chaos_assets/yaml.hpp>

#include "chaos_data/file_io.hpp"
#include "export_hash.hpp"

namespace chaos::data {

using internal::digestOf;

namespace {

// The clock evidence of one moment: it has to name the same epoch as the
// segment and it has to report a synchronized clock with a finite
// uncertainty.
void verifyClock(const nlohmann::json & clock, int epoch)
{
  if (!clock.is_object() || clock.value("epoch", -1) != epoch ||
      clock.value("allowed", false) != true) {
    throw std::runtime_error(
      "the clock evidence must be synchronized in one epoch");
  }
  const nlohmann::json sample =
    clock.contains("time_sample") ? clock["time_sample"] : nlohmann::json();
  if (!sample.is_object() || sample.value("status", std::string()) != "synced") {
    throw std::runtime_error(
      "the clock evidence must be synchronized in one epoch");
  }
  if (!sample.contains("uncertainty_s") || !sample["uncertainty_s"].is_number()) {
    throw std::runtime_error("the clock evidence requires a finite uncertainty");
  }
  const double uncertainty = sample["uncertainty_s"].get<double>();
  if (!std::isfinite(uncertainty) || uncertainty < 0.0) {
    throw std::runtime_error("the clock evidence requires a finite uncertainty");
  }
}

}  // namespace

// A regular file inside a directory, without a symbolic link in the way.
std::filesystem::path containedFile(const std::filesystem::path & path,
                                    const std::filesystem::path & root)
{
  std::error_code error;
  const std::filesystem::path real =
    std::filesystem::weakly_canonical(path, error);
  if (error || !std::filesystem::is_regular_file(real, error)) {
    throw std::runtime_error(
      "the source file must be a regular file inside its session: " +
      path.string());
  }
  if (!pathIsInside(root, real)) {
    throw std::runtime_error(
      "the source file must be inside its session: " + path.string());
  }
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(path, error))) {
    throw std::runtime_error(
      "the source file must not be a symbolic link: " + path.string());
  }
  return real;
}

// Validates the manifest, the clock journal and the bag metadata of one
// episode and collects the files that have to stay unchanged.
SourceEpisode validateSource(const EpisodeSpec & episode, const Profile & profile)
{
  SourceEpisode source;
  source.episode = &episode;
  const std::filesystem::path root = episode.session;

  const std::filesystem::path manifest_path =
    containedFile(root / "manifest.json", root);
  std::ifstream manifest_stream(manifest_path);
  if (!manifest_stream) {
    throw std::runtime_error("cannot read the session manifest");
  }
  try {
    source.manifest = nlohmann::json::parse(manifest_stream);
  } catch (const std::exception & error) {
    throw std::runtime_error(
      std::string("cannot parse the session manifest: ") + error.what());
  }
  if (source.manifest.value("schema_version", 0) != 1 ||
      source.manifest.value("status", std::string()) != "completed") {
    throw std::runtime_error("the source session must be schema 1 and completed");
  }
  const nlohmann::json asset =
    source.manifest.contains("asset") ? source.manifest["asset"] :
    nlohmann::json::object();
  source.synthetic = asset.value("mode", std::string()) == "mock";
  if (profile.allow_unverified_time && !source.synthetic) {
    throw std::runtime_error(
      "allow_unverified_time is restricted to mock assets");
  }
  const bool bypass = profile.allow_unverified_time && source.synthetic;

  const nlohmann::json segments =
    source.manifest.contains("segments") ? source.manifest["segments"] :
    nlohmann::json::array();
  const nlohmann::json * match = nullptr;
  int matches = 0;
  for (const auto & segment : segments) {
    if (segment.contains("index") && segment["index"].get<int>() == episode.segment) {
      match = &segment;
      ++matches;
    }
  }
  if (matches != 1 || match == nullptr ||
      match->value("status", std::string()) != "closed") {
    throw std::runtime_error(
      "the episode requires exactly one closed recording segment");
  }
  source.segment = *match;
  if (source.segment.contains("suspect_interval") &&
      !source.segment["suspect_interval"].is_null()) {
    throw std::runtime_error(
      "segments with a suspect_interval cannot be training episodes");
  }
  const std::int64_t started = source.segment.value(
    "started_utc_ns", episode.end_utc_ns);
  const std::int64_t finished = source.segment.value(
    "finished_utc_ns", episode.start_utc_ns);
  if (!(started <= episode.start_utc_ns && episode.end_utc_ns <= finished)) {
    throw std::runtime_error(
      "the episode interval must be contained in one clock segment");
  }
  const nlohmann::json clock = source.segment.contains("clock_at_start") ?
    source.segment["clock_at_start"] : nlohmann::json::object();
  if (!clock.contains("epoch") || !clock["epoch"].is_number_integer()) {
    throw std::runtime_error("the clock epoch must be an integer");
  }
  source.epoch = clock["epoch"].get<int>();
  if (source.epoch < 0) {
    throw std::runtime_error("the clock epoch must not be negative");
  }
  if (!bypass) {
    const nlohmann::json recording = source.manifest.contains("recording") ?
      source.manifest["recording"] : nlohmann::json::object();
    const nlohmann::json time_sync = recording.contains("time_sync") ?
      recording["time_sync"] : nlohmann::json::object();
    if (time_sync.value("require_sync", false) != true) {
      throw std::runtime_error(
        "a strict export requires recording.time_sync.require_sync=true");
    }
    verifyClock(clock, source.epoch);
    const nlohmann::json startup = source.segment.contains("startup_clock_check") ?
      source.segment["startup_clock_check"] : nlohmann::json::object();
    if (!startup.contains("clock_after_start") ||
        startup["clock_after_start"].is_null()) {
      throw std::runtime_error(
        "a strict export requires startup clock evidence");
    }
    verifyClock(startup["clock_after_start"], source.epoch);
  }

  const std::string relative = source.segment.value("relative_path", std::string());
  if (relative.empty() || std::filesystem::path(relative).is_absolute()) {
    throw std::runtime_error("the segment must use a relative bag path");
  }
  std::error_code error;
  const std::filesystem::path bag =
    std::filesystem::weakly_canonical(root / relative, error);
  if (error || !std::filesystem::is_directory(bag, error) ||
      !pathIsInside(root, bag)) {
    throw std::runtime_error("the bag must be inside its source session");
  }
  source.bag = bag;

  const std::filesystem::path metadata_path =
    containedFile(bag / "metadata.yaml", root);
  std::ifstream metadata_stream(metadata_path);
  if (!metadata_stream) {
    throw std::runtime_error("cannot read the bag metadata");
  }
  nlohmann::json metadata;
  try {
    // The shared engine resolves the scalar types with its own strict
    // resolver and refuses a duplicate mapping key, so the metadata is not
    // parsed a second time here.
    const chaos::assets::Json parsed =
      chaos::assets::load_yaml_file(metadata_path.string());
    metadata = nlohmann::json(parsed);
  } catch (const std::exception & error) {
    throw std::runtime_error(
      std::string("cannot parse the bag metadata: ") + error.what());
  }
  const nlohmann::json bag_info =
    metadata.contains("rosbag2_bagfile_information") ?
    metadata["rosbag2_bagfile_information"] : nlohmann::json::object();
  source.storage_id = bag_info.value("storage_identifier", std::string());
  if (source.storage_id != "mcap" && source.storage_id != "sqlite3") {
    throw std::runtime_error(
      "only MCAP and SQLite rosbag storage are supported");
  }

  std::vector<std::filesystem::path> files = {
    manifest_path, containedFile(root / "time_sync.jsonl", root), metadata_path};
  for (const auto & name : bag_info.value("relative_file_paths",
      std::vector<std::string>())) {
    if (name.empty() || std::filesystem::path(name).is_absolute()) {
      throw std::runtime_error(
        "the bag metadata contains an invalid relative file path");
    }
    files.push_back(containedFile(bag / name, bag));
  }

  // The clock journal has to belong to this session and has to end with the
  // session end event.
  std::ifstream journal(root / "time_sync.jsonl");
  if (!journal) {
    throw std::runtime_error("cannot read the clock journal");
  }
  bool finalized = false;
  int evidence = 0;
  std::string line;
  while (std::getline(journal, line)) {
    if (line.empty()) {
      continue;
    }
    nlohmann::json event;
    try {
      event = nlohmann::json::parse(line);
    } catch (const std::exception & error) {
      throw std::runtime_error(
        std::string("cannot parse the clock journal: ") + error.what());
    }
    const std::string kind = event.value("event", std::string());
    if (kind != "clock_sample" && kind != "session_end") {
      continue;
    }
    if (event.value("session_id", std::string()) !=
          source.manifest.value("session_id", std::string()) ||
        event.value("asset_id", std::string()) != asset.value("id", std::string())) {
      throw std::runtime_error(
        "the clock journal belongs to a different session or asset");
    }
    if (kind == "session_end") {
      finalized = true;
    }
    if (kind == "clock_sample" &&
        event.value("segment_count", -1) == episode.segment &&
        event.value("state", std::string()) == "recording") {
      if (!bypass) {
        verifyClock(event.contains("clock") ? event["clock"] :
                     nlohmann::json::object(), source.epoch);
      }
      ++evidence;
    }
  }
  // The end of the journal is the only reason to stop. A read error has to be
  // reported: a journal that was read only half way would otherwise pass as a
  // complete one whenever the session end event happened to come first.
  if (journal.bad() || (journal.fail() && !journal.eof())) {
    throw std::runtime_error("cannot read the clock journal: an I/O error occurred");
  }
  if (!finalized) {
    throw std::runtime_error(
      "the clock journal is incomplete: missing session_end");
  }
  if (!bypass && evidence == 0) {
    throw std::runtime_error(
      "a strict export requires recorded clock samples for the selected segment");
  }
  source.time_verified = !bypass;

  std::set<std::filesystem::path> seen;
  for (const auto & file : files) {
    if (!seen.insert(file).second) {
      continue;
    }
    SourceFile snapshot;
    snapshot.path = file;
    snapshot.relative = std::filesystem::relative(file, root).string();
    std::error_code file_error;
    snapshot.size = std::filesystem::file_size(file, file_error);
    snapshot.mtime_ns = std::filesystem::last_write_time(file, file_error)
      .time_since_epoch().count();
    snapshot.sha256 = digestOf(file);
    source.files.push_back(snapshot);
  }
  return source;
}

}  // namespace chaos::data
