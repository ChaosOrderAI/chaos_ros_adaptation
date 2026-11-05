#include "scene_internal.hpp"

#include <mujoco/mujoco.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chaos::sim {
namespace {

void require(bool condition, const std::string& message) {
  if (!condition) throw std::invalid_argument(message);
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string trimCR(std::string value) {
  if (!value.empty() && value.back() == '\r') value.pop_back();
  return value;
}

enum class Type { I8, U8, I16, U16, I32, U32, F32, F64 };

Type scalarType(std::string name) {
  static const std::unordered_map<std::string, Type> types = {
    {"char", Type::I8}, {"int8", Type::I8}, {"uchar", Type::U8}, {"uint8", Type::U8},
    {"short", Type::I16}, {"int16", Type::I16}, {"ushort", Type::U16}, {"uint16", Type::U16},
    {"int", Type::I32}, {"int32", Type::I32}, {"uint", Type::U32}, {"uint32", Type::U32},
    {"float", Type::F32}, {"float32", Type::F32}, {"double", Type::F64}, {"float64", Type::F64},
  };
  const auto found = types.find(lower(name));
  require(found != types.end(), "Unsupported scalar type: " + name);
  return found->second;
}

std::size_t typeSize(Type type) {
  switch (type) {
    case Type::I8: case Type::U8: return 1;
    case Type::I16: case Type::U16: return 2;
    case Type::I32: case Type::U32: case Type::F32: return 4;
    case Type::F64: return 8;
  }
  throw std::logic_error("Unknown scalar type");
}

template <typename T>
double readBinaryValue(std::istream& input) {
  T value;
  input.read(reinterpret_cast<char*>(&value), sizeof(value));
  require(static_cast<bool>(input), "Unexpected end of binary point cloud");
  return static_cast<double>(value);
}

double readScalar(std::istream& input, Type type, bool binary) {
  if (binary) {
    switch (type) {
      case Type::I8: return readBinaryValue<std::int8_t>(input);
      case Type::U8: return readBinaryValue<std::uint8_t>(input);
      case Type::I16: return readBinaryValue<std::int16_t>(input);
      case Type::U16: return readBinaryValue<std::uint16_t>(input);
      case Type::I32: return readBinaryValue<std::int32_t>(input);
      case Type::U32: return readBinaryValue<std::uint32_t>(input);
      case Type::F32: return readBinaryValue<float>(input);
      case Type::F64: return readBinaryValue<double>(input);
    }
  }
  std::string token;
  require(static_cast<bool>(input >> token), "Unexpected end of ASCII point cloud");
  std::size_t used = 0;
  const double value = std::stod(token, &used);
  require(used == token.size(), "Invalid point cloud number: " + token);
  return value;
}

enum Role : std::size_t {
  X, Y, Z, RED, GREEN, BLUE, ALPHA, DC0, DC1, DC2,
  SCALE0, SCALE1, SCALE2, ROT0, ROT1, ROT2, ROT3, OPACITY, RGB, RGBA, NONE,
  ROLE_COUNT
};

Role roleOf(const std::string& name) {
  static const std::unordered_map<std::string, Role> roles = {
    {"x", X}, {"y", Y}, {"z", Z}, {"red", RED}, {"green", GREEN}, {"blue", BLUE},
    {"diffuse_red", RED}, {"diffuse_green", GREEN}, {"diffuse_blue", BLUE},
    {"alpha", ALPHA}, {"f_dc_0", DC0}, {"f_dc_1", DC1}, {"f_dc_2", DC2},
    {"scale_0", SCALE0}, {"scale_1", SCALE1}, {"scale_2", SCALE2},
    {"rot_0", ROT0}, {"rot_1", ROT1}, {"rot_2", ROT2}, {"rot_3", ROT3},
    {"opacity", OPACITY}, {"rgb", RGB}, {"rgba", RGBA},
  };
  const auto found = roles.find(lower(name));
  return found == roles.end() ? NONE : found->second;
}

bool colorChannel(Role role) {
  return role == RED || role == GREEN || role == BLUE || role == ALPHA;
}

// Integer channels scale by their storage width. Float channels in [0, 1] are
// already normalized; larger floats are the 0–255 convention.
double normalizeChannel(double value, Type type) {
  if (!std::isfinite(value)) return value;
  if (type == Type::F32 || type == Type::F64) {
    if (value <= 1.0) return std::clamp(value, 0.0, 1.0);
    return std::clamp(value / 255.0, 0.0, 1.0);
  }
  double width = 255.0;
  switch (type) {
    case Type::I16: case Type::U16: width = 65535.0; break;
    case Type::I32: case Type::U32: width = 4294967295.0; break;
    default: break;
  }
  return std::clamp(value / width, 0.0, 1.0);
}

void storeRole(std::array<double, ROLE_COUNT>& values, std::array<bool, ROLE_COUNT>& present,
               Role role, double value, Type type, bool binary_packed_float) {
  if (role == NONE) return;
  if (colorChannel(role)) value = normalizeChannel(value, type);
  // Binary PCD stores packed rgb/rgba as the bits of a float. ASCII writes the
  // same packing as a decimal integer, so only the binary float form is bit-cast.
  if ((role == RGB || role == RGBA) && binary_packed_float && type == Type::F32 && std::isfinite(value)) {
    const float color = static_cast<float>(value);
    std::uint32_t packed = 0;
    std::memcpy(&packed, &color, sizeof(packed));
    value = packed;
  }
  values[role] = value;
  present[role] = true;
}

struct Point {
  std::array<double, 3> position{};
  std::array<double, 3> size{};
  std::array<double, 4> quat{1, 0, 0, 0};
  std::array<float, 4> rgba{0.65F, 0.65F, 0.65F, 1.0F};
  bool ellipsoid = false;
};

struct VoxelKey {
  std::int64_t x, y, z;
  bool operator==(const VoxelKey& other) const { return x == other.x && y == other.y && z == other.z; }
};

struct VoxelHash {
  std::size_t operator()(const VoxelKey& key) const {
    std::size_t hash = 0;
    for (const auto value : {key.x, key.y, key.z}) {
      const auto bits = static_cast<std::uint64_t>(value);
      hash ^= std::hash<std::uint64_t>{}(bits) + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    }
    return hash;
  }
};

class PointCollector {
 public:
  explicit PointCollector(const SceneConfig& config) : config_(config) {
    require(config.voxel_size >= 0 && std::isfinite(config.voxel_size),
            "scene_voxel_size must be finite and nonnegative");
    require(config.point_radius > 0 && std::isfinite(config.point_radius),
            "scene_point_radius must be positive and finite");
    require(config.max_geoms > 0, "scene_max_geoms must be positive");
  }

  void add(const std::array<double, ROLE_COUNT>& values, const std::array<bool, ROLE_COUNT>& present) {
    for (const auto role : {X, Y, Z}) require(present[role], "Point cloud requires x/y/z fields");
    if (!std::isfinite(values[X]) || !std::isfinite(values[Y]) || !std::isfinite(values[Z])) {
      ++invalid_;
      return;
    }
    const bool gaussian = present[SCALE0] && present[SCALE1] && present[SCALE2];
    if (gaussian) {
      for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(values[SCALE0 + i])) {
          ++invalid_;
          return;
        }
      }
    }
    ++input_;
    Point point;
    point.position = {values[X], values[Y], values[Z]};
    point.size = {config_.point_radius, config_.point_radius, config_.point_radius};
    setColor(point, values, present);
    if (gaussian) {
      gaussian_ = true;
      point.ellipsoid = true;
      for (int i = 0; i < 3; ++i) {
        point.size[i] = std::clamp(std::exp(values[SCALE0 + i]), 1e-4, 10.0);
      }
      if (present[ROT0] && present[ROT1] && present[ROT2] && present[ROT3]) {
        const double norm = std::sqrt(values[ROT0] * values[ROT0] + values[ROT1] * values[ROT1] +
                                      values[ROT2] * values[ROT2] + values[ROT3] * values[ROT3]);
        if (std::isfinite(norm) && norm > 1e-12) {
          for (int i = 0; i < 4; ++i) point.quat[i] = values[ROT0 + i] / norm;
        }
      }
      if (present[OPACITY] && std::isfinite(values[OPACITY])) {
        point.rgba[3] = static_cast<float>(1.0 / (1.0 + std::exp(-values[OPACITY])));
      }
    }
    if (config_.voxel_size == 0) {
      append(std::move(point));
      return;
    }
    std::array<std::int64_t, 3> cell{};
    for (int i = 0; i < 3; ++i) {
      const double scaled = point.position[i] / config_.voxel_size;
      const double nearest = std::round(scaled);
      // 0.3/0.1 is 2.9999999999999996 in IEEE-754. Snap only that kind of
      // representation error, not a point that is genuinely below the boundary.
      const double tolerance = 1e-9 * std::max(1.0, std::abs(nearest));
      const double coordinate = std::abs(scaled - nearest) <= tolerance ? nearest : std::floor(scaled);
      // int64 max is not exact as a double, so reject 2^63 before the cast.
      require(std::isfinite(coordinate) && coordinate >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
                  coordinate < 9223372036854775808.0,
              "Point coordinate exceeds voxel index range");
      cell[i] = static_cast<std::int64_t>(coordinate);
    }
    const VoxelKey key{cell[0], cell[1], cell[2]};
    if (indices_.find(key) != indices_.end()) return;
    indices_.emplace(key, points_.size());
    append(std::move(point));
  }

