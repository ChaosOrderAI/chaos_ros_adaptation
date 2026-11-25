cmake_minimum_required(VERSION 3.16)
project(chaos_thermal_fusion)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(image_transport REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc features2d calib3d imgcodecs)

add_executable(thermal_fusion_node src/thermal_fusion_node.cpp)
target_include_directories(thermal_fusion_node PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(thermal_fusion_node ${OpenCV_LIBS})
ament_target_dependencies(thermal_fusion_node rclcpp sensor_msgs cv_bridge image_transport)

add_executable(thermal_register_calib src/thermal_register_calib.cpp)
target_include_directories(thermal_register_calib PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(thermal_register_calib ${OpenCV_LIBS})
ament_target_dependencies(thermal_register_calib rclcpp sensor_msgs cv_bridge image_transport)

install(TARGETS thermal_fusion_node thermal_register_calib
  DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME})

ament_package()
