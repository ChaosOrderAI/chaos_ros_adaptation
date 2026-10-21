#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "chaos_sensors::chaos_sensors_health" for configuration "Release"
set_property(TARGET chaos_sensors::chaos_sensors_health APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(chaos_sensors::chaos_sensors_health PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libchaos_sensors_health.a"
  )

list(APPEND _cmake_import_check_targets chaos_sensors::chaos_sensors_health )
list(APPEND _cmake_import_check_files_for_chaos_sensors::chaos_sensors_health "${_IMPORT_PREFIX}/lib/libchaos_sensors_health.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
