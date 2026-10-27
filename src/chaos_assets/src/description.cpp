#include "chaos_assets/description.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <urdf_parser/urdf_parser.h>

#include <ament_index_cpp/get_package_share_directory.hpp>

#include "chaos_assets/paths.hpp"
#include "chaos_assets/process.hpp"
#include "chaos_assets/sensors.hpp"
#include "chaos_assets/xml.hpp"

namespace chaos::assets {

namespace {

constexpr size_t kSourceLimit = 1024 * 1024;
constexpr size_t kXmlLimit = 4 * 1024 * 1024;
constexpr int kProcessTimeoutMs = 10000;

const std::vector<std::string> kFields = {"enabled",       "file",      "format",
                                          "arguments",     "frame_prefix", "publish_frequency",
                                          "joint_states_topic", "use_sim_time"};

std::regex argument_re(R"(^[A-Za-z_][A-Za-z0-9_]*$)");
std::regex frame_prefix_re(R"(^[A-Za-z_][A-Za-z0-9_]*(?:/[A-Za-z_][A-Za-z0-9_]*)*$)");

// The xacro namespace markers that must not survive into a URDF document.
const char* const kXacroNamespaces[] = {"http://www.ros.org/wiki/xacro", "http://ros.org/wiki/xacro"};

std::string sha256_hex(const std::string& content) {
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (context == nullptr) throw ConfigurationError("sha256 computation failed");
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(context, content.data(), content.size()) != 1 ||
      EVP_DigestFinal_ex(context, hash, &length) != 1) {
    EVP_MD_CTX_free(context);
    throw ConfigurationError("sha256 computation failed");
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

// ---------------------------------------------------------------------------
// URI handling for mesh and texture resources.
// ---------------------------------------------------------------------------

struct Uri {
  std::string scheme;
  std::string netloc;
  std::string path;
  std::string query;
  std::string fragment;
};

// Split a URI reference the way the standard library does: the scheme stops
// at the first colon, the authority follows two slashes, and the query and
// fragment follow their delimiters.
Uri split_uri(const std::string& value) {
  Uri uri;
  std::string rest = value;
  const size_t hash = rest.find('#');
  if (hash != std::string::npos) {
    uri.fragment = rest.substr(hash + 1);
    rest = rest.substr(0, hash);
  }
  const size_t question = rest.find('?');
  if (question != std::string::npos) {
    uri.query = rest.substr(question + 1);
    rest = rest.substr(0, question);
  }
  const size_t colon = rest.find(':');
  if (colon != std::string::npos && colon > 0) {
    const std::string candidate = rest.substr(0, colon);
    const bool valid = std::isalpha(static_cast<unsigned char>(candidate[0])) != 0;
    bool well_formed = valid;
    for (char c : candidate) {
      if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '-' && c != '.') {
        well_formed = false;
        break;
      }
    }
    if (well_formed) {
      uri.scheme = candidate;
      rest = rest.substr(colon + 1);
    }
  }
  if (rest.rfind("//", 0) == 0) {
    rest = rest.substr(2);
    const size_t slash = rest.find('/');
    if (slash == std::string::npos) {
      uri.netloc = rest;
      rest.clear();
    } else {
      uri.netloc = rest.substr(0, slash);
      rest = rest.substr(slash);
    }
  }
  uri.path = rest;
  return uri;
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string percent_decode(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    if (value[index] == '%' && index + 2 < value.size()) {
      const int high = hex_value(value[index + 1]);
      const int low = hex_value(value[index + 2]);
      if (high >= 0 && low >= 0) {
        out += static_cast<char>((high << 4) | low);
        index += 2;
        continue;
      }
    }
    out += value[index];
  }
  return out;
}

// Percent-encode a local path for a file URI.
std::string percent_encode_path(const std::string& value) {
  static const char* const kUnreserved =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~/";
  std::string out;
  out.reserve(value.size());
  for (unsigned char c : value) {
    if (std::strchr(kUnreserved, c) != nullptr && c != 0) {
      out += static_cast<char>(c);
    } else {
      char buffer[4];
      std::snprintf(buffer, sizeof(buffer), "%%%02X", c);
      out += buffer;
    }
  }
  return out;
}

// ---------------------------------------------------------------------------
// Declaration handling.
// ---------------------------------------------------------------------------

Json declaration(const Json& value, const std::string& directory, const std::string& where) {
  Json result = record(value, where, kFields);
  for (const char* key : {"enabled", "use_sim_time"}) {
    auto found = result.find(key);
    if (found != result.end() && !found->is_boolean()) {
      throw ConfigurationError(where + "." + key + ": expected boolean");
    }
  }
  auto file = result.find("file");
  if (file != result.end()) {
    file.value() = resolve_path(file.value(), directory, where + ".file");
  }
  auto format = result.find("format");
  if (format != result.end()) {
    if (!format->is_string() || (format->get_ref<const std::string&>() != "urdf" &&
                                 format->get_ref<const std::string&>() != "xacro")) {
      throw ConfigurationError(where + ".format: expected urdf or xacro");
    }
  }
  auto arguments = result.find("arguments");
  if (arguments != result.end()) {
    record(arguments.value(), where + ".arguments");
    for (auto it = arguments->begin(); it != arguments->end(); ++it) {
      text(it.key(), where + ".arguments name", &argument_re);
      const Json& item = it.value();
      const bool scalar = item.is_string() || item.is_boolean() ||
                          (item.is_number() && is_finite_number(item));
      if (!scalar) {
        throw ConfigurationError(where + ".arguments." + it.key() + ": expected a finite scalar");
      }
    }
  }
  auto frame_prefix = result.find("frame_prefix");
  if (frame_prefix != result.end()) {
    if (!frame_prefix->is_string()) {
      throw ConfigurationError(
          where + ".frame_prefix: expected empty string or a frame path ending in /");
    }
    const std::string prefix = frame_prefix->get_ref<const std::string&>();
    if (!prefix.empty()) {
      const bool ends_with_slash = prefix.back() == '/';
      const std::string body = ends_with_slash ? prefix.substr(0, prefix.size() - 1) : prefix;
      if (!ends_with_slash || !std::regex_match(body, kFrameRe)) {
        throw ConfigurationError(
            where + ".frame_prefix: expected empty string or a frame path ending in /");
      }
    }
  }
  auto frequency = result.find("publish_frequency");
  if (frequency != result.end()) {
    const double value_number = finite_number(frequency.value(), where + ".publish_frequency");
    if (value_number < 0.1 || value_number > 1000.0) {
      throw ConfigurationError(
          where + ".publish_frequency: expected finite number in [0.1, 1000]");
    }
  }
  auto joint_states = result.find("joint_states_topic");
  if (joint_states != result.end()) {
    // Validate now; the relative name is resolved against the final asset
    // namespace once the merged declaration is known.
    resolve_topic(joint_states.value(), "/unused", where + ".joint_states_topic");
  }
  return result;
}

std::string read_source(const std::string& path, const std::string& where) {
  struct stat info {};
  if (::stat(path.c_str(), &info) != 0) {
    throw ConfigurationError(where + ": cannot read description " + path + ": " +
                             std::strerror(errno));
  }
  if (!S_ISREG(info.st_mode)) {
    throw ConfigurationError(where + ": description source must be a regular file");
  }
  if (static_cast<size_t>(info.st_size) > kSourceLimit) {
    throw ConfigurationError(where + ": description source exceeds 1 MiB");
  }
  std::FILE* stream = std::fopen(path.c_str(), "rb");
  if (stream == nullptr) {
    throw ConfigurationError(where + ": cannot read description " + path + ": " +
                             std::strerror(errno));
  }
  std::string content;
  content.resize(static_cast<size_t>(info.st_size));
  const size_t read = std::fread(&content[0], 1, content.size(), stream);
  const bool failed = std::ferror(stream) != 0;
  std::fclose(stream);
  if (failed) {
    throw ConfigurationError(where + ": cannot read description " + path);
  }
  content.resize(read);
  return content;
}

void reject_entities(const std::string& xml, const std::string& where) {
  if (declares_doctype_or_entity(xml)) {
    throw ConfigurationError(where + ": DOCTYPE and ENTITY declarations are not allowed");
  }
}

// Resolve one mesh or texture reference to a normalized, local URI.
std::string resource(const std::string& value, const std::string& directory, const std::string& where) {
  const Uri uri = split_uri(value);
  if (!uri.query.empty() || !uri.fragment.empty()) {
    throw ConfigurationError(where + ": resource queries/fragments are not supported");
  }
  std::filesystem::path path;
  std::string normalized;
  if (uri.scheme == "package") {
    path = std::filesystem::path(resolve_path(Json(value), directory, where));
    normalized = value;
  } else if (uri.scheme == "file") {
    if (!uri.netloc.empty() || uri.path.empty() || uri.path[0] != '/') {
      throw ConfigurationError(
          where + ": file resource must be an absolute local file URI without host");
    }
    path = std::filesystem::path(percent_decode(uri.path));
    std::error_code error;
    path = std::filesystem::weakly_canonical(path, error);
    if (error) path = path.lexically_normal();
    normalized = "file://" + percent_encode_path(path.string());
  } else if (!uri.scheme.empty() || !uri.netloc.empty()) {
    throw ConfigurationError(where + ": remote resource URI is not supported");
  } else {
    path = std::filesystem::path(resolve_path(Json(value), directory, where));
    normalized = "file://" + percent_encode_path(path.string());
  }
  if (!std::filesystem::is_regular_file(path)) {
    throw ConfigurationError(where + ": mesh/texture resource does not exist: " + path.string());
  }
  return normalized;
}

// ---------------------------------------------------------------------------
// URDF document handling.
// ---------------------------------------------------------------------------

bool is_xacro_namespace(const std::string& uri) {
  for (const char* candidate : kXacroNamespaces) {
    if (uri.rfind(candidate, 0) == 0) return true;
  }
  return false;
}

std::vector<const XmlElement*> find_children(const XmlElement& parent, const std::string& name) {
  std::vector<const XmlElement*> result;
  for (const auto& child : parent.children) {
    if (child->name == name && child->namespace_uri.empty()) result.push_back(child.get());
  }
  return result;
}

const XmlElement* find_child(const XmlElement& parent, const std::string& name) {
  const std::vector<const XmlElement*> found = find_children(parent, name);
  return found.empty() ? nullptr : found.front();
}

std::string attribute_of(const XmlElement& element, const std::string& name) {
  for (const auto& attribute : element.attributes) {
    if (attribute.name == name && attribute.namespace_uri.empty()) return attribute.value;
  }
  return std::string();
}

bool has_attribute(const XmlElement& element, const std::string& name) {
  for (const auto& attribute : element.attributes) {
    if (attribute.name == name && attribute.namespace_uri.empty()) return true;
  }
  return false;
}

// Read a required, nonempty attribute.
std::string required_attribute(const XmlElement& element, const std::string& name,
                               const std::string& where) {
  const std::string value = attribute_of(element, name);
  if (value.empty()) {
    throw ConfigurationError(where + ": invalid nonempty string '" + value + "'");
  }
  return value;
}

// Parse, validate and normalize one URDF document.
std::pair<std::unique_ptr<XmlDocument>, std::string> parse_urdf(const std::string& xml,
                                                               const std::string& directory,
                                                               const std::string& where) {
  reject_entities(xml, where);
  auto document = XmlDocument::parse(xml, where);
  const XmlElement& root = document->root();
  if (root.name != "robot" || !root.namespace_uri.empty()) {
    throw ConfigurationError(where + ": URDF root must be robot");
  }
  for (const XmlElement* element : document->elements()) {
    if (is_xacro_namespace(element->namespace_uri)) {
      throw ConfigurationError(
          where + ": unexpanded Xacro element or attribute; declare format: xacro");
    }
    for (const auto& attribute : element->attributes) {
      if (is_xacro_namespace(attribute.namespace_uri)) {
        throw ConfigurationError(
            where + ": unexpanded Xacro element or attribute; declare format: xacro");
      }
    }
    if (element->name == "mesh" || element->name == "texture") {
      if (!has_attribute(*element, "filename")) {
        throw ConfigurationError(where + ": " + element->name + " requires filename");
      }
      const std::string resolved = resource(attribute_of(*element, "filename"), directory, where);
      document->set_attribute(*element, "filename", resolved);
    }
  }
  const std::string normalized = document->serialize();
  if (normalized.size() > kXmlLimit) {
    throw ConfigurationError(where + ": expanded URDF exceeds 4 MiB");
  }
  return {std::move(document), normalized};
}

// ---------------------------------------------------------------------------
// Structure validation.
// ---------------------------------------------------------------------------

void acyclic(const std::map<std::string, std::string>& parents, const std::string& message) {
  std::set<std::string> complete;
  for (const auto& entry : parents) {
    std::string frame = entry.first;
    std::set<std::string> seen;
    while (parents.count(frame) != 0 && complete.count(frame) == 0) {
      if (!seen.insert(frame).second) {
        throw ConfigurationError(message + ": " + frame);
      }
      frame = parents.at(frame);
    }
    complete.insert(seen.begin(), seen.end());
  }
}

struct Structure {
  std::vector<std::string> links;
  std::vector<Json> joints;
  std::string root;
};

Structure structure(const XmlElement& root, const std::string& prefix, const std::string& where) {
  Structure result;
  std::set<std::string> link_names;
  for (const XmlElement* link : find_children(root, "link")) {
    const std::string name = required_attribute(*link, "name", where + ".link.name");
    const std::string frame = prefix + name;
    text(frame, where + ".link frame", &kFrameRe);
    if (link_names.count(frame) != 0) {
      throw ConfigurationError(where + ": duplicate URDF link " + frame);
    }
    result.links.push_back(frame);
    link_names.insert(frame);
  }
  if (result.links.empty()) {
    throw ConfigurationError(where + ": URDF needs at least one link");
  }
  std::set<std::string> joint_names;
  std::map<std::string, std::string> parents;
  std::map<std::string, std::string> joint_types;
  for (const XmlElement* joint : find_children(root, "joint")) {
    const std::string name = required_attribute(*joint, "name", where + ".joint.name");
    if (joint_names.count(name) != 0) {
      throw ConfigurationError(where + ": duplicate URDF joint " + name);
    }
    joint_names.insert(name);
    const std::string type = attribute_of(*joint, "type");
    if (type != "fixed" && type != "revolute" && type != "continuous" && type != "prismatic") {
      throw ConfigurationError(where + ": joint " + name + " has unsupported type '" + type +
                               "'; supported types are fixed, revolute, continuous and prismatic");
    }
    const XmlElement* parent = find_child(*joint, "parent");
    const XmlElement* child = find_child(*joint, "child");
    if (parent == nullptr || child == nullptr) {
      throw ConfigurationError(where + ": joint " + name + " requires parent and child");
    }
    const std::string parent_frame =
        prefix + required_attribute(*parent, "link", where + ".joint.parent");
    const std::string child_frame =
        prefix + required_attribute(*child, "link", where + ".joint.child");
    if (link_names.count(parent_frame) == 0 || link_names.count(child_frame) == 0) {
      throw ConfigurationError(where + ": joint " + name + " references an unknown link");
    }
    if (parents.count(child_frame) != 0) {
      throw ConfigurationError(where + ": duplicate URDF joint child " + child_frame);
    }
    parents[child_frame] = parent_frame;
    joint_types[name] = type;
    Json entry = Json::object();
    entry["name"] = name;
    entry["type"] = type;
    entry["parent"] = parent_frame;
    entry["child"] = child_frame;
    result.joints.push_back(entry);
  }
  acyclic(parents, where + ": URDF transform cycle");
  std::set<std::string> roots;
  for (const std::string& link : result.links) {
    if (parents.count(link) == 0) roots.insert(link);
  }
  if (roots.size() != 1) {
    throw ConfigurationError(where + ": URDF must form a single rooted tree");
  }
  std::map<std::string, std::string> mimics;
  for (const XmlElement* joint : find_children(root, "joint")) {
    const XmlElement* mimic = find_child(*joint, "mimic");
    if (mimic == nullptr) continue;
    const std::string name = attribute_of(*joint, "name");
    const std::string target = attribute_of(*mimic, "joint");
    if (joint_types.count(target) == 0 || joint_types.at(target) == "fixed" ||
        joint_types.at(name) == "fixed") {
      throw ConfigurationError(where + ": mimic joint " + name +
                               " must reference an existing movable joint");
    }
    mimics[name] = target;
  }
  acyclic(mimics, where + ": mimic joint cycle");
  // The robot state publisher evaluates mimic values in one ordered pass, so
  // a chain is order dependent; only direct mimic relations are accepted.
  for (const auto& entry : mimics) {
    if (mimics.count(entry.second) != 0) {
      throw ConfigurationError(
          where +
          ": chained mimic joints are not supported; use a direct movable joint reference");
    }
  }
  result.root = *roots.begin();
  return result;
}

// Validate the document with the native urdfdom parser.
void native_validate(const std::string& xml, const std::string& where) {
  try {
    const urdf::ModelInterfaceSharedPtr model = urdf::parseURDF(xml);
    if (!model) {
      throw ConfigurationError(where + ": invalid URDF: the model could not be built");
    }
    if (model->getRoot() == nullptr) {
      throw ConfigurationError(where + ": invalid URDF: the model has no root link");
    }
    std::vector<urdf::LinkSharedPtr> links;
    model->getLinks(links);
    if (links.empty()) {
      throw ConfigurationError(where + ": invalid URDF: the model has no links");
    }
  } catch (const ConfigurationError&) {
    throw;
  } catch (const std::exception& error) {
    throw ConfigurationError(where + ": invalid URDF: " + error.what());
  }
}

// ---------------------------------------------------------------------------
// Xacro expansion through the third-party xacro tool.
// ---------------------------------------------------------------------------

// A temporary file that is removed when the scope ends.
class ScopedTempFile {
 public:
  ScopedTempFile() = default;
  ~ScopedTempFile() { remove(); }
  ScopedTempFile(const ScopedTempFile&) = delete;
  ScopedTempFile& operator=(const ScopedTempFile&) = delete;

