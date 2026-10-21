// The export profile and the lossless image sink.
//
// The profile is the only description of what is exported, so every
// violation has to be refused with a message that names the field. The
// image sink has to keep the original pixels, the original byte order and
// the original encoding, and it may not expand the payload.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <sensor_msgs/msg/image.hpp>

#include "chaos_data/export.hpp"
#include "chaos_data/npy.hpp"
#include "export_records.hpp"

namespace {

namespace fs = std::filesystem;

// A directory that is removed after the test.
class ExportTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    directory_ = fs::temp_directory_path() /
      ("chaos_data_export_test_" + std::to_string(::getpid()) + "_" +
       std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    fs::create_directories(directory_);
    // The session of the profile has to exist: the loader resolves it and
    // refuses a session that is not there.
    fs::create_directories(directory_ / "session");
  }

  void TearDown() override
  {
    std::error_code error;
    fs::remove_all(directory_, error);
  }

  // Writes a profile and returns its path.
  fs::path writeProfile(const std::string & name, const std::string & content)
  {
    const fs::path path = directory_ / name;
    std::ofstream output(path);
    output << content;
    output.close();
    return path;
  }

  // A profile with the given body, so every case only names its violation.
  std::string profileWith(const std::string & body, const std::string & fps = "10")
  {
    return std::string(
      "schema_version: 1\n"
      "fps: ") + fps + "\n"
      "features:\n"
      "  observation.state:\n"
      "    kind: vector\n"
      "    topic: /pose\n"
      "    type: geometry_msgs/msg/PoseStamped\n"
      "    fields: [pose.position.x]\n"
      "    units: [m]\n"
      "    stamp_field: header.stamp\n"
      "    frame_field: header.frame_id\n"
      "    frame: odom\n"
      "    clock_domain: system_utc\n"
      "    max_age_ms: 500\n"
      "action:\n"
      "  topic: /control_trace\n"
      "  fields: [velocity.linear.x]\n"
      "  units: [m/s]\n"
      "  frame: base_link\n"
      "  space: base_velocity_2d\n"
      "  clock_domain: system_utc\n"
      "  max_age_ms: 1000\n"
      "episodes:\n"
      "  - id: pick_001\n"
      "    session: SESSION\n"
      "    segment: 1\n"
      "    start_utc_ns: 10000000000\n"
      "    end_utc_ns: 11000000000\n"
      "    task: drive to marker\n"
      "    outcome: success\n" + body;
  }
  fs::path directory_;
};

}  // namespace

TEST_F(ExportTest, AcceptsAValidProfile) {
  std::string body = profileWith("");
  // The session of the profile is the directory of the test.
  const std::string session = (directory_ / "session").string();
  body.replace(body.find("SESSION"), 7, session);
  const fs::path path = writeProfile("valid.yaml", body);
  const chaos::data::Profile profile = chaos::data::loadProfile(path.string());
  EXPECT_EQ(profile.schema_version, 1);
  EXPECT_DOUBLE_EQ(profile.fps, 10.0);
  // The frame rate is kept exactly, so the grid of a step has no rounding
  // error.
  EXPECT_EQ(profile.fps_numerator, 10);
  EXPECT_EQ(profile.fps_denominator, 1);
  ASSERT_EQ(profile.features.size(), 1u);
  EXPECT_EQ(profile.features.front().name, "observation.state");
  EXPECT_EQ(profile.features.front().max_age_ns, 500000000);
  ASSERT_EQ(profile.episodes.size(), 1u);
  EXPECT_EQ(profile.episodes.front().id, "pick_001");
  EXPECT_EQ(profile.episodes.front().session, fs::canonical(session));
}

TEST_F(ExportTest, KeepsADecimalFrameRateExact) {
  std::string body = profileWith("", "29.97");
  const std::string session = (directory_ / "session").string();
  body.replace(body.find("SESSION"), 7, session);
  const fs::path path = writeProfile("decimal.yaml", body);
  const chaos::data::Profile profile = chaos::data::loadProfile(path.string());
  EXPECT_EQ(profile.fps_numerator, 2997);
  EXPECT_EQ(profile.fps_denominator, 100);
}