  std::vector<Point> finish() {
    require(!points_.empty(), "Point cloud contains no finite points");
    return std::move(points_);
  }

  std::size_t input() const { return input_; }
  std::size_t invalid() const { return invalid_; }
  bool gaussian() const { return gaussian_; }

 private:
  void append(Point&& point) {
    if (points_.size() >= config_.max_geoms) {
      throw std::invalid_argument("Scene exceeds scene_max_geoms=" + std::to_string(config_.max_geoms) +
                                  "; increase scene_voxel_size or explicitly raise the limit");
    }
    points_.push_back(std::move(point));
  }

  static void setColor(Point& point, const std::array<double, ROLE_COUNT>& values,
                       const std::array<bool, ROLE_COUNT>& present) {
    if (present[RGB] || present[RGBA]) {
      const Role role = present[RGBA] ? RGBA : RGB;
      std::uint32_t packed = 0;
      if (std::isfinite(values[role]) && values[role] >= 0 &&
          values[role] <= std::numeric_limits<std::uint32_t>::max()) {
        packed = static_cast<std::uint32_t>(values[role]);
      } else if (std::isfinite(values[role])) {
        const float color = static_cast<float>(values[role]);
        std::memcpy(&packed, &color, sizeof(packed));
      }
      point.rgba[0] = ((packed >> 16) & 0xff) / 255.0F;
      point.rgba[1] = ((packed >> 8) & 0xff) / 255.0F;
      point.rgba[2] = (packed & 0xff) / 255.0F;
      if (present[RGBA]) point.rgba[3] = ((packed >> 24) & 0xff) / 255.0F;
      return;
    }
    const bool direct = present[RED] && present[GREEN] && present[BLUE];
    const bool spherical = present[DC0] && present[DC1] && present[DC2];
    if (!direct && !spherical) return;
    for (int i = 0; i < 3; ++i) {
      const Role role = direct ? static_cast<Role>(RED + i) : static_cast<Role>(DC0 + i);
      const double color = values[role];
      if (std::isfinite(color)) {
        const double normalized = spherical ? 0.5 + 0.28209479177387814 * color : color;
        point.rgba[i] = static_cast<float>(std::clamp(normalized, 0.0, 1.0));
      }
    }
    if (present[ALPHA] && std::isfinite(values[ALPHA])) {
      point.rgba[3] = static_cast<float>(std::clamp(values[ALPHA], 0.0, 1.0));
    }
  }

