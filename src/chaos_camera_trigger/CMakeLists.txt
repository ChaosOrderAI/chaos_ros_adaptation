cmake_minimum_required(VERSION 3.16)
project(chaos_camera_trigger)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(image_transport REQUIRED)
find_package(chaos_interfaces REQUIRED)

add_executable(trigger_pair_node src/trigger_pair_node.cpp)
ament_target_dependencies(trigger_pair_node
  rclcpp sensor_msgs diagnostic_msgs image_transport chaos_interfaces)

install(TARGETS trigger_pair_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})

ament_package()
