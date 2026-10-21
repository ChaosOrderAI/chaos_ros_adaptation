#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "chaos_data::chaos_data_timing" for configuration "RelWithDebInfo"
set_property(TARGET chaos_data::chaos_data_timing APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(chaos_data::chaos_data_timing PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/libchaos_data_timing.a"
  )

list(APPEND _cmake_import_check_targets chaos_data::chaos_data_timing )
list(APPEND _cmake_import_check_files_for_chaos_data::chaos_data_timing "${_IMPORT_PREFIX}/lib/libchaos_data_timing.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
