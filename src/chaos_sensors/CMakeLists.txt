cmake_minimum_required(VERSION 3.20)
project(chaos_sensors LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(chaos_core REQUIRED)
find_package(chaos_interfaces REQUIRED)
find_package(rclcpp REQUIRED)
find_package(nav_msgs REQUIRED)
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

# --- Sensor acquisition and timing nodes -----------------------------------
# AT600 dual-spectrum split: raw V4L2 capture with kernel buffer timestamps.
add_executable(at600_split_node src/at600_split_node.cpp)
target_include_directories(at600_split_node PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(at600_split_node PRIVATE
  rclcpp::rclcpp
  sensor_msgs::sensor_msgs__rosidl_typesupport_cpp
  cv_bridge::cv_bridge
  ${OpenCV_LIBS})

# Thermal/visible fusion and SIFT+RANSAC homography calibration.
foreach(thermal_target thermal_fusion_node thermal_register_calib)
  add_executable(${thermal_target} src/${thermal_target}.cpp)
  target_include_directories(${thermal_target} PRIVATE ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(${thermal_target} PRIVATE
    rclcpp::rclcpp
    sensor_msgs::sensor_msgs__rosidl_typesupport_cpp
    cv_bridge::cv_bridge
    image_transport::image_transport
    ${OpenCV_LIBS})
endforeach()

# 3D Thermal-LIVO point-cloud coloring node.
add_executable(thermal_cloud_node src/thermal_cloud_node.cpp)
target_include_directories(thermal_cloud_node PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${OpenCV_INCLUDE_DIRS})
target_link_libraries(thermal_cloud_node PRIVATE
  rclcpp::rclcpp
  nav_msgs::nav_msgs__rosidl_typesupport_cpp
  sensor_msgs::sensor_msgs__rosidl_typesupport_cpp
  cv_bridge::cv_bridge
  image_transport::image_transport
  ${OpenCV_LIBS})

# Trigger pairing: stamp externally triggered frames with the board's edge.
add_executable(trigger_pair_node src/trigger_pair_node.cpp)
target_link_libraries(trigger_pair_node PRIVATE
  rclcpp::rclcpp
  sensor_msgs::sensor_msgs__rosidl_typesupport_cpp
  diagnostic_msgs::diagnostic_msgs__rosidl_typesupport_cpp
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp
  image_transport::image_transport)

# Sync board client: PTP-like clock exchange, trigger/IMU reports, diagnostics.
add_executable(chaos_time_sync_node src/time_sync_node.cpp)
target_link_libraries(chaos_time_sync_node PRIVATE
  rclcpp::rclcpp
  sensor_msgs::sensor_msgs__rosidl_typesupport_cpp
  diagnostic_msgs::diagnostic_msgs__rosidl_typesupport_cpp
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp
  nlohmann_json::nlohmann_json)

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config launch docs DESTINATION share/${PROJECT_NAME})
install(TARGETS chaos_sensors_health chaos_sensors_config chaos_sensors_ros
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(TARGETS mock_sensor sensor_monitor at600_split_node
  thermal_fusion_node thermal_register_calib thermal_cloud_node
  trigger_pair_node chaos_time_sync_node
  DESTINATION lib/${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
ament_export_dependencies(chaos_core chaos_interfaces rclcpp nav_msgs sensor_msgs diagnostic_msgs
  std_msgs nlohmann_json cv_bridge image_transport)

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
  ament_add_gtest(test_thermal_cloud_cpp test/test_thermal_cloud_cpp.cpp)
  target_include_directories(test_thermal_cloud_cpp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS})
  target_link_libraries(test_thermal_cloud_cpp ${OpenCV_LIBS})
endif()

ament_package()
