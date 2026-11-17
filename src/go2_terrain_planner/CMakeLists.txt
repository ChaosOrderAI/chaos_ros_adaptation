cmake_minimum_required(VERSION 3.16)
project(go2_terrain_planner)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)
find_package(ament_cmake_gtest REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(pcl_conversions REQUIRED)
find_package(PCL REQUIRED COMPONENTS common filters)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2_ros REQUIRED)

add_library(elevation_planner src/elevation_planner.cpp)
target_include_directories(elevation_planner PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(elevation_planner Eigen3::Eigen)

add_executable(terrain_planner_node src/terrain_planner_node.cpp)
target_include_directories(terrain_planner_node PRIVATE ${PCL_INCLUDE_DIRS})
target_link_libraries(terrain_planner_node elevation_planner ${PCL_LIBRARIES})
ament_target_dependencies(terrain_planner_node
  geometry_msgs nav_msgs pcl_conversions rclcpp sensor_msgs tf2_ros)

install(TARGETS elevation_planner terrain_planner_node
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  ament_add_gtest(test_elevation_planner test/test_elevation_planner.cpp)
  target_link_libraries(test_elevation_planner elevation_planner)
endif()

ament_export_include_directories(include)
ament_export_libraries(elevation_planner)
ament_package()
