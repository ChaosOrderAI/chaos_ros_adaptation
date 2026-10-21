#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "chaos_runtime::chaos_control" for configuration "Release"
set_property(TARGET chaos_runtime::chaos_control APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(chaos_runtime::chaos_control PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libchaos_control.a"
  )

list(APPEND _cmake_import_check_targets chaos_runtime::chaos_control )
list(APPEND _cmake_import_check_files_for_chaos_runtime::chaos_control "${_IMPORT_PREFIX}/lib/libchaos_control.a" )

# Import target "chaos_runtime::chaos_go2_core" for configuration "Release"
set_property(TARGET chaos_runtime::chaos_go2_core APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(chaos_runtime::chaos_go2_core PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libchaos_go2_core.a"
  )

list(APPEND _cmake_import_check_targets chaos_runtime::chaos_go2_core )
list(APPEND _cmake_import_check_files_for_chaos_runtime::chaos_go2_core "${_IMPORT_PREFIX}/lib/libchaos_go2_core.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
