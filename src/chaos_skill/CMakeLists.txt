cmake_minimum_required(VERSION 3.20)
project(chaos_skill LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(chaos_core REQUIRED)
find_package(chaos_interfaces REQUIRED)
find_package(chaos_sdk REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

add_library(chaos_skill SHARED
  src/vocabulary.cpp
  src/capability.cpp
  src/executor.cpp
  src/intent_ingress.cpp
  src/server_ports.cpp)
target_include_directories(chaos_skill PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_skill PUBLIC
  chaos_sdk::chaos_sdk
  chaos_core::chaos_core
  chaos_interfaces::chaos_interfaces__rosidl_generator_cpp
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp
  sensor_msgs::sensor_msgs__rosidl_typesupport_cpp
  nlohmann_json::nlohmann_json
  Threads::Threads)

add_executable(chaos_skill_server src/skill_server_main.cpp)
target_link_libraries(chaos_skill_server PRIVATE
  chaos_skill
  rclcpp::rclcpp
  rclcpp_action::rclcpp_action
  chaos_interfaces::chaos_interfaces__rosidl_generator_cpp
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp)

install(TARGETS chaos_skill
  EXPORT export_chaos_skill
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
install(TARGETS chaos_skill_server DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_test REQUIRED)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_vocabulary test/vocabulary_tests.cpp)
  target_link_libraries(test_vocabulary chaos_skill)

  ament_add_gtest(test_skill_layer test/skill_layer_tests.cpp)
  target_link_libraries(test_skill_layer chaos_skill)
  set_tests_properties(test_skill_layer PROPERTIES TIMEOUT 180 ENVIRONMENT "ROS_DOMAIN_ID=187")
endif()

ament_export_targets(export_chaos_skill HAS_LIBRARY_TARGET)
ament_export_dependencies(chaos_core chaos_interfaces chaos_sdk sensor_msgs nlohmann_json Threads)

ament_package()