TEST_F(ExportTest, KeepsTheFullPrecisionOfAFrameRate) {
  // The frame rate is written with more digits than a double can hold, so
  // the shortest representation that reads back as the same value is the
  // one the profile was written with.
  std::string body = profileWith("", "29.970000123");
  const std::string session = (directory_ / "session").string();
  body.replace(body.find("SESSION"), 7, session);
  const fs::path path = writeProfile("precise.yaml", body);
  const chaos::data::Profile profile = chaos::data::loadProfile(path.string());
  EXPECT_EQ(profile.fps_numerator, 29970000123);
  EXPECT_EQ(profile.fps_denominator, 1000000000);
}

TEST_F(ExportTest, AcceptsAVerySmallFrameRate) {
  // A frame rate of 1e-7 is a step of ten million nanoseconds, so the
  // fraction has a denominator that does not fit into 64 bits: the exact
  // arithmetic of the profile keeps it.
  std::string body = profileWith("", "1.0e-7");
  const std::string session = (directory_ / "session").string();
  body.replace(body.find("SESSION"), 7, session);
  const fs::path path = writeProfile("tiny.yaml", body);
  const chaos::data::Profile profile = chaos::data::loadProfile(path.string());
  EXPECT_EQ(profile.fps_numerator, 1);
  EXPECT_EQ(profile.fps_denominator, 10000000);
  EXPECT_GT(profile.fps, 0.0);
}

TEST_F(ExportTest, AcceptsTheLargestFrameRate) {
  std::string body = profileWith("", "240");
  const std::string session = (directory_ / "session").string();
  body.replace(body.find("SESSION"), 7, session);
  const fs::path path = writeProfile("fast.yaml", body);
  const chaos::data::Profile profile = chaos::data::loadProfile(path.string());
  EXPECT_EQ(profile.fps_numerator, 240);
  EXPECT_EQ(profile.fps_denominator, 1);
  EXPECT_DOUBLE_EQ(profile.fps, 240.0);
}

TEST_F(ExportTest, RefusesAFrameRateAboveTheLimit) {
  std::string body = profileWith("", "240.5");
  const std::string session = (directory_ / "session").string();
  body.replace(body.find("SESSION"), 7, session);
  const fs::path path = writeProfile("too_fast.yaml", body);
  EXPECT_THROW(chaos::data::loadProfile(path.string()), std::runtime_error);
}

TEST_F(ExportTest, RefusesAFrameRateWithoutAFraction) {
  // "1e-7" has no fraction, so the implicit typing reads it as a string
  // and a string is not a frame rate.
  std::string body = profileWith("", "1e-7");
  const std::string session = (directory_ / "session").string();
  body.replace(body.find("SESSION"), 7, session);
  const fs::path path = writeProfile("exponent.yaml", body);
  EXPECT_THROW(chaos::data::loadProfile(path.string()), std::runtime_error);
}

TEST_F(ExportTest, RefusesAQuotedFrameRate) {
  // A quoted value is a string, and a string is not a frame rate.
  std::string body = profileWith("", "'10'");
  const std::string session = (directory_ / "session").string();
  body.replace(body.find("SESSION"), 7, session);
  const fs::path path = writeProfile("quoted.yaml", body);
  EXPECT_THROW(chaos::data::loadProfile(path.string()), std::runtime_error);
}

TEST_F(ExportTest, RefusesAnExplicitlyTaggedFrameRate) {
  std::string body = profileWith("", "!!str 10");
  const std::string session = (directory_ / "session").string();
  body.replace(body.find("SESSION"), 7, session);
  const fs::path path = writeProfile("tagged.yaml", body);
  EXPECT_THROW(chaos::data::loadProfile(path.string()), std::runtime_error);
}

TEST_F(ExportTest, AcceptsAHexadecimalFrameRate) {
  // The implicit typing of the shared engine resolves 0x0a as the integer ten.
  std::string body = profileWith("", "0x0a");
  const std::string session = (directory_ / "session").string();
  body.replace(body.find("SESSION"), 7, session);
  const fs::path path = writeProfile("hex.yaml", body);
  const chaos::data::Profile profile = chaos::data::loadProfile(path.string());
  EXPECT_EQ(profile.fps_numerator, 10);
  EXPECT_EQ(profile.fps_denominator, 1);
}

