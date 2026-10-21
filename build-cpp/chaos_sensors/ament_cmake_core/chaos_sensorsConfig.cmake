# generated from ament/cmake/core/templates/nameConfig.cmake.in

# prevent multiple inclusion
if(_chaos_sensors_CONFIG_INCLUDED)
  # ensure to keep the found flag the same
  if(NOT DEFINED chaos_sensors_FOUND)
    # explicitly set it to FALSE, otherwise CMake will set it to TRUE
    set(chaos_sensors_FOUND FALSE)
  elseif(NOT chaos_sensors_FOUND)
    # use separate condition to avoid uninitialized variable warning
    set(chaos_sensors_FOUND FALSE)
  endif()
  return()
endif()
set(_chaos_sensors_CONFIG_INCLUDED TRUE)

# output package information
if(NOT chaos_sensors_FIND_QUIETLY)
  message(STATUS "Found chaos_sensors: 0.1.0 (${chaos_sensors_DIR})")
endif()

# warn when using a deprecated package
if(NOT "" STREQUAL "")
  set(_msg "Package 'chaos_sensors' is deprecated")
  # append custom deprecation text if available
  if(NOT "" STREQUAL "TRUE")
    set(_msg "${_msg} ()")
  endif()
  # optionally quiet the deprecation message
  if(NOT chaos_sensors_DEPRECATED_QUIET)
    message(DEPRECATION "${_msg}")
  endif()
endif()

# flag package as ament-based to distinguish it after being find_package()-ed
set(chaos_sensors_FOUND_AMENT_PACKAGE TRUE)

# include all config extra files
set(_extras "ament_cmake_export_targets-extras.cmake;ament_cmake_export_include_directories-extras.cmake;ament_cmake_export_dependencies-extras.cmake")
foreach(_extra ${_extras})
  include("${chaos_sensors_DIR}/${_extra}")
endforeach()