  const std::string& path() const { return path_; }

  void write(const std::string& content) {
    char pattern[] = "/tmp/chaos-xacro-XXXXXX";
    const int fd = ::mkstemp(pattern);
    if (fd < 0) {
      throw ConfigurationError("failed to create a temporary xacro request file: " +
                               std::string(std::strerror(errno)));
    }
    path_ = pattern;
    ::fchmod(fd, S_IRUSR | S_IWUSR);
    const size_t written = ::write(fd, content.data(), content.size());
    const bool ok = written == content.size();
    ::close(fd);
    if (!ok) {
      remove();
      throw ConfigurationError("failed to write the temporary xacro request file");
    }
  }

  void remove() {
    if (!path_.empty()) {
      ::unlink(path_.c_str());
      path_.clear();
    }
  }

 private:
  std::string path_;
};

std::string xacro_bridge_script() {
  // An explicit override keeps the expansion testable without a full ROS
  // installation and lets an integrator ship a patched bridge.
  const char* override_path = std::getenv("CHAOS_XACRO_BRIDGE");
  if (override_path != nullptr && *override_path != '\0') {
    if (!std::filesystem::is_regular_file(override_path)) {
      throw ConfigurationError("the xacro bridge script is unavailable: " +
                               std::string(override_path));
    }
    return override_path;
  }
  std::string share;
  try {
    share = ament_index_cpp::get_package_share_directory("chaos_assets");
  } catch (const std::exception&) {
    throw ConfigurationError(
        "the xacro bridge script is unavailable; source the installed ROS workspace");
  }
  const std::filesystem::path script = std::filesystem::path(share) / "tools" / "xacro_expand.py";
  if (!std::filesystem::is_regular_file(script)) {
    throw ConfigurationError("the xacro bridge script is unavailable: " + script.string());
  }
  return script.string();
}

std::string expand_xacro(const std::string& path, const std::string& source,
                          const Json& arguments, const std::string& where) {
  Json request = Json::object();
  request["file"] = path;
  request["source"] = source;
  Json mappings = Json::object();
  for (auto it = arguments.begin(); it != arguments.end(); ++it) {
    if (it.value().is_boolean()) {
      mappings[it.key()] = it.value().get<bool>() ? "true" : "false";
    } else if (it.value().is_string()) {
      mappings[it.key()] = it.value().get_ref<const std::string&>();
    } else {
      mappings[it.key()] = it.value().dump();
    }
  }
  request["arguments"] = mappings;

  ScopedTempFile request_file;
  request_file.write(request.dump());
  const std::string script = xacro_bridge_script();
  const ProcessResult result =
      run_bounded({"python3", script, request_file.path()}, where,
                  std::chrono::milliseconds(kProcessTimeoutMs), kXmlLimit);
  if (result.exit_code != 0) {
    std::string detail = result.err.empty() ? result.out : result.err;
    if (detail.size() > 2048) detail.resize(2048);
    while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r')) detail.pop_back();
    throw ConfigurationError(where + ": validation/expansion failed: " + detail);
  }
  return result.out;
}

}  // namespace

Json resolve_description(const Json& model_description, const Json& instance_description,
                         const std::string& model_directory, const std::string& inventory_directory,
                         const Json& asset) {
  if (model_description.is_null() && instance_description.is_null()) {
    return Json();
  }
  const std::string asset_id = asset.at("id").get_ref<const std::string&>();
  const std::string where = asset_id + ".description";
  Json model = declaration(model_description.is_null() ? Json(Json::object()) : model_description,
                            model_directory, where + " (model)");
  Json instance = declaration(instance_description.is_null() ? Json(Json::object()) : instance_description,
                               inventory_directory, where);
  // Parameters and an explicit format belong to their source template.
  // Selecting another template must not inherit incompatible Xacro arguments.
  if (instance.contains("file") && instance.at("file") != model.value("file", Json())) {
    model.erase("arguments");
    if (!instance.contains("format")) model.erase("format");
  }
  Json description = deep_merge(model, instance);
  auto enabled = description.find("enabled");
  if (enabled != description.end() && !enabled->is_boolean()) {
    throw ConfigurationError(where + ".enabled: expected boolean");
  }
  if (enabled != description.end() && !enabled->get<bool>()) {
    Json disabled = Json::object();
    disabled["enabled"] = false;
    return disabled;
  }
  const std::string path =
      text(description.contains("file") ? description.at("file") : Json(), where + ".file");
  std::string format_name;
  if (description.contains("format")) {
    format_name = description.at("format").get_ref<const std::string&>();
  } else {
    const std::filesystem::path suffix(std::filesystem::path(path).extension().string());
    format_name = suffix.string().substr(1);
  }
  if (format_name != "urdf" && format_name != "xacro") {
    throw ConfigurationError(where + ".format: cannot infer format; expected urdf or xacro");
  }
  Json arguments = description.contains("arguments") ? description.at("arguments") : Json(Json::object());
  if (format_name == "urdf" && !arguments.empty()) {
    throw ConfigurationError(where + ".arguments: only Xacro accepts arguments");
  }
  const std::string source = read_source(path, where);
  reject_entities(source, where);
  std::string xml = source;
  if (format_name == "xacro") {
    xml = expand_xacro(path, source, arguments, where);
  }
  const auto parsed = parse_urdf(xml, std::filesystem::path(path).parent_path().string(), where);
  const std::string& normalized = parsed.second;
  // Check the tree before urdfdom: a malformed cyclic graph can trigger a
  // native recursive traversal in some urdfdom versions.
  const std::string prefix = description.contains("frame_prefix")
                                 ? description.at("frame_prefix").get_ref<const std::string&>()
                                 : asset_id + "/";
  const Structure info = structure(parsed.first->root(), prefix, where);
  native_validate(normalized, where);
  if (info.root != asset.at("frames").at("base").get_ref<const std::string&>()) {
    throw ConfigurationError(where + ": root frame '" + info.root +
                             "' must equal asset.frames.base '" +
                             asset.at("frames").at("base").get_ref<const std::string&>() + "'");
  }

  Json result = Json::object();
  result["enabled"] = true;
  result["file"] = path;
  result["format"] = format_name;
  result["arguments"] = arguments;
  result["frame_prefix"] = prefix;
  result["publish_frequency"] = description.contains("publish_frequency")
                                    ? finite_number(description.at("publish_frequency"),
                                                    where + ".publish_frequency")
                                    : 30.0;
  const std::string namespace_ = asset.at("namespace").get_ref<const std::string&>();
  result["joint_states_topic"] = resolve_topic(
      description.contains("joint_states_topic") ? description.at("joint_states_topic")
                                                 : Json("joint_states"),
      namespace_, where + ".joint_states_topic");
  result["use_sim_time"] = description.contains("use_sim_time") ? description.at("use_sim_time")
                                                                : Json(false);
  result["xml"] = normalized;
  result["root"] = info.root;
  result["links"] = info.links;
  result["joints"] = info.joints;
  result["sha256"] = sha256_hex(normalized);
  result["source_sha256"] = sha256_hex(source);
  return result;
}

void validate_description_graph(const std::vector<Json>& assets) {
  // Combine robot description joints and sensor mounting transforms within
  // each DDS domain.
  using Domain = int64_t;
  std::map<std::pair<Domain, std::string>, std::vector<std::pair<std::string, std::string>>> reserved;
  std::map<std::pair<Domain, std::string>, std::string> links;
  std::map<std::pair<Domain, std::string>, std::string> transforms;
  for (const Json& asset : assets) {
    if (!asset.at("enabled").get<bool>()) continue;
    const Domain domain = asset.at("ros_domain_id").get<int64_t>();
    const std::string owner = asset.at("id").get_ref<const std::string&>();
    for (auto it = asset.at("frames").begin(); it != asset.at("frames").end(); ++it) {
      reserved[{domain, it.value().get<std::string>()}].emplace_back(owner, it.key());
    }
  }
  for (const Json& asset : assets) {
    if (!asset.at("enabled").get<bool>()) continue;
    const Json& description = asset.at("description");
    if (description.is_null() || !description.at("enabled").get<bool>()) continue;
    const Domain domain = asset.at("ros_domain_id").get<int64_t>();
    const std::string owner = asset.at("id").get_ref<const std::string&>();
    for (const Json& link : description.at("links")) {
      const std::string frame = link.get<std::string>();
      const auto key = std::make_pair(domain, frame);
      const auto found = reserved.find(key);
      if (found != reserved.end()) {
        for (const auto& entry : found->second) {
          if (entry.first != owner || entry.second != "base") {
            throw ConfigurationError(owner + ".description: URDF link " + frame +
                                     " occupies reserved " + entry.first + ".frames." + entry.second);
          }
        }
      }
      if (links.count(key) != 0) {
        throw ConfigurationError("duplicate URDF frame in ROS domain " + std::to_string(domain) +
                                 ": " + frame + " (" + links.at(key) + ", " + owner + ")");
      }
      links[key] = owner;
    }
    for (const Json& joint : description.at("joints")) {
      transforms[{domain, joint.at("child").get<std::string>()}] = joint.at("parent").get<std::string>();
    }
  }
  for (const Json& asset : assets) {
    if (!asset.at("enabled").get<bool>()) continue;
    const Domain domain = asset.at("ros_domain_id").get<int64_t>();
    const std::string owner = asset.at("id").get_ref<const std::string&>();
    for (auto it = asset.at("sensors").begin(); it != asset.at("sensors").end(); ++it) {
      const Json& sensor = it.value();
      if (!sensor.at("enabled").get<bool>()) continue;
      std::set<std::string> frames{sensor.at("frame_id").get<std::string>()};
      for (auto stream = sensor.at("streams").begin(); stream != sensor.at("streams").end();
           ++stream) {
        frames.insert(stream.value().at("frame_id").get<std::string>());
      }
      for (const std::string& frame : frames) {
        const auto found = links.find({domain, frame});
        if (found != links.end() && found->second != owner) {
          throw ConfigurationError(owner + ".sensors." + it.key() + ": frame " + frame +
                                   " belongs to another asset URDF (" + found->second + ")");
        }
      }
      auto transform = sensor.find("transform");
      if (transform != sensor.end()) {
        const auto child = std::make_pair(domain, sensor.at("frame_id").get<std::string>());
        if (transforms.count(child) != 0) {
          throw ConfigurationError("duplicate transform publisher in ROS domain " +
                                   std::to_string(domain) + ": " + child.second +
                                   " is already published by URDF");
        }
        transforms[child] = transform->at("parent").get<std::string>();
      }
    }
  }
  std::map<std::string, std::string> transform_parents;
  for (const auto& entry : transforms) {
    transform_parents[entry.first.second] = entry.second;
  }
  acyclic(transform_parents, "URDF/sensor transform cycle");
}

}  // namespace chaos::assets