  const SceneConfig& config_;
  std::vector<Point> points_;
  std::unordered_map<VoxelKey, std::size_t, VoxelHash> indices_;
  std::size_t input_ = 0, invalid_ = 0;
  bool gaussian_ = false;
};

struct PlyProperty {
  Type type;
  Type index_type;
  Role role = NONE;
  bool list = false;
};

struct PlyElement {
  std::string name;
  std::size_t count = 0;
  std::vector<PlyProperty> properties;
};

std::vector<Point> readPly(std::istream& input, PointCollector& collector) {
  std::string line;
  require(static_cast<bool>(std::getline(input, line)) && trimCR(line) == "ply", "PLY magic is missing");
  bool binary = false, ended = false;
  std::size_t header_bytes = 3;
  std::vector<PlyElement> elements;
  while (std::getline(input, line)) {
    line = trimCR(line);
    header_bytes += line.size() + 1;
    require(header_bytes <= 1024 * 1024, "PLY header exceeds 1 MiB");
    std::istringstream tokens(line);
    std::string keyword;
    tokens >> keyword;
    if (keyword == "format") {
      std::string format, version;
      tokens >> format >> version;
      require(version == "1.0", "Only PLY format 1.0 is supported");
      require(format == "ascii" || format == "binary_little_endian",
              "Only ASCII and binary_little_endian PLY are supported");
      binary = format == "binary_little_endian";
    } else if (keyword == "element") {
      PlyElement element;
      tokens >> element.name >> element.count;
      require(static_cast<bool>(tokens), "Invalid PLY element declaration");
      elements.push_back(std::move(element));
    } else if (keyword == "property") {
      require(!elements.empty(), "PLY property declared before an element");
      std::string type;
      tokens >> type;
      PlyProperty property{};
      if (type == "list") {
        std::string count_type, index_type, name;
        tokens >> count_type >> index_type >> name;
        require(static_cast<bool>(tokens), "Invalid PLY list property");
        property.type = scalarType(count_type);
        property.index_type = scalarType(index_type);
        property.list = true;
      } else {
        std::string name;
        tokens >> name;
        require(static_cast<bool>(tokens), "Invalid PLY scalar property");
        property.type = scalarType(type);
        property.role = roleOf(name);
      }
      elements.back().properties.push_back(property);
    } else if (keyword == "end_header") {
      ended = true;
      break;
    }
  }
  require(ended && !elements.empty(), "Incomplete PLY header");
  for (const auto& element : elements) {
    const bool vertex = element.name == "vertex";
    for (std::size_t row = 0; row < element.count; ++row) {
      std::array<double, ROLE_COUNT> values{};
      std::array<bool, ROLE_COUNT> present{};
      for (const auto& property : element.properties) {
        if (property.list) {
          require(!vertex, "List properties are not supported on PLY vertices");
          const double count = readScalar(input, property.type, binary);
          require(std::isfinite(count) && count >= 0 && count <= 1000000 &&
                      count == std::floor(count), "Invalid PLY list length");
          for (int i = 0; i < static_cast<int>(count); ++i) readScalar(input, property.index_type, binary);
        } else {
          const double value = readScalar(input, property.type, binary);
          if (vertex) storeRole(values, present, property.role, value, property.type, false);
        }
      }
      if (vertex) collector.add(values, present);
    }
  }
  return collector.finish();
}

