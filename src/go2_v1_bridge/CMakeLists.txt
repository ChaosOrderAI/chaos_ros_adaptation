cmake_minimum_required(VERSION 3.20)
project(go2_v1_bridge LANGUAGES C)

# Rust-only package: the bridge is a single cargo binary that talks to ROS 2
# through r2r, so there is no native C++ target. The CMake side only drives the
# cargo build and installs the result, mirroring src/chaos_mqtt/CMakeLists.txt.
find_package(ament_cmake REQUIRED)

find_program(GO2_V1_CARGO cargo REQUIRED)
find_program(GO2_V1_RUSTC rustc REQUIRED)

option(GO2_V1_OFFLINE "Use only already cached Cargo dependencies" OFF)
set(GO2_V1_BUILD_JOBS "2" CACHE STRING "Maximum parallel Cargo compilation jobs")
set(GO2_V1_IDL_PACKAGES
  "std_msgs;geometry_msgs;nav_msgs;sensor_msgs"
  CACHE STRING "ROS message packages to compile into r2r; include transitive message dependencies")

if(CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "Build go2_v1_bridge natively on the target architecture; cross compilation requires a matching ROS sysroot and a separate toolchain setup.")
endif()
if(NOT GO2_V1_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "GO2_V1_BUILD_JOBS must be a positive integer")
endif()
if(NOT DEFINED ENV{AMENT_PREFIX_PATH} OR "$ENV{AMENT_PREFIX_PATH}" STREQUAL "")
  message(FATAL_ERROR "Source the ROS 2 environment before building go2_v1_bridge")
endif()
if(NOT DEFINED ENV{ROS_DISTRO} OR NOT "$ENV{ROS_DISTRO}" STREQUAL "jazzy")
  message(FATAL_ERROR "go2_v1_bridge requires a sourced ROS 2 Jazzy environment")
endif()

# r2r always includes these packages even when its IDL filter is set.
set(_go2_v1_required_packages
  rcl rcl_action rcl_logging_spdlog rcl_yaml_param_parser rcutils rmw rmw_implementation
  rosidl_runtime_c rosidl_typesupport_c rosidl_typesupport_introspection_c
  builtin_interfaces rcl_interfaces unique_identifier_msgs action_msgs
  std_msgs)
foreach(_package IN LISTS GO2_V1_IDL_PACKAGES)
  if(NOT _package MATCHES "^[a-z][a-z0-9_]*$")
    message(FATAL_ERROR "Invalid ROS message package in GO2_V1_IDL_PACKAGES: ${_package}")
  endif()
  list(APPEND _go2_v1_required_packages "${_package}")
endforeach()
list(REMOVE_DUPLICATES _go2_v1_required_packages)
foreach(_package IN LISTS _go2_v1_required_packages)
  find_package(${_package} REQUIRED)
endforeach()

execute_process(
  COMMAND "${GO2_V1_RUSTC}" -vV
  OUTPUT_VARIABLE _go2_v1_rust_version
  COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "host: ([^\r\n]+)" _go2_v1_host_match "${_go2_v1_rust_version}")
set(GO2_V1_RUST_HOST "${CMAKE_MATCH_1}")
if(GO2_V1_RUST_HOST STREQUAL "")
  message(FATAL_ERROR "Unable to determine the native target from rustc -vV")
endif()
set(GO2_V1_CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo"
  CACHE PATH "Cargo target directory of the go2_v1_bridge crate")
set(GO2_V1_BINARY "${GO2_V1_CARGO_TARGET_DIR}/${GO2_V1_RUST_HOST}/release/go2_v1_bridge")

execute_process(
  COMMAND "${CMAKE_C_COMPILER}" -print-file-name=include
  OUTPUT_VARIABLE GO2_V1_COMPILER_INCLUDE
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _go2_v1_include_result)
if(NOT _go2_v1_include_result EQUAL 0 OR
   NOT IS_ABSOLUTE "${GO2_V1_COMPILER_INCLUDE}" OR
   NOT IS_DIRECTORY "${GO2_V1_COMPILER_INCLUDE}")
  set(GO2_V1_COMPILER_INCLUDE "")
endif()

configure_file(cmake/cargo_runner.cmake.in cargo_runner.cmake @ONLY)

add_custom_target(go2_v1_build ALL
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cargo_runner.cmake"
  BYPRODUCTS "${GO2_V1_BINARY}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  COMMENT "Building locked go2_v1_bridge Rust release"
  VERBATIM USES_TERMINAL)

install(PROGRAMS "${GO2_V1_BINARY}" DESTINATION lib/${PROJECT_NAME}
  RENAME go2_v1_bridge)
install(FILES package.xml DESTINATION share/${PROJECT_NAME})
install(FILES config.go2_002.example.json
  DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_test REQUIRED)
  ament_add_test(go2_v1_cargo_test
    COMMAND "${CMAKE_COMMAND}" -DGO2_V1_RUN_TESTS=ON
      -P "${CMAKE_CURRENT_BINARY_DIR}/cargo_runner.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    TIMEOUT 900
    GENERATE_RESULT_FOR_RETURN_CODE_ZERO)
endif()

ament_package()
