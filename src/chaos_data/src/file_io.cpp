// Exclusive file output and path containment helpers.
#include "chaos_data/file_io.hpp"

#include <stdexcept>

namespace chaos::data {

std::filesystem::path canonicalPath(const std::string & path)
{
  std::error_code error;
  const std::filesystem::path absolute =
    std::filesystem::absolute(std::filesystem::path(path), error);
  if (error) {
    throw std::runtime_error("cannot resolve path: " + path);
  }
  // weakly_canonical resolves every symbolic link that exists and folds the
  // ".." components, so two spellings of the same path compare equal.
  const std::filesystem::path canonical =
    std::filesystem::weakly_canonical(absolute, error);
  if (error) {
    throw std::runtime_error("cannot resolve path: " + path);
  }
  return canonical;
}

// Resolves a symbolic link at the end of a path.
//
// weakly_canonical resolves the links of the existing part of a path, but a
// link whose target does not exist yet keeps its own name. The index of an
// audit is created after the check, so a link into the bag that points to a
// file that is not there yet has to be recognised as well.
std::filesystem::path resolveFinalLink(const std::filesystem::path & path,
                                       int depth)
{
  if (depth <= 0) {
    return path;
  }
  std::error_code error;
  const std::filesystem::path target =
    std::filesystem::read_symlink(path, error);
  if (error) {
    // Not a symbolic link: the path is its own target.
    return path;
  }
  const std::filesystem::path combined = target.is_absolute()
    ? target
    : path.parent_path() / target;
  return resolveFinalLink(combined, depth - 1);
}

bool pathIsInside(const std::filesystem::path & parent,
                  const std::filesystem::path & candidate)
{
  if (parent.empty() || candidate.empty()) {
    return false;
  }
  std::error_code error;
  const std::filesystem::path parent_canonical =
    std::filesystem::weakly_canonical(parent, error);
  if (error) {
    return false;
  }
  const std::filesystem::path candidate_canonical =
    std::filesystem::weakly_canonical(candidate, error);
  if (error) {
    return false;
  }
  const std::filesystem::path candidate_resolved =
    resolveFinalLink(candidate_canonical, 16);
  // A component wise comparison: "/a/bc" is not inside "/a/b".
  auto parent_part = parent_canonical.begin();
  auto candidate_part = candidate_resolved.begin();
  for (; parent_part != parent_canonical.end();
       ++parent_part, ++candidate_part) {
    if (candidate_part == candidate_resolved.end() ||
        *parent_part != *candidate_part) {
      return false;
    }
  }
  return true;
}

ExclusiveFile::ExclusiveFile(const std::filesystem::path & path)
: path_(path)
{
  // "wx" maps to O_WRONLY|O_CREAT|O_EXCL: the open fails when the path
  // exists, also when it is a symbolic link, so nothing is ever truncated
  // and no link is followed.
  file_ = std::fopen(path.c_str(), "wx");
  if (file_ == nullptr) {
    throw std::runtime_error("cannot create the file: " + path.string());
  }
}

ExclusiveFile::~ExclusiveFile()
{
  if (file_ != nullptr) {
    // The destructor cannot report an error, so the explicit close() is the
    // place where a write failure is detected. The best effort close here
    // only releases the descriptor.
    std::fclose(file_);
    file_ = nullptr;
  }
}

void ExclusiveFile::write(const char * data, std::size_t size)
{
  if (file_ == nullptr) {
    throw std::runtime_error("the file is already closed: " + path_.string());
  }
  if (size == 0) {
    return;
  }
  const std::size_t written = std::fwrite(data, 1, size, file_);
  if (written != size) {
    throw std::runtime_error("cannot write to the file: " + path_.string());
  }
}

void ExclusiveFile::write(const std::string & text)
{
  write(text.data(), text.size());
}

void ExclusiveFile::close()
{
  if (file_ == nullptr) {
    return;
  }
  std::FILE * file = file_;
  file_ = nullptr;
  // The flush is checked separately: a full disk or a closed pipe is
  // reported by the flush, while fclose reports the state of the stream.
  if (std::fflush(file) != 0) {
    std::fclose(file);
    throw std::runtime_error("cannot flush the file: " + path_.string());
  }
  if (std::fclose(file) != 0) {
    throw std::runtime_error("cannot close the file: " + path_.string());
  }
}

}  // namespace chaos::data
