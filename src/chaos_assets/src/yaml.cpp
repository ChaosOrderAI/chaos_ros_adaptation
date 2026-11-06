#include "chaos_assets/yaml.hpp"

#include <sys/stat.h>

#include <cerrno>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <regex>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace chaos::assets {

namespace {

constexpr size_t kSourceLimit = 1024 * 1024;
constexpr int kMaxDepth = 200;
// Total-node budget guards against exponential alias expansion in small
// documents (a pathological alias tree can double node count per level).
constexpr size_t kMaxNodes = 200000;

// PyYAML SafeResolver implicit scalar resolution (YAML 1.1 core schema).
std::regex bool_re(R"(^(?:yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF)$)");
std::regex null_re(R"(^(?:~|null|Null|NULL|)$)");
std::regex int_dec_re(R"(^[-+]?(?:0|[1-9][0-9_]*)$)");
std::regex int_oct_re(R"(^[-+]?0[0-7_]+$)");
std::regex int_hex_re(R"(^[-+]?0x[0-9a-fA-F_]+$)");
std::regex int_bin_re(R"(^[-+]?0b[01_]+$)");
std::regex int_sex_re(R"(^[-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+$)");
// The exponent sign is mandatory: PyYAML's implicit float resolver only
// accepts it that way, so a plain scalar like "1.0e5" stays a string while
// "1.0e+5" is a float.
std::regex float_dec_re(R"(^[-+]?(?:[0-9][0-9_]*\.[0-9_]*|\.[0-9]+)(?:[eE][-+][0-9]+)?$)");
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
  const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
  return c == 'y' || c == 't' || (c == 'o' && s.size() > 1 &&
                                  std::tolower(static_cast<unsigned char>(s[1])) == 'n');
}

// Checked integer parse in an arbitrary base; rejects out-of-range values
// instead of narrowing. Accepts a leading '-' but not '+' (handled by caller).
int64_t parse_base(const std::string& digits, int base, const std::string& where) {
  int64_t value = 0;
  const char* first = digits.data();
  const char* last = digits.data() + digits.size();
  if (first != last && *first == '+') ++first;
  const auto result = std::from_chars(first, last, value, base);
  if (result.ec == std::errc::result_out_of_range) {
    throw ConfigurationError(where + ": integer exceeds signed 64-bit range");
  }
  if (result.ec != std::errc() || result.ptr != last) {
    throw ConfigurationError(where + ": invalid integer");
  }
  return value;
}

// Multi-component sexagesimal with overflow checks. Each non-first component
// must be a minute/second in 0..59; only the last may carry a fraction.
double parse_sexagesimal(const std::string& raw, const std::string& where) {
  const bool negative = raw[0] == '-';
  size_t pos = (raw[0] == '-' || raw[0] == '+') ? 1 : 0;
  double total = 0.0;
  bool first = true;
  while (pos <= raw.size()) {
    const size_t colon = raw.find(':', pos);
    const std::string part = raw.substr(pos, colon == std::string::npos ? std::string::npos : colon - pos);
    if (part.empty()) throw ConfigurationError(where + ": invalid sexagesimal number");
    const size_t dot = part.find('.');
    const std::string whole = dot == std::string::npos ? part : part.substr(0, dot);
    if (whole.empty()) throw ConfigurationError(where + ": invalid sexagesimal number");
    int64_t whole_value = 0;
    const auto parsed = std::from_chars(whole.data(), whole.data() + whole.size(), whole_value, 10);
    if (parsed.ec != std::errc() || parsed.ptr != whole.data() + whole.size()) {
      throw ConfigurationError(where + ": invalid sexagesimal number");
    }
    if (!first && (whole_value < 0 || whole_value > 59)) {
      throw ConfigurationError(where + ": invalid sexagesimal number");
    }
    double component = static_cast<double>(whole_value);
    if (dot != std::string::npos) {
      // A fraction is allowed only in the final component.
      if (colon != std::string::npos || part.find('.', dot + 1) != std::string::npos) {
        throw ConfigurationError(where + ": invalid sexagesimal number");
      }
      const std::string frac = part.substr(dot + 1);
      double place = 1.0;
      for (char c : frac) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
          throw ConfigurationError(where + ": invalid sexagesimal number");
        }
        place /= 10.0;
        component += (c - '0') * place;
      }
    }
    total = total * 60.0 + component;
    first = false;
    if (colon == std::string::npos) break;
    pos = colon + 1;
  }
  return negative ? -total : total;
}

