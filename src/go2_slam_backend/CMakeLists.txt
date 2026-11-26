cmake_minimum_required(VERSION 3.16)
project(go2_slam_backend)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_gtest REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(pcl_conversions REQUIRED)
find_package(PCL REQUIRED COMPONENTS common filters registration)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_sensor_msgs REQUIRED)

add_library(se3_graph src/se3_graph.cpp)
target_include_directories(se3_graph PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(se3_graph Eigen3::Eigen)

add_executable(slam_backend_node src/slam_backend_node.cpp)
target_include_directories(slam_backend_node PRIVATE ${PCL_INCLUDE_DIRS})
target_link_libraries(slam_backend_node se3_graph ${PCL_LIBRARIES})
ament_target_dependencies(slam_backend_node
  geometry_msgs nav_msgs pcl_conversions rclcpp sensor_msgs tf2_ros)

# Near-field obstacle cloud. Nothing in the workspace consumes
# /navigation/obstacle_cloud yet; the node is kept buildable for planned
# consumers (stack.launch.py does not start it).
add_executable(obstacle_cloud_node src/obstacle_cloud_node.cpp)
target_include_directories(obstacle_cloud_node PRIVATE ${PCL_INCLUDE_DIRS})
target_link_libraries(obstacle_cloud_node ${PCL_LIBRARIES})
ament_target_dependencies(obstacle_cloud_node
  geometry_msgs pcl_conversions rclcpp sensor_msgs tf2 tf2_ros tf2_sensor_msgs)

install(TARGETS se3_graph slam_backend_node obstacle_cloud_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  ament_add_gtest(test_se3_graph test/test_se3_graph.cpp)
  target_link_libraries(test_se3_graph se3_graph)
endif()

ament_export_include_directories(include)
ament_export_libraries(se3_graph)
ament_package()
