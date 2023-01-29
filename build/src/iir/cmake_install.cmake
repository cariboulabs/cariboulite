# Install script for directory: /home/pi/projects/cariboulite/software/libcariboulite/src/iir

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  foreach(file
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libiir.so.1.9.1"
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libiir.so.1"
      )
    if(EXISTS "${file}" AND
       NOT IS_SYMLINK "${file}")
      file(RPATH_CHECK
           FILE "${file}"
           RPATH "")
    endif()
  endforeach()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES
    "/home/pi/projects/cariboulite/build/src/iir/libiir.so.1.9.1"
    "/home/pi/projects/cariboulite/build/src/iir/libiir.so.1"
    )
  foreach(file
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libiir.so.1.9.1"
      "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libiir.so.1"
      )
    if(EXISTS "${file}" AND
       NOT IS_SYMLINK "${file}")
      if(CMAKE_INSTALL_DO_STRIP)
        execute_process(COMMAND "/usr/bin/strip" "${file}")
      endif()
    endif()
  endforeach()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libiir.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libiir.so")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libiir.so"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE SHARED_LIBRARY FILES "/home/pi/projects/cariboulite/build/src/iir/libiir.so")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libiir.so" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libiir.so")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libiir.so")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/iir" TYPE FILE FILES
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Biquad.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Butterworth.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Cascade.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/ChebyshevI.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/ChebyshevII.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Common.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Custom.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Layout.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/MathSupplement.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/PoleFilter.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/RBJ.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/State.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Types.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/Iir.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/pi/projects/cariboulite/build/src/iir/libiir_static.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/iir" TYPE FILE FILES
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Biquad.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Butterworth.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Cascade.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/ChebyshevI.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/ChebyshevII.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Common.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Custom.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Layout.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/MathSupplement.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/PoleFilter.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/RBJ.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/State.h"
    "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/iir/Types.h"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES "/home/pi/projects/cariboulite/software/libcariboulite/src/iir/Iir.h")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/iir/iir-config.cmake")
    file(DIFFERENT EXPORT_FILE_CHANGED FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/iir/iir-config.cmake"
         "/home/pi/projects/cariboulite/build/src/iir/CMakeFiles/Export/lib/cmake/iir/iir-config.cmake")
    if(EXPORT_FILE_CHANGED)
      file(GLOB OLD_CONFIG_FILES "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/iir/iir-config-*.cmake")
      if(OLD_CONFIG_FILES)
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/iir/iir-config.cmake\" will be replaced.  Removing files [${OLD_CONFIG_FILES}].")
        file(REMOVE ${OLD_CONFIG_FILES})
      endif()
    endif()
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/iir" TYPE FILE FILES "/home/pi/projects/cariboulite/build/src/iir/CMakeFiles/Export/lib/cmake/iir/iir-config.cmake")
  if("${CMAKE_INSTALL_CONFIG_NAME}" MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/iir" TYPE FILE FILES "/home/pi/projects/cariboulite/build/src/iir/CMakeFiles/Export/lib/cmake/iir/iir-config-release.cmake")
  endif()
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/pi/projects/cariboulite/build/src/iir/test/cmake_install.cmake")
  include("/home/pi/projects/cariboulite/build/src/iir/demo/cmake_install.cmake")

endif()

