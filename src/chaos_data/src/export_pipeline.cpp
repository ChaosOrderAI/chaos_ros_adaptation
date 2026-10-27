// The export pipeline: the causal resampler and the atomic publication.
//
// The source provenance, the typed bag reader and the control label stream
// are internal modules of their own, so the grid alignment stays readable.
//
// Every step of the pipeline is read only with respect to the source: the
// bag, the manifest and the clock journal are never written. The export is
// built in a private staging directory and published with a single atomic
// rename that never replaces an existing output.
#include "chaos_data/export.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <vector>

#include <fcntl.h>
#include <linux/fs.h>
#include <unistd.h>
#include <cerrno>

#include <boost/multiprecision/cpp_int.hpp>

#include "chaos_core/time.hpp"
#include "chaos_data/file_io.hpp"
#include "chaos_data/npy.hpp"
#include "export_hash.hpp"
#include "export_records.hpp"
#include "export_source.hpp"

namespace chaos::data {
namespace {

using boost::multiprecision::cpp_int;
using internal::digestOf;

constexpr std::int64_t kNanoseconds = 1000000000LL;

// ---------------------------------------------------------------------------
// The atomic publication
// ---------------------------------------------------------------------------

// Publishes the staging directory as the output directory.
//
// The rename never replaces an existing output, so a directory that was
// created in the meantime is not overwritten.
void publishDirectory(const std::filesystem::path & staging,
                      const std::filesystem::path & output)
{
#ifdef __linux__
  // RENAME_NOREPLACE is 1.
  if (::renameat2(AT_FDCWD, staging.c_str(), AT_FDCWD, output.c_str(),
                  RENAME_NOREPLACE) != 0) {
    const int error = errno;
    throw std::runtime_error(
      "cannot publish the dataset: " + std::string(std::strerror(error)) +
      " (" + output.string() + ")");
  }
#else
  throw std::runtime_error(
    "the atomic publication without replacement is only supported on Linux");
#endif
}

// Removes a directory tree that this process created.
void removeTree(const std::filesystem::path & path)
{
  std::error_code error;
  std::filesystem::remove_all(path, error);
}

// ---------------------------------------------------------------------------
// The causal resampler
// ---------------------------------------------------------------------------

// One observation of a feature at one grid step.
struct Sample {
  std::int64_t source_ns = 0;
  std::int64_t receive_ns = 0;
  std::int64_t message_index = 0;
  // The reason why the observation is not usable, empty when it is.
  std::string reason;
  std::vector<double> values;
  std::shared_ptr<const sensor_msgs::msg::Image> image;
  std::size_t image_bytes = 0;
  // The file of the image, written once: the same observation is used by
  // several grid steps and the pixels are never written twice.
  std::string image_path;
};

// The state of one feature while the grid is walked.
struct FeatureState {
  const FeatureSpec * spec = nullptr;
  bool has_latest = false;
  Sample latest;
  // The observations that wait for their grid step, ordered by source time.
  std::map<std::pair<std::int64_t, std::int64_t>, Sample> pending;
  std::int64_t invalid_stamps = 0;
  bool has_previous_stamp = false;
  std::int64_t previous_stamp = 0;
};

// The number of image bytes that are retained at the same time.
std::uint64_t retainedBytes(const std::vector<FeatureState> & states)
{
  std::uint64_t total = 0;
  for (const auto & state : states) {
    if (state.has_latest) {
      total += state.latest.image_bytes;
    }
    for (const auto & item : state.pending) {
      total += item.second.image_bytes;
    }
  }
  return total;
}

// Reads the source stamp of a message. A stamp that is not valid makes the
// observation unusable, because a state without a time cannot refresh a
// previous observation.
bool sampleStamp(const DecodedMessage & message, const FeatureSpec & feature,
                 std::int64_t & stamp)
{
  FieldValue value;
  if (!message.read(feature.stamp_field, value) ||
      value.kind != FieldValue::Kind::kTimestamp) {
    return false;
  }
  std::int64_t result = 0;
  // Zero is a valid ROS time, but a sample without a source time cannot refresh
  // an observation, so it is rejected here rather than inside rosStampToNs.
  if (!chaos::rosStampToNs(value.seconds, static_cast<std::int64_t>(value.nanoseconds),
                           result) ||
      result == 0) {
    return false;
  }
  stamp = result;
  return true;
}

// Exports one episode: the control journal, the steps and the images.
nlohmann::ordered_json exportEpisode(const SourceEpisode & source,
                                      const Profile & profile,
                                      const std::filesystem::path & episodes_dir)
{
  const EpisodeSpec & episode = *source.episode;
  std::error_code error;
  const std::filesystem::path episode_dir = episodes_dir / episode.id;
  if (!std::filesystem::create_directory(episode_dir, error) || error) {
    throw std::runtime_error("cannot create the episode directory");
  }

  const ControlJournalSummary summary =
    readControlEvents(source, profile.action, episode_dir / "control_events.jsonl");
  const std::int64_t last_event_ns = summary.last_event_ns;
  // The journal is read with a cursor, so the memory of the export does not
  // grow with the number of control events of the episode.
  ControlJournalCursor control_cursor(episode_dir / "control_events.jsonl");

  std::map<std::string, std::string> expected;
  for (const auto & feature : profile.features) {
    expected[feature.topic] = feature.type;
  }  BagReader reader(source.bag, source.storage_id, expected);

  std::vector<FeatureState> states;
  for (const auto & feature : profile.features) {
    FeatureState state;
    state.spec = &feature;
    states.push_back(state);
  }
  std::map<std::string, std::size_t> index_of;
  for (std::size_t index = 0; index < states.size(); ++index) {
    index_of[states[index].spec->name] = index;
  }

  // The grid step: the exact rational frame rate keeps the step free of a
  // rounding error. The integers are unbounded, so a frame rate like 1e-300
  // produces a step that does not fit into 64 bits and is still exact.
  const cpp_int step_numerator =
    cpp_int(kNanoseconds) * profile.fps_denominator;
  const cpp_int step_denominator = profile.fps_numerator;

  std::int64_t row_count = 0;
  std::int64_t valid_count = 0;
  std::map<std::string, std::int64_t> invalid_reasons;
  const ControlEvent * active = nullptr;
  BagRecord record;
  bool has_record = reader.next(record);

  ExclusiveFile steps(episode_dir / "steps.jsonl");
  while (true) {
    const cpp_int grid_value =
      cpp_int(episode.start_utc_ns) +
      (cpp_int(row_count) * step_numerator) / step_denominator;
    if (grid_value >= cpp_int(episode.end_utc_ns)) {
      // The step left the episode: the grid of the next step is beyond the
      // end of the episode, and it is computed without an overflow.
      break;
    }
    const std::int64_t grid = grid_value.convert_to<std::int64_t>();

    // The control labels become effective at their own source time.
    control_cursor.apply(grid, active);

    // The observations that were received before this step.
    while (has_record && record.receive_ns <= grid) {
      for (auto & state : states) {
        if (state.spec->topic != record.topic) {
          continue;
        }
        Sample sample;
        sample.receive_ns = record.receive_ns;
        sample.message_index = record.sequence;
        // The frame and the validity flags decide whether the observation may
        // be used; a wrong frame or a false flag is a reason, not an error.
        FieldValue frame_value;
        if (!record.message.read(state.spec->frame_field, frame_value) ||
            frame_value.kind != FieldValue::Kind::kString ||
            frame_value.text != state.spec->frame) {
          sample.reason = "frame_mismatch";
        }
        for (const auto & path : state.spec->validity_fields) {
          FieldValue flag;
          if (!record.message.read(path, flag) ||
              flag.kind != FieldValue::Kind::kBool || !flag.boolean) {
            sample.reason = "invalid_source_flag";
          }
        }
        if (state.spec->kind == "vector") {
          bool numeric = true;
          for (const auto & path : state.spec->fields) {
            FieldValue value;
            double number = 0.0;
            if (!record.message.read(path, value)) {
              numeric = false;
              break;
            }
            switch (value.kind) {
              case FieldValue::Kind::kDouble:
                number = value.number;
                break;
              case FieldValue::Kind::kInt:
                number = static_cast<double>(value.integer);
                break;
              case FieldValue::Kind::kUint:
                number = static_cast<double>(value.uinteger);
                break;
              default:
                numeric = false;
                break;
            }
            if (!numeric || !std::isfinite(number)) {
              numeric = false;
              break;
            }
            sample.values.push_back(number);
          }
          if (!numeric) {
            sample.values.clear();
            sample.reason = "nonfinite_vector";
          }
        } else {
          const sensor_msgs::msg::Image * image = record.message.image();
          if (image == nullptr) {
            sample.reason = "nonfinite_vector";
          } else {
            // The image is kept alive by the sample: the row is written
            // after the record of the bag has moved on.
            sample.image = record.message.imagePtr();
            sample.image_bytes = image->data.size();
          }
        }
        std::int64_t stamp = 0;
        if (!sampleStamp(record.message, *state.spec, stamp)) {
          ++state.invalid_stamps;
          // A state without a time cannot refresh a previous observation.
          state.has_latest = false;
          state.latest = Sample();
          state.pending.clear();
          continue;
        }
        if (state.has_previous_stamp && stamp < state.previous_stamp) {
          throw std::runtime_error(
            "the observation source timestamps are reversed: " + state.spec->name);
        }
        state.previous_stamp = stamp;
        state.has_previous_stamp = true;
        if (stamp < episode.start_utc_ns) {
          // A sample from before the episode is never carried into it.
          continue;
        }
        sample.source_ns = stamp;
        if (stamp <= grid) {
          if (!state.has_latest ||
              std::make_tuple(sample.source_ns, sample.receive_ns,
                              sample.message_index) >=
              std::make_tuple(state.latest.source_ns, state.latest.receive_ns,
                              state.latest.message_index)) {
            state.latest = sample;
            state.has_latest = true;
          }
        } else {
          state.pending.emplace(
            std::make_pair(sample.source_ns, sample.message_index), sample);
          if (state.pending.size() > kMaxPendingPerFeature) {
            throw std::runtime_error(
              "too many future stamped observations; the clock domain may be "
              "wrong");
          }
        }
        if (retainedBytes(states) > kMaxRetainedImageBytes) {
          throw std::runtime_error(
            "the retained image buffers exceed the 128 MiB export limit");
        }
      }
      has_record = reader.next(record);
    }

    // The observations whose source time has reached the step.
    for (auto & state : states) {
      while (!state.pending.empty() &&
             state.pending.begin()->first.first <= grid) {
        const Sample sample = state.pending.begin()->second;
        state.pending.erase(state.pending.begin());
        if (!state.has_latest ||
            std::make_tuple(sample.source_ns, sample.receive_ns,
                            sample.message_index) >=
            std::make_tuple(state.latest.source_ns, state.latest.receive_ns,
                            state.latest.message_index)) {
          state.latest = sample;
          state.has_latest = true;
        }
      }
    }
    if (retainedBytes(states) > kMaxRetainedImageBytes) {
      throw std::runtime_error(
        "the retained image buffers exceed the 128 MiB export limit");
    }

    nlohmann::ordered_json features = nlohmann::ordered_json::object();
    bool features_valid = true;
    for (auto & state : states) {
      const FeatureSpec & feature = *state.spec;
      nlohmann::ordered_json row = nlohmann::ordered_json::object();
      if (!state.has_latest) {
        row["valid"] = false;
        row["reason"] = "no_causal_sample";
        features_valid = false;
        ++invalid_reasons["feature:" + feature.name + ":no_causal_sample"];
        features[feature.name] = row;
        continue;
      }
      const Sample & sample = state.latest;
      row["source_ns"] = sample.source_ns;
      row["receive_ns"] = sample.receive_ns;
      row["receive_minus_source_ns"] = sample.receive_ns - sample.source_ns;
      row["age_ns"] = grid - sample.source_ns;
      row["message_index"] = sample.message_index;
      row["frame"] = feature.frame;
      row["clock_domain"] = feature.clock_domain;
      std::string reason = sample.reason;
      if (grid - sample.source_ns > feature.max_age_ns) {
        reason = "sample_too_old";
      }
      row["valid"] = reason.empty();
      if (!reason.empty()) {
        row["reason"] = reason;
        features_valid = false;
        ++invalid_reasons["feature:" + feature.name + ":" + reason];
      } else if (feature.kind == "vector") {
        row["values"] = sample.values;
      } else {
        if (!sample.image) {
          row["valid"] = false;
          row["reason"] = "no_causal_sample";
          features_valid = false;
          ++invalid_reasons["feature:" + feature.name + ":no_causal_sample"];
          features[feature.name] = row;
          continue;
        }
        const sensor_msgs::msg::Image & image = *sample.image;
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%012lld",
                      static_cast<long long>(sample.message_index));
        if (state.latest.image_path.empty()) {
          const std::string relative =
            "images/" + feature.name + "/" + buffer + ".npy";
          const std::filesystem::path target = episode_dir / relative;
          std::filesystem::create_directories(target.parent_path(), error);
          NpyWriter::write(target, image);
          state.latest.image_path = relative;
        }
        const std::string & relative = state.latest.image_path;
        row["path"] = relative;
        row["encoding"] = image.encoding;
        row["height"] = image.height;
        row["width"] = image.width;
        row["original_step"] = image.step;
        row["is_bigendian"] = image.is_bigendian;
        row["units"] = feature.image_units;
      }
      features[feature.name] = row;
    }

    nlohmann::ordered_json action = nlohmann::ordered_json::object();
    bool action_valid = false;
    if (active == nullptr) {
      action["valid"] = false;
      action["reason"] = "no_active_issued_velocity";
      ++invalid_reasons["action:no_active_issued_velocity"];
    } else {
      action["event_index"] = active->event_index;
      action["sequence"] = active->sequence;
      action["monotonic_ns"] = active->monotonic_ns;
      action["source_ns"] = active->source_ns;
      action["receive_ns"] = active->receive_ns;
      action["receive_minus_source_ns"] =
        active->receive_ns - active->source_ns;
      action["valid_until_ns"] = active->valid_until_ns;
      action["request_id"] = active->request_id;
      std::string reason;
      if (grid >= last_event_ns) {
        reason = "action_unclosed";
      } else if (grid >= active->valid_until_ns) {
        reason = "velocity_expired";
      } else if (grid - active->source_ns > profile.action.max_age_ns) {
        reason = "action_too_old";
      }
      action["valid"] = reason.empty();
      action["frame"] = profile.action.frame;
      action["space"] = profile.action.space;
      action["clock_domain"] = profile.action.clock_domain;
      action["semantics"] = "runtime_issued";
      if (!reason.empty()) {
        action["reason"] = reason;
        ++invalid_reasons["action:" + reason];
      } else {
        action["values"] = active->values;
      }
      action_valid = reason.empty();
    }

    const bool valid = action_valid && features_valid;
    nlohmann::ordered_json row;
    row["grid_index"] = row_count;
    row["timestamp_ns"] = grid;
    row["elapsed_ns"] = grid - episode.start_utc_ns;
    row["clock_epoch"] = source.epoch;
    row["valid"] = valid;
    row["features"] = features;
    row["action"] = action;
    steps.write(row.dump() + "\n");
    ++row_count;
    if (valid) {
      ++valid_count;
    }
  }
  steps.close();

