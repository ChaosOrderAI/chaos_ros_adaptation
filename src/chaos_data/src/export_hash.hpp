// The SHA-256 digest of a source file.
//
// The digest is computed with the OpenSSL library that the workspace already
// uses, so the hashing is not implemented a second time here. A read error
// is reported: a digest of a partially read file would look like a valid
// provenance record.
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace chaos::data {
namespace internal {

// The lowercase hex SHA-256 digest of a file.
std::string digestOf(const std::filesystem::path & path);

// The lowercase hex SHA-256 digest of a buffer.
std::string digestOfBytes(const void * data, std::size_t size);

}  // namespace internal
}  // namespace chaos::data