// Checked integer sexagesimal: value = c0*60^n + c1*60^(n-1) + ... with all
// arithmetic range-checked before it happens.
int64_t parse_sexagesimal_int(const std::string& raw, const std::string& where) {
  const bool negative = raw[0] == '-';
  size_t pos = (raw[0] == '-' || raw[0] == '+') ? 1 : 0;
  int64_t total = 0;
  bool first = true;
  while (pos <= raw.size()) {
    const size_t colon = raw.find(':', pos);
    const std::string part = raw.substr(pos, colon == std::string::npos ? std::string::npos : colon - pos);
    if (part.empty() || part.find('.') != std::string::npos) {
      throw ConfigurationError(where + ": invalid sexagesimal number");
    }
    int64_t component = 0;
    const auto parsed = std::from_chars(part.data(), part.data() + part.size(), component, 10);
    if (parsed.ec != std::errc() || parsed.ptr != part.data() + part.size()) {
      throw ConfigurationError(where + ": invalid sexagesimal number");
    }
    if (!first && (component < 0 || component > 59)) {
      throw ConfigurationError(where + ": invalid sexagesimal number");
    }
    if (total > (std::numeric_limits<int64_t>::max() - component) / 60) {
      throw ConfigurationError(where + ": integer exceeds signed 64-bit range");
    }
    total = total * 60 + component;
    first = false;
    if (colon == std::string::npos) break;
    pos = colon + 1;
  }
  return negative ? -total : total;
}


// Strict converters for explicit !!int / !!float tags: the scalar must match
// the corresponding YAML 1.1 production and converts exactly.
int64_t strict_int(const std::string& raw, const std::string& where) {
  if (std::regex_match(raw, int_dec_re)) return parse_base(strip_underscores(raw), 10, where);
  if (std::regex_match(raw, int_oct_re)) {
    std::string digits = strip_underscores(raw);
    const bool negative = digits[0] == '-';
    if (digits[0] == '-' || digits[0] == '+') digits = digits.substr(1);
    const int64_t value = parse_base(digits, 8, where);
    return negative ? -value : value;
  }
  if (std::regex_match(raw, int_hex_re)) {
    std::string digits = strip_underscores(raw);
    const bool negative = digits[0] == '-';
    if (digits[0] == '-' || digits[0] == '+') digits = digits.substr(1);
    const int64_t value = parse_base(digits.substr(2), 16, where);
    return negative ? -value : value;
  }
  if (std::regex_match(raw, int_bin_re)) {
    std::string digits = strip_underscores(raw);
    const bool negative = digits[0] == '-';
    if (digits[0] == '-' || digits[0] == '+') digits = digits.substr(1);
    const int64_t value = parse_base(digits.substr(2), 2, where);
    return negative ? -value : value;
  }
  if (std::regex_match(raw, int_sex_re)) return parse_sexagesimal_int(strip_underscores(raw), where);
  throw ConfigurationError(where + ": invalid value for explicit YAML tag 'int'");
}

