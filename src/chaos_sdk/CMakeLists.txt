cmake_minimum_required(VERSION 3.20)
project(chaos_sdk LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_python REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(std_msgs REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(chaos_interfaces REQUIRED)
find_package(pybind11_vendor REQUIRED)
find_package(pybind11 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

option(CHAOS_SDK_SANITIZE "Build unit tests with ASan+UBSan" OFF)

# --- Native C++ SDK ---------------------------------------------------------
add_library(chaos_sdk SHARED
  src/robot.cpp
  src/sdk_core.cpp)
target_include_directories(chaos_sdk PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_sdk PUBLIC
  rclcpp::rclcpp
  rclcpp_action::rclcpp_action
  ${std_msgs_TARGETS}
  ${builtin_interfaces_TARGETS}
  ${geometry_msgs_TARGETS}
  chaos_interfaces::chaos_interfaces__rosidl_generator_cpp
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp)
set_target_properties(chaos_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Mock asset for the integration tests and the example client ----------
# The mock asset is a test peer: it is not needed in a production
# installation, so it is only installed with the tests.
add_executable(chaos_sdk_mock_asset src/mock_asset_main.cpp)
target_link_libraries(chaos_sdk_mock_asset PRIVATE
  rclcpp::rclcpp
  rclcpp_action::rclcpp_action
  chaos_interfaces::chaos_interfaces__rosidl_generator_cpp
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp)

# --- Python thin bindings ---------------------------------------------------
pybind11_add_module(_chaos_sdk MODULE src/py_bindings.cpp)
target_link_libraries(_chaos_sdk PRIVATE chaos_sdk)

ament_python_install_package(${PROJECT_NAME})

install(TARGETS chaos_sdk
  EXPORT export_chaos_sdk
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(TARGETS _chaos_sdk DESTINATION ${PYTHON_INSTALL_DIR}/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

ament_export_targets(export_chaos_sdk HAS_LIBRARY_TARGET)
ament_export_dependencies(rclcpp rclcpp_action std_msgs builtin_interfaces
  geometry_msgs chaos_interfaces)

if(BUILD_TESTING)
  find_package(ament_cmake_test REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(ament_cmake_pytest REQUIRED)

  set(_sanitizer_flags "")
  if(CHAOS_SDK_SANITIZE)
    set(_sanitizer_flags -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    # The library itself must be instrumented, not only the test entry point.
    target_compile_options(chaos_sdk PUBLIC ${_sanitizer_flags})
    target_link_options(chaos_sdk PUBLIC ${_sanitizer_flags})
  endif()

  ament_add_gtest(test_robot_native test/test_robot_native.cpp)
  target_link_libraries(test_robot_native chaos_sdk)
  if(CHAOS_SDK_SANITIZE)
    target_compile_options(test_robot_native PRIVATE ${_sanitizer_flags})
    target_link_options(test_robot_native PRIVATE ${_sanitizer_flags})
  endif()
  set_tests_properties(test_robot_native PROPERTIES TIMEOUT 180 ENVIRONMENT "ROS_DOMAIN_ID=189")

  install(TARGETS chaos_sdk_mock_asset DESTINATION lib/${PROJECT_NAME})

  # The Python test drives the native client against the mock asset in a
  # separate process. The path of the mock asset is a single value, so it is
  # set with ENV; APPEND_ENV would prepend a path separator to it. The
  # installed Python package is a path list, so it uses APPEND_ENV.
  #
  # The test runs in an empty working directory: the source tree holds a thin
  # Python package without the extension module, and it must never shadow the
  # installed package.
  set(_chaos_sdk_test_workdir "${CMAKE_CURRENT_BINARY_DIR}/test_workdir")
  file(MAKE_DIRECTORY "${_chaos_sdk_test_workdir}")
  ament_add_pytest_test(chaos_sdk_python test/test_client.py
    ENV "CHAOS_SDK_MOCK_ASSET=${CMAKE_CURRENT_BINARY_DIR}/chaos_sdk_mock_asset"
    APPEND_ENV "PYTHONPATH=${CMAKE_INSTALL_PREFIX}/${PYTHON_INSTALL_DIR}"
    WORKING_DIRECTORY "${_chaos_sdk_test_workdir}"
    TIMEOUT 300)
endif()

ament_package()
