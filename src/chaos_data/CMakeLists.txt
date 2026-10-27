cmake_minimum_required(VERSION 3.20)
project(chaos_data LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(chaos_core REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(keyboard_handler REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rosbag2_cpp REQUIRED)
find_package(rosbag2_storage REQUIRED)
find_package(rosbag2_transport REQUIRED)
find_package(rosidl_typesupport_introspection_cpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(std_msgs REQUIRED)
find_package(diagnostic_msgs REQUIRED)
find_package(chaos_interfaces REQUIRED)
find_package(chaos_assets REQUIRED)
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Domain libraries (no ROS dependency)
# ---------------------------------------------------------------------------

# Clock quality and monotonic admission checks.
add_library(chaos_data_timing src/timing.cpp)
target_compile_features(chaos_data_timing PUBLIC cxx_std_17)
target_include_directories(chaos_data_timing PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_data_timing PUBLIC chaos_core::chaos_core)

# Durable session metadata.
add_library(chaos_data_session src/session.cpp)
target_compile_features(chaos_data_session PUBLIC cxx_std_17)
target_include_directories(chaos_data_session PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_data_session PUBLIC
  chaos_data_timing chaos_core::chaos_core nlohmann_json::nlohmann_json)

# Exclusive file output and path containment helpers. The audit index and the
# exported dataset are evidence, so they are never written over an existing
# file.
add_library(chaos_data_file_io src/file_io.cpp)
target_compile_features(chaos_data_file_io PUBLIC cxx_std_17)
target_include_directories(chaos_data_file_io PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

# Bounded supervision of the isolated native recorder worker.
add_library(chaos_data_supervisor src/recorder_supervisor.cpp)
target_compile_features(chaos_data_supervisor PUBLIC cxx_std_17)
target_include_directories(chaos_data_supervisor PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_data_supervisor PUBLIC
  chaos_data_timing chaos_core::chaos_core nlohmann_json::nlohmann_json
  ament_index_cpp::ament_index_cpp)

# Clock gated collection supervisor.
add_library(chaos_data_controller src/controller.cpp)
target_compile_features(chaos_data_controller PUBLIC cxx_std_17)
target_include_directories(chaos_data_controller PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_data_controller PUBLIC
  chaos_data_session chaos_data_supervisor chaos_data_timing
  chaos_core::chaos_core nlohmann_json::nlohmann_json)

# ---------------------------------------------------------------------------
# Isolated native recorder worker (separate process, never forked into ROS)
# ---------------------------------------------------------------------------

add_executable(chaos_data_recorder_worker src/recorder_worker_main.cpp)
target_compile_features(chaos_data_recorder_worker PUBLIC cxx_std_17)
target_include_directories(chaos_data_recorder_worker PRIVATE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(chaos_data_recorder_worker PRIVATE
  chaos_core::chaos_core
  rclcpp::rclcpp
  rosbag2_cpp::rosbag2_cpp
  rosbag2_storage::rosbag2_storage
  rosbag2_transport::rosbag2_transport
  keyboard_handler::keyboard_handler
  nlohmann_json::nlohmann_json
  Threads::Threads)

install(TARGETS chaos_data_recorder_worker
  RUNTIME DESTINATION lib/${PROJECT_NAME})

# Production entry points. The installed names keep the original console
# scripts: collector, audit, data_audit and data_export.
add_executable(chaos_data_collector_cli src/collector_main.cpp)
target_compile_features(chaos_data_collector_cli PRIVATE cxx_std_17)
target_link_libraries(chaos_data_collector_cli PRIVATE
  chaos_data_controller
  chaos_data_session
  chaos_data_supervisor
  chaos_data_timing
  chaos_core::chaos_core
  nlohmann_json::nlohmann_json
  rclcpp::rclcpp
  std_msgs::std_msgs__rosidl_typesupport_cpp
  diagnostic_msgs::diagnostic_msgs__rosidl_typesupport_cpp)
install(PROGRAMS $<TARGET_FILE:chaos_data_collector_cli>
  DESTINATION lib/${PROJECT_NAME} RENAME collector)

add_executable(chaos_data_audit_cli src/audit_main.cpp)
target_compile_features(chaos_data_audit_cli PRIVATE cxx_std_17)
target_link_libraries(chaos_data_audit_cli PRIVATE
  chaos_data_audit
  chaos_data_file_io
  nlohmann_json::nlohmann_json)
install(PROGRAMS $<TARGET_FILE:chaos_data_audit_cli>
  DESTINATION lib/${PROJECT_NAME} RENAME audit)
install(PROGRAMS $<TARGET_FILE:chaos_data_audit_cli>
  DESTINATION lib/${PROJECT_NAME} RENAME data_audit)

# Native ROS message reading layer. The data plane uses the deserialization
# that the middleware provides: known types through rclcpp::Serialization<T>,
# other types through the introspection metadata and the rmw typesupport. No
# payload is expanded into JSON numbers.
add_library(chaos_data_ros_message src/ros_message.cpp)
target_compile_features(chaos_data_ros_message PUBLIC cxx_std_17)
target_include_directories(chaos_data_ros_message PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_data_ros_message PUBLIC
  rclcpp::rclcpp
  rosidl_typesupport_introspection_cpp::rosidl_typesupport_introspection_cpp)

# Read only source timestamp audit.
add_library(chaos_data_audit src/audit.cpp)
target_compile_features(chaos_data_audit PUBLIC cxx_std_17)
target_include_directories(chaos_data_audit PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_data_audit PUBLIC
  chaos_core::chaos_core
  chaos_data_ros_message
  chaos_data_file_io
  rosbag2_cpp::rosbag2_cpp
  rosbag2_storage::rosbag2_storage
  nlohmann_json::nlohmann_json
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp
  sensor_msgs::sensor_msgs__rosidl_typesupport_cpp)

# Native export of labelled episodes. The profile, the source provenance, the
# typed bag reader, the control labels, the causal resampler, the lossless
# image sink and the atomic publication are separate responsibilities.
add_library(chaos_data_export
  src/export_profile.cpp
  src/export_pipeline.cpp
  src/export_source.cpp
  src/export_records.cpp
  src/export_hash.cpp
  src/npy.cpp)
target_compile_features(chaos_data_export PUBLIC cxx_std_17)
target_include_directories(chaos_data_export PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(chaos_data_export PUBLIC
  chaos_core::chaos_core
  chaos_data_ros_message
  chaos_data_file_io
  rosbag2_cpp::rosbag2_cpp
  rosbag2_storage::rosbag2_storage
  nlohmann_json::nlohmann_json
  yaml-cpp::yaml-cpp
  chaos_interfaces::chaos_interfaces__rosidl_typesupport_cpp
  sensor_msgs::sensor_msgs__rosidl_typesupport_cpp
  chaos_assets::chaos_assets_core
  Boost::headers
  OpenSSL::Crypto)

add_executable(chaos_data_export_cli src/export_main.cpp)
target_compile_features(chaos_data_export_cli PRIVATE cxx_std_17)
target_link_libraries(chaos_data_export_cli PRIVATE
  chaos_data_export
  nlohmann_json::nlohmann_json)
install(PROGRAMS $<TARGET_FILE:chaos_data_export_cli>
  DESTINATION lib/${PROJECT_NAME} RENAME data_export)

install(DIRECTORY include/ DESTINATION include)
install(TARGETS chaos_data_timing chaos_data_session chaos_data_supervisor
                chaos_data_controller chaos_data_ros_message chaos_data_audit
                chaos_data_file_io chaos_data_export
  EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin)
ament_export_targets(export_${PROJECT_NAME} HAS_LIBRARY_TARGET)
ament_export_include_directories(include)
# Every package that an exported target links PUBLIC has to be part of the
# exported dependencies, so a downstream consumer needs nothing but
# find_package(chaos_data) to link the libraries. The list covers the
# public headers (nlohmann_json, Boost, sensor_msgs, rclcpp) and the public
# link libraries of the export (chaos_assets, yaml-cpp, OpenSSL,
# chaos_interfaces, rosbag2).
ament_export_dependencies(chaos_core nlohmann_json ament_index_cpp rclcpp
                           rosbag2_cpp rosbag2_storage
                           rosidl_typesupport_introspection_cpp
                           chaos_assets chaos_interfaces sensor_msgs
                           yaml-cpp Boost OpenSSL)

install(DIRECTORY config/ DESTINATION share/${PROJECT_NAME}/config)
install(FILES resource/${PROJECT_NAME}
  DESTINATION share/ament_index/resource_index/packages)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_timing_cpp test/test_timing_cpp.cpp)
  target_link_libraries(test_timing_cpp chaos_data_timing)

  # A real worker executable so the supervisor can be tested against a real
  # process instead of a mocked interface.
  add_executable(fake_recorder_worker test/fake_recorder_worker.cpp)

  ament_add_gtest(test_supervisor_cpp test/test_supervisor_cpp.cpp TIMEOUT 120)
  target_link_libraries(test_supervisor_cpp chaos_data_supervisor)
  target_compile_definitions(test_supervisor_cpp PRIVATE
    CHAOS_DATA_FAKE_WORKER_ENV="CHAOS_DATA_RECORDER_WORKER"
    CHAOS_DATA_FAKE_WORKER_PATH="$<TARGET_FILE:fake_recorder_worker>")
  add_dependencies(test_supervisor_cpp fake_recorder_worker)

  ament_add_gtest(test_audit_cpp test/test_audit_cpp.cpp TIMEOUT 120)
  target_link_libraries(test_audit_cpp
    chaos_data_audit
    rosbag2_cpp::rosbag2_cpp
    rosbag2_storage::rosbag2_storage
    sensor_msgs::sensor_msgs__rosidl_typesupport_cpp)

  # The introspection driven decoder: the fallback path for every type that
  # the audit does not know by name.
  ament_add_gtest(test_ros_message_cpp test/test_ros_message_cpp.cpp TIMEOUT 120)
  target_link_libraries(test_ros_message_cpp
    chaos_data_ros_message
    geometry_msgs::geometry_msgs__rosidl_typesupport_cpp
    sensor_msgs::sensor_msgs__rosidl_typesupport_cpp)

  # The exclusive file output and the path containment rules.
  ament_add_gtest(test_file_io_cpp test/test_file_io_cpp.cpp TIMEOUT 60)
  target_link_libraries(test_file_io_cpp chaos_data_file_io)

  # The recorder worker control loop: a recorder executor failure on an idle
  # control socket must end the worker unsuccessfully through the bounded
  # poll timeout, never by blocking forever in poll(control_fd, -1); the
  # failed-executor teardown is bounded by the in-process exit watchdog.
  ament_add_gtest(test_recorder_control_cpp test/test_recorder_control_cpp.cpp TIMEOUT 60)
  target_include_directories(test_recorder_control_cpp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(test_recorder_control_cpp
    chaos_core::chaos_core nlohmann_json::nlohmann_json Threads::Threads)

  # The export profile and the lossless image sink.
  ament_add_gtest(test_export_cpp test/test_export_cpp.cpp TIMEOUT 60)
  target_include_directories(test_export_cpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_export_cpp
    chaos_data_export
    sensor_msgs::sensor_msgs__rosidl_typesupport_cpp)

  # The native exporter as a black box: a real bag in both storages, a real
  # session and a real profile, and the exit code of the executable. The
  # path of the executable is the one of the build tree, and it is set with
  # ENV because it is a single path and not an addition to the environment.
  # The generator expression of the path is also the build order: the test
  # depends on the executable.
  find_package(ament_cmake_pytest REQUIRED)
  ament_add_pytest_test(test_export_cli test/test_export_cli.py TIMEOUT 300
    ENV "DATA_EXPORT_EXECUTABLE=$<TARGET_FILE:chaos_data_export_cli>")
endif()

ament_package()
