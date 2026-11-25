cmake_minimum_required(VERSION 3.16)
project(chaos_time_sync)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(image_transport REQUIRED)
find_package(chaos_interfaces REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(chaos_time_sync_node src/time_sync_node.cpp)
target_link_libraries(chaos_time_sync_node nlohmann_json::nlohmann_json)
ament_target_dependencies(chaos_time_sync_node
  rclcpp std_msgs sensor_msgs diagnostic_msgs chaos_interfaces)

add_executable(trigger_pair_node src/trigger_pair_node.cpp)
ament_target_dependencies(trigger_pair_node
  rclcpp sensor_msgs diagnostic_msgs image_transport chaos_interfaces)

install(TARGETS chaos_time_sync_node trigger_pair_node
  DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})

ament_package()
