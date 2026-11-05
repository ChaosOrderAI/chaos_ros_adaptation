cmake_minimum_required(VERSION 3.20)
project(chaos_sim VERSION 0.1.0 LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
option(CHAOS_SIM_ENABLE_MUJOCO "Build the optional MuJoCo simulator" OFF)
set(MUJOCO_ROOT "$ENV{MUJOCO_ROOT}" CACHE PATH "Existing MuJoCo C SDK root")

install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME}
  PATTERN "__pycache__" EXCLUDE)
install(PROGRAMS tools/fetch_models.py tools/viewer.py DESTINATION lib/${PROJECT_NAME})

if(CHAOS_SIM_ENABLE_MUJOCO)
  # Resolve the current root on every configure rather than silently retaining
  # a library cached from a different SDK.
  unset(MUJOCO_INCLUDE_DIR CACHE)
  unset(MUJOCO_LIBRARY CACHE)
  find_path(MUJOCO_INCLUDE_DIR mujoco/mujoco.h HINTS "${MUJOCO_ROOT}/include" NO_DEFAULT_PATH)
  find_library(MUJOCO_LIBRARY NAMES mujoco HINTS "${MUJOCO_ROOT}/lib" "${MUJOCO_ROOT}/bin" NO_DEFAULT_PATH)
  if(NOT MUJOCO_ROOT OR NOT MUJOCO_INCLUDE_DIR OR NOT MUJOCO_LIBRARY)
    message(FATAL_ERROR "MuJoCo requested but SDK unavailable. Set MUJOCO_ROOT to an existing SDK with include/mujoco and lib/libmujoco.")
  endif()
  find_package(rclcpp REQUIRED)
  find_package(sensor_msgs REQUIRED)
  find_package(nav_msgs REQUIRED)
  find_package(diagnostic_msgs REQUIRED)
  find_package(std_srvs REQUIRED)
  find_package(nlohmann_json REQUIRED)

  add_library(chaos_sim_world SHARED src/world.cpp src/scene.cpp)
  target_compile_features(chaos_sim_world PUBLIC cxx_std_17)
  target_include_directories(chaos_sim_world PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
  target_include_directories(chaos_sim_world PRIVATE ${MUJOCO_INCLUDE_DIR})
  target_link_libraries(chaos_sim_world PRIVATE ${MUJOCO_LIBRARY})
  get_filename_component(MUJOCO_LIBRARY_DIR "${MUJOCO_LIBRARY}" DIRECTORY)
  set_target_properties(chaos_sim_world PROPERTIES INSTALL_RPATH "${MUJOCO_LIBRARY_DIR}")

  add_executable(chaos_sim_node src/simulator_node.cpp)
  set_target_properties(chaos_sim_node PROPERTIES OUTPUT_NAME simulator
    INSTALL_RPATH "$ORIGIN/..;${MUJOCO_LIBRARY_DIR}")
  target_compile_features(chaos_sim_node PRIVATE cxx_std_17)
  target_include_directories(chaos_sim_node PRIVATE ${MUJOCO_INCLUDE_DIR})
  target_link_libraries(chaos_sim_node chaos_sim_world nlohmann_json::nlohmann_json ${MUJOCO_LIBRARY})
  ament_target_dependencies(chaos_sim_node rclcpp sensor_msgs nav_msgs diagnostic_msgs std_srvs)

  add_executable(chaos_sim_scene_import src/scene_import.cpp)
  set_target_properties(chaos_sim_scene_import PROPERTIES OUTPUT_NAME scene_import
    INSTALL_RPATH "$ORIGIN/..;${MUJOCO_LIBRARY_DIR}")
  target_compile_features(chaos_sim_scene_import PRIVATE cxx_std_17)
  target_link_libraries(chaos_sim_scene_import chaos_sim_world)

  install(TARGETS chaos_sim_world EXPORT export_${PROJECT_NAME} LIBRARY DESTINATION lib)
  install(TARGETS chaos_sim_node chaos_sim_scene_import RUNTIME DESTINATION lib/${PROJECT_NAME})
  install(DIRECTORY include/ DESTINATION include)
  ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)

  if(BUILD_TESTING)
    enable_testing()
    add_executable(chaos_sim_world_test test/world_test.cpp)
    target_compile_features(chaos_sim_world_test PRIVATE cxx_std_17)
    target_link_libraries(chaos_sim_world_test PRIVATE chaos_sim_world)
    add_test(NAME chaos_sim_world COMMAND chaos_sim_world_test "${CMAKE_CURRENT_SOURCE_DIR}/test")

    add_executable(chaos_sim_scene_test test/scene_test.cpp)
    target_compile_features(chaos_sim_scene_test PRIVATE cxx_std_17)
    target_include_directories(chaos_sim_scene_test PRIVATE ${MUJOCO_INCLUDE_DIR})
    target_link_libraries(chaos_sim_scene_test PRIVATE chaos_sim_world ${MUJOCO_LIBRARY})
    add_test(NAME chaos_sim_scene COMMAND chaos_sim_scene_test
      "${CMAKE_CURRENT_SOURCE_DIR}/test" "${CMAKE_CURRENT_BINARY_DIR}/scene-test")
  endif()
else()
  message(STATUS "chaos_sim: MuJoCo disabled; launch/config/tools installed, simulator executable is NOT built. Enable CHAOS_SIM_ENABLE_MUJOCO and set MUJOCO_ROOT to build it.")
endif()

if(BUILD_TESTING)
  find_package(ament_cmake_pytest REQUIRED)
  ament_add_pytest_test(chaos_sim_launch_test test/test_simulator_launch.py)
endif()

ament_package()
