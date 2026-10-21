cmake_minimum_required(VERSION 3.20)
project(chaos_data LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(chaos_core REQUIRED)

add_library(chaos_data_timing src/timing.cpp)
target_compile_features(chaos_data_timing PUBLIC cxx_std_17)
target_include_directories(chaos_data_timing PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_data_timing PUBLIC chaos_core::chaos_core)

install(DIRECTORY include/ DESTINATION include)
install(TARGETS chaos_data_timing EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
ament_export_dependencies(chaos_core)

ament_python_install_package(${PROJECT_NAME})

# Package data and resource marker previously installed via setup.py
# data_files.
install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)
install(FILES resource/${PROJECT_NAME}
  DESTINATION share/ament_index/resource_index/packages)

set(_distinfo "${PYTHON_INSTALL_DIR}/chaos_data-0.1.0.dist-info")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/entry_points.txt"
"[console_scripts]
collector = chaos_data.recorder:main
audit = chaos_data.audit:main
data_audit = chaos_data.audit:main
data_export = chaos_data.dataset:main
")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/METADATA"
"Metadata-Version: 2.1
Name: chaos-data
Version: 0.1.0
Summary: Asset-scoped rosbag recording and offline episode export.
")
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/entry_points.txt"
  "${CMAKE_CURRENT_BINARY_DIR}/METADATA"
  DESTINATION "${_distinfo}")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/collector"
"#!/usr/bin/env python3
import sys
from chaos_data.recorder import main
if __name__ == '__main__':
    sys.exit(main())
")
foreach(_alias audit data_audit data_export)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${_alias}"
"#!/usr/bin/env python3
import sys
from chaos_data.dataset import main
if __name__ == '__main__':
    sys.exit(main())
")
endforeach()
# audit/data_audit import chaos_data.audit, not chaos_data.dataset.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/audit"
"#!/usr/bin/env python3
import sys
from chaos_data.audit import main
if __name__ == '__main__':
    sys.exit(main())
")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/data_audit"
"#!/usr/bin/env python3
import sys
from chaos_data.audit import main
if __name__ == '__main__':
    sys.exit(main())
")
install(PROGRAMS
  "${CMAKE_CURRENT_BINARY_DIR}/collector"
  "${CMAKE_CURRENT_BINARY_DIR}/audit"
  "${CMAKE_CURRENT_BINARY_DIR}/data_audit"
  "${CMAKE_CURRENT_BINARY_DIR}/data_export"
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)
  ament_add_gtest(test_timing_cpp test/test_timing_cpp.cpp)
  target_link_libraries(test_timing_cpp chaos_data_timing)
  ament_add_pytest_test(chaos_data_python test
    APPEND_ENV PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}
    TIMEOUT 300)
endif()

ament_package()
