cmake_minimum_required(VERSION 3.20)
project(chaos_assets LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
# The link flags are used directly instead of imported targets: a consumer of
# the installed static library must not have to know about the internal
# dependencies of the engine.
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)
pkg_check_modules(URDFDOM REQUIRED urdfdom)

# --- Native configuration engine --------------------------------------------
# The engine carries every business rule of the package: schema validation,
# model inheritance, limits, sensors, recording policy, robot description and
# the configuration digest. It never depends on rclcpp or a device SDK.
add_library(chaos_assets_core STATIC
  src/yaml.cpp
  src/json.cpp
  src/paths.cpp
  src/sensors.cpp
  src/collection.cpp
  src/description.cpp
  src/inventory.cpp
  src/xml.cpp
  src/process.cpp)
target_include_directories(chaos_assets_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_include_directories(chaos_assets_core PRIVATE
  ${LIBXML2_INCLUDE_DIRS}
  ${URDFDOM_INCLUDE_DIRS})
target_link_libraries(chaos_assets_core
  PUBLIC
    nlohmann_json::nlohmann_json
    yaml-cpp
  PRIVATE
    ${LIBXML2_LDFLAGS}
    ${URDFDOM_LDFLAGS}
    ament_index_cpp::ament_index_cpp
    OpenSSL::Crypto)
target_compile_options(chaos_assets_core PRIVATE -Wall -Wextra)
# The engine is linked into the Python extension module as well.
set_target_properties(chaos_assets_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Native command line entry point ---------------------------------------
add_executable(asset_config src/asset_config_main.cpp)
target_link_libraries(asset_config PRIVATE chaos_assets_core)
target_compile_options(asset_config PRIVATE -Wall -Wextra)

# --- Python thin bindings ---------------------------------------------------
find_package(pybind11_vendor REQUIRED)
find_package(pybind11 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
pybind11_add_module(_chaos_assets_core MODULE src/py_bindings.cpp)
target_link_libraries(_chaos_assets_core PRIVATE chaos_assets_core)

# The Python package only re-exports the native engine.
ament_python_install_package(${PROJECT_NAME})

install(TARGETS
  chaos_assets_core
  EXPORT export_chaos_assets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(TARGETS asset_config DESTINATION lib/${PROJECT_NAME})
install(TARGETS _chaos_assets_core DESTINATION ${PYTHON_INSTALL_DIR}/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config models tools DESTINATION share/${PROJECT_NAME})
install(FILES package.xml DESTINATION share/${PROJECT_NAME})

ament_export_targets(export_chaos_assets HAS_LIBRARY_TARGET)
ament_export_dependencies(nlohmann_json yaml-cpp ament_index_cpp OpenSSL)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)

  ament_add_gtest(test_yaml test/test_yaml.cpp)
  target_link_libraries(test_yaml chaos_assets_core)

  ament_add_gtest(test_paths test/test_paths.cpp)
  target_link_libraries(test_paths chaos_assets_core ament_index_cpp::ament_index_cpp)

  ament_add_gtest(test_inventory test/test_inventory.cpp)
  target_link_libraries(test_inventory chaos_assets_core ament_index_cpp::ament_index_cpp)

  ament_add_gtest(test_sensors test/test_sensors.cpp)
  target_link_libraries(test_sensors chaos_assets_core)

  ament_add_gtest(test_collection test/test_collection.cpp)
  target_link_libraries(test_collection chaos_assets_core)

  ament_add_gtest(test_description test/test_description.cpp)
  target_link_libraries(test_description chaos_assets_core ament_index_cpp::ament_index_cpp)

  ament_add_gtest(test_process test/test_process.cpp)
  target_link_libraries(test_process chaos_assets_core)

  ament_add_gtest(test_golden_configs test/test_golden_configs.cpp)
  target_link_libraries(test_golden_configs chaos_assets_core ament_index_cpp::ament_index_cpp
    OpenSSL::Crypto)
  target_compile_definitions(test_golden_configs
    PRIVATE CHAOS_ASSETS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

  # The test imports the installed package, so the source tree must not be
  # on the module search path.
  ament_add_pytest_test(chaos_assets_python test/test_python_bindings.py TIMEOUT 300)
  ament_add_pytest_test(chaos_assets_inventory test/test_inventory.py TIMEOUT 300)
  ament_add_pytest_test(chaos_assets_collection test/test_collection.py TIMEOUT 300)
  ament_add_pytest_test(chaos_assets_description test/test_description.py TIMEOUT 300)
  ament_add_pytest_test(chaos_assets_sensors test/test_sensors.py TIMEOUT 300)
  ament_add_pytest_test(chaos_assets_go2_inventory test/test_go2_inventory.py TIMEOUT 300)
endif()

ament_package()
