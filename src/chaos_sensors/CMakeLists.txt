cmake_minimum_required(VERSION 3.20)
project(chaos_sensors LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(chaos_core REQUIRED)

add_library(chaos_sensors_health src/health.cpp)
target_compile_features(chaos_sensors_health PUBLIC cxx_std_17)
target_include_directories(chaos_sensors_health PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_sensors_health PUBLIC chaos_core::chaos_core)

install(DIRECTORY include/ DESTINATION include)
install(TARGETS chaos_sensors_health EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
ament_export_dependencies(chaos_core)

ament_python_install_package(${PROJECT_NAME})

set(_distinfo "${PYTHON_INSTALL_DIR}/chaos_sensors-0.1.0.dist-info")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/entry_points.txt"
"[console_scripts]
mock_sensor = chaos_sensors.mock:main
sensor_monitor = chaos_sensors.monitor:main
")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/METADATA"
"Metadata-Version: 2.1
Name: chaos-sensors
Version: 0.1.0
Summary: Mock sensor publishers and asset-scoped sensor health diagnostics.
")
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/entry_points.txt"
  "${CMAKE_CURRENT_BINARY_DIR}/METADATA"
  DESTINATION "${_distinfo}")

foreach(_script mock_sensor sensor_monitor)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${_script}"
"#!/usr/bin/env python3
import sys
from chaos_sensors.${_script} import main
if __name__ == '__main__':
    sys.exit(main())
")
endforeach()
install(PROGRAMS
  "${CMAKE_CURRENT_BINARY_DIR}/mock_sensor"
  "${CMAKE_CURRENT_BINARY_DIR}/sensor_monitor"
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)
  ament_add_gtest(test_health_cpp test/test_health_cpp.cpp)
  target_link_libraries(test_health_cpp chaos_sensors_health)
  ament_add_pytest_test(chaos_sensors_python test
    APPEND_ENV PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}
    TIMEOUT 300)
endif()

ament_package()
