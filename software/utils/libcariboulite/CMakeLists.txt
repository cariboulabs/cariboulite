cmake_minimum_required(VERSION 3.15)
project(cariboulite 
        VERSION 1.2.0
        LANGUAGES C CXX
        DESCRIPTION "CaribouLite RpiSDR C API")
        
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Bring the headers
include_directories(./src)
include_directories(${PROJECT_SOURCE_DIR}/src)
add_compile_options(-Wall -Wextra -Wno-unused-variable -Wno-missing-braces)

# ------------------------------------
# MAIN - Source files for main library
# ------------------------------------
set(SOURCES_LIB src/cariboulite.c src/cariboulite_setup.c src/cariboulite_events.c src/cariboulite_radio.c)
set(TARGET_LINK_LIBS    datatypes
                        production_utils
                        caribou_fpga
                        at86rf215
                        rffc507x
                        caribou_smi
                        caribou_prog
                        hat
                        io_utils
                        zf_log
                        rt
                        m
                        pthread)

set(SOURCES_CPP_LIB src/CaribouLiteCpp.cpp src/CaribouLiteRadioCpp.cpp)

# Add internal project dependencies
add_subdirectory(src/datatypes EXCLUDE_FROM_ALL)
add_subdirectory(src/caribou_programming EXCLUDE_FROM_ALL)
add_subdirectory(src/caribou_fpga EXCLUDE_FROM_ALL)
add_subdirectory(src/at86rf215 EXCLUDE_FROM_ALL)
add_subdirectory(src/caribou_smi EXCLUDE_FROM_ALL)
add_subdirectory(src/io_utils EXCLUDE_FROM_ALL)
add_subdirectory(src/rffc507x EXCLUDE_FROM_ALL)
add_subdirectory(src/hat EXCLUDE_FROM_ALL)
add_subdirectory(src/production_utils EXCLUDE_FROM_ALL)
add_subdirectory(src/zf_log EXCLUDE_FROM_ALL)
add_subdirectory(src/iir EXCLUDE_FROM_ALL)

# Create the library cariboulite
add_library(cariboulite STATIC ${SOURCES_LIB} ${SOURCES_CPP_LIB})
target_link_libraries(cariboulite PRIVATE ${TARGET_LINK_LIBS})                                                                  
set_target_properties(cariboulite PROPERTIES PUBLIC_HEADER "src/cariboulite.h;src/cariboulite_radio.h;src/CaribouLite.hpp")
set_target_properties(cariboulite PROPERTIES OUTPUT_NAME cariboulite)

add_library(cariboulite_shared SHARED ${SOURCES_LIB} ${SOURCES_CPP_LIB})
target_link_libraries(cariboulite_shared PRIVATE ${TARGET_LINK_LIBS})                                                                  
set_target_properties(cariboulite_shared PROPERTIES PUBLIC_HEADER "src/cariboulite.h;src/cariboulite_radio.h;src/CaribouLite.hpp")
set_property(TARGET cariboulite_shared PROPERTY POSITION_INDEPENDENT_CODE 1)
set_target_properties(cariboulite_shared PROPERTIES OUTPUT_NAME cariboulite)



#--------------------------------------
# create the Soapy shared object
#--------------------------------------
find_package(SoapySDR CONFIG)
if (NOT SoapySDR_FOUND)
    message(WARNING "SoapySDR development files not found - skipping support")
    return()
endif ()

if(CMAKE_COMPILER_IS_GNUCXX)
    include(CheckCXXCompilerFlag)
    CHECK_CXX_COMPILER_FLAG("-std=c++11" HAS_STD_CXX11)
    if(HAS_STD_CXX11)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O3")
    else(HAS_STD_CXX11)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -O3")
    endif()
    #Thread support enabled (not the same as -lpthread)
    list(APPEND SOAPY_CARIBOULITE_LIBRARIES)
    add_definitions(-Wno-unused-parameter -Wno-missing-field-initializers -Wno-parentheses -Wno-psabi)
endif(CMAKE_COMPILER_IS_GNUCXX)

execute_process (
    COMMAND bash -c "SoapySDRUtil --info | grep 'Search path' | cut -d':' -f2 | xargs | cut -d' ' -f1 | xargs"
    OUTPUT_VARIABLE SOAPY_DEST
)
string(STRIP ${SOAPY_DEST} SOAPY_DEST)

execute_process (
    COMMAND bash -c "SoapySDRUtil --info | grep 'Search path' | cut -d':' -f2 | xargs | cut -d' ' -f1 | awk -F '/lib/' '{print $1}' | xargs"
    OUTPUT_VARIABLE BIN_DEST
)
string(STRIP ${BIN_DEST} BIN_DEST)

SOAPY_SDR_MODULE_UTIL(
    TARGET SoapyCariboulite
    SOURCES
        src/soapy_api/SoapyCariboulite.cpp
        src/soapy_api/Cariboulite.cpp
        src/soapy_api/Cariboulite.hpp
        src/soapy_api/CaribouliteStreamFunctions.cpp
        src/soapy_api/CaribouliteStream.cpp
        src/soapy_api/CaribouliteSession.cpp
        src/soapy_api/CaribouliteSensors.cpp
    LIBRARIES cariboulite iir
    DESTINATION ${SOAPY_DEST}
    PREFIX ""
)

# ----------------------------------
# TESTS - source files for the tests
# ----------------------------------
set(SOURCES_CARIBOU_PROGRAMMER test/caribou_programmer.c)
set(SOURCES_FPGA_COMM test/fpga_comm_test.c)
set(SOURCES_TEST_MAIN src/cariboulite_test_app.c src/app_menu.c)
set(SOURCES_MAIN src/cariboulite_util.c)
set(SOURCES_PROD src/cariboulite_production.c)

add_executable(caribou_programmer ${SOURCES_CARIBOU_PROGRAMMER})
add_executable(fpgacomm ${SOURCES_FPGA_COMM})
add_executable(cariboulite_test_app ${SOURCES_TEST_MAIN})
add_executable(cariboulite_util ${SOURCES_MAIN})

target_link_libraries(caribou_programmer cariboulite)
target_link_libraries(fpgacomm cariboulite)
target_link_libraries(cariboulite_test_app cariboulite)
target_link_libraries(cariboulite_util cariboulite)

set_target_properties( caribou_programmer PROPERTIES RUNTIME_OUTPUT_DIRECTORY test)
set_target_properties( fpgacomm PROPERTIES RUNTIME_OUTPUT_DIRECTORY test)

# ------------
# INSTALLATION
# ------------

install(TARGETS cariboulite 
        LIBRARY DESTINATION ${BIN_DEST}/lib/
        ARCHIVE DESTINATION ${BIN_DEST}/lib/
        PUBLIC_HEADER DESTINATION ${BIN_DEST}/include/)
        
install(TARGETS cariboulite_shared
        LIBRARY DESTINATION ${BIN_DEST}/lib/
        ARCHIVE DESTINATION ${BIN_DEST}/lib/
        PUBLIC_HEADER DESTINATION ${BIN_DEST}/include/)

# generate pc file for pkg-config
set(PC_FILE cariboulite)
configure_file(cariboulite.pc.in
               cariboulite.pc @ONLY)        
install(FILES ${CMAKE_BINARY_DIR}/cariboulite.pc 
        DESTINATION ${BIN_DEST}/lib/pkgconfig/)
        
#install(TARGETS cariboulite_test_app DESTINATION ${BIN_DEST}/bin/)
install(TARGETS cariboulite_util DESTINATION ${BIN_DEST}/bin/)
