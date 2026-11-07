cmake_minimum_required(VERSION 3.20)
project(chaos_bt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(ament_cmake REQUIRED)
find_package(chaos_sdk REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(behaviortree_cpp CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(chaos_bt SHARED
  src/nodes.cpp
  src/register_nodes.cpp)
target_include_directories(chaos_bt PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_bt PUBLIC
  chaos_sdk::chaos_sdk
  behaviortree_cpp::behaviortree_cpp
  ${geometry_msgs_TARGETS}
  Threads::Threads)

install(TARGETS chaos_bt
  EXPORT export_chaos_bt
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)

ament_export_targets(export_chaos_bt HAS_LIBRARY_TARGET)
ament_export_dependencies(chaos_sdk geometry_msgs behaviortree_cpp)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  find_package(rclcpp REQUIRED)
  find_package(rclcpp_action REQUIRED)
  find_package(chaos_interfaces REQUIRED)

  ament_add_gtest(test_nodes test/test_nodes.cpp TIMEOUT 180)
  target_include_directories(test_nodes PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test)
  target_link_libraries(test_nodes
    chaos_bt
    rclcpp::rclcpp
    rclcpp_action::rclcpp_action
    chaos_interfaces::chaos_interfaces__rosidl_generator_cpp
    chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp)
  set_tests_properties(test_nodes PROPERTIES ENVIRONMENT "ROS_DOMAIN_ID=188")
endif()

ament_package()
