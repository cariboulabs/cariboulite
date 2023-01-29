#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "iir::iir" for configuration "Release"
set_property(TARGET iir::iir APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(iir::iir PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libiir.so.1.9.1"
  IMPORTED_SONAME_RELEASE "libiir.so.1"
  )

list(APPEND _IMPORT_CHECK_TARGETS iir::iir )
list(APPEND _IMPORT_CHECK_FILES_FOR_iir::iir "${_IMPORT_PREFIX}/lib/libiir.so.1.9.1" )

# Import target "iir::iir_static" for configuration "Release"
set_property(TARGET iir::iir_static APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(iir::iir_static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libiir_static.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS iir::iir_static )
list(APPEND _IMPORT_CHECK_FILES_FOR_iir::iir_static "${_IMPORT_PREFIX}/lib/libiir_static.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
