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
find_package(chaos_core REQUIRED)
find_package(pybind11_vendor REQUIRED)
find_package(pybind11 REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
find_package(Threads REQUIRED)

option(CHAOS_SDK_SANITIZE "Build unit tests with ASan+UBSan" OFF)
# BehaviorTree.CPP node library (Navigate/Takeoff/Land wrapping the SDK's
# actions). The 4.x API is required; Humble ships only v3, so the Humble
# validation build turns this off.
option(CHAOS_SDK_WITH_BT "Build the BehaviorTree.CPP node library" ON)
if(CHAOS_SDK_WITH_BT)
  find_package(behaviortree_cpp 4.0 CONFIG REQUIRED)
endif()

# --- Internal feedback dispatcher -------------------------------------------
# The dispatcher runs the user feedback callbacks off the ROS executor. It is
# internal to the SDK: the header lives in src/, it is not installed and it is
# not a public API. It depends on the standard library only, so the focused
# unit test links it directly without the ROS dependencies.
add_library(chaos_sdk_feedback_dispatcher STATIC
  src/feedback_dispatcher.cpp)
target_include_directories(chaos_sdk_feedback_dispatcher PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(chaos_sdk_feedback_dispatcher PUBLIC Threads::Threads)
set_target_properties(chaos_sdk_feedback_dispatcher PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp
  chaos_core::chaos_core)
# The dispatcher is internal: it is linked privately, so the internal include
# directory and the internal target stay out of the exported interface.
target_link_libraries(chaos_sdk PRIVATE chaos_sdk_feedback_dispatcher)
set_target_properties(chaos_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- BehaviorTree.CPP nodes -------------------------------------------------
# The tree is ticked by an external caller process; the nodes only wrap the
# SDK's typed action handles and never acquire a lease or spin an executor.
if(CHAOS_SDK_WITH_BT)
  add_library(chaos_sdk_bt SHARED
    src/bt/nodes.cpp
    src/bt/register_nodes.cpp)
  target_compile_features(chaos_sdk_bt PUBLIC cxx_std_17)
  target_include_directories(chaos_sdk_bt PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
  target_link_libraries(chaos_sdk_bt PUBLIC
    chaos_sdk
    behaviortree_cpp::behaviortree_cpp
    ${geometry_msgs_TARGETS}
    Threads::Threads)
  set_target_properties(chaos_sdk_bt PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

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
if(CHAOS_SDK_WITH_BT)
  install(TARGETS chaos_sdk_bt
    EXPORT export_chaos_sdk
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
endif()
install(TARGETS _chaos_sdk DESTINATION ${PYTHON_INSTALL_DIR}/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

ament_export_targets(export_chaos_sdk HAS_LIBRARY_TARGET)
ament_export_dependencies(rclcpp rclcpp_action std_msgs builtin_interfaces
  geometry_msgs chaos_interfaces chaos_core)
if(CHAOS_SDK_WITH_BT)
  # behaviortree_cpp is linked PUBLIC by chaos_sdk_bt, so a consumer that
  # links chaos_sdk::chaos_sdk_bt needs the imported target to exist.
  ament_export_dependencies(behaviortree_cpp)
endif()

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
    # The dispatcher is compiled into the library, so it is instrumented too.
    target_compile_options(chaos_sdk_feedback_dispatcher PUBLIC ${_sanitizer_flags})
    target_link_options(chaos_sdk_feedback_dispatcher PUBLIC ${_sanitizer_flags})
  endif()

  ament_add_gtest(test_sdk_core test/test_sdk_core.cpp)
  target_link_libraries(test_sdk_core chaos_sdk)
  if(CHAOS_SDK_SANITIZE)
    target_compile_options(test_sdk_core PRIVATE ${_sanitizer_flags})
    target_link_options(test_sdk_core PRIVATE ${_sanitizer_flags})
  endif()

  ament_add_gtest(test_robot_native test/test_robot_native.cpp)
  target_link_libraries(test_robot_native chaos_sdk)
  if(CHAOS_SDK_SANITIZE)
    target_compile_options(test_robot_native PRIVATE ${_sanitizer_flags})
    target_link_options(test_robot_native PRIVATE ${_sanitizer_flags})
  endif()
  set_tests_properties(test_robot_native PROPERTIES TIMEOUT 180 ENVIRONMENT "ROS_DOMAIN_ID=189")

  # Focused unit test of the internal feedback dispatcher. It needs no ROS
  # runtime: it links the dispatcher directly.
  ament_add_gtest(test_feedback_dispatcher test/test_feedback_dispatcher.cpp)
  target_link_libraries(test_feedback_dispatcher chaos_sdk_feedback_dispatcher)
  if(CHAOS_SDK_SANITIZE)
    target_compile_options(test_feedback_dispatcher PRIVATE ${_sanitizer_flags})
    target_link_options(test_feedback_dispatcher PRIVATE ${_sanitizer_flags})
  endif()
  set_tests_properties(test_feedback_dispatcher PROPERTIES TIMEOUT 120)

  if(CHAOS_SDK_WITH_BT)
    ament_add_gtest(test_bt_nodes test/test_bt_nodes.cpp TIMEOUT 180)
    target_include_directories(test_bt_nodes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
    target_link_libraries(test_bt_nodes
      chaos_sdk_bt
      rclcpp::rclcpp
      rclcpp_action::rclcpp_action
      chaos_interfaces::chaos_interfaces__rosidl_generator_cpp
      chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp)
    set_tests_properties(test_bt_nodes PROPERTIES ENVIRONMENT "ROS_DOMAIN_ID=188")
  endif()

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
