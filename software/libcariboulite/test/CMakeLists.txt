cmake_minimum_required(VERSION 3.15)
project(cariboulite)
set(CMAKE_BUILD_TYPE Release)

#Bring the headers, such as Student.h into the project
include_directories(./ ../src)
include_directories(${PROJECT_SOURCE_DIR}/..)

set(SOURCES_ICE40_PROG ice40_prog.c)
set(SOURCES_ICE40_PROGRAMMER ice40_programmer.c)
set(SOURCES_FPGA_COMM fpga_comm_test.c)

#Exectuable
add_executable(ice40prog ${SOURCES_ICE40_PROG})
target_link_libraries(ice40prog cariboulite pthread io_utils)

add_executable(ice40programmer ${SOURCES_ICE40_PROGRAMMER})
target_link_libraries(ice40programmer cariboulite pthread io_utils)

add_executable(fpgacomm ${SOURCES_FPGA_COMM})
target_link_libraries(fpgacomm cariboulite pthread io_utils)

#Set the location for library installation -- i.e., /usr/lib in this case
# not really necessary in this example. Use "sudo make install" to apply
install(TARGETS caribou_fpga DESTINATION /usr/lib)