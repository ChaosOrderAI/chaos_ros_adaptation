#include "chaos_assets/json.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace chaos::assets {

namespace {

// Escape a string the way the standard JSON encoder does with its default
// "ensure ASCII" behaviour: every character outside the printable ASCII
// range becomes a \u escape, so the digest of a configuration never depends
// on the byte encoding of the host locale.
void emit_string(const std::string& value, std::string& out) {
  static const char* const kHex = "0123456789abcdef";
  out += '"';
  size_t index = 0;
  while (index < value.size()) {
    const unsigned char c = static_cast<unsigned char>(value[index]);
    if (c == '"') {
      out += "\\\"";
      ++index;
      continue;
    }
    if (c == '\\') {
      out += "\\\\";
      ++index;
      continue;
    }
    if (c == '\n') {
      out += "\\n";
      ++index;
      continue;
    }
    if (c == '\r') {
      out += "\\r";
      ++index;
      continue;
    }
    if (c == '\t') {
      out += "\\t";
      ++index;
      continue;
    }
    if (c == '\b') {
      out += "\\b";
      ++index;
      continue;
    }
    if (c == '\f') {
      out += "\\f";
      ++index;
      continue;
    }
    if (c < 0x20) {
      out += "\\u00";
      out += kHex[(c >> 4) & 0x0f];
      out += kHex[c & 0x0f];
      ++index;
      continue;
    }
    if (c < 0x7f) {
      out += static_cast<char>(c);
      ++index;
      continue;
    }
    // Decode one UTF-8 sequence and emit it as a \u escape (a surrogate
    // pair for a code point above the basic multilingual plane).
    uint32_t code = 0;
    size_t length = 0;
    if ((c & 0xe0) == 0xc0) {
      code = c & 0x1fu;
      length = 2;
    } else if ((c & 0xf0) == 0xe0) {
      code = c & 0x0fu;
      length = 3;
    } else if ((c & 0xf8) == 0xf0) {
      code = c & 0x07u;
      length = 4;
    } else {
      // An invalid byte is escaped as the replacement character.
      code = 0xfffd;
      length = 1;
    }
    if (length > 1 && index + length <= value.size()) {
      for (size_t offset = 1; offset < length; ++offset) {
        code = (code << 6) | (static_cast<unsigned char>(value[index + offset]) & 0x3fu);
      }
    } else {
      code = 0xfffd;
      length = 1;
    }
    index += length;
    auto escape = [&](uint32_t unit) {
      out += "\\u";
      out += kHex[(unit >> 12) & 0x0f];
      out += kHex[(unit >> 8) & 0x0f];
      out += kHex[(unit >> 4) & 0x0f];
      out += kHex[unit & 0x0f];
    };
    if (code >= 0x10000) {
      const uint32_t adjusted = code - 0x10000;
      escape(0xd800 + (adjusted >> 10));
      escape(0xdc00 + (adjusted & 0x3ff));
    } else {
      escape(code);
    }
  }
  out += '"';
}

void emit(const Json& value, std::string& out) {
  if (value.is_object()) {
    std::vector<std::string> keys;
    keys.reserve(value.size());
    for (auto it = value.begin(); it != value.end(); ++it) keys.push_back(it.key());
    std::sort(keys.begin(), keys.end());
    out += '{';
    bool first = true;
    for (const auto& key : keys) {
      if (!first) out += ", ";
      first = false;
      emit_string(key, out);
      out += ": ";
      emit(value.at(key), out);
    }
    out += '}';
  } else if (value.is_array()) {
    out += '[';
    bool first = true;
    for (const auto& item : value) {
      if (!first) out += ", ";
      first = false;
      emit(item, out);
    }
    out += ']';
  } else if (value.is_string()) {
    emit_string(value.get_ref<const std::string&>(), out);
  } else if (value.is_number_float()) {
    const double number = value.get<double>();
    if (!std::isfinite(number)) {
      throw ConfigurationError("configuration contains a non-finite number");
    }
    out += py_repr_float(number);
  } else if (value.is_number_integer()) {
    out += std::to_string(value.get<int64_t>());
  } else if (value.is_number_unsigned()) {
    out += std::to_string(value.get<uint64_t>());
  } else {
    // Booleans and null serialize identically to the standard JSON module.
    out += value.dump();
  }
}

}  // namespace

std::string canonical_json(const Json& value) {
  std::string out;
  emit(value, out);
  return out;
}

std::string config_digest(const Json& value) {
  const std::string encoded = canonical_json(value);
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (context == nullptr) throw std::runtime_error("EVP_MD_CTX_new failed");
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(context, encoded.data(), encoded.size()) != 1 ||
      EVP_DigestFinal_ex(context, hash, &length) != 1) {
    EVP_MD_CTX_free(context);
    throw std::runtime_error("sha256 computation failed");
  }
  EVP_MD_CTX_free(context);
  static const char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(length * 2);
  for (unsigned int i = 0; i < length; ++i) {
    out += hex[hash[i] >> 4];
    out += hex[hash[i] & 0x0f];
  }
  return out;
}

}  // namespace chaos::assets