struct PcdField {
  Type type;
  Role role;
  char kind = 'F';
  int size = 4;
  int count = 1;
};

char pcdKind(char type) {
  const char kind = static_cast<char>(std::toupper(static_cast<unsigned char>(type)));
  require(kind == 'F' || kind == 'I' || kind == 'U', std::string("Unsupported PCD type: ") + type);
  return kind;
}

Type pcdScalarType(char kind, int size) {
  if (kind == 'F') {
    require(size == 4 || size == 8, "PCD float SIZE must be 4 or 8");
    return size == 4 ? Type::F32 : Type::F64;
  }
  require(size == 1 || size == 2 || size == 4 || size == 8, "PCD integer SIZE must be 1, 2, 4, or 8");
  if (kind == 'I') {
    if (size == 1) return Type::I8;
    if (size == 2) return Type::I16;
    return Type::I32;
  }
  if (size == 1) return Type::U8;
  if (size == 2) return Type::U16;
  return Type::U32;
}

double readPcdBinary(std::istream& input, char kind, int size) {
  unsigned char bytes[8]{};
  input.read(reinterpret_cast<char*>(bytes), size);
  require(static_cast<bool>(input), "Unexpected end of binary point cloud");
  auto load = [&](auto value) {
    std::memcpy(&value, bytes, sizeof(value));
    return static_cast<double>(value);
  };
  if (kind == 'F' && size == 4) return load(float{});
  if (kind == 'F' && size == 8) return load(double{});
  if (kind == 'I' && size == 1) return load(std::int8_t{});
  if (kind == 'I' && size == 2) return load(std::int16_t{});
  if (kind == 'I' && size == 4) return load(std::int32_t{});
  if (kind == 'I' && size == 8) return load(std::int64_t{});
  if (kind == 'U' && size == 1) return load(std::uint8_t{});
  if (kind == 'U' && size == 2) return load(std::uint16_t{});
  if (kind == 'U' && size == 4) return load(std::uint32_t{});
  if (kind == 'U' && size == 8) return load(std::uint64_t{});
  throw std::invalid_argument("Unsupported PCD type/size combination");
}

