cmake_minimum_required(VERSION 3.20)
project(chaos_bringup LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(chaos_assets REQUIRED)
find_package(nlohmann_json REQUIRED)

# --- Native launch planner --------------------------------------------------
# The planner owns every startup decision: the selection of the assets, the
# collision rules, the MQTT identity, the component derivation, the preflight
# checks and the process specifications. It depends on the asset engine, not
# on ROS.
add_library(chaos_bringup_core STATIC
  src/planner.cpp
  src/components.cpp
  src/runtime.cpp)
target_include_directories(chaos_bringup_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_bringup_core
  PUBLIC
    chaos_assets::chaos_assets_core
    nlohmann_json::nlohmann_json
    ament_index_cpp::ament_index_cpp)
target_compile_options(chaos_bringup_core PRIVATE -Wall -Wextra)
set_target_properties(chaos_bringup_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Native command line entry point ---------------------------------------
add_executable(chaos_bringup_plan src/plan_main.cpp)
target_link_libraries(chaos_bringup_plan PRIVATE chaos_bringup_core)
target_compile_options(chaos_bringup_plan PRIVATE -Wall -Wextra)

# The MQTT adapter configuration belongs to chaos_mqtt. When the package is
# available, the command uses its native generator; otherwise the adapter
# configurations can be supplied explicitly.
find_package(chaos_mqtt QUIET)
if(chaos_mqtt_FOUND)
  target_link_libraries(chaos_bringup_plan PRIVATE chaos_mqtt::chaos_mqtt_core)
  target_compile_definitions(chaos_bringup_plan PRIVATE CHAOS_BRINGUP_WITH_MQTT=1)
endif()

# --- Python thin bindings ---------------------------------------------------
find_package(pybind11_vendor REQUIRED)
find_package(pybind11 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
pybind11_add_module(_chaos_bringup_core MODULE src/py_bindings.cpp)
target_link_libraries(_chaos_bringup_core PRIVATE chaos_bringup_core)

# The Python package only re-exports the native planner and assembles the
# ROS launch actions.
ament_python_install_package(${PROJECT_NAME})

install(TARGETS
  chaos_bringup_core
  EXPORT export_chaos_bringup
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(TARGETS chaos_bringup_plan DESTINATION lib/${PROJECT_NAME})
install(TARGETS _chaos_bringup_core DESTINATION ${PYTHON_INSTALL_DIR}/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})
install(FILES package.xml DESTINATION share/${PROJECT_NAME})

ament_export_targets(export_chaos_bringup HAS_LIBRARY_TARGET)
ament_export_dependencies(chaos_assets nlohmann_json ament_index_cpp)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)

  ament_add_gtest(test_planner test/test_planner.cpp)
  target_link_libraries(test_planner chaos_bringup_core)

  ament_add_gtest(test_components test/test_components.cpp)
  target_link_libraries(test_components chaos_bringup_core)

  ament_add_gtest(test_runtime test/test_runtime.cpp)
  target_link_libraries(test_runtime chaos_bringup_core)

  # The binding tests run against the installed package, so the source tree
  # must not be on the module search path.
  ament_add_pytest_test(chaos_bringup_python test/test_python_bindings.py TIMEOUT 300)
  ament_add_pytest_test(chaos_bringup_planning test/test_planning.py TIMEOUT 300)
  ament_add_pytest_test(chaos_bringup_edge test/test_edge.py TIMEOUT 300)
  ament_add_pytest_test(chaos_bringup_launching test/test_launching.py TIMEOUT 300)
  ament_add_pytest_test(chaos_bringup_collection test/test_collection_launch.py TIMEOUT 300)
  ament_add_pytest_test(chaos_bringup_description test/test_description_launch.py TIMEOUT 300)
  ament_add_pytest_test(chaos_bringup_sensor test/test_sensor_launch.py TIMEOUT 300)
  ament_add_pytest_test(chaos_bringup_go2 test/test_go2_launch.py TIMEOUT 300)
  ament_add_pytest_test(chaos_bringup_edge_environment test/test_edge_driver_environment.py
    TIMEOUT 300)
endif()

ament_package()
