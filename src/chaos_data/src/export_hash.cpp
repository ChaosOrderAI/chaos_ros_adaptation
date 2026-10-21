// The SHA-256 digest of a source file, with the OpenSSL library.
#include "export_hash.hpp"

#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

#include <openssl/evp.h>

namespace chaos::data {
namespace internal {
namespace {

std::string toHex(const unsigned char * digest, unsigned int length)
{
  static const char * digits = "0123456789abcdef";
  std::string result;
  result.reserve(static_cast<std::size_t>(length) * 2);
  for (unsigned int index = 0; index < length; ++index) {
    result.push_back(digits[(digest[index] >> 4) & 0x0fU]);
    result.push_back(digits[digest[index] & 0x0fU]);
  }
  return result;
}

}  // namespace

std::string digestOfBytes(const void * data, std::size_t size)
{
  // The context owns the resources of the digest and releases them even when
  // an error is thrown.
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
    EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  if (!context) {
    throw std::runtime_error("cannot create the digest context");
  }
  if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("cannot start the digest");
  }
  if (size > 0 && EVP_DigestUpdate(context.get(), data, size) != 1) {
    throw std::runtime_error("cannot update the digest");
  }
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  if (EVP_DigestFinal_ex(context.get(), digest, &length) != 1) {
    throw std::runtime_error("cannot finish the digest");
  }
  return toHex(digest, length);
}

std::string digestOf(const std::filesystem::path & path)
{
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot read the source file: " + path.string());
  }
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
    EVP_MD_CTX_new(), &EVP_MD_CTX_free);
  if (!context) {
    throw std::runtime_error("cannot create the digest context");
  }
  if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("cannot start the digest");
  }
  std::vector<char> chunk(1024 * 1024);
  while (input) {
    input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const std::streamsize size = input.gcount();
    if (size > 0 &&
        EVP_DigestUpdate(context.get(), chunk.data(),
                         static_cast<std::size_t>(size)) != 1) {
      throw std::runtime_error("cannot update the digest");
    }
  }
  // The end of the file and a read error are different: only the end of the
  // file ends the digest.
  if (!input.eof()) {
    throw std::runtime_error("cannot read the source file: " + path.string());
  }
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  if (EVP_DigestFinal_ex(context.get(), digest, &length) != 1) {
    throw std::runtime_error("cannot finish the digest");
  }
  return toHex(digest, length);
}

}  // namespace internal
}  // namespace chaos::data
