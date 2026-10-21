// Contract tests for the bounded child process runner.
//
// The runner must never block on a pipe, never leave a child behind and
// always bound the output and the runtime of the child. Every fixture uses
// an absolute executable path, so a fixture that cannot start is reported as
// a failure instead of passing silently.

#include <gtest/gtest.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "chaos_assets/process.hpp"

using chaos::assets::ConfigurationError;
using chaos::assets::ProcessResult;
using chaos::assets::run_bounded;

namespace {

const char* const kShell = "/bin/sh";
const char* const kSleep = "/bin/sleep";

class TempDirectory {
 public:
  TempDirectory() {
    path_ = std::filesystem::temp_directory_path() /
            ("chaos-assets-process-" + std::to_string(::getpid()) + "-" + std::to_string(counter++));
    std::filesystem::create_directories(path_);
  }
  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }
  const std::string& path() const { return path_; }
  std::string write(const std::string& name, const std::string& content) const {
    const std::string full = path_ + "/" + name;
    std::ofstream stream(full);
    stream << content;
    stream.close();
    return full;
  }

 private:
  static int counter;
  std::string path_;
};

int TempDirectory::counter = 0;

}  // namespace

TEST(Process, CapturesOutputAndExitCode) {
  const ProcessResult result =
      run_bounded({kShell, "-c", "printf 'out'; printf 'err' 1>&2; exit 7"}, "probe",
                  std::chrono::milliseconds(5000), 4096);
  EXPECT_EQ(result.exit_code, 7);
  EXPECT_EQ(result.out, "out");
  EXPECT_EQ(result.err, "err");
}

TEST(Process, TheChildReallyRuns) {
  // A fixture that cannot start must not look like a successful run.
  const ProcessResult result = run_bounded({kShell, "-c", "echo started"}, "probe",
                                           std::chrono::milliseconds(5000), 4096);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.out, "started\n");
}

TEST(Process, EnforcesTheOutputLimit) {
  EXPECT_THROW(run_bounded({kShell, "-c", "i=0; while [ $i -lt 100000 ]; do echo abcdefghij; "
                                           "i=$((i+1)); done"},
                           "probe", std::chrono::milliseconds(5000), 1024),
               ConfigurationError);
}

TEST(Process, EnforcesTheDeadline) {
  const auto start = std::chrono::steady_clock::now();
  EXPECT_THROW(run_bounded({kSleep, "30"}, "probe", std::chrono::milliseconds(300), 4096),
               ConfigurationError);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 5000);
}

TEST(Process, ReportsASignalTermination) {
  // A child that is killed by a signal is reported as 128 plus the signal
  // number, not as a plain exit code.
  const ProcessResult result =
      run_bounded({kShell, "-c", "kill -TERM $$"}, "probe", std::chrono::milliseconds(5000),
                  4096);
  EXPECT_EQ(result.exit_code, 128 + 15);
}

TEST(Process, HandlesAChildThatClosesItsStreamsAndExits) {
  const ProcessResult result =
      run_bounded({kShell, "-c", "exec 1>&-; exec 2>&-; exit 0"}, "probe",
                  std::chrono::milliseconds(5000), 4096);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.out.empty());
  EXPECT_TRUE(result.err.empty());
}

TEST(Process, ToleratesAChildThatClosesItsOutputAndSleeps) {
  const auto start = std::chrono::steady_clock::now();
  EXPECT_THROW(run_bounded({kShell, "-c", "exec 1>&-; exec 2>&-; sleep 30"}, "probe",
                           std::chrono::milliseconds(300), 4096),
               ConfigurationError);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 5000);
}

TEST(Process, ToleratesAGrandchildThatKeepsThePipeOpen) {
  // The direct child exits at once, but a background process inherits the
  // output pipe. The runner must not wait for that pipe to be closed, and it
  // must not leave the background process behind.
  const std::string marker = "/tmp/chaos-process-orphan-" + std::to_string(::getpid());
  std::filesystem::remove(marker);
  const auto start = std::chrono::steady_clock::now();
  const ProcessResult result =
      run_bounded({kShell, "-c", "(sleep 1; touch " + marker + ") & printf 'done'"}, "probe",
                  std::chrono::milliseconds(3000), 4096);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.out, "done");
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 2500);
  // The process group of the child is terminated, so the background process
  // cannot create the marker.
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  EXPECT_FALSE(std::filesystem::exists(marker));
}

TEST(Process, HandlesLargeStandardError) {
  const ProcessResult result = run_bounded(
      {kShell, "-c", "i=0; while [ $i -lt 20000 ]; do echo abcdefghij 1>&2; i=$((i+1)); done"},
      "probe", std::chrono::milliseconds(5000), 1 << 20);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_GT(result.err.size(), 1000u);
}

