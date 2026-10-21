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

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- C++ domain libraries (ROS-free) ---------------------------------------
#
# chaos_runtime_core is the ROS-free domain library: the asset contract, the
# control coordinator and the mock adapter. It links chaos_control only, so a
# domain test or an external consumer never has to pull in rclcpp or DDS.

add_library(chaos_runtime_core
  src/asset_config.cpp
  src/asset_validation.cpp
  src/coordinator.cpp
  src/adapters/mock_adapter.cpp)
target_compile_features(chaos_runtime_core PUBLIC cxx_std_17)
target_include_directories(chaos_runtime_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_runtime_core PUBLIC chaos_control
  nlohmann_json::nlohmann_json)

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

# --- Native ROS runtime ----------------------------------------------------
#
# The ROS facade (runtime_node / ros_adapters) is a thin translation layer:
# it owns callback groups, executors and message conversion only. All control
# decisions live in the ROS-free chaos_runtime_core coordinator.

add_library(chaos_runtime_ros
  src/ros_adapters.cpp
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

add_executable(robot_runtime src/robot_runtime_main.cpp)
target_link_libraries(robot_runtime PRIVATE chaos_runtime_ros)

add_executable(go2_driver src/go2_driver_main.cpp)
target_link_libraries(go2_driver PRIVATE chaos_go2_ros)

install(DIRECTORY include/ DESTINATION include)
install(TARGETS chaos_control chaos_go2_core chaos_runtime_core chaos_runtime_ros
  chaos_go2_ros
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(TARGETS robot_runtime go2_driver
  DESTINATION lib/${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
ament_export_dependencies(chaos_core chaos_interfaces rclcpp rclcpp_action
  geometry_msgs nav_msgs std_msgs nlohmann_json)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_control_cpp test/test_control_cpp.cpp)
  target_link_libraries(test_control_cpp chaos_control)
  ament_add_gtest(test_go2_driver_core_cpp test/test_go2_driver_core.cpp)
  target_link_libraries(test_go2_driver_core_cpp chaos_go2_core)
  # The domain test links the ROS-free domain library only.
  ament_add_gtest(test_coordinator_cpp test/test_coordinator_cpp.cpp)
  target_link_libraries(test_coordinator_cpp chaos_runtime_core)
  ament_add_gtest(test_runtime_ros_cpp test/test_runtime_ros_cpp.cpp)
  target_link_libraries(test_runtime_ros_cpp chaos_runtime_ros)
  ament_add_gtest(test_ros_adapters_cpp test/test_ros_adapters_cpp.cpp)
  target_link_libraries(test_ros_adapters_cpp chaos_runtime_ros)
  ament_add_gtest(test_asset_validation_cpp test/test_asset_validation_cpp.cpp)
  target_link_libraries(test_asset_validation_cpp chaos_runtime_core)
  ament_add_gtest(test_go2_driver_ros_cpp test/test_go2_driver_ros_cpp.cpp)
  target_link_libraries(test_go2_driver_ros_cpp chaos_go2_ros)
endif()

ament_package()
