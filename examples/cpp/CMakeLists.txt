cmake_minimum_required(VERSION 3.15)
project(cariboulite_soapy1090)
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

########################################################################
## Feature registration
########################################################################
#include(FeatureSummary)
#include(CMakeDependentOption)
#cmake_dependent_option(ENABLE_APPS "Enable command line applications" ON "ENABLE_LIBRARY" OFF)
#add_feature_info(Apps ENABLE_APPS "command line applications")
#if (NOT ENABLE_APPS)
#    return()
#endif()

########################################################################
# Build utility executable
########################################################################
add_executable(caribou_dump1090
    dump1090.cpp
	modes.c
)

target_link_libraries(caribou_dump1090 SoapySDR)
#install(TARGETS caribou_dump1090 DESTINATION ${CMAKE_INSTALL_BINDIR})