TEST(Process, ReportsAMissingExecutable) {
  EXPECT_THROW(run_bounded({"/nonexistent/binary", "arg"}, "probe",
                           std::chrono::milliseconds(1000), 4096),
               ConfigurationError);
}

TEST(Process, KillsTheWholeProcessGroupOnTimeout) {
  // The child spawns a background process that would outlive it; after the
  // timeout both must be gone.
  const std::string marker = "/tmp/chaos-process-group-" + std::to_string(::getpid());
  std::filesystem::remove(marker);
  EXPECT_THROW(run_bounded({kShell, "-c", "(sleep 1; touch " + marker + ") & sleep 30"}, "probe",
                           std::chrono::milliseconds(300), 4096),
               ConfigurationError);
  // The group is killed, so the background process cannot create the marker
  // one second later.
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));
  EXPECT_FALSE(std::filesystem::exists(marker));
}

TEST(Process, DoesNotWriteToTheStandardInputOfTheChild) {
  // The child never reads standard input; the runner must not block on it.
  const ProcessResult result =
      run_bounded({kShell, "-c", "cat; echo done"}, "probe", std::chrono::milliseconds(3000),
                  4096);
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.out, "done\n");
}

// The number of open descriptors of this process.
//
// The runner opens descriptors for the child and closes them again. A leak
// would slowly exhaust the descriptor table of a long running launcher.
int open_descriptors() {
  int count = 0;
  for (int fd = 0; fd < 4096; ++fd) {
    if (::fcntl(fd, F_GETFD, 0) != -1) ++count;
  }
  return count;
}

TEST(Process, TheStandardInputReachesTheChild) {
  // The payload of the caller is the standard input of the child.
  const ProcessResult result =
      run_bounded({kShell, "-c", "cat"}, "probe", std::chrono::milliseconds(3000), 4096,
                  "payload");
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.out, "payload");
}

TEST(Process, AChildThatClosesItsStandardInputIsNotABlocker) {
  // The child closes its standard input at once. The runner must not wait
  // for a reader that does not exist, and the parent must not be killed by
  // a signal of the child.
  const ProcessResult result =
      run_bounded({kShell, "-c", "exec 0<&-; printf closed"}, "probe",
                  std::chrono::milliseconds(3000), 4096, std::string(256 * 1024, 'a'));
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.out, "closed");
}

TEST(Process, AChildThatWritesBeforeItReadsDoesNotDeadlock) {
  // The child writes more than the capacity of a pipe before it reads its
  // input. A pipe for the standard input would deadlock here, because the
  // parent would have to write the whole payload before it drains the
  // output of the child.
  const char* const kPython = "/usr/bin/python3";
  const std::string script =
      "import sys; sys.stdout.write('x'*131072); sys.stdout.flush(); "
      "sys.stdout.write(str(len(sys.stdin.buffer.read()))); sys.stdout.flush()";
  const ProcessResult result =
      run_bounded({kPython, "-c", script}, "probe", std::chrono::milliseconds(5000),
                  1024 * 1024, std::string(256 * 1024, 'a'));
  EXPECT_EQ(result.exit_code, 0);
  // The child wrote 128 KiB and read the whole payload of 256 KiB.
  EXPECT_EQ(result.out, std::string(131072, 'x') + "262144");
}

TEST(Process, AChildThatNeverReadsItsInputStillTerminates) {
  // The child ignores its standard input completely. The run has to finish
  // with the exit code of the child, not with a timeout.
  const ProcessResult result =
      run_bounded({kShell, "-c", "printf ignored"}, "probe", std::chrono::milliseconds(3000),
                  4096, std::string(256 * 1024, 'a'));
  EXPECT_EQ(result.exit_code, 0);
  EXPECT_EQ(result.out, "ignored");
}

TEST(Process, AStandardInputAboveTheLimitIsRejected) {
  // The payload is bounded, so a caller cannot fill the memory of the
  // launcher with an unbounded document.
  EXPECT_THROW(run_bounded({kShell, "-c", "cat"}, "probe", std::chrono::milliseconds(1000),
                           4096, std::string(2 * 1024 * 1024, 'a')),
               ConfigurationError);
}

TEST(Process, TheRunnerDoesNotLeakDescriptors) {
  // The descriptors of the child and of the input file are closed again.
  const int before = open_descriptors();
  for (int round = 0; round < 5; ++round) {
    const ProcessResult result =
        run_bounded({kShell, "-c", "cat"}, "probe", std::chrono::milliseconds(3000),
                    128 * 1024, std::string(64 * 1024, 'a'));
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.out.size(), 64u * 1024u);
  }
  EXPECT_EQ(open_descriptors(), before);
}
