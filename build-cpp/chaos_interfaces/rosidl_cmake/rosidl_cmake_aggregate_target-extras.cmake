# generated from rosidl_cmake/cmake/rosidl_cmake_aggregate_target-extras.cmake.in

# Create a convenience aggregate target chaos_interfaces::chaos_interfaces
# that links all generated interface targets, so downstream packages can use
# a single modern CMake target name instead of ${chaos_interfaces_TARGETS}.
if(chaos_interfaces_TARGETS AND NOT TARGET chaos_interfaces::chaos_interfaces)
  add_library(chaos_interfaces::chaos_interfaces INTERFACE IMPORTED)
  set_target_properties(chaos_interfaces::chaos_interfaces PROPERTIES
    INTERFACE_LINK_LIBRARIES "${chaos_interfaces_TARGETS}")
endif()
