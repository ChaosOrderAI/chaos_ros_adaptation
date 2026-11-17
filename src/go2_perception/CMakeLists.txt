cmake_minimum_required(VERSION 3.16)
project(go2_perception)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(pcl_conversions REQUIRED)
find_package(PCL REQUIRED COMPONENTS common filters)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_sensor_msgs REQUIRED)

add_executable(obstacle_cloud_node src/obstacle_cloud_node.cpp)
target_include_directories(obstacle_cloud_node PRIVATE ${PCL_INCLUDE_DIRS})
target_link_libraries(obstacle_cloud_node ${PCL_LIBRARIES})
ament_target_dependencies(obstacle_cloud_node
  geometry_msgs pcl_conversions rclcpp sensor_msgs tf2 tf2_ros tf2_sensor_msgs)

install(TARGETS obstacle_cloud_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})
ament_package()
