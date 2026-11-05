#include "chaos_sim/scene.hpp"
#include "chaos_sim/world.hpp"

#include <mujoco/mujoco.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

template <class F>
void throws(F action, const char* message) {
  bool rejected = false;
  try { action(); } catch (const std::exception&) { rejected = true; }
  check(rejected, message);
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void writeBinaryPly(const std::filesystem::path& path) {
  std::ofstream output(path, std::ios::binary);
  output << "ply\nformat binary_little_endian 1.0\nelement vertex 2\n"
            "property float x\nproperty float y\nproperty float z\n"
            "property uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n";
  for (int i = 0; i < 2; ++i) {
    const std::array<float, 3> position{static_cast<float>(i), 0.F, 0.2F};
    const std::array<std::uint8_t, 3> color{255, static_cast<std::uint8_t>(i * 255), 0};
    output.write(reinterpret_cast<const char*>(position.data()), sizeof(position));
    output.write(reinterpret_cast<const char*>(color.data()), sizeof(color));
  }
}

void writeBinaryPcd(const std::filesystem::path& path) {
  std::ofstream output(path, std::ios::binary);
  output << "VERSION 0.7\nFIELDS x y z rgb\nSIZE 4 4 4 4\nTYPE F F F U\n"
            "COUNT 1 1 1 1\nWIDTH 2\nHEIGHT 1\nPOINTS 2\nDATA binary\n";
  for (int i = 0; i < 2; ++i) {
    const std::array<float, 3> position{0.F, static_cast<float>(i), 0.3F};
    const std::uint32_t color = 0x00ff0000U >> (i * 8);
    output.write(reinterpret_cast<const char*>(position.data()), sizeof(position));
    output.write(reinterpret_cast<const char*>(&color), sizeof(color));
  }
}

}  // namespace