std::vector<Point> readPcd(std::istream& input, PointCollector& collector) {
  std::string line, data_mode;
  std::vector<std::string> names;
  std::vector<int> sizes, counts;
  std::vector<char> types;
  std::size_t declared_points = 0;
  bool ended = false;
  while (std::getline(input, line)) {
    line = trimCR(line);
    if (line.empty() || line[0] == '#') continue;
    std::istringstream tokens(line);
    std::string keyword;
    tokens >> keyword;
    keyword = lower(keyword);
    if (keyword == "fields") {
      std::string name;
      while (tokens >> name) names.push_back(name);
    } else if (keyword == "size" || keyword == "count") {
      auto& destination = keyword == "size" ? sizes : counts;
      int value;
      while (tokens >> value) destination.push_back(value);
    } else if (keyword == "type") {
      char value;
      while (tokens >> value) types.push_back(value);
    } else if (keyword == "points") {
      tokens >> declared_points;
    } else if (keyword == "data") {
      tokens >> data_mode;
      data_mode = lower(data_mode);
      require(data_mode != "binary_compressed",
              "PCD binary_compressed is not supported; convert it to ascii or binary first");
      require(data_mode == "ascii" || data_mode == "binary", "Unsupported PCD DATA mode: " + data_mode);
      ended = true;
      break;
    }
  }
  require(ended && declared_points > 0, "PCD header requires positive POINTS and DATA");
  require(!names.empty() && names.size() == sizes.size() && names.size() == types.size(),
          "PCD FIELDS/SIZE/TYPE lengths differ");
  if (counts.empty()) counts.assign(names.size(), 1);
  require(names.size() == counts.size(), "PCD FIELDS/COUNT lengths differ");
  std::vector<PcdField> fields;
  for (std::size_t i = 0; i < names.size(); ++i) {
    require(sizes[i] > 0 && counts[i] > 0, "PCD SIZE and COUNT must be positive");
    const char kind = pcdKind(types[i]);
    fields.push_back({pcdScalarType(kind, sizes[i]), roleOf(names[i]), kind, sizes[i], counts[i]});
  }
  const bool binary = data_mode == "binary";
  for (std::size_t row = 0; row < declared_points; ++row) {
    std::array<double, ROLE_COUNT> values{};
    std::array<bool, ROLE_COUNT> present{};
    for (const auto& field : fields) {
      for (int component = 0; component < field.count; ++component) {
        const double value = binary ? readPcdBinary(input, field.kind, field.size)
                                    : readScalar(input, field.type, false);
        if (component == 0) {
          storeRole(values, present, field.role, value, field.type, binary && field.kind == 'F' && field.size == 4);
        }
      }
    }
    collector.add(values, present);
  }
  return collector.finish();
}

