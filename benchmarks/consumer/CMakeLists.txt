cmake_minimum_required(VERSION 3.20)
project(chaos_consumer_check LANGUAGES CXX)

# Configured with CMAKE_PREFIX_PATH pointing at the install prefix only.
find_package(chaos_core REQUIRED)
find_package(chaos_runtime REQUIRED)
find_package(chaos_sensors REQUIRED)
find_package(chaos_data REQUIRED)

add_executable(consumer main.cpp)
target_compile_features(consumer PRIVATE cxx_std_17)
target_link_libraries(consumer PRIVATE
  chaos_core::chaos_core
  chaos_runtime::chaos_control
  chaos_runtime::chaos_go2_core
  chaos_sensors::chaos_sensors_health
  chaos_data::chaos_data_timing)
