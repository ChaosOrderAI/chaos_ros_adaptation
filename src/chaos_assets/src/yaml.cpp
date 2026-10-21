#include "chaos_assets/yaml.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <regex>
#include <set>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace chaos::assets {

namespace {

constexpr size_t kSourceLimit = 1024 * 1024;
constexpr int kMaxDepth = 200;

// PyYAML SafeResolver implicit scalar resolution (YAML 1.1 core schema).
std::regex bool_re(R"(^(?:yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF)$)");
std::regex null_re(R"(^(?:~|null|Null|NULL|)$)");
std::regex int_dec_re(R"(^[-+]?(?:0|[1-9][0-9_]*)$)");
std::regex int_oct_re(R"(^[-+]?0[0-7_]+$)");
std::regex int_hex_re(R"(^[-+]?0x[0-9a-fA-F_]+$)");
std::regex int_bin_re(R"(^[-+]?0b[01_]+$)");
std::regex int_sex_re(R"(^[-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+$)");
std::regex float_dec_re(R"(^[-+]?(?:[0-9][0-9_]*\.[0-9_]*|\.[0-9]+)(?:[eE][-+]?[0-9]+)?$)");
std::regex float_sex_re(R"(^[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+\.[0-9_]*$)");
std::regex float_special_re(R"(^[-+]?\.(?:inf|Inf|INF|nan|NaN|NAN)$)");

std::string strip_underscores(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c != '_') out += c;
  }
  return out;
}

bool resolve_bool(const std::string& s) {
  char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
  return c == 'y' || c == 't' || c == 'o' && (s.size() > 1 && std::tolower(static_cast<unsigned char>(s[1])) == 'n');
}

int64_t parse_base(const std::string& digits, int base, const std::string& where) {
  errno = 0;
  char* end = nullptr;
  long long value = std::strtoll(digits.c_str(), &end, base);
  if (errno == ERANGE || end == nullptr || *end != '\0') {
    throw ConfigurationError(where + ": integer exceeds signed 64-bit range");
  }
  return static_cast<int64_t>(value);
}

int64_t parse_sexagesimal_int(const std::string& raw) {
  bool negative = raw[0] == '-';
  size_t pos = (raw[0] == '-' || raw[0] == '+') ? 1 : 0;
  int64_t value = 0;
  while (pos < raw.size()) {
    size_t colon = raw.find(':', pos);
    std::string part = raw.substr(pos, colon == std::string::npos ? std::string::npos : colon - pos);
    value = value * 60 + std::strtoll(part.c_str(), nullptr, 10);
    if (colon == std::string::npos) break;
    pos = colon + 1;
  }
  return negative ? -value : value;
}

