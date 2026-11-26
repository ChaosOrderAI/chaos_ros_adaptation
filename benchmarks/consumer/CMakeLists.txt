cmake_minimum_required(VERSION 3.20)
project(chaos_consumer_check LANGUAGES CXX)

# Configured with CMAKE_PREFIX_PATH pointing at the install prefix only.
find_package(chaos_assets REQUIRED)
find_package(chaos_bringup REQUIRED)
find_package(chaos_sdk REQUIRED)
find_package(chaos_mqtt REQUIRED)
find_package(chaos_core REQUIRED)
find_package(chaos_runtime REQUIRED)
find_package(chaos_sensors REQUIRED)
find_package(chaos_data REQUIRED)

add_executable(consumer main.cpp)
target_compile_features(consumer PRIVATE cxx_std_17)
# The BT library only exists when chaos_sdk was installed with its default
# CHAOS_SDK_WITH_BT=ON; turn this off to check a gate-off install prefix.
option(CONSUMER_WITH_BT "Also link the chaos_sdk BT library" ON)
if(CONSUMER_WITH_BT)
  target_link_libraries(consumer PRIVATE chaos_sdk::chaos_sdk_bt)
  target_compile_definitions(consumer PRIVATE CONSUMER_WITH_BT=1)
endif()
target_link_libraries(consumer PRIVATE
  chaos_core::chaos_core
  chaos_runtime::chaos_control
  chaos_runtime::chaos_go2_core
  chaos_sensors::chaos_sensors_health
  chaos_data::chaos_data_timing
  chaos_data::chaos_data_export
  chaos_data::chaos_data_ros_message
  chaos_assets::chaos_assets_core
  chaos_bringup::chaos_bringup_core
  chaos_sdk::chaos_sdk
  chaos_mqtt::chaos_mqtt_core)
