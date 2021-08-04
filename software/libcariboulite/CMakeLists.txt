cmake_minimum_required(VERSION 3.15)
project(cariboulite)
set(CMAKE_BUILD_TYPE Release)

# Bring the headers
include_directories(./ ./include)
include_directories(${PROJECT_SOURCE_DIR}/src)

# ------------------------------------
# MAIN - Source files for main library
# ------------------------------------
set(SOURCES_LIB src/cariboulite.c src/cariboulite_setup.c)

# Add internal project dependencies
add_subdirectory(src/datatypes EXCLUDE_FROM_ALL)
add_subdirectory(src/ustimer EXCLUDE_FROM_ALL)
add_subdirectory(src/caribou_fpga EXCLUDE_FROM_ALL)
add_subdirectory(src/at86rf215 EXCLUDE_FROM_ALL)
add_subdirectory(src/caribou_smi EXCLUDE_FROM_ALL)
add_subdirectory(src/latticeice40 EXCLUDE_FROM_ALL)
add_subdirectory(src/io_utils EXCLUDE_FROM_ALL)
add_subdirectory(src/rffc507x EXCLUDE_FROM_ALL)
add_subdirectory(src/zf_log EXCLUDE_FROM_ALL)

add_library(cariboulite STATIC ${SOURCES_LIB})
target_link_libraries(cariboulite datatypes ustimer caribou_fpga caribou_smi latticeice40 io_utils m rt pthread zf_log)
target_include_directories(cariboulite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ----------------------------------
# TESTS - source files for the tests
# ----------------------------------
set(SOURCES_ICE40_PROGRAMMER test/ice40_programmer.c)
set(SOURCES_FPGA_COMM test/fpga_comm_test.c)

add_executable(ice40programmer ${SOURCES_ICE40_PROGRAMMER})
add_executable(fpgacomm ${SOURCES_FPGA_COMM})

target_link_libraries(ice40programmer cariboulite pthread m rt)
target_link_libraries(fpgacomm cariboulite pthread m rt)

set_target_properties( ice40programmer PROPERTIES RUNTIME_OUTPUT_DIRECTORY test)
set_target_properties( fpgacomm PROPERTIES RUNTIME_OUTPUT_DIRECTORY test)

# ------------
# INSTALLATION
# ------------
# Set the location for library installation -- i.e., /usr/lib in this case
# not really necessary in this example. Use "sudo make install" to apply
install(TARGETS cariboulite DESTINATION /usr/lib)