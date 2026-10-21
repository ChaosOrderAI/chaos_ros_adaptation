cmake_minimum_required(VERSION 3.20)
project(chaos_benchmarks LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(chaos_runtime REQUIRED)
find_package(chaos_sensors REQUIRED)
find_package(nlohmann_json REQUIRED)

add_executable(micro_benchmark micro_benchmark.cpp)
target_compile_features(micro_benchmark PRIVATE cxx_std_17)
target_link_libraries(micro_benchmark PRIVATE
  chaos_runtime::chaos_control
  chaos_sensors::chaos_sensors_health
  nlohmann_json::nlohmann_json)

install(TARGETS micro_benchmark DESTINATION lib/${PROJECT_NAME})

ament_package()
