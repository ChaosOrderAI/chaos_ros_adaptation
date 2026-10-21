#include "chaos_assets/json.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace chaos::assets {

namespace {

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
      out += nlohmann::json(key).dump();
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
    out += nlohmann::json(value.get_ref<const std::string&>()).dump();
  } else if (value.is_number_float()) {
    const double number = value.get<double>();
    if (!std::isfinite(number)) {
      throw ConfigurationError("configuration contains a non-finite number");
    }
    out += py_repr_float(number);
  } else if (value.is_number_integer()) {
    out += std::to_string(value.get<int64_t>());
  } else {
    // booleans and null serialize identically to Python's json module.
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
