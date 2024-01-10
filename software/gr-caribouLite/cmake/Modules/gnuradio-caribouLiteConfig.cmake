find_package(PkgConfig)

PKG_CHECK_MODULES(PC_GR_CARIBOULITE gnuradio-caribouLite)

FIND_PATH(
    GR_CARIBOULITE_INCLUDE_DIRS
    NAMES gnuradio/caribouLite/api.h
    HINTS $ENV{CARIBOULITE_DIR}/include
        ${PC_CARIBOULITE_INCLUDEDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/include
          /usr/local/include
          /usr/include
)

FIND_LIBRARY(
    GR_CARIBOULITE_LIBRARIES
    NAMES gnuradio-caribouLite
    HINTS $ENV{CARIBOULITE_DIR}/lib
        ${PC_CARIBOULITE_LIBDIR}
    PATHS ${CMAKE_INSTALL_PREFIX}/lib
          ${CMAKE_INSTALL_PREFIX}/lib64
          /usr/local/lib
          /usr/local/lib64
          /usr/lib
          /usr/lib64
          )

include("${CMAKE_CURRENT_LIST_DIR}/gnuradio-caribouLiteTarget.cmake")

INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(GR_CARIBOULITE DEFAULT_MSG GR_CARIBOULITE_LIBRARIES GR_CARIBOULITE_INCLUDE_DIRS)
MARK_AS_ADVANCED(GR_CARIBOULITE_LIBRARIES GR_CARIBOULITE_INCLUDE_DIRS)