TEST_F(ExportTest, RoundsTheAgeLimitToTheNearestEvenNanosecond) {
  // The age limit is the product of the milliseconds and a million,
  // rounded to the nearest integer with a half going to the even neighbour.
  std::string body = profileWith("");
  body.replace(body.find("max_age_ms: 500"), 15, "max_age_ms: 0.0000015");
  const std::string session = (directory_ / "session").string();
  body.replace(body.find("SESSION"), 7, session);
  const fs::path path = writeProfile("age.yaml", body);
  const chaos::data::Profile profile = chaos::data::loadProfile(path.string());
  ASSERT_EQ(profile.features.size(), 1u);
  // 1.5 nanoseconds is a tie, and the tie goes to the even neighbour.
  EXPECT_EQ(profile.features.front().max_age_ns, 2);
}

TEST_F(ExportTest, RefusesADuplicateKey) {
  const fs::path path = writeProfile(
    "duplicate.yaml",
    "schema_version: 1\nfps: 10\nfps: 20\nfeatures: {}\naction: {}\n"
    "episodes: []\n");
  EXPECT_THROW(chaos::data::loadProfile(path.string()), std::runtime_error);
}

TEST_F(ExportTest, RefusesABooleanAsANumber) {
  const fs::path path = writeProfile(
    "bool.yaml",
    "schema_version: 1\nfps: true\nfeatures: {}\naction: {}\nepisodes: []\n");
  EXPECT_THROW(chaos::data::loadProfile(path.string()), std::runtime_error);
}

TEST_F(ExportTest, RefusesAnUnknownField) {
  const fs::path path = writeProfile(
    "unknown.yaml",
    "schema_version: 1\nfps: 10\nsurprise: 1\nfeatures: {}\naction: {}\n"
    "episodes: []\n");
  EXPECT_THROW(chaos::data::loadProfile(path.string()), std::runtime_error);
}

TEST_F(ExportTest, RefusesARelativeTopic) {
  const fs::path path = writeProfile(
    "topic.yaml",
    "schema_version: 1\nfps: 10\nfeatures:\n  f:\n    kind: vector\n"
    "    topic: relative/topic\n    type: geometry_msgs/msg/PoseStamped\n"
    "    fields: [pose.position.x]\n    units: [m]\n"
    "    stamp_field: header.stamp\n    frame_field: header.frame_id\n"
    "    frame: odom\n    clock_domain: system_utc\n    max_age_ms: 500\n"
    "action:\n  topic: /control_trace\n  fields: [velocity.linear.x]\n"
    "  units: [m/s]\n  frame: base_link\n  space: base_velocity_2d\n"
    "  clock_domain: system_utc\n  max_age_ms: 1000\n"
    "episodes: []\n");
  EXPECT_THROW(chaos::data::loadProfile(path.string()), std::runtime_error);
}

TEST_F(ExportTest, RefusesAnUnsafeFieldPath) {
  const fs::path path = writeProfile(
    "field.yaml",
    "schema_version: 1\nfps: 10\nfeatures:\n  f:\n    kind: vector\n"
    "    topic: /pose\n    type: geometry_msgs/msg/PoseStamped\n"
    "    fields: ['pose.position.x; rm -rf /']\n    units: [m]\n"
    "    stamp_field: header.stamp\n    frame_field: header.frame_id\n"
    "    frame: odom\n    clock_domain: system_utc\n    max_age_ms: 500\n"
    "action:\n  topic: /control_trace\n  fields: [velocity.linear.x]\n"
    "  units: [m/s]\n  frame: base_link\n  space: base_velocity_2d\n"
    "  clock_domain: system_utc\n  max_age_ms: 1000\n"
    "episodes: []\n");
  EXPECT_THROW(chaos::data::loadProfile(path.string()), std::runtime_error);
}

