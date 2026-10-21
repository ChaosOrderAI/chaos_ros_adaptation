#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "chaos_assets/errors.hpp"

namespace chaos::assets {

// The result of a bounded child process.
struct ProcessResult {
  int exit_code = -1;   // exit status, or 128 + signal number when killed
  std::string out;      // captured standard output
  std::string err;      // captured standard error
};

// Run a local executable with a steady-clock deadline after spawning.
//
// The child is started with posix_spawn, so no allocation happens between
// fork and exec. Standard input is /dev/null, or the bounded payload of the
// caller: a child that never reads it can neither block the parent nor raise
// SIGPIPE in it. Standard output and standard error are read through
// non-blocking pipes with a combined size limit, and the child runs in its
// own process group so that a timeout can terminate the whole group instead
// of leaving orphans behind. The child is always reaped, and the pipes are
// closed even when the run fails.
//
// Throws ConfigurationError when the executable cannot be started, when the
// output limit is exceeded or when the deadline passes.
ProcessResult run_bounded(const std::vector<std::string>& command, const std::string& where,
                          std::chrono::milliseconds timeout, std::size_t output_limit);

// The largest standard input payload of a bounded child process.
constexpr std::size_t kMaxProcessInput = 1024u * 1024u;

// Run a local executable with a bounded standard input.
//
// Before spawning, the bounded payload is written to an anonymous file and
// rewound. The child receives this descriptor as stdin and reaches EOF after
// the payload. It can write output before reading input, or close stdin,
// without pipe backpressure or SIGPIPE in the parent. The execution deadline
// starts after spawning; input-file preparation is not included.
//
// Throws ConfigurationError when the payload is too large, when the
// executable cannot be started, when the output limit is exceeded or when
// the deadline passes.
ProcessResult run_bounded(const std::vector<std::string>& command, const std::string& where,
                          std::chrono::milliseconds timeout, std::size_t output_limit,
                          const std::string& input);

}  // namespace chaos::assets
