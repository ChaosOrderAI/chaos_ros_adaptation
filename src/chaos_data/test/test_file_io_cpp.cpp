// The exclusive file output and the path containment rules.
//
// Both are the guard that keeps an audit or an export from replacing
// something that already exists and from writing into the source bag.
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "chaos_data/file_io.hpp"

namespace {

namespace fs = std::filesystem;

class FileIoTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    directory_ = fs::temp_directory_path() /
      ("chaos_data_file_io_" + std::to_string(::getpid()) + "_" +
       std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    fs::create_directories(directory_);
  }

  void TearDown() override
  {
    std::error_code error;
    fs::remove_all(directory_, error);
  }

  fs::path directory_;
};

std::string readAll(const fs::path & path)
{
  std::ifstream input(path, std::ios::binary);
  return std::string(
    (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_F(FileIoTest, WritesANewFile) {
  const fs::path target = directory_ / "index.jsonl";
  {
    chaos::data::ExclusiveFile file(target);
    file.write(std::string("{\"sequence\": 1}\n"));
    file.write("{\"sequence\": 2}\n");
    file.close();
  }
  ASSERT_TRUE(fs::exists(target));
  EXPECT_EQ(readAll(target), "{\"sequence\": 1}\n{\"sequence\": 2}\n");
}

TEST_F(FileIoTest, RefusesAnExistingFile) {
  const fs::path target = directory_ / "index.jsonl";
  {
    std::ofstream seed(target);
    seed << "original";
  }
  EXPECT_THROW(chaos::data::ExclusiveFile file(target), std::runtime_error);
  // The existing file is untouched.
  EXPECT_EQ(readAll(target), "original");
}

TEST_F(FileIoTest, RefusesASymlinkAndLeavesTheTargetIntact) {
  const fs::path target = directory_ / "target.txt";
  {
    std::ofstream seed(target);
    seed << "original";
  }
  const fs::path link = directory_ / "link.txt";
  ASSERT_EQ(::symlink(target.c_str(), link.c_str()), 0);
  EXPECT_THROW(chaos::data::ExclusiveFile file(link), std::runtime_error);
  EXPECT_EQ(readAll(target), "original");
}

TEST_F(FileIoTest, ReportsAFailedWrite) {
  const fs::path target = directory_ / "index.jsonl";
  chaos::data::ExclusiveFile file(target);
  file.write("first\n");
  // A write on a closed handle is an error, not a silent no operation.
  file.close();
  EXPECT_THROW(file.write("second\n"), std::runtime_error);
  EXPECT_EQ(readAll(target), "first\n");
}

TEST_F(FileIoTest, CanonicalPathResolvesRelativeParts) {
  const fs::path target = directory_ / "sub" / "file.txt";
  fs::create_directories(directory_ / "sub");
  {
    std::ofstream seed(target);
    seed << "x";
  }
  const fs::path canonical =
    chaos::data::canonicalPath((directory_ / "sub" / ".." / "sub" / "file.txt").string());
  EXPECT_EQ(canonical, fs::weakly_canonical(target));
}

TEST_F(FileIoTest, DetectsPathsInsideADirectory) {
  const fs::path bag = directory_ / "bag";
  fs::create_directories(bag / "nested");
  EXPECT_TRUE(chaos::data::pathIsInside(bag, bag / "nested" / "index.jsonl"));
  EXPECT_TRUE(chaos::data::pathIsInside(bag, bag));
  // A sibling with a common name prefix is not inside the directory.
  EXPECT_FALSE(chaos::data::pathIsInside(bag, directory_ / "bag_other"));
  EXPECT_FALSE(chaos::data::pathIsInside(bag, directory_ / "other.jsonl"));
}

TEST_F(FileIoTest, DetectsASymlinkIntoADirectory) {
  const fs::path bag = directory_ / "bag";
  fs::create_directories(bag);
  const fs::path link = directory_ / "link.jsonl";
  ASSERT_EQ(::symlink((bag / "index.jsonl").c_str(), link.c_str()), 0);
  // The link points into the bag, so it is refused even though the spelling
  // of the path is outside of the bag.
  EXPECT_TRUE(chaos::data::pathIsInside(bag, link));
}
