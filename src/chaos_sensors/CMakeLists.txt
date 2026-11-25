cmake_minimum_required(VERSION 3.20)
project(chaos_sensors LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(chaos_core REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc features2d calib3d imgcodecs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(chaos_sensors_health src/health.cpp)
target_compile_features(chaos_sensors_health PUBLIC cxx_std_17)
target_include_directories(chaos_sensors_health PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_sensors_health PUBLIC chaos_core::chaos_core)

add_library(chaos_sensors_config src/sensor_config.cpp)
target_compile_features(chaos_sensors_config PUBLIC cxx_std_17)
target_include_directories(chaos_sensors_config PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

target_link_libraries(chaos_sensors_config PUBLIC nlohmann_json::nlohmann_json)

add_library(chaos_sensors_ros
  src/mock_sensor_node.cpp
  src/sensor_monitor_node.cpp)
target_compile_features(chaos_sensors_ros PUBLIC cxx_std_17)
target_include_directories(chaos_sensors_ros PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_sensors_ros PUBLIC
  chaos_sensors_config
  chaos_sensors_health
  rclcpp::rclcpp
  sensor_msgs::sensor_msgs__rosidl_typesupport_cpp
  diagnostic_msgs::diagnostic_msgs__rosidl_typesupport_cpp
  std_msgs::std_msgs__rosidl_typesupport_cpp)

add_executable(mock_sensor src/mock_sensor_main.cpp)
target_link_libraries(mock_sensor PRIVATE chaos_sensors_ros)
add_executable(sensor_monitor src/sensor_monitor_main.cpp)
target_link_libraries(sensor_monitor PRIVATE chaos_sensors_ros)

foreach(thermal_target at600_split_node thermal_fusion_node thermal_register_calib)
  add_executable(${thermal_target} src/${thermal_target}.cpp)
  target_include_directories(${thermal_target} PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(${thermal_target} ${OpenCV_LIBS})
  set(_thermal_deps rclcpp sensor_msgs cv_bridge)
  if(NOT thermal_target STREQUAL at600_split_node)
    # the fusion/calibration nodes consume image_transport publishers
    list(APPEND _thermal_deps image_transport)
  endif()
  ament_target_dependencies(${thermal_target} ${_thermal_deps})
endforeach()

install(DIRECTORY include/ DESTINATION include)
install(TARGETS chaos_sensors_health chaos_sensors_config chaos_sensors_ros
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(TARGETS mock_sensor sensor_monitor at600_split_node thermal_fusion_node
  thermal_register_calib
  DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
ament_export_dependencies(chaos_core rclcpp sensor_msgs diagnostic_msgs std_msgs nlohmann_json)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(rosgraph_msgs REQUIRED)
  ament_add_gtest(test_health_cpp test/test_health_cpp.cpp)
  target_link_libraries(test_health_cpp chaos_sensors_health)
  # The ROS-level clock test: the mock sensor follows the ROS clock, the
  # monitor follows the steady clock. It needs the node factories, so it
  # links the ROS facade.
  ament_add_gtest(test_sensor_clock_cpp test/test_sensor_clock_cpp.cpp)
  target_link_libraries(test_sensor_clock_cpp chaos_sensors_ros
    chaos_core::chaos_core rosgraph_msgs::rosgraph_msgs__rosidl_typesupport_cpp)
endif()

ament_package()
