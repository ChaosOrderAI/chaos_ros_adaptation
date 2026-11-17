cmake_minimum_required(VERSION 3.20)
project(chaos_mqtt LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(std_msgs REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(chaos_core REQUIRED)
find_package(chaos_interfaces REQUIRED)

option(CHAOS_MQTT_SANITIZE "Build unit tests with ASan+UBSan" OFF)

# The sanitizer flags must instrument the library under test, not only the test
# entry point, so they are attached to the libraries as PUBLIC usage
# requirements and inherited by every consumer (tests, pybind module, node).
set(CHAOS_MQTT_SANITIZER_FLAGS "")
if(CHAOS_MQTT_SANITIZE)
  set(CHAOS_MQTT_SANITIZER_FLAGS
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
endif()

# --- Imported Rust transport (kept as independent infrastructure) ----------
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

execute_process(
  COMMAND "${MQTT_ADAPTER_RUSTC}" -vV
  OUTPUT_VARIABLE _mqtt_adapter_rust_version
  COMMAND_ERROR_IS_FATAL ANY)
string(REGEX MATCH "host: ([^\r\n]+)" _mqtt_adapter_host_match "${_mqtt_adapter_rust_version}")
set(MQTT_ADAPTER_RUST_HOST "${CMAKE_MATCH_1}")
if(MQTT_ADAPTER_RUST_HOST STREQUAL "")
  message(FATAL_ERROR "Unable to determine the native target from rustc -vV")
endif()
# The cargo target directory is a cache: pointing it at an existing directory
# reuses the compiled Rust artifacts instead of rebuilding them from scratch.
set(MQTT_ADAPTER_CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo"
  CACHE PATH "Cargo target directory of the vendored Rust transport")
# Preserve the imported crate name and Cargo.lock. Only the installed ROS
# executable is renamed; transport/ is an implementation directory, not a ROS
# package of its own.
set(MQTT_ADAPTER_BINARY "${MQTT_ADAPTER_CARGO_TARGET_DIR}/${MQTT_ADAPTER_RUST_HOST}/release/mqtt_ros2_adapter")

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

add_custom_target(mqtt_adapter_build ALL
  COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cargo_runner.cmake"
  BYPRODUCTS "${MQTT_ADAPTER_BINARY}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
  COMMENT "Building locked mqtt_ros2_adapter Rust release"
  VERBATIM USES_TERMINAL)

# --- Native C++ business logic (no rclcpp) ---------------------------------
add_library(chaos_mqtt_core STATIC
  src/protocol.cpp
  src/adapter_config.cpp)
target_include_directories(chaos_mqtt_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_mqtt_core PUBLIC chaos_core::chaos_core nlohmann_json::nlohmann_json)
# The pybind module links this static library into a shared object.
set_target_properties(chaos_mqtt_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Native C++ gateway node -----------------------------------------------
add_library(chaos_mqtt_gateway STATIC src/gateway_node.cpp)
target_include_directories(chaos_mqtt_gateway PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_mqtt_gateway
  PUBLIC
    chaos_mqtt_core
    rclcpp::rclcpp
    rclcpp_action::rclcpp_action
    ${std_msgs_TARGETS}
    ${builtin_interfaces_TARGETS}
    ${geometry_msgs_TARGETS}
    chaos_interfaces::chaos_interfaces__rosidl_generator_cpp
    chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp)
add_dependencies(chaos_mqtt_gateway mqtt_adapter_build)
set_target_properties(chaos_mqtt_gateway PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CHAOS_MQTT_SANITIZE)
  foreach(_mqtt_sanitized_target chaos_mqtt_core chaos_mqtt_gateway)
    target_compile_options(${_mqtt_sanitized_target} PUBLIC ${CHAOS_MQTT_SANITIZER_FLAGS})
    target_link_options(${_mqtt_sanitized_target} PUBLIC ${CHAOS_MQTT_SANITIZER_FLAGS})
  endforeach()
endif()

add_executable(gateway src/gateway_main.cpp)
target_link_libraries(gateway PRIVATE chaos_mqtt_gateway)

# --- Python thin bindings ---------------------------------------------------
find_package(pybind11_vendor REQUIRED)
find_package(pybind11 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
pybind11_add_module(_chaos_mqtt_core MODULE src/py_bindings.cpp)
target_link_libraries(_chaos_mqtt_core PRIVATE chaos_mqtt_core)

ament_python_install_package(${PROJECT_NAME})

# Stage the python package inside the build tree: the compiled extension has
# to sit next to the python sources, exactly like the installed layout. The
# binding test imports the staged package, so it exercises the same import
# path as a consumer of the install space.
set(CHAOS_MQTT_PYTHON_STAGE "${CMAKE_CURRENT_BINARY_DIR}/python/${PROJECT_NAME}")
add_custom_command(TARGET _chaos_mqtt_core POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CHAOS_MQTT_PYTHON_STAGE}"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}" "${CHAOS_MQTT_PYTHON_STAGE}"
  COMMAND ${CMAKE_COMMAND} -E copy
    "$<TARGET_FILE:_chaos_mqtt_core>" "${CHAOS_MQTT_PYTHON_STAGE}"
  COMMENT "Staging the ${PROJECT_NAME} python package for the binding test")

install(TARGETS
  chaos_mqtt_core
  chaos_mqtt_gateway
  EXPORT export_chaos_mqtt
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(TARGETS gateway DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(TARGETS _chaos_mqtt_core DESTINATION ${PYTHON_INSTALL_DIR}/${PROJECT_NAME})
install(PROGRAMS "${MQTT_ADAPTER_BINARY}" DESTINATION lib/${PROJECT_NAME}
  RENAME mqtt_transport)
install(FILES README.md DESTINATION share/${PROJECT_NAME})
install(FILES transport/README.md transport/SOURCE_PROVENANCE.md
  transport/SOURCE_MANIFEST.json transport/Cargo.toml transport/Cargo.lock
  transport/config.example.json transport/config.wifi.example.json
  transport/config.4g.example.json transport/config.5g.example.json
  transport/config.fleet.robot.example.json
  transport/config.fleet.platform.example.json
  DESTINATION share/${PROJECT_NAME}/transport)
install(DIRECTORY transport/examples transport/scripts transport/deploy
  DESTINATION share/${PROJECT_NAME}/transport
  PATTERN "__pycache__" EXCLUDE
  PATTERN "*.pyc" EXCLUDE)

ament_export_targets(export_chaos_mqtt HAS_LIBRARY_TARGET)
ament_export_dependencies(nlohmann_json rclcpp rclcpp_action std_msgs
  builtin_interfaces geometry_msgs chaos_interfaces chaos_core)

if(BUILD_TESTING)
  find_package(ament_cmake_test REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)

  set(_sanitizer_flags ${CHAOS_MQTT_SANITIZER_FLAGS})

  ament_add_gtest(test_protocol test/protocol_tests.cpp)
  target_link_libraries(test_protocol chaos_mqtt_core)
  if(CHAOS_MQTT_SANITIZE)
    target_compile_options(test_protocol PRIVATE ${_sanitizer_flags})
    target_link_options(test_protocol PRIVATE ${_sanitizer_flags})
  endif()

  ament_add_gtest(test_adapter_config test/adapter_config_tests.cpp)
  target_link_libraries(test_adapter_config chaos_mqtt_core)
  if(CHAOS_MQTT_SANITIZE)
    target_compile_options(test_adapter_config PRIVATE ${_sanitizer_flags})
    target_link_options(test_adapter_config PRIVATE ${_sanitizer_flags})
  endif()

  ament_add_gtest(test_gateway_unit test/gateway_unit_tests.cpp)
  target_link_libraries(test_gateway_unit chaos_mqtt_gateway)
  if(CHAOS_MQTT_SANITIZE)
    target_compile_options(test_gateway_unit PRIVATE ${_sanitizer_flags})
    target_link_options(test_gateway_unit PRIVATE ${_sanitizer_flags})
  endif()

  ament_add_gtest(test_gateway_integration test/gateway_integration_tests.cpp)
  target_link_libraries(test_gateway_integration chaos_mqtt_gateway)
  set_tests_properties(test_gateway_integration PROPERTIES TIMEOUT 120 ENVIRONMENT "ROS_DOMAIN_ID=188")

  # Narrow regression tests of the action observation layer. They link the
  # instrumented library, so the sanitizer run covers the production code and
  # not only the test entry point.
  ament_add_gtest(test_action_observation test/action_observation_tests.cpp)
  target_link_libraries(test_action_observation chaos_mqtt_gateway)
  set_tests_properties(test_action_observation PROPERTIES TIMEOUT 120 ENVIRONMENT "ROS_DOMAIN_ID=187")
  if(CHAOS_MQTT_SANITIZE)
    target_compile_options(test_action_observation PRIVATE ${_sanitizer_flags})
    target_link_options(test_action_observation PRIVATE ${_sanitizer_flags})
  endif()

  # The staged package must precede an already installed chaos_mqtt package
  # in PYTHONPATH: the installed one carries the python sources but not the
  # compiled extension.
  if(NOT CHAOS_MQTT_SANITIZE)
    # A sanitized extension cannot be loaded into an unsanitized interpreter
    # unless the sanitizer runtime is preloaded; the binding test is therefore
    # skipped in the sanitizer build.
    ament_add_pytest_test(chaos_mqtt_python test/test_python_bindings.py
      ENV "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python:$ENV{PYTHONPATH}"
      TIMEOUT 120)
  endif()
  ament_add_test(mqtt_adapter_cargo_test
    COMMAND "${CMAKE_COMMAND}" -DMQTT_ADAPTER_RUN_TESTS=ON
      -P "${CMAKE_CURRENT_BINARY_DIR}/cargo_runner.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    TIMEOUT 900
    GENERATE_RESULT_FOR_RETURN_CODE_ZERO)
endif()

ament_package()