  if (valid_count == 0) {
    throw std::runtime_error(
      "the episode " + episode.id + " has no fully valid training rows");
  }

  nlohmann::ordered_json report;
  report["id"] = episode.id;
  report["session"] = episode.session.string();
  report["segment"] = episode.segment;
  report["start_utc_ns"] = episode.start_utc_ns;
  report["end_utc_ns"] = episode.end_utc_ns;
  report["task"] = episode.task;
  report["outcome"] = episode.outcome;
  report["clock_epoch"] = source.epoch;
  report["synthetic"] = source.synthetic;
  report["time_verified"] = source.time_verified;
  report["rows"] = row_count;
  report["valid_rows"] = valid_count;
  report["invalid_rows"] = row_count - valid_count;
  report["control_events"] = summary.count;
  nlohmann::ordered_json reasons = nlohmann::ordered_json::object();
  for (const auto & entry : invalid_reasons) {
    reasons[entry.first] = entry.second;
  }
  report["invalid_reasons"] = reasons;
  nlohmann::ordered_json invalid_source_stamps = nlohmann::ordered_json::object();
  for (const auto & state : states) {
    invalid_source_stamps[state.spec->name] = state.invalid_stamps;
  }
  report["invalid_source_stamps"] = invalid_source_stamps;
  // The reversals are counted per feature, so an empty counter is an object
  // with one entry per feature and not a single number.
  nlohmann::ordered_json source_reversals = nlohmann::ordered_json::object();
  for (const auto & state : states) {
    source_reversals[state.spec->name] = 0;
  }
  report["observation_source_reversals"] = source_reversals;
  report["asset"] = source.manifest["asset"];
  nlohmann::ordered_json files = nlohmann::ordered_json::array();
  for (const auto & file : source.files) {
    nlohmann::ordered_json entry;
    entry["path"] = file.relative;
    entry["size_bytes"] = file.size;
    entry["sha256"] = file.sha256;
    files.push_back(entry);
  }
  report["source_files"] = files;

