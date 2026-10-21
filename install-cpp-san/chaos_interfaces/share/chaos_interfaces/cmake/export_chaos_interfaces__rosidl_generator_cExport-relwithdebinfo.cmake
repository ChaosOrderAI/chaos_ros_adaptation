#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "chaos_interfaces::chaos_interfaces__rosidl_generator_c" for configuration "RelWithDebInfo"
set_property(TARGET chaos_interfaces::chaos_interfaces__rosidl_generator_c APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(chaos_interfaces::chaos_interfaces__rosidl_generator_c PROPERTIES
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libchaos_interfaces__rosidl_generator_c.so"
  IMPORTED_SONAME_RELWITHDEBINFO "libchaos_interfaces__rosidl_generator_c.so"
  )

list(APPEND _cmake_import_check_targets chaos_interfaces::chaos_interfaces__rosidl_generator_c )
list(APPEND _cmake_import_check_files_for_chaos_interfaces::chaos_interfaces__rosidl_generator_c "${_IMPORT_PREFIX}/lib/libchaos_interfaces__rosidl_generator_c.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
