#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "rcl_logging_spdlog::rcl_logging_spdlog" for configuration "Debug"
set_property(TARGET rcl_logging_spdlog::rcl_logging_spdlog APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rcl_logging_spdlog::rcl_logging_spdlog PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/librcl_logging_spdlog.so"
  IMPORTED_SONAME_DEBUG "librcl_logging_spdlog.so"
  )

list(APPEND _IMPORT_CHECK_TARGETS rcl_logging_spdlog::rcl_logging_spdlog )
list(APPEND _IMPORT_CHECK_FILES_FOR_rcl_logging_spdlog::rcl_logging_spdlog "${_IMPORT_PREFIX}/lib/librcl_logging_spdlog.so" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