  {
    ExclusiveFile file(episode_dir / "episode.json");
    file.write(report.dump(2) + "\n");
    file.close();
  }
  return report;
}

}  // namespace

// ---------------------------------------------------------------------------
// The export
// ---------------------------------------------------------------------------

ExportReport exportDataset(const std::string & profile_path,
                            const std::string & output)
{
  Profile profile = loadProfile(profile_path);
  profile.digest = digestOf(profile.path);

  std::error_code error;
  const std::filesystem::path output_path =
    std::filesystem::weakly_canonical(
      std::filesystem::absolute(std::filesystem::path(output), error), error);
  if (error) {
    throw std::runtime_error("cannot resolve the output path: " + output);
  }
  if (std::filesystem::exists(output_path, error)) {
    throw std::runtime_error("the output already exists: " + output);
  }
  for (const auto & episode : profile.episodes) {
    if (output_path == episode.session ||
        pathIsInside(episode.session, output_path)) {
      throw std::runtime_error(
        "the dataset output must be outside every source session");
    }
  }
  if (!std::filesystem::is_directory(output_path.parent_path(), error)) {
    throw std::runtime_error(
      "the parent directory of the output must already exist");
  }

  std::vector<SourceEpisode> sources;
  for (const auto & episode : profile.episodes) {
    sources.push_back(validateSource(episode, profile));
  }

  // The export is built in a private staging directory next to the output.
  // The name is unique and the directory is created by this process, so a
  // directory that belongs to somebody else is never removed.
  const std::filesystem::path staging_template =
    output_path.parent_path() /
    ("." + output_path.filename().string() + ".partial-XXXXXX");
  const std::string staging_name = staging_template.string();
  std::vector<char> staging_buffer(staging_name.begin(), staging_name.end());
  staging_buffer.push_back('\0');
  if (::mkdtemp(staging_buffer.data()) == nullptr) {
    throw std::runtime_error("cannot create the staging directory");
  }
  const std::filesystem::path staging(staging_buffer.data());

  ExportReport report;
  try {
    const std::filesystem::path episodes_dir = staging / "episodes";
    if (!std::filesystem::create_directory(episodes_dir, error) || error) {
      throw std::runtime_error("cannot create the episodes directory");
    }
    std::vector<nlohmann::ordered_json> episode_documents;
    for (const auto & source : sources) {
      episode_documents.push_back(exportEpisode(source, profile, episodes_dir));
    }
    // The source must not change while it is read.
    for (const auto & source : sources) {
      for (const auto & file : source.files) {
        std::error_code file_error;
        const std::uintmax_t size = std::filesystem::file_size(file.path, file_error);
        const std::int64_t mtime_ns =
          std::filesystem::last_write_time(file.path, file_error).time_since_epoch().count();
        if (file_error || size != file.size || mtime_ns != file.mtime_ns ||
            digestOf(file.path) != file.sha256) {
          throw std::runtime_error("a source recording changed during the export");
        }
      }
    }
    if (digestOf(profile.path) != profile.digest) {
      throw std::runtime_error("the export profile changed during the export");
    }

    bool synthetic = false;
    bool verified = true;
    for (const auto & source : sources) {
      synthetic = synthetic || source.synthetic;
      verified = verified && source.time_verified;
    }
    nlohmann::ordered_json document;
    document["schema_version"] = 1;
    document["format"] = "chaos_portable_episode";
    document["status"] = "completed";
    document["fps"] = profile.fps;
    document["profile"] = profile.raw;
    document["profile_sha256"] = profile.digest;
    document["synthetic"] = synthetic;
    document["time_verified"] = verified;
    document["action_semantics"] =
      "runtime_issued; not hardware-confirmed execution";
    document["observation_alignment"] =
      "latest source<=grid and receive<=grid within episode; no interpolation";
    document["action_alignment"] =
      "offline labels effective at source header time, including late received "
      "stops";
    document["image_policy"] =
      "lossless pixel arrays; original encoding/endian/units, no resize or depth "
      "scaling";
    document["clock_claim"] =
      "host quality evidence only; sensor UTC clock domain is a profile assertion";
    nlohmann::ordered_json episodes = nlohmann::ordered_json::array();
    for (const auto & item : episode_documents) {
      nlohmann::ordered_json entry;
      entry["id"] = item["id"];
      entry["rows"] = item["rows"];
      entry["valid_rows"] = item["valid_rows"];
      entry["path"] = "episodes/" + item["id"].get<std::string>() + "/episode.json";
      episodes.push_back(entry);
    }
    document["episodes"] = episodes;

    const std::filesystem::path dataset_path = staging / "dataset.json";
    {
      ExclusiveFile file(dataset_path);
      file.write(document.dump(2) + "\n");
      file.close();
    }
    publishDirectory(staging, output_path);

    report.output = output_path;
    report.synthetic = synthetic;
    report.time_verified = verified;
    report.document = document;
    for (const auto & item : episode_documents) {
      EpisodeReport entry;
      entry.id = item["id"].get<std::string>();
      entry.rows = item["rows"].get<std::int64_t>();
      entry.valid_rows = item["valid_rows"].get<std::int64_t>();
      entry.path = "episodes/" + entry.id + "/episode.json";
      report.episodes.push_back(entry);
    }
    return report;
  } catch (...) {
    // Only the staging directory of this process is removed.
    removeTree(staging);
    throw;
  }
}

}  // namespace chaos::data