std::vector<Point> readScene(const SceneConfig& config, PointCollector& collector) {
  const std::string extension = lower(std::filesystem::path(config.path).extension().string());
  require(extension == ".ply" || extension == ".pcd", "Scene must be a .ply or .pcd file");
  std::ifstream input(config.path, std::ios::binary);
  require(static_cast<bool>(input), "Cannot open scene: " + config.path);
  try {
    return extension == ".ply" ? readPly(input, collector) : readPcd(input, collector);
  } catch (const std::exception& error) {
    throw std::invalid_argument("Cannot import scene " + config.path + ": " + error.what());
  }
}

mjsBody* worldBody(mjSpec* spec) {
  mjsBody* world = mjs_findBody(spec, "world");
  require(world != nullptr, "Cannot find MuJoCo world body");
  return world;
}

bool hasFreeJoint(mjsBody* body) {
  for (mjsElement* element = mjs_firstChild(body, mjOBJ_JOINT, 0); element;
       element = mjs_nextChild(body, element, 0)) {
    const mjsJoint* joint = mjs_asJoint(element);
    if (joint && joint->type == mjJNT_FREE) return true;
  }
  return false;
}

void deleteChildren(mjSpec* robot, mjsBody* world, mjtObj type) {
  mjsElement* element = mjs_firstChild(world, type, 0);
  while (element) {
    mjsElement* next = mjs_nextChild(world, element, 0);
    require(mjs_delete(robot, element) == 0, "Cannot remove robot model environment");
    element = next;
  }
}

void stripRobotEnvironment(mjSpec* robot) {
  mjsBody* world = worldBody(robot);
  for (const auto type : {mjOBJ_GEOM, mjOBJ_LIGHT, mjOBJ_CAMERA}) deleteChildren(robot, world, type);
  bool floating_base = false;
  for (mjsElement* element = mjs_firstChild(world, mjOBJ_BODY, 0); element;
       element = mjs_nextChild(world, element, 0)) {
    if (hasFreeJoint(mjs_asBody(element))) floating_base = true;
  }
  // Only a floating base identifies the robot. Fixed-base models keep sibling bodies.
  if (!floating_base) return;
  mjsElement* element = mjs_firstChild(world, mjOBJ_BODY, 0);
  while (element) {
    mjsElement* next = mjs_nextChild(world, element, 0);
    if (!hasFreeJoint(mjs_asBody(element))) {
      require(mjs_delete(robot, element) == 0, "Cannot remove static scene body");
    }
    element = next;
  }
}

void assignSceneContact(mjsGeom* geom, bool collision) {
  // Explicit stock contact parameters. Otherwise a customized root <default>
  // class leaks condim, friction, and margin onto every imported proxy.
  geom->condim = 3;
  geom->friction[0] = 1;
  geom->friction[1] = 0.005;
  geom->friction[2] = 0.0001;
  geom->margin = 0;
  geom->gap = 0;
  geom->contype = collision ? 1 : 0;
  geom->conaffinity = collision ? 1 : 0;
  geom->group = 1;
}

}  // namespace