// Convert one already-loaded yaml-cpp node. `ancestors` guards against
// recursive anchors/aliases, which yaml-cpp otherwise resolves silently.
Json convert(const YAML::Node& node, const std::string& where, int depth,
             std::vector<const YAML::Node*>& ancestors) {
  if (depth > kMaxDepth) {
    throw ConfigurationError(where + ": configuration nesting is too deep or recursive");
  }
  switch (node.Type()) {
    case YAML::NodeType::Null:
      return nullptr;
    case YAML::NodeType::Scalar: {
      const std::string raw = node.Scalar();
      // Quoted scalars keep their verbatim string form.
      if (node.Tag() == "!") return Json(raw);
      if (std::regex_match(raw, null_re)) return nullptr;
      if (std::regex_match(raw, bool_re)) return Json(resolve_bool(raw));
      if (std::regex_match(raw, int_dec_re)) return Json(parse_base(strip_underscores(raw), 10, where));
      if (std::regex_match(raw, int_oct_re)) {
        std::string digits = strip_underscores(raw);
        bool negative = digits[0] == '-';
        if (digits[0] == '-' || digits[0] == '+') digits = digits.substr(1);
        int64_t value = parse_base(digits, 8, where);
        return Json(negative ? -value : value);
      }
      if (std::regex_match(raw, int_hex_re)) {
        std::string digits = strip_underscores(raw);
        bool negative = digits[0] == '-';
        if (digits[0] == '-' || digits[0] == '+') digits = digits.substr(1);
        int64_t value = parse_base(digits, 16, where);
        return Json(negative ? -value : value);
      }
      if (std::regex_match(raw, int_bin_re)) {
        std::string digits = strip_underscores(raw);
        bool negative = digits[0] == '-';
        if (digits[0] == '-' || digits[0] == '+') digits = digits.substr(1);
        int64_t value = parse_base(digits, 2, where);
        return Json(negative ? -value : value);
      }
      if (std::regex_match(raw, int_sex_re)) return Json(parse_sexagesimal_int(strip_underscores(raw)));
      if (std::regex_match(raw, float_special_re)) {
        bool negative = raw[0] == '-';
        bool infinite = std::tolower(static_cast<unsigned char>(raw[raw.size() - 3])) == 'i';
        double value = infinite ? std::numeric_limits<double>::infinity()
                                : std::numeric_limits<double>::quiet_NaN();
        return Json(negative ? -value : value);
      }
      if (std::regex_match(raw, float_dec_re) || std::regex_match(raw, float_sex_re)) {
        std::string digits = strip_underscores(raw);
        double value;
        if (std::regex_match(raw, float_sex_re)) {
          bool negative = digits[0] == '-';
          size_t pos = (digits[0] == '-' || digits[0] == '+') ? 1 : 0;
          double base = 0.0, frac = 0.0, scale = 1.0;
          while (pos < digits.size() && digits[pos] != ':') {
            base = base * 10 + (digits[pos] - '0');
            ++pos;
          }
          bool after_colon = false;
          for (; pos < digits.size(); ++pos) {
            char c = digits[pos];
            if (c == ':') {
              after_colon = true;
              continue;
            }
            if (!after_colon) {
              base = base * 10 + (c - '0');
            } else {
              frac = frac * 60 + (c - '0');
              scale *= 60;
            }
          }
          value = base + frac / scale;
          if (negative) value = -value;
        } else {
          errno = 0;
          value = std::strtod(digits.c_str(), nullptr);
          if (errno == ERANGE && std::isinf(value)) {
            // Overflow to infinity mirrors Python's float("1e500") == inf;
            // downstream finite checks reject it with a clear error.
          }
        }
        return Json(value);
      }
      return Json(raw);
    }
    case YAML::NodeType::Sequence: {
      Json result = Json::array();
      for (const auto& item : node) {
        result.push_back(convert(item, where, depth + 1, ancestors));
      }
      return result;
    }
    case YAML::NodeType::Map: {
      for (const YAML::Node* ancestor : ancestors) {
        if (node.is(*ancestor)) {
          throw ConfigurationError(where + ": configuration nesting is too deep or recursive");
        }
      }
      ancestors.push_back(&node);
      Json result = Json::object();
      for (const auto& pair : node) {
        const YAML::Node key_node = pair.first;
        if (key_node.Type() != YAML::NodeType::Scalar) {
          throw ConfigurationError(where + ": YAML mapping keys must be strings");
        }
        const std::string key = key_node.Scalar();
        if (result.contains(key)) {
          throw ConfigurationError(where + ": duplicate YAML key: " + key);
        }
        result[key] = convert(pair.second, where, depth + 1, ancestors);
      }
      ancestors.pop_back();
      return result;
    }
    case YAML::NodeType::Undefined:
      throw ConfigurationError(where + ": undefined YAML node");
  }
  throw ConfigurationError(where + ": unsupported YAML node");
}

bool valid_utf8(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      ++i;
      continue;
    }
    int need;
    uint32_t code;
    if ((c & 0xe0) == 0xc0) {
      need = 1;
      code = c & 0x1f;
    } else if ((c & 0xf0) == 0xe0) {
      need = 2;
      code = c & 0x0f;
    } else if ((c & 0xf8) == 0xf0) {
      need = 3;
      code = c & 0x07;
    } else {
      return false;
    }
    if (i + need >= s.size()) return false;
    for (int k = 1; k <= need; ++k) {
      unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xc0) != 0x80) return false;
      code = (code << 6) | (cc & 0x3f);
    }
    if ((need == 1 && code < 0x80) || (need == 2 && code < 0x800) ||
        (need == 3 && code < 0x10000) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
      return false;
    }
    i += need + 1;
  }
  return true;
}

}  // namespace

Json load_yaml_string(const std::string& content, const std::string& where) {
  if (!valid_utf8(content)) {
    throw ConfigurationError(where + ": invalid UTF-8");
  }
  YAML::Node root;
  try {
    root = YAML::Load(content);
  } catch (const std::exception& error) {
    throw ConfigurationError(where + ": " + error.what());
  }
  if (root.Type() == YAML::NodeType::Null || root.Type() == YAML::NodeType::Undefined) {
    return nullptr;
  }
  std::vector<const YAML::Node*> ancestors;
  return convert(root, where, 0, ancestors);
}

Json load_yaml_file(const std::string& path) {
  struct stat info {};
  if (stat(path.c_str(), &info) != 0) {
    throw ConfigurationError(path + ": " + std::strerror(errno));
  }
  if (info.st_size > static_cast<off_t>(kSourceLimit)) {
    throw ConfigurationError(path + ": configuration exceeds 1 MiB");
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw ConfigurationError(path + ": cannot open file");
  }
  std::string content((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  return load_yaml_string(content, path);
}

}  // namespace chaos::assets