TEST_F(ExportTest, RefusesAControlTraceAsAnObservation) {
  const fs::path path = writeProfile(
    "action.yaml",
    "schema_version: 1\nfps: 10\nfeatures:\n  f:\n    kind: vector\n"
    "    topic: /control_trace\n    type: chaos_interfaces/msg/ControlTrace\n"
    "    fields: [velocity.linear.x]\n    units: [m/s]\n"
    "    stamp_field: header.stamp\n    frame_field: header.frame_id\n"
    "    frame: base_link\n    clock_domain: system_utc\n    max_age_ms: 500\n"
    "action:\n  topic: /control_trace\n  fields: [velocity.linear.x]\n"
    "  units: [m/s]\n  frame: base_link\n  space: base_velocity_2d\n"
    "  clock_domain: system_utc\n  max_age_ms: 1000\n"
    "episodes: []\n");
  EXPECT_THROW(chaos::data::loadProfile(path.string()), std::runtime_error);
}

TEST_F(ExportTest, RefusesAMissingProfile) {
  EXPECT_THROW(
    chaos::data::loadProfile((directory_ / "absent.yaml").string()),
    std::runtime_error);
}

TEST(ExportNpy, WritesTheOriginalPixelsWithoutTheRowPadding) {
  const fs::path directory =
    fs::temp_directory_path() / "chaos_data_npy_test";
  std::error_code error;
  fs::remove_all(directory, error);
  fs::create_directories(directory);

  sensor_msgs::msg::Image image;
  image.encoding = "rgb8";
  image.is_bigendian = 0;
  image.height = 1;
  image.width = 2;
  // The row is padded with two bytes that are not part of the pixels.
  image.step = 8;
  image.data = std::vector<std::uint8_t>{1, 2, 3, 4, 5, 6, 99, 98};

  const fs::path target = directory / "image.npy";
  chaos::data::NpyWriter::write(target, image);
  ASSERT_TRUE(fs::exists(target));

  std::ifstream input(target, std::ios::binary);
  const std::string content(
    (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  ASSERT_GT(content.size(), 10u);
  // The magic and the version of the NumPy format.
  EXPECT_EQ(content.substr(0, 6), std::string("\x93NUMPY", 6));
  EXPECT_EQ(content[6], 1);
  EXPECT_EQ(content[7], 0);
  const std::uint16_t header_length =
    static_cast<std::uint16_t>(static_cast<unsigned char>(content[8])) |
    (static_cast<std::uint16_t>(static_cast<unsigned char>(content[9])) << 8);
  const std::string header = content.substr(10, header_length);
  EXPECT_NE(header.find("'descr': '<u1'"), std::string::npos) << header;
  EXPECT_NE(header.find("'shape': (1, 2, 3)"), std::string::npos) << header;
  EXPECT_NE(header.find("'fortran_order': False"), std::string::npos) << header;
  // The data holds the pixels without the row padding.
  const std::string data = content.substr(10 + header_length);
  ASSERT_EQ(data.size(), 6u);
  const std::vector<std::uint8_t> expected{1, 2, 3, 4, 5, 6};
  EXPECT_TRUE(std::equal(expected.begin(), expected.end(),
                         reinterpret_cast<const std::uint8_t *>(data.data())));
  fs::remove_all(directory, error);
}

TEST(ExportNpy, KeepsTheByteOrderOfTheMessage) {
  const fs::path directory =
    fs::temp_directory_path() / "chaos_data_npy_endian_test";
  std::error_code error;
  fs::remove_all(directory, error);
  fs::create_directories(directory);

  sensor_msgs::msg::Image image;
  image.encoding = "16UC1";
  image.is_bigendian = 1;
  image.height = 1;
  image.width = 1;
  image.step = 2;
  // A big endian 16 bit value.
  image.data = std::vector<std::uint8_t>{0x12, 0x34};

  const fs::path target = directory / "endian.npy";
  chaos::data::NpyWriter::write(target, image);
  std::ifstream input(target, std::ios::binary);
  const std::string content(
    (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  const std::uint16_t header_length =
    static_cast<std::uint16_t>(static_cast<unsigned char>(content[8])) |
    (static_cast<std::uint16_t>(static_cast<unsigned char>(content[9])) << 8);
  const std::string header = content.substr(10, header_length);
  EXPECT_NE(header.find("'descr': '>u2'"), std::string::npos) << header;
  // The pixel bytes are not converted.
  const std::string data = content.substr(10 + header_length);
  ASSERT_EQ(data.size(), 2u);
  EXPECT_EQ(static_cast<unsigned char>(data[0]), 0x12);
  EXPECT_EQ(static_cast<unsigned char>(data[1]), 0x34);
  fs::remove_all(directory, error);
}

TEST(ExportNpy, RefusesAnUnsupportedEncoding) {
  const fs::path directory =
    fs::temp_directory_path() / "chaos_data_npy_encoding_test";
  std::error_code error;
  fs::remove_all(directory, error);
  fs::create_directories(directory);

  sensor_msgs::msg::Image image;
  image.encoding = "bayer_rggb8";
  image.is_bigendian = 0;
  image.height = 1;
  image.width = 1;
  image.step = 1;
  image.data = std::vector<std::uint8_t>{1};
  EXPECT_THROW(
    chaos::data::NpyWriter::write(directory / "bad.npy", image),
    std::runtime_error);
  fs::remove_all(directory, error);
}

// The cursor over the control journal.
//
// The journal is read while the episode is exported, so a read error has to
// end the export instead of ending the action silently: the rest of the
// episode would be labelled with a command that was never stopped.
TEST(ControlJournalCursor, ReadsTheEventsInTheOrderOfTheJournal) {
  const fs::path directory =
    fs::temp_directory_path() / "chaos_data_journal_cursor_test";
  std::error_code error;
  fs::remove_all(directory, error);
  fs::create_directories(directory);
  const fs::path journal = directory / "control_events.jsonl";
  {
    std::ofstream stream(journal);
    stream << R"({"event_index":0,"sequence":1,"monotonic_ns":5,)"
      R"("source_ns":10,"receive_ns":20,"stage":"issued",)"
      R"("command":"motion.velocity","request_id":"a","code":"ok",)"
      R"("velocity_valid":true,"valid_until_ns":30,"values":[1.0,0.0]})"
      << "\n";
    stream << R"({"event_index":1,"sequence":2,"monotonic_ns":6,)"
      R"("source_ns":40,"receive_ns":50,"stage":"invalidated",)"
      R"("command":"motion.velocity","request_id":"b","code":"ok",)"
      R"("velocity_valid":false,"valid_until_ns":0,"values":null})"
      << "\n";
  }
  chaos::data::ControlJournalCursor cursor(journal);
  const chaos::data::ControlEvent * active = nullptr;
  cursor.apply(10, active);
  ASSERT_NE(active, nullptr);
  EXPECT_EQ(active->sequence, 1u);
  EXPECT_EQ(active->values, (std::vector<double>{1.0, 0.0}));
  // The invalidated event ends the action at its own source time.
  cursor.apply(40, active);
  EXPECT_EQ(active, nullptr);
  fs::remove_all(directory, error);
}

TEST(ControlJournalCursor, RefusesAMalformedLine) {
  const fs::path directory =
    fs::temp_directory_path() / "chaos_data_journal_broken_test";
  std::error_code error;
  fs::remove_all(directory, error);
  fs::create_directories(directory);
  const fs::path journal = directory / "control_events.jsonl";
  {
    std::ofstream stream(journal);
    stream << "{not json}\n";
  }
  chaos::data::ControlJournalCursor cursor(journal);
  const chaos::data::ControlEvent * active = nullptr;
  EXPECT_THROW(cursor.apply(10, active), std::runtime_error);
  fs::remove_all(directory, error);
}

TEST(ControlJournalCursor, ReportsAnIoErrorInsteadOfTheEndOfTheJournal) {
  const fs::path directory =
    fs::temp_directory_path() / "chaos_data_journal_io_error_test";
  std::error_code error;
  fs::remove_all(directory, error);
  fs::create_directories(directory);
  // A directory can be opened, but it cannot be read: the first read fails.
  const fs::path journal = directory / "control_events.jsonl";
  fs::create_directories(journal);
  chaos::data::ControlJournalCursor cursor(journal);
  const chaos::data::ControlEvent * active = nullptr;
  try {
    cursor.apply(10, active);
    FAIL() << "a read error has to end the export";
  } catch (const std::runtime_error & failure) {
    EXPECT_NE(std::string(failure.what()).find("control journal"),
      std::string::npos) << failure.what();
    EXPECT_NE(std::string(failure.what()).find("I/O error"),
      std::string::npos) << failure.what();
  }
  fs::remove_all(directory, error);
}
