cmake_minimum_required(VERSION 3.20)
project(chaos_core VERSION 0.1.0 LANGUAGES CXX)

find_package(ament_cmake REQUIRED)

add_library(chaos_core INTERFACE)
target_compile_features(chaos_core INTERFACE cxx_std_17)
target_include_directories(chaos_core INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

install(DIRECTORY include/ DESTINATION include)
install(TARGETS chaos_core EXPORT export_${PROJECT_NAME})
ament_export_targets(export_${PROJECT_NAME})
ament_export_include_directories(include)
ament_package()