SpecPtr loadComposedSpec(const WorldConfig& config, SceneStats& stats) {
  char error[2048]{};
  SpecPtr robot{mj_parseXML(config.model_path.c_str(), nullptr, error, sizeof(error)), mj_deleteSpec};
  if (!robot) throw std::runtime_error("Cannot parse model: " + std::string(error));
  if (config.scene.path.empty()) {
    robot->compiler.discardvisual = config.discard_visual;
    return robot;
  }
  PointCollector collector(config.scene);
  auto points = readScene(config.scene, collector);
  stats.input_points = collector.input();
  stats.gaussian = collector.gaussian();
  // Headless simulation discards visual geoms. Display-only proxies would be
  // deleted after the official floor was stripped, leaving the robot with neither.
  const bool apply_scene = config.scene.collision || !config.discard_visual;
  if (!apply_scene) {
    stats.geoms = 0;
    robot->compiler.discardvisual = config.discard_visual;
    return robot;
  }
  stats.geoms = points.size();

  // Each World contains one robot, so adding static scene geoms directly to its
  // spec preserves all authored defaults, assets and solver options without an
  // unnecessary cross-spec attachment.
  stripRobotEnvironment(robot.get());
  robot->compiler.discardvisual = config.discard_visual;
  mjsDefault* scene_class = mjs_addDefault(robot.get(), "chaos_scene", mjs_getSpecDefault(robot.get()));
  require(scene_class != nullptr && scene_class->geom != nullptr, "Cannot allocate scene geom class");
  assignSceneContact(scene_class->geom, config.scene.collision);
  mjsBody* world = worldBody(robot.get());
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto& point = points[i];
    mjsGeom* geom = mjs_addGeom(world, scene_class);
    require(geom != nullptr, "Cannot allocate MuJoCo scene geom");
    geom->type = point.ellipsoid ? mjGEOM_ELLIPSOID : mjGEOM_SPHERE;
    std::copy(point.position.begin(), point.position.end(), geom->pos);
    std::copy(point.size.begin(), point.size.end(), geom->size);
    std::copy(point.quat.begin(), point.quat.end(), geom->quat);
    std::copy(point.rgba.begin(), point.rgba.end(), geom->rgba);
    assignSceneContact(geom, config.scene.collision);
  }
  return robot;
}

SceneStats exportSceneModel(const WorldConfig& config, const std::string& output_path) {
  require(!config.scene.path.empty(), "scene_path must not be empty when exporting a composed model");
  require(!output_path.empty(), "output_path must not be empty");
  SceneStats stats;
  WorldConfig visual = config;
  visual.discard_visual = false;
  auto spec = loadComposedSpec(visual, stats);
  // The viewer writes to a temporary directory, so preserve asset locations as
  // absolute paths instead of resolving the robot's meshdir relative to output.
  // mjString is opaque; mjs_getString owns the representation and may return null.
  const auto model_dir = std::filesystem::absolute(config.model_path).parent_path();
  auto absoluteAssetDir = [&](const mjString* source, const std::filesystem::path& fallback) {
    const char* text = source ? mjs_getString(source) : nullptr;
    const auto relative = text ? std::filesystem::path(text) : std::filesystem::path();
    return std::filesystem::absolute(relative.empty() ? fallback : model_dir / relative).lexically_normal();
  };
  const auto mesh_dir = absoluteAssetDir(spec->compiler.meshdir, model_dir);
  mjs_setString(spec->compiler.meshdir, mesh_dir.string().c_str());
  // An omitted texturedir resolves against the model file, not meshdir.
  const auto texture_dir = absoluteAssetDir(spec->compiler.texturedir, model_dir);
  mjs_setString(spec->compiler.texturedir, texture_dir.string().c_str());
  std::unique_ptr<mjModel, decltype(&mj_deleteModel)> model{mj_compile(spec.get(), nullptr), mj_deleteModel};
  require(model != nullptr, "Cannot compile composed scene: " + std::string(mjs_getError(spec.get())));
  require(mj_copyBack(spec.get(), model.get()) == 1, "Cannot prepare composed scene for XML export");
  char error[2048]{};
  std::vector<char> xml(4096);
  int result = mj_saveXMLString(spec.get(), xml.data(), xml.size(), error, sizeof(error));
  if (result > 0) {
    xml.resize(static_cast<std::size_t>(result) + 1);
    result = mj_saveXMLString(spec.get(), xml.data(), xml.size(), error, sizeof(error));
  }
  require(result == 0, "Cannot serialize composed scene: " + std::string(error));
  std::ofstream output(output_path, std::ios::binary);
  require(static_cast<bool>(output), "Cannot open composed scene output: " + output_path);
  output.write(xml.data(), std::strlen(xml.data()));
  output.flush();
  require(static_cast<bool>(output), "Cannot write composed scene output: " + output_path);
  output.close();
  require(static_cast<bool>(output), "Cannot finish composed scene output: " + output_path);
  return stats;
}

}  // namespace chaos::sim
