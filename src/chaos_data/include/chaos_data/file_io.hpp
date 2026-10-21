// Bounded, exclusive file output and path containment helpers.
//
// Every file that this package creates is created exclusively: an existing
// file is never overwritten. The audit index and the exported dataset are
// evidence of a run, so a later run must not be able to replace them.
//
// The path helpers resolve symlinks and "..", so a path that points into the
// source bag is recognised even when it is written in a different way
// ("bag/../bag/x", a symlink into the bag, a relative path).
#pragma once

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>

namespace chaos::data {

// The absolute, symlink resolved form of a path.
//
// The file does not have to exist: the parent directory is resolved as far as
// it exists, so a path that will be created is normalised in the same way as
// an existing one.
std::filesystem::path canonicalPath(const std::string & path);

// True when candidate is the directory parent itself or lies below it.
//
// Both paths are compared component by component in their canonical form, so
// "/a/bc" is not inside "/a/b" and a symbolic link into a directory is
// recognised.
bool pathIsInside(const std::filesystem::path & parent,
                  const std::filesystem::path & candidate);

// A file that is created with exclusive create semantics and never truncated.
//
// The failure of a write is reported as an exception: a partially written
// index must not be mistaken for a complete one, and the error has to reach
// the caller instead of ending silently in a closed file.
class ExclusiveFile {
public:
  // Creates the file. An existing file (including a symbolic link) is an
  // error and the file is not opened.
  explicit ExclusiveFile(const std::filesystem::path & path);
  ~ExclusiveFile();

  ExclusiveFile(const ExclusiveFile &) = delete;
  ExclusiveFile & operator=(const ExclusiveFile &) = delete;

  // Writes the whole buffer. A short write or a buffered error is an error.
  void write(const char * data, std::size_t size);
  void write(const std::string & text);

  // Flushes and closes the file. The result of the flush and of the close is
  // checked, because a write error can appear only when the buffer leaves
  // the process.
  void close();

  const std::filesystem::path & path() const { return path_; }

private:
  std::filesystem::path path_;
  std::FILE * file_ = nullptr;
};

}  // namespace chaos::data
