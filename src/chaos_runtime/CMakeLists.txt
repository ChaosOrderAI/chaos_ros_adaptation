cmake_minimum_required(VERSION 3.20)
project(chaos_runtime LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(chaos_core REQUIRED)

# --- C++ domain libraries (ROS-free) ---------------------------------------

add_library(chaos_control src/control.cpp)
target_compile_features(chaos_control PUBLIC cxx_std_17)
target_include_directories(chaos_control PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_control PUBLIC chaos_core::chaos_core)

add_library(chaos_go2_core src/go2_driver_core.cpp)
target_compile_features(chaos_go2_core PUBLIC cxx_std_17)
target_include_directories(chaos_go2_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_go2_core PUBLIC chaos_control)

install(DIRECTORY include/ DESTINATION include)
install(TARGETS chaos_control chaos_go2_core EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
ament_export_dependencies(chaos_core)

# --- Python runtime (kept as the production entry during migration) ---------

ament_python_install_package(${PROJECT_NAME})

# ament_python_install_package installs the package directory but not the
# setuptools entry-point metadata. Recreate the dist-info so adapter plugin
# discovery (importlib.metadata group "chaos.robot_adapters") and the console
# scripts keep working exactly as with the previous ament_python build.
set(_distinfo "${PYTHON_INSTALL_DIR}/chaos_runtime-0.1.0.dist-info")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/entry_points.txt"
"[console_scripts]
robot_runtime = chaos_runtime.node:main
go2_driver = chaos_runtime.go2_driver:main

[chaos.robot_adapters]
mock = chaos_runtime.adapters.mock:MockAdapter
lingao_ros2 = chaos_runtime.adapters.lingao:LingaoAdapter
unitree_go2 = chaos_runtime.adapters.go2:Go2Adapter
dji_psdk_legacy = chaos_runtime.adapters.dji_legacy:DjiLegacyAdapter
")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/METADATA"
"Metadata-Version: 2.1
Name: chaos-runtime
Version: 0.1.0
Summary: Asset-scoped robot runtime and adapter plugins.
")
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/entry_points.txt"
  "${CMAKE_CURRENT_BINARY_DIR}/METADATA"
  DESTINATION "${_distinfo}")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/go2_driver"
"#!/usr/bin/env python3
import sys
from chaos_runtime.go2_driver import main
if __name__ == '__main__':
    sys.exit(main())
")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/robot_runtime"
"#!/usr/bin/env python3
import sys
from chaos_runtime.node import main
if __name__ == '__main__':
    sys.exit(main())
")
install(PROGRAMS
  "${CMAKE_CURRENT_BINARY_DIR}/robot_runtime"
  "${CMAKE_CURRENT_BINARY_DIR}/go2_driver"
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)
  ament_add_gtest(test_control_cpp test/test_control_cpp.cpp)
  target_link_libraries(test_control_cpp chaos_control)
  ament_add_gtest(test_go2_driver_core_cpp test/test_go2_driver_core.cpp)
  target_link_libraries(test_go2_driver_core_cpp chaos_go2_core)
  ament_add_pytest_test(chaos_runtime_python test
    APPEND_ENV PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}
    TIMEOUT 300)
endif()

ament_package()
