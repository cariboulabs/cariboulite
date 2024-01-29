cmake_minimum_required(VERSION 3.1.0)

project(iir VERSION 1.9.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif()

cmake_policy(SET CMP0048 NEW) # set VERSION in project()
cmake_policy(SET CMP0042 NEW) # enable MACOSX_RPATH by default

include(GNUInstallDirs)
add_subdirectory(test)
add_subdirectory(demo)
enable_testing ()

if (MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wconversion -Wextra -pedantic)
endif()

set(LIBSRC
  iir/Biquad.cpp
  iir/Butterworth.cpp
  iir/Cascade.cpp
  iir/ChebyshevI.cpp
  iir/ChebyshevII.cpp
  iir/Custom.cpp
  iir/PoleFilter.cpp
  iir/RBJ.cpp)

set(LIBINCLUDE
  iir/Biquad.h
  iir/Butterworth.h
  iir/Cascade.h
  iir/ChebyshevI.h
  iir/ChebyshevII.h
  iir/Common.h
  iir/Custom.h
  iir/Layout.h
  iir/MathSupplement.h
  iir/PoleFilter.h
  iir/RBJ.h
  iir/State.h
  iir/Types.h)

add_library(iir SHARED ${LIBSRC})
add_library(iir::iir ALIAS iir)

target_include_directories(iir
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>      # for Iir.h
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/iir>  # everything else
)

set_target_properties(iir PROPERTIES
  SOVERSION 1
  VERSION ${PROJECT_VERSION}
  PUBLIC_HEADER Iir.h
  PRIVATE_HEADER "${LIBINCLUDE}")

install(TARGETS iir EXPORT iir-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  PRIVATE_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iir)

configure_file(iir.pc.in iir.pc @ONLY)

add_library(iir_static STATIC ${LIBSRC})
add_library(iir::iir_static ALIAS iir_static)

target_include_directories(iir_static
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>      # for Iir.h
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/iir>  # everything else
)

set_target_properties(iir_static PROPERTIES
  POSITION_INDEPENDENT_CODE TRUE
  VERSION ${PROJECT_VERSION}
  PUBLIC_HEADER Iir.h
  PRIVATE_HEADER "${LIBINCLUDE}")

install(TARGETS iir_static EXPORT iir-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  PRIVATE_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iir)

install(EXPORT iir-targets
  DESTINATION lib/cmake/iir
  NAMESPACE iir::
  FILE iir-config.cmake
)
