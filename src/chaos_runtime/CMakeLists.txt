cmake_minimum_required(VERSION 3.20)
project(chaos_runtime LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(chaos_core REQUIRED)
find_package(chaos_interfaces REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(std_msgs REQUIRED)
# The public headers expose the nlohmann JSON type, so the dependency is
# explicit: it is found, linked and exported. It is a header-only library;
# the pure domain targets stay free of ROS and of DDS.
find_package(nlohmann_json 3.11 CONFIG REQUIRED)
# The domain library owns the observation service, whose workers are joinable
# threads. Threads is found, linked and exported so an installed consumer of
# the exported core resolves the same dependency.
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Humanoid transport safety is a separate core, not a Go2 subclass. Its
# registry and driver have no ROS or vendor-sdk includes.
add_library(chaos_humanoid_core
  src/humanoid/backend.cpp
  src/humanoid/options.cpp
  src/humanoid/driver_core.cpp
  src/humanoid/process_guard.cpp)
target_compile_features(chaos_humanoid_core PUBLIC cxx_std_17)
target_include_directories(chaos_humanoid_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_humanoid_core PUBLIC chaos_control
  nlohmann_json::nlohmann_json)

# chaos_runtime_core remains ROS-free and DDS-free; humanoid preflight is
# linked into it so --validate-asset and startup share the complete checks.
add_library(chaos_runtime_core
  src/asset_config.cpp
  src/asset_validation.cpp
  src/coordinator.cpp
  src/observation.cpp
  src/adapters/mock_adapter.cpp)
target_compile_features(chaos_runtime_core PUBLIC cxx_std_17)
target_include_directories(chaos_runtime_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_runtime_core PUBLIC chaos_control
  chaos_humanoid_core
  nlohmann_json::nlohmann_json
  Threads::Threads)

# --- Native ROS runtime ----------------------------------------------------
#
# The ROS facade (runtime_node / ros_adapters) is a thin translation layer:
# it owns callback groups, executors and message conversion only. All control
# decisions live in the ROS-free chaos_runtime_core coordinator.

add_library(chaos_runtime_ros
  src/ros_adapters.cpp
  src/humanoid/ros_adapter.cpp
  src/runtime_node.cpp)
target_compile_features(chaos_runtime_ros PUBLIC cxx_std_17)
target_include_directories(chaos_runtime_ros PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_runtime_ros PUBLIC
  chaos_runtime_core
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp
  rclcpp::rclcpp
  rclcpp_action::rclcpp_action
  geometry_msgs::geometry_msgs__rosidl_typesupport_cpp
  nav_msgs::nav_msgs__rosidl_typesupport_cpp
  std_msgs::std_msgs__rosidl_typesupport_cpp)

# --- Native Go2 driver (separate watchdog process) -------------------------

add_library(chaos_go2_ros
  src/go2/go2_driver_node.cpp
  src/go2/unitree_sdk_backend.cpp)
target_compile_features(chaos_go2_ros PUBLIC cxx_std_17)
target_include_directories(chaos_go2_ros PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_go2_ros PUBLIC
  chaos_go2_core
  chaos_runtime_ros
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp
  rclcpp::rclcpp
  nav_msgs::nav_msgs__rosidl_typesupport_cpp
  geometry_msgs::geometry_msgs__rosidl_typesupport_cpp)

# The real Unitree SDK2 backend is an explicit opt-in; an SDK-OFF build
# refuses real mode instead of falling back to a mock. The SDK is
# consumed through its own exported CMake package, so the include paths,
# the library location and the ddsc/ddscxx/Threads dependencies come from
# the SDK itself and are never hand-written here.
option(CHAOS_GO2_WITH_UNITREE_SDK "Build the Unitree SDK2 backend" OFF)
if(CHAOS_GO2_WITH_UNITREE_SDK)
  find_package(unitree_sdk2 CONFIG REQUIRED)
  # PUBLIC: the go2_driver executable answers --build-capabilities from the
  # same compile-time flag, so it must see it too.
  target_compile_definitions(chaos_go2_ros PUBLIC CHAOS_GO2_WITH_UNITREE_SDK)
  target_link_libraries(chaos_go2_ros PRIVATE unitree_sdk2)
  # The consumer of the installed chaos_runtime package needs to resolve
  # the SDK package as well.
  ament_export_dependencies(unitree_sdk2)
endif()

# --- Native humanoid driver (independent watchdog process) -----------------
add_library(chaos_humanoid_ros
  src/humanoid/driver_node.cpp
  src/humanoid/booster_backend.cpp)
target_compile_features(chaos_humanoid_ros PUBLIC cxx_std_17)
target_include_directories(chaos_humanoid_ros PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_humanoid_ros PUBLIC
  chaos_humanoid_core
  chaos_runtime_ros
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp
  rclcpp::rclcpp
  nav_msgs::nav_msgs__rosidl_typesupport_cpp
  Threads::Threads)

option(CHAOS_HUMANOID_WITH_BOOSTER_SDK "Build the Booster humanoid SDK backend" OFF)
if(CHAOS_HUMANOID_WITH_BOOSTER_SDK)
  set(BOOSTER_SDK_ROOT "" CACHE PATH "Root of the unpacked Booster Robotics SDK")
  if(NOT BOOSTER_SDK_ROOT)
    message(FATAL_ERROR "CHAOS_HUMANOID_WITH_BOOSTER_SDK=ON requires explicit BOOSTER_SDK_ROOT")
  endif()
  get_filename_component(BOOSTER_SDK_ROOT "${BOOSTER_SDK_ROOT}" ABSOLUTE)
  set(BOOSTER_SDK_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
  if(BOOSTER_SDK_ARCH MATCHES "^(AMD64|amd64)$")
    set(BOOSTER_SDK_ARCH x86_64)
  endif()
  set(BOOSTER_SDK_LIBRARY
    "${BOOSTER_SDK_ROOT}/lib/${BOOSTER_SDK_ARCH}/libbooster_robotics_sdk.a")
  if(NOT EXISTS "${BOOSTER_SDK_ROOT}/include/booster/robot/b1/b1_loco_client.hpp")
    message(FATAL_ERROR "Booster SDK includes not found below ${BOOSTER_SDK_ROOT}/include")
  endif()
  if(NOT EXISTS "${BOOSTER_SDK_LIBRARY}")
    message(FATAL_ERROR "Booster SDK static library not found: ${BOOSTER_SDK_LIBRARY}")
  endif()
  target_compile_definitions(chaos_humanoid_ros PUBLIC CHAOS_HUMANOID_WITH_BOOSTER_SDK)
  target_include_directories(chaos_humanoid_ros PRIVATE "${BOOSTER_SDK_ROOT}/include")
  target_link_libraries(chaos_humanoid_ros PRIVATE
    "${BOOSTER_SDK_LIBRARY}" Threads::Threads ${CMAKE_DL_LIBS} rt)
endif()

# --- Native host shutdown helper -------------------------------------------
#
# The host helper owns the scoped signal handlers, the single host supervisor
# thread, the outer shutdown deadline and the one ordered cleanup path. It
# terminates the embedding process, so it is compiled ONLY into the host
# executables: it must never become part of chaos_runtime_core or of the
# reusable node library, where it would terminate every embedding process.
# The child harness of the host test task links the same target, so the host
# and the harness run the same production code.
add_library(chaos_runtime_host STATIC src/runtime_shutdown.cpp)
target_compile_features(chaos_runtime_host PUBLIC cxx_std_17)
target_include_directories(chaos_runtime_host PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(chaos_runtime_host PUBLIC chaos_runtime_core
  Threads::Threads)

add_executable(robot_runtime src/robot_runtime_main.cpp)
target_link_libraries(robot_runtime PRIVATE chaos_runtime_host chaos_runtime_ros)

add_executable(go2_driver src/go2_driver_main.cpp)
target_link_libraries(go2_driver PRIVATE chaos_go2_ros)

add_executable(humanoid_driver src/humanoid_driver_main.cpp)
target_link_libraries(humanoid_driver PRIVATE chaos_humanoid_ros)

# The host shutdown helper is internal: it owns the terminating host paths,
# so its header stays out of the installed public surface. The package build
# and the child harness of the host test task still see it.
install(DIRECTORY include/ DESTINATION include
  PATTERN "runtime_shutdown.hpp" EXCLUDE)
install(TARGETS chaos_control chaos_go2_core chaos_humanoid_core
  chaos_runtime_core chaos_runtime_ros chaos_go2_ros chaos_humanoid_ros
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(TARGETS robot_runtime go2_driver humanoid_driver
  DESTINATION lib/${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
ament_export_dependencies(chaos_core chaos_interfaces rclcpp rclcpp_action
  geometry_msgs nav_msgs std_msgs nlohmann_json Threads)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_control_cpp test/test_control_cpp.cpp)
  target_link_libraries(test_control_cpp chaos_control)
  ament_add_gtest(test_go2_driver_core_cpp test/test_go2_driver_core.cpp)
  target_link_libraries(test_go2_driver_core_cpp chaos_go2_core)
  ament_add_gtest(test_humanoid_driver_core_cpp test/test_humanoid_driver_core.cpp TIMEOUT 30)
  target_link_libraries(test_humanoid_driver_core_cpp chaos_humanoid_core Threads::Threads)
  ament_add_gtest(test_humanoid_validation_mock_cpp test/test_humanoid_validation_mock.cpp)
  target_link_libraries(test_humanoid_validation_mock_cpp chaos_humanoid_ros)
  # The domain test links the ROS-free domain library only.
  ament_add_gtest(test_coordinator_cpp test/test_coordinator_cpp.cpp)
  target_link_libraries(test_coordinator_cpp chaos_runtime_core)
  ament_add_gtest(test_coordinator_observation_cpp test/test_coordinator_observation_cpp.cpp TIMEOUT 90)
  target_link_libraries(test_coordinator_observation_cpp chaos_runtime_core)
  ament_add_gtest(test_runtime_ros_cpp test/test_runtime_ros_cpp.cpp)
  target_link_libraries(test_runtime_ros_cpp chaos_runtime_ros)
  ament_add_gtest(test_ros_adapters_cpp test/test_ros_adapters_cpp.cpp)
  target_link_libraries(test_ros_adapters_cpp chaos_runtime_ros)
  ament_add_gtest(test_asset_validation_cpp test/test_asset_validation_cpp.cpp)
  target_link_libraries(test_asset_validation_cpp chaos_runtime_core)
  ament_add_gtest(test_go2_driver_ros_cpp test/test_go2_driver_ros_cpp.cpp)
  target_link_libraries(test_go2_driver_ros_cpp chaos_go2_ros)
  ament_add_gtest(test_humanoid_driver_ros_cpp test/test_humanoid_driver_ros.cpp TIMEOUT 30)
  target_link_libraries(test_humanoid_driver_ros_cpp chaos_humanoid_ros)
  # The driver deadline helper is a ROS-free header-only facility; its test
  # links the ROS-free domain library only.
  ament_add_gtest(test_driver_deadline_cpp test/test_driver_deadline_cpp.cpp)
  target_link_libraries(test_driver_deadline_cpp chaos_runtime_core)
  # The observation service is a ROS-free facility of the domain library; its
  # test links the ROS-free domain library only. The generous finite timeout
  # contains an accidental deadlock instead of hanging the whole suite.
  ament_add_gtest(test_observation_cpp test/test_observation_cpp.cpp TIMEOUT 90)
  target_link_libraries(test_observation_cpp chaos_runtime_core)

  # The host shutdown child harness: a real executable that runs the SAME
  # production host helper (chaos_runtime_host) against the REAL observation
  # service. The terminating helper code stays inside this executable, it
  # never becomes part of a core or node library. The ROS runtime is linked
  # because one harness path uses a real ROS context and executor.
  add_executable(observation_shutdown_child test/observation_shutdown_child.cpp)
  target_link_libraries(observation_shutdown_child PRIVATE
    chaos_runtime_host
    rclcpp::rclcpp)
  target_compile_definitions(observation_shutdown_child PRIVATE
    CHAOS_SHUTDOWN_MARKER_FD=3
    CHAOS_SHUTDOWN_COMMAND_FD=4)

  # The parent gtest drives the child through a small ordered pipe protocol.
  # The child path is a target generated absolute path, so the test never
  # assumes a working directory, and the dependency keeps the child built
  # before the test runs. The finite timeout contains a hung child.
  ament_add_gtest(test_observation_shutdown_cpp test/test_observation_shutdown_cpp.cpp TIMEOUT 120)
  target_link_libraries(test_observation_shutdown_cpp chaos_runtime_host)
  target_compile_definitions(test_observation_shutdown_cpp PRIVATE
    CHAOS_SHUTDOWN_MARKER_FD=3
    CHAOS_SHUTDOWN_COMMAND_FD=4
    CHAOS_SHUTDOWN_CHILD_PATH="$<TARGET_FILE:observation_shutdown_child>")
  add_dependencies(test_observation_shutdown_cpp observation_shutdown_child)
endif()

ament_package()
