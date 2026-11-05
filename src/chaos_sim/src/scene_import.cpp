#include "chaos_sim/scene.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

double number(const std::string& value, const char* option) {
  std::size_t used = 0;
  const double result = std::stod(value, &used);
  if (used != value.size()) throw std::invalid_argument(std::string("Invalid value for ") + option);
  return result;
}

std::size_t integer(const std::string& value) {
  if (value.empty() || value[0] == '-' || value[0] == '+') {
    throw std::invalid_argument("--max-geoms must be a positive integer");
  }
  std::size_t used = 0;
  const auto result = std::stoull(value, &used);
  if (used != value.size() || result == 0) {
    throw std::invalid_argument("--max-geoms must be a positive integer");
  }
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 4) {
      throw std::invalid_argument(
          "Usage: scene_import ROBOT_XML SCENE.ply|pcd OUTPUT.xml "
          "[--voxel-size S] [--point-radius R] [--max-geoms N] [--no-collision]");
    }
    chaos::sim::WorldConfig config;
    config.model_path = argv[1];
    config.scene.path = argv[2];
    config.discard_visual = false;
    for (int i = 4; i < argc; ++i) {
      const std::string option = argv[i];
      if (option == "--no-collision") {
        config.scene.collision = false;
      } else {
        if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + option);
        const std::string value = argv[++i];
        if (option == "--voxel-size") config.scene.voxel_size = number(value, "--voxel-size");
        else if (option == "--point-radius") config.scene.point_radius = number(value, "--point-radius");
        else if (option == "--max-geoms") config.scene.max_geoms = integer(value);
        else throw std::invalid_argument("Unknown option: " + option);
      }
    }
    const auto stats = chaos::sim::exportSceneModel(config, argv[3]);
    std::cout << "{\"input_points\":" << stats.input_points << ",\"geoms\":" << stats.geoms
              << ",\"gaussian\":" << (stats.gaussian ? "true" : "false") << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "scene_import: " << error.what() << '\n';
    return 1;
  }
}
