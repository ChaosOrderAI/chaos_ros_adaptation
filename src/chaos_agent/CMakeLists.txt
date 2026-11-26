cmake_minimum_required(VERSION 3.20)
project(chaos_agent LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(chaos_core REQUIRED)
find_package(chaos_interfaces REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_library(chaos_agent SHARED
  src/llama_client.cpp
  src/agent_loop.cpp)
target_include_directories(chaos_agent PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_agent PUBLIC
  rclcpp::rclcpp
  rclcpp_action::rclcpp_action
  chaos_core::chaos_core
  chaos_interfaces::chaos_interfaces__rosidl_generator_cpp
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp
  nlohmann_json::nlohmann_json
  CURL::libcurl
  Threads::Threads)

add_executable(chaos_agent_node src/agent_main.cpp)
target_link_libraries(chaos_agent_node PRIVATE
  chaos_agent
  rclcpp::rclcpp
  nlohmann_json::nlohmann_json)

install(TARGETS chaos_agent
  EXPORT export_chaos_agent
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(TARGETS chaos_agent_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

ament_export_targets(export_chaos_agent HAS_LIBRARY_TARGET)
ament_export_dependencies(chaos_core chaos_interfaces nlohmann_json CURL Threads)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_agent_loop test/agent_loop_tests.cpp)
  target_link_libraries(test_agent_loop chaos_agent)
  set_tests_properties(test_agent_loop PROPERTIES TIMEOUT 60 ENVIRONMENT "ROS_DOMAIN_ID=189")
endif()

ament_package()
