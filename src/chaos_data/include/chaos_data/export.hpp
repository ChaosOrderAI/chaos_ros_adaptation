// Native, read only export of explicitly labelled robot episodes.
//
// The exported format is the Chaos portable intermediate format, not a native
// LeRobot dataset. Two causal rules define it:
//
//  * an observation is used at a grid step only when its source stamp and
//    its receive time are both at or before that step, so no future
//    information leaks into the past;
//  * a control action is an offline label that becomes effective at the
//    source time of the event, even when the event was received late, so a
//    late delivered stop still ends the action at the moment it was issued.
//
// Nothing is interpolated, resized, normalised or re-timestamped. The
// exported images are the original pixels with their original encoding, byte
// order and units.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>
#include <nlohmann/json.hpp>

namespace chaos::data {

// The maximum number of grid steps of one episode.
inline constexpr std::int64_t kMaxEpisodeSteps = 1000000;
// The maximum number of observations that may wait for their grid step.
inline constexpr std::size_t kMaxPendingPerFeature = 128;
// The maximum number of image bytes that may be retained at the same time.
inline constexpr std::uint64_t kMaxRetainedImageBytes = 128ULL * 1024ULL * 1024ULL;
// The maximum size of one selected ROS message.
inline constexpr std::size_t kMaxMessageBytes = 64ULL * 1024ULL * 1024ULL;
// The maximum size of the export profile.
inline constexpr std::size_t kMaxProfileBytes = 1024ULL * 1024ULL;

// One observation input of the profile.
struct FeatureSpec {
  std::string name;
  // "vector" for a list of numeric fields, "image" for a lossless image.
  std::string kind;
  std::string topic;
  std::string type;
  // The field that carries the source stamp of the observation.
  std::string stamp_field;
  // The field that carries the frame of the observation.
  std::string frame_field;
  std::string frame;
  // Only an explicitly established system UTC clock is supported.
  std::string clock_domain;
  // Fields that have to be true for the observation to be usable.
  std::vector<std::string> validity_fields;
  // The age after which an observation is too old to be used.
  double max_age_ms = 0.0;
  std::int64_t max_age_ns = 0;
  // The selected numeric fields and their units (vector features).
  std::vector<std::string> fields;
  std::vector<std::string> units;
  // The unit of the image values (image features).
  std::string image_units;
};

// The action labels of the profile: the ControlTrace topic and the selected
// velocity components.
struct ActionSpec {
  std::string topic;
  std::string type = "chaos_interfaces/msg/ControlTrace";
  std::vector<std::string> fields;
  std::vector<std::string> units;
  std::string frame;
  std::string space;
  std::string clock_domain;
  double max_age_ms = 0.0;
  std::int64_t max_age_ns = 0;
};

// One labelled task interval of the profile.
struct EpisodeSpec {
  std::string id;
  // The absolute path of the source session directory.
  std::filesystem::path session;
  int segment = 1;
  std::int64_t start_utc_ns = 0;
  std::int64_t end_utc_ns = 0;
  std::string task;
  std::string outcome;
};

// The validated export profile.
//
// The frame rate is kept as an exact rational number, so the grid of a step
// is computed with integer arithmetic: a double would accumulate a rounding
// error over a long episode.
struct Profile {
  int schema_version = 1;
  double fps = 0.0;
  // The frame rate as an exact rational number of unbounded integers: the
  // grid of a step is computed with them, so the frame rate is exact and a
  // value like 1e-300 does not overflow.
  boost::multiprecision::cpp_int fps_numerator = 1;
  boost::multiprecision::cpp_int fps_denominator = 1;
  bool allow_unverified_time = false;
  // The features keep the order of the profile document.
  std::vector<FeatureSpec> features;
  ActionSpec action;
  std::vector<EpisodeSpec> episodes;
  // The profile as it was written, with the resolved session paths.
  nlohmann::ordered_json raw;
  // The SHA-256 of the profile file.
  std::string digest;
  std::filesystem::path path;
};

// Loads and validates the profile. Every violation is an exception with a
// message that names the offending field.
Profile loadProfile(const std::string & profile_path);

// The result of one exported episode.
struct EpisodeReport {
  std::string id;
  std::int64_t rows = 0;
  std::int64_t valid_rows = 0;
  std::string path;
};

// The result of the whole export.
struct ExportReport {
  std::filesystem::path output;
  bool synthetic = false;
  bool time_verified = false;
  std::vector<EpisodeReport> episodes;
  nlohmann::ordered_json document;
};

// Exports every episode of the profile into a new output directory.
//
// The export is published atomically: the output directory only appears when
// every check has passed. An existing output is never replaced.
ExportReport exportDataset(const std::string & profile_path,
                            const std::string & output);

}  // namespace chaos::data
