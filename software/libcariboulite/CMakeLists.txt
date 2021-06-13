cmake_minimum_required(VERSION 3.15)
project(cariboulite)
set(CMAKE_BUILD_TYPE Release)

#Bring the headers
include_directories(./ ./include)
include_directories(thirdparty/pigpio)
include_directories(${PROJECT_SOURCE_DIR}/src)

#Source files for different builds
set(SOURCES_LIB src/cariboulite.c src/cariboulite_setup.c)

#Add the third party directories

#Add internal project dependencies
add_subdirectory(src/datatypes)
add_subdirectory(src/ustimer)
add_subdirectory(src/caribou_fpga)
add_subdirectory(src/at86rf215)
add_subdirectory(src/caribou_smi)
add_subdirectory(src/latticeice40)
add_subdirectory(src/io_utils)
add_subdirectory(src/rffc507x)
add_subdirectory(test)

#Generate the static library from the sources
add_library(cariboulite STATIC ${SOURCES_LIB})
target_link_libraries(cariboulite datatypes ustimer caribou_fpga caribou_smi latticeice40 io_utils)
target_include_directories(cariboulite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


#Set the location for library installation -- i.e., /usr/lib in this case
# not really necessary in this example. Use "sudo make install" to apply
install(TARGETS cariboulite DESTINATION /usr/lib)