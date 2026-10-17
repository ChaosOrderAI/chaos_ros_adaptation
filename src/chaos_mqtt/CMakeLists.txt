cmake_minimum_required(VERSION 3.20)
project(chaos_mqtt LANGUAGES C)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_program(MQTT_ADAPTER_CARGO cargo REQUIRED)
find_program(MQTT_ADAPTER_RUSTC rustc REQUIRED)

option(MQTT_ADAPTER_OFFLINE "Use only already cached Cargo dependencies" OFF)
set(MQTT_ADAPTER_BUILD_JOBS "2" CACHE STRING "Maximum parallel Cargo compilation jobs")
set(MQTT_ADAPTER_IDL_PACKAGES
  "std_msgs;geometry_msgs;sensor_msgs;nav_msgs;diagnostic_msgs"
  CACHE STRING "ROS message packages to compile into r2r; include transitive message dependencies")

if(CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "Build chaos_mqtt natively on the target architecture; cross compilation requires a matching ROS sysroot and a separate toolchain setup.")
endif()
if(NOT MQTT_ADAPTER_BUILD_JOBS MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR "MQTT_ADAPTER_BUILD_JOBS must be a positive integer")
endif()
if(NOT DEFINED ENV{AMENT_PREFIX_PATH} OR "$ENV{AMENT_PREFIX_PATH}" STREQUAL "")
  message(FATAL_ERROR "Source the ROS 2 environment before building chaos_mqtt")
endif()
if(NOT DEFINED ENV{ROS_DISTRO} OR NOT "$ENV{ROS_DISTRO}" STREQUAL "jazzy")
  message(FATAL_ERROR "chaos_mqtt requires a sourced ROS 2 Jazzy environment")
endif()

# r2r always includes these four packages even when its IDL filter is set.
set(_mqtt_adapter_required_packages
  rcl rcl_action rcl_logging_spdlog rcl_yaml_param_parser rcutils rmw rmw_implementation
  rosidl_runtime_c rosidl_typesupport_c rosidl_typesupport_introspection_c
  builtin_interfaces rcl_interfaces unique_identifier_msgs action_msgs
  std_msgs)
foreach(_package IN LISTS MQTT_ADAPTER_IDL_PACKAGES)
  if(NOT _package MATCHES "^[a-z][a-z0-9_]*$")
    message(FATAL_ERROR "Invalid ROS message package in MQTT_ADAPTER_IDL_PACKAGES: ${_package}")
  endif()
  list(APPEND _mqtt_adapter_required_packages "${_package}")
endforeach()
list(FIND MQTT_ADAPTER_IDL_PACKAGES std_msgs _mqtt_adapter_std_msgs_index)
if(_mqtt_adapter_std_msgs_index EQUAL -1)
  message(FATAL_ERROR "MQTT_ADAPTER_IDL_PACKAGES must include std_msgs for the adapter's typed routes")
endif()
list(REMOVE_DUPLICATES _mqtt_adapter_required_packages)
foreach(_package IN LISTS _mqtt_adapter_required_packages)
  find_package(${_package} REQUIRED)
endforeach()

# Select the native Rust host explicitly so CARGO_BUILD_TARGET cannot silently
# move the binary outside the directory that gets installed.
execute_process(
  COMMAND "${MQTT_ADAPTER_RUSTC}" -vV
  OUTPUT_VARIABLE _mqtt_adapter_rust_version
  COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "host: ([^\r\n]+)" _mqtt_adapter_host_match "${_mqtt_adapter_rust_version}")
set(MQTT_ADAPTER_RUST_HOST "${CMAKE_MATCH_1}")
if(MQTT_ADAPTER_RUST_HOST STREQUAL "")
  message(FATAL_ERROR "Unable to determine the native target from rustc -vV")
endif()
set(MQTT_ADAPTER_CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo")
# Preserve the imported crate name and Cargo.lock. Only the installed ROS
# executable is renamed; transport/ is an implementation directory, not a ROS
# package of its own.
set(MQTT_ADAPTER_BINARY "${MQTT_ADAPTER_CARGO_TARGET_DIR}/${MQTT_ADAPTER_RUST_HOST}/release/mqtt_ros2_adapter")

# GCC's intrinsic headers can be outside libclang's default search path on
# Ubuntu. Ask the selected native compiler instead of assuming an architecture
# or GCC version. An explicit user setting takes precedence in the runner.
execute_process(
  COMMAND "${CMAKE_C_COMPILER}" -print-file-name=include
  OUTPUT_VARIABLE MQTT_ADAPTER_COMPILER_INCLUDE
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _mqtt_adapter_include_result)
if(NOT _mqtt_adapter_include_result EQUAL 0 OR
   NOT IS_ABSOLUTE "${MQTT_ADAPTER_COMPILER_INCLUDE}" OR
   NOT IS_DIRECTORY "${MQTT_ADAPTER_COMPILER_INCLUDE}")
  set(MQTT_ADAPTER_COMPILER_INCLUDE "")
endif()

configure_file(cmake/cargo_runner.cmake.in cargo_runner.cmake @ONLY)

# Let Cargo check its complete dependency graph on each build. This handles
# changed/added/deleted Rust files, Cargo.lock and generated message bindings,
# without CMake attempting to duplicate Cargo's freshness rules.
add_custom_target(mqtt_adapter_build ALL
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cargo_runner.cmake"
  BYPRODUCTS "${MQTT_ADAPTER_BINARY}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  COMMENT "Building locked mqtt_ros2_adapter Rust release"
  VERBATIM USES_TERMINAL)

ament_python_install_package(${PROJECT_NAME})
configure_file(scripts/gateway.in gateway @ONLY)
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/gateway" DESTINATION lib/${PROJECT_NAME})
install(PROGRAMS "${MQTT_ADAPTER_BINARY}" DESTINATION lib/${PROJECT_NAME}
  RENAME mqtt_transport)
install(FILES README.md DESTINATION share/${PROJECT_NAME})
install(FILES transport/README.md transport/SOURCE_PROVENANCE.md
  transport/SOURCE_MANIFEST.json transport/Cargo.toml transport/Cargo.lock
  transport/config.example.json transport/config.wifi.example.json
  transport/config.4g.example.json transport/config.5g.example.json
  DESTINATION share/${PROJECT_NAME}/transport)
install(DIRECTORY transport/examples transport/scripts transport/deploy
  DESTINATION share/${PROJECT_NAME}/transport
  PATTERN "__pycache__" EXCLUDE
  PATTERN "*.pyc" EXCLUDE)

if(BUILD_TESTING)
  find_package(ament_cmake_test REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)
  ament_add_pytest_test(chaos_mqtt_python test
    APPEND_ENV PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}
    TIMEOUT 120)
  ament_add_test(mqtt_adapter_cargo_test
    COMMAND "${CMAKE_COMMAND}" -DMQTT_ADAPTER_RUN_TESTS=ON
      -P "${CMAKE_CURRENT_BINARY_DIR}/cargo_runner.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    TIMEOUT 900
    GENERATE_RESULT_FOR_RETURN_CODE_ZERO)
endif()

ament_package()