// Explicit !!float follows PyYAML's construct_yaml_float, which is wider
// than the implicit resolver: underscores are stripped, one leading sign is
// peeled off and the rest is handed to the language float conversion. That
// accepts "1e5" (no exponent sign) and sexagesimal forms, and it rejects base
// prefixes such as "0b10" and "0x10".
double strict_float(const std::string& raw, const std::string& where) {
  if (raw.empty()) {
    throw ConfigurationError(where + ": invalid value for explicit YAML tag 'float'");
  }
  std::string value = strip_underscores(raw);
  for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  double sign = 1.0;
  if (value[0] == '-') sign = -1.0;
  if (value[0] == '+' || value[0] == '-') value = value.substr(1);
  if (value.empty()) {
    throw ConfigurationError(where + ": invalid value for explicit YAML tag 'float'");
  }
  if (value == ".inf" || value == "inf" || value == "infinity") {
    return sign * std::numeric_limits<double>::infinity();
  }
  if (value == ".nan" || value == "nan") {
    return sign * std::numeric_limits<double>::quiet_NaN();
  }
  if (value.find(':') != std::string::npos) {
    return sign * parse_sexagesimal(value, where);
  }
  // glibc strtod accepts hexadecimal floats ("0x10"), which Python's
  // float() rejects, so those are excluded before the conversion. The
  // conversion itself must consume the whole scalar: a trailing suffix is an
  // error, not a prefix that happens to parse.
  if (value.size() > 1 && value[0] == '0' && value[1] == 'x') {
    throw ConfigurationError(where + ": invalid value for explicit YAML tag 'float'");
  }
  // Overflow to infinity mirrors Python float("1e500"); downstream finite
  // checks reject it with a clear error.
  char* end = nullptr;
  const double converted = std::strtod(value.c_str(), &end);
  if (end == value.c_str() || end == nullptr || *end != '\0') {
    throw ConfigurationError(where + ": invalid value for explicit YAML tag 'float'");
  }
  return sign * converted;
}