int main(int argc, char** argv) {
  using chaos::sim::SceneStats;
  using chaos::sim::State;
  using chaos::sim::World;
  using chaos::sim::WorldConfig;
  using chaos::sim::exportSceneModel;
  try {
    check(argc == 3, "Pass fixture and output directories");
    const std::filesystem::path fixtures = argv[1];
    const std::filesystem::path output = argv[2];
    std::filesystem::create_directories(output);

    WorldConfig cloud;
    cloud.model_path = (fixtures / "tiny.xml").string();
    cloud.scene.path = (fixtures / "cloud.ply").string();
    World world(cloud);
    check(world.scenePoints() == 5 && world.sceneGeoms() == 4, "PLY voxel downsampling failed");
    check(world.jointNames() == std::vector<std::string>({"hinge", "slide"}), "scene changed robot joints");
    State initial, state;
    world.readState(initial);
    for (int i = 0; i < 50; ++i) world.step();
    world.readState(state);
    check(std::isfinite(state.base_position[2]) && state.simulation_time > initial.simulation_time,
          "robot did not step in imported PLY scene");
    world.reset();
    world.readState(state);
    check(state.simulation_time == 0, "reset failed in imported scene");
    const auto cloud_stats = chaos::sim::exportSceneModel(cloud, (output / "cloud.xml").string());
    check(cloud_stats.geoms == 4 && !cloud_stats.gaussian, "PLY export statistics differ");
    check(readFile(output / "cloud.xml").find("scene") != std::string::npos ||
              readFile(output / "cloud.xml").find("geom") != std::string::npos,
          "composed PLY XML is empty");

    WorldConfig gaussian;
    gaussian.model_path = cloud.model_path;
    gaussian.scene.path = (fixtures / "gaussian.ply").string();
    gaussian.scene.voxel_size = 0;
    World gaussian_world(gaussian);
    check(gaussian_world.sceneGeoms() == 2, "3D Gaussian count differs");
    const SceneStats gaussian_stats = chaos::sim::exportSceneModel(
        gaussian, (output / "gaussian.xml").string());
    check(gaussian_stats.gaussian && gaussian_stats.input_points == 2, "3D Gaussian was not detected");
    const std::string gaussian_xml = readFile(output / "gaussian.xml");
    check(gaussian_xml.find("ellipsoid") != std::string::npos, "3D Gaussian ellipsoids were not exported");
    check(gaussian_xml.find("quat=") != std::string::npos, "3D Gaussian rotation was not exported");

    WorldConfig pcd = cloud;
    pcd.scene.path = (fixtures / "cloud.pcd").string();
    pcd.scene.voxel_size = 0;
    World pcd_world(pcd);
    check(pcd_world.scenePoints() == 3 && pcd_world.sceneGeoms() == 3, "ASCII PCD import failed");

    writeBinaryPly(output / "binary.ply");
    WorldConfig binary_ply = cloud;
    binary_ply.scene.path = (output / "binary.ply").string();
    binary_ply.scene.voxel_size = 0;
    World binary_ply_world(binary_ply);
    check(binary_ply_world.sceneGeoms() == 2, "binary PLY import failed");

    writeBinaryPcd(output / "binary.pcd");
    WorldConfig binary_pcd = pcd;
    binary_pcd.scene.path = (output / "binary.pcd").string();
    World binary_pcd_world(binary_pcd);
    check(binary_pcd_world.sceneGeoms() == 2, "binary PCD import failed");

    WorldConfig visual_only = cloud;
    visual_only.scene.collision = false;
    chaos::sim::exportSceneModel(visual_only, (output / "visual.xml").string());
    check(readFile(output / "visual.xml").find("contype=\"0\"") != std::string::npos,
          "visual-only scene still has collision");

    WorldConfig limited = cloud;
    limited.scene.voxel_size = 0;
    limited.scene.max_geoms = 1;
    throws([&] { World rejected(limited); }, "scene geometry limit was ignored");
    WorldConfig compressed = cloud;
    compressed.scene.path = (fixtures / "compressed.pcd").string();
    throws([&] { World rejected(compressed); }, "compressed PCD was not rejected explicitly");
    WorldConfig missing = cloud;
    missing.scene.path = (fixtures / "missing.ply").string();
    throws([&] { World rejected(missing); }, "missing scene was accepted");
    WorldConfig invalid = cloud;
    invalid.scene.voxel_size = std::numeric_limits<double>::quiet_NaN();
    throws([&] { World rejected(invalid); }, "NaN voxel size was accepted");

    const auto grid = output / "grid.ply";
    {
      std::ofstream ply(grid);
      ply << "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\n"
             "property float z\nend_header\n0 0 0\n0.1 0 0\n0.2 0 0\n0.3 0 0\n";
    }
    WorldConfig grid_world = cloud;
    grid_world.scene.path = grid.string();
    grid_world.scene.voxel_size = 0.1;
    check(World(grid_world).sceneGeoms() == 4, "0.2 and 0.3 collapsed into one voxel");

    const auto float_color = output / "float-color.ply";
    {
      std::ofstream ply(float_color);
      ply << "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\n"
             "property float z\nproperty float red\nproperty float green\nproperty float blue\n"
             "property float alpha\nend_header\n0 0 0 1 0 0 1\n";
    }
    WorldConfig float_color_world = cloud;
    float_color_world.scene.path = float_color.string();
    float_color_world.scene.voxel_size = 0;
    exportSceneModel(float_color_world, (output / "float-color.xml").string());
    const std::string float_xml = readFile(output / "float-color.xml");
    check(float_xml.find("rgba=\"1 0 0 1\"") != std::string::npos, "float RGB/alpha was divided by 255");

    WorldConfig display_only = cloud;
    display_only.scene.collision = false;
    display_only.discard_visual = true;
    World display_world(display_only);
    check(display_world.scenePoints() == 5 && display_world.sceneGeoms() == 0,
          "display-only scene was counted as physics geometry");
    for (int i = 0; i < 1500; ++i) display_world.step();
    display_world.readState(state);
    check(state.base_position[2] > 0, "display-only import removed the official floor");

    const auto nan_scale = output / "nan-scale.ply";
    {
      std::ofstream ply(nan_scale);
      ply << "ply\nformat ascii 1.0\nelement vertex 1\nproperty float x\nproperty float y\n"
             "property float z\nproperty float scale_0\nproperty float scale_1\nproperty float scale_2\n"
             "end_header\n0 0 0.5 nan 0 0\n";
    }
    WorldConfig nan_world = cloud;
    nan_world.scene.path = nan_scale.string();
    nan_world.scene.voxel_size = 0;
    throws([&] { World rejected(nan_world); }, "NaN Gaussian scale was imported");

    const auto ascii_pcd = output / "ascii-upper.pcd";
    {
      std::ofstream pcd(ascii_pcd);
      pcd << "VERSION 0.7\nFIELDS x y z rgb\nSIZE 4 4 4 4\nTYPE F F F F\nCOUNT 1 1 1 1\n"
             "WIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA ASCII\n0 0 0 16711680\n";
    }
    WorldConfig ascii_pcd_world = cloud;
    ascii_pcd_world.scene.path = ascii_pcd.string();
    ascii_pcd_world.scene.voxel_size = 0;
    exportSceneModel(ascii_pcd_world, (output / "ascii-pcd.xml").string());
    check(readFile(output / "ascii-pcd.xml").find("rgba=\"1 0 0 1\"") != std::string::npos,
          "ASCII packed rgb was bit-cast");

    const auto wide_pcd = output / "wide.pcd";
    {
      std::ofstream pcd(wide_pcd, std::ios::binary);
      pcd << "VERSION 0.7\nFIELDS x y z timestamp\nSIZE 4 4 4 8\nTYPE F F F U\nCOUNT 1 1 1 1\n"
             "WIDTH 2\nHEIGHT 1\nPOINTS 2\nDATA binary\n";
      const std::uint64_t stamp = 0;
      for (float x : {0.F, 1.F}) {
        const std::array<float, 3> position{x, 1.F, 2.F};
        pcd.write(reinterpret_cast<const char*>(position.data()), sizeof(position));
        pcd.write(reinterpret_cast<const char*>(&stamp), sizeof(stamp));
      }
    }
    WorldConfig wide_world = cloud;
    wide_world.scene.path = wide_pcd.string();
    wide_world.scene.voxel_size = 0;
    exportSceneModel(wide_world, (output / "wide.xml").string());
    const std::string wide_xml = readFile(output / "wide.xml");
    check(wide_xml.find("pos=\"0 1 2\"") != std::string::npos && wide_xml.find("pos=\"1 1 2\"") != std::string::npos,
          "8-byte PCD field desynchronized the next point");

    const auto lower_pcd = output / "lower.pcd";
    {
      std::ofstream pcd(lower_pcd, std::ios::binary);
      pcd << "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE f f f\nCOUNT 1 1 1\n"
             "WIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA binary\n";
      const std::array<float, 3> position{1.F, 2.F, 3.F};
      pcd.write(reinterpret_cast<const char*>(position.data()), sizeof(position));
    }
    WorldConfig lower_world = cloud;
    lower_world.scene.path = lower_pcd.string();
    lower_world.scene.voxel_size = 0;
    exportSceneModel(lower_world, (output / "lower.xml").string());
    check(readFile(output / "lower.xml").find("pos=\"1 2 3\"") != std::string::npos,
          "lowercase PCD type was read as an integer");

    const auto rgba_ply = output / "rgba.ply";
    {
      std::ofstream ply(rgba_ply, std::ios::binary);
      ply << "ply\nformat binary_little_endian 1.0\nelement vertex 1\nproperty float x\nproperty float y\n"
             "property float z\nproperty uint rgba\nend_header\n";
      const std::array<float, 3> position{};
      const std::uint32_t packed = 0x80FF0000U;
      ply.write(reinterpret_cast<const char*>(position.data()), sizeof(position));
      ply.write(reinterpret_cast<const char*>(&packed), sizeof(packed));
    }
    WorldConfig rgba_world = cloud;
    rgba_world.scene.path = rgba_ply.string();
    rgba_world.scene.voxel_size = 0;
    exportSceneModel(rgba_world, (output / "rgba.xml").string());
    check(readFile(output / "rgba.xml").find("rgba=\"1 0 0 0.501961\"") != std::string::npos,
          "packed rgba dropped alpha");

    const auto table = output / "table.xml";
    {
      std::ofstream model(table);
      model << "<mujoco><option timestep=\"0.002\"/><worldbody>"
               "<body name=\"table\"><geom type=\"box\" size=\"1 1 0.1\" pos=\"0 0 0.1\"/></body>"
               "<body name=\"base\" pos=\"0 0 2\"><freejoint/><geom type=\"sphere\" size=\"0.1\" mass=\"1\"/>"
               "<body><joint name=\"hinge\" axis=\"0 1 0\"/><geom type=\"sphere\" size=\"0.04\" mass=\"0.1\"/></body>"
               "</body></worldbody><actuator><motor joint=\"hinge\"/></actuator></mujoco>\n";
    }
    WorldConfig table_world = cloud;
    table_world.model_path = table.string();
    table_world.scene.voxel_size = 0;
    exportSceneModel(table_world, (output / "table-scene.xml").string());
    check(readFile(output / "table-scene.xml").find("table") == std::string::npos,
          "body-wrapped obstacle was not stripped");

    const auto custom_default = output / "custom-default.xml";
    {
      std::ofstream model(custom_default);
      model << "<mujoco><option timestep=\"0.002\"/><default><geom condim=\"1\" friction=\"0.2\" margin=\"0.01\"/>"
               "</default><worldbody><body name=\"base\" pos=\"0 0 2\"><freejoint/>"
               "<geom type=\"sphere\" size=\"0.1\" mass=\"1\"/><body><joint name=\"hinge\" axis=\"0 1 0\"/>"
               "<geom type=\"sphere\" size=\"0.04\" mass=\"0.1\"/></body></body></worldbody>"
               "<actuator><motor joint=\"hinge\"/></actuator></mujoco>\n";
    }
    WorldConfig custom_world = cloud;
    custom_world.model_path = custom_default.string();
    custom_world.scene.path = float_color.string();
    custom_world.scene.voxel_size = 0;
    custom_world.scene.point_radius = 0.03;
    exportSceneModel(custom_world, (output / "custom-default-scene.xml").string());
    char error[1024]{};
    mjModel* composed = mj_loadXML((output / "custom-default-scene.xml").c_str(), nullptr, error, sizeof(error));
    check(composed != nullptr, error);
    bool stock_contact = false;
    for (int geom = 0; geom < composed->ngeom; ++geom) {
      if (std::abs(composed->geom_size[geom * 3] - 0.03) < 1e-6) {
        stock_contact = composed->geom_condim[geom] == 3 && composed->geom_friction[geom * 3] == 1 &&
                        composed->geom_margin[geom] == 0;
      }
    }
    mj_deleteModel(composed);
    check(stock_contact, "scene geom inherited the root default contact parameters");

    const auto texture_dir = output / "texture-model";
    std::filesystem::create_directories(texture_dir / "assets");
    {
      const unsigned char png[] = {
          0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
          0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
          0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
          0x00, 0x03, 0x01, 0x01, 0x00, 0xC9, 0xFE, 0x92, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
          0x44, 0xAE, 0x42, 0x60, 0x82};
      std::ofstream image(texture_dir / "wood.png", std::ios::binary);
      image.write(reinterpret_cast<const char*>(png), sizeof(png));
      std::ofstream model(texture_dir / "robot.xml");
      model << "<mujoco><compiler meshdir=\"assets\"/><option timestep=\"0.002\"/><asset>"
               "<texture name=\"wood\" type=\"2d\" file=\"wood.png\"/></asset><worldbody>"
               "<body name=\"base\" pos=\"0 0 2\"><freejoint/><geom type=\"sphere\" size=\"0.1\" mass=\"1\"/>"
               "<body><joint name=\"hinge\" axis=\"0 1 0\"/><geom type=\"sphere\" size=\"0.04\" mass=\"0.1\"/>"
               "</body></body></worldbody><actuator><motor joint=\"hinge\"/></actuator></mujoco>\n";
    }
    WorldConfig texture_world = cloud;
    texture_world.model_path = (texture_dir / "robot.xml").string();
    texture_world.scene.path = float_color.string();
    texture_world.scene.voxel_size = 0;
    exportSceneModel(texture_world, (output / "texture-scene.xml").string());
    mjModel* textured = mj_loadXML((output / "texture-scene.xml").c_str(), nullptr, error, sizeof(error));
    check(textured != nullptr, error);
    mj_deleteModel(textured);

    const auto importer = std::filesystem::path(argv[0]).parent_path() / "scene_import";
    if (std::filesystem::is_regular_file(importer)) {
      const std::string command = importer.string() + " " + cloud.model_path + " " + float_color.string() + " " +
                                  (output / "rejected.xml").string() + " --max-geoms -1 >/dev/null 2>/dev/null";
      check(std::system(command.c_str()) != 0, "--max-geoms -1 was accepted");
    }

    std::cout << "Scene import tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Scene import tests failed: " << error.what() << '\n';
    return 1;
  }
}
