cmake_minimum_required(VERSION 3.16)
project(fast_livo2)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Upstream builds with -march=native. Turn it off when the binary is built on
# one machine and run on another.
option(FAST_LIVO2_NATIVE "Compile for the build machine's CPU" ON)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|AARCH64)")
  add_compile_definitions(ARM_ARCH)
  if(FAST_LIVO2_NATIVE)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -mcpu=native -mtune=native -ffast-math")
  endif()
else()
  add_compile_definitions(X86_ARCH)
  if(FAST_LIVO2_NATIVE)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -march=native -mtune=native -funroll-loops")
  endif()
endif()

include(ProcessorCount)
ProcessorCount(N)
if(N GREATER 4)
  add_compile_definitions(MP_EN MP_PROC_NUM=4)
elseif(N GREATER 1)
  add_compile_definitions(MP_EN MP_PROC_NUM=${N})
else()
  add_compile_definitions(MP_PROC_NUM=1)
endif()

find_package(ament_cmake REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs calib3d)
find_package(OpenMP REQUIRED)
find_package(PCL REQUIRED COMPONENTS common filters io)
find_package(pcl_conversions REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(visualization_msgs REQUIRED)

add_library(fast_livo2_core
  src/camera.cpp
  src/frame.cpp
  src/IMU_Processing.cpp
  src/preprocess.cpp
  src/vio.cpp
  src/vision.cpp
  src/visual_point.cpp
  src/voxel_map.cpp)
target_include_directories(fast_livo2_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${PCL_INCLUDE_DIRS}
  ${OpenCV_INCLUDE_DIRS})
target_link_libraries(fast_livo2_core
  Eigen3::Eigen OpenMP::OpenMP_CXX ${PCL_LIBRARIES} ${OpenCV_LIBS})
ament_target_dependencies(fast_livo2_core
  geometry_msgs pcl_conversions rclcpp sensor_msgs visualization_msgs)

add_executable(fastlivo_mapping src/main.cpp src/LIVMapper.cpp)
target_link_libraries(fastlivo_mapping fast_livo2_core)
ament_target_dependencies(fastlivo_mapping
  cv_bridge geometry_msgs nav_msgs rclcpp sensor_msgs tf2_ros visualization_msgs)

install(TARGETS fast_livo2_core
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib)
install(TARGETS fastlivo_mapping RUNTIME DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config launch rviz DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_fast_livo2 test/test_camera.cpp test/test_se3.cpp test/test_preprocess.cpp)
  target_link_libraries(test_fast_livo2 fast_livo2_core)
endif()

ament_package()