// Convert one already-loaded yaml-cpp node. `ancestors` guards against
// recursive anchors/aliases; `budget` bounds alias expansion.
Json convert(const YAML::Node& node, const std::string& where, int depth,
             std::vector<const YAML::Node*>& ancestors, size_t& budget) {
  if (depth > kMaxDepth || budget == 0) {
    throw ConfigurationError(where + ": configuration nesting is too deep or recursive");
  }
  --budget;
  switch (node.Type()) {
    case YAML::NodeType::Null:
      return nullptr;
    case YAML::NodeType::Scalar: {
      const std::string raw = node.Scalar();
      const std::string tag = node.Tag();
      // Quoted scalars are verbatim strings.
      if (tag == "!") return Json(raw);
      // Explicit safe-schema tags dispatch strictly: the declared type is
      // enforced instead of falling through to implicit resolution.
      if (tag == "tag:yaml.org,2002:str") return Json(raw);
      if (tag == "tag:yaml.org,2002:null") return nullptr;
      if (tag == "tag:yaml.org,2002:bool") {
        if (!std::regex_match(raw, bool_re)) {
          throw ConfigurationError(where + ": invalid value for explicit YAML tag 'bool'");
        }
        return Json(resolve_bool(raw));
      }
      if (tag == "tag:yaml.org,2002:int") {
        return Json(strict_int(raw, where));
      }
      if (tag == "tag:yaml.org,2002:float") {
        return Json(strict_float(raw, where));
      }
      if (tag != "?") {
        throw ConfigurationError(where + ": unsupported YAML tag '" + tag + "'");
      }
      // Plain scalar: full PyYAML-compatible implicit resolution.
      if (std::regex_match(raw, null_re)) return nullptr;
      if (std::regex_match(raw, bool_re)) return Json(resolve_bool(raw));
      if (std::regex_match(raw, int_dec_re)) return Json(parse_base(strip_underscores(raw), 10, where));
      if (std::regex_match(raw, int_oct_re)) {
        std::string digits = strip_underscores(raw);
        const bool negative = digits[0] == '-';
        if (digits[0] == '-' || digits[0] == '+') digits = digits.substr(1);
        const int64_t value = parse_base(digits, 8, where);
        return Json(negative ? -value : value);
      }
      if (std::regex_match(raw, int_hex_re)) {
        std::string digits = strip_underscores(raw);
        const bool negative = digits[0] == '-';
        if (digits[0] == '-' || digits[0] == '+') digits = digits.substr(1);
        const int64_t value = parse_base(digits.substr(2), 16, where);
        return Json(negative ? -value : value);
      }
      if (std::regex_match(raw, int_bin_re)) {
        std::string digits = strip_underscores(raw);
        const bool negative = digits[0] == '-';
        if (digits[0] == '-' || digits[0] == '+') digits = digits.substr(1);
        const int64_t value = parse_base(digits.substr(2), 2, where);
        return Json(negative ? -value : value);
      }
      if (std::regex_match(raw, int_sex_re)) {
        return Json(parse_sexagesimal_int(strip_underscores(raw), where));
      }
      if (std::regex_match(raw, float_special_re)) {
        const bool negative = raw[0] == '-';
        const bool infinite = std::tolower(static_cast<unsigned char>(raw[raw.size() - 3])) == 'i';
        double value = infinite ? std::numeric_limits<double>::infinity()
                                : std::numeric_limits<double>::quiet_NaN();
        return Json(negative ? -value : value);
      }
      if (std::regex_match(raw, float_dec_re)) {
        const std::string digits = strip_underscores(raw);
        // Overflow to infinity mirrors Python float("1e500"); downstream
        // finite checks reject it with a clear error.
        return Json(std::strtod(digits.c_str(), nullptr));
      }
      if (std::regex_match(raw, float_sex_re)) {
        return Json(parse_sexagesimal(strip_underscores(raw), where));
      }
      return Json(raw);
    }
    case YAML::NodeType::Sequence: {
      if (node.Tag() != "?" && node.Tag() != "tag:yaml.org,2002:seq") {
        throw ConfigurationError(where + ": unsupported YAML tag '" + node.Tag() + "'");
      }
      for (const YAML::Node* ancestor : ancestors) {
        if (node.is(*ancestor)) {
          throw ConfigurationError(where + ": configuration nesting is too deep or recursive");
        }
      }
      ancestors.push_back(&node);
      Json result = Json::array();
      for (const auto& item : node) {
        result.push_back(convert(item, where, depth + 1, ancestors, budget));
      }
      ancestors.pop_back();
      return result;
    }
    case YAML::NodeType::Map: {
      if (node.Tag() != "?" && node.Tag() != "tag:yaml.org,2002:map") {
        throw ConfigurationError(where + ": unsupported YAML tag '" + node.Tag() + "'");
      }
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
        const Json typed_key = convert(key_node, where, depth + 1, ancestors, budget);
        if (!typed_key.is_string()) {
          throw ConfigurationError(where + ": YAML mapping keys must be strings");
        }
        const std::string key = typed_key.get_ref<const std::string&>();
        if (result.contains(key)) {
          throw ConfigurationError(where + ": duplicate YAML key: " + key);
        }
        result[key] = convert(pair.second, where, depth + 1, ancestors, budget);
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
    const unsigned char c = static_cast<unsigned char>(s[i]);
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
    if (i + static_cast<size_t>(need) >= s.size()) return false;
    for (int k = 1; k <= need; ++k) {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xc0) != 0x80) return false;
      code = (code << 6) | (cc & 0x3f);
    }
    if ((need == 1 && code < 0x80) || (need == 2 && code < 0x800) ||
        (need == 3 && code < 0x10000) || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
      return false;
    }
    i += static_cast<size_t>(need) + 1;
  }
  return true;
}

}  // namespace

Json load_yaml_string(const std::string& content, const std::string& where) {
  if (!valid_utf8(content)) {
    throw ConfigurationError(where + ": invalid UTF-8");
  }
  // PyYAML's safe_load refuses a stream that holds more than one document.
  // yaml-cpp's YAML::Load silently returns the first one, so the count has to
  // be checked separately.
  std::vector<YAML::Node> documents;
  try {
    documents = YAML::LoadAll(content);
  } catch (const std::exception& error) {
    throw ConfigurationError(where + ": " + error.what());
  }
  if (documents.size() > 1) {
    throw ConfigurationError(where + ": expected a single document in the stream");
  }
  const YAML::Node root = documents.empty() ? YAML::Node() : documents.front();
  if (root.Type() == YAML::NodeType::Null || root.Type() == YAML::NodeType::Undefined) {
    return nullptr;
  }
  std::vector<const YAML::Node*> ancestors;
  size_t budget = kMaxNodes;
  return convert(root, where, 0, ancestors, budget);
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
