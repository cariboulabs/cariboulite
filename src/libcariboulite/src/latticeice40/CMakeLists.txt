cmake_minimum_required(VERSION 3.15)
project(cariboulite)
set(CMAKE_BUILD_TYPE Release)

#Bring the headers, such as Student.h into the project
set(SUPER_DIR ${PROJECT_SOURCE_DIR}/..)
include_directories(/.)
include_directories(${SUPER_DIR})

#However, the file(GLOB...) allows for wildcard additions:
set(SOURCES_LIB latticeice40.c)

#Generate the static library from the sources
add_library(latticeice40 STATIC ${SOURCES_LIB})
target_include_directories(latticeice40 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

#Set the location for library installation -- i.e., /usr/lib in this case
# not really necessary in this example. Use "sudo make install" to apply
install(TARGETS latticeice40 DESTINATION /usr/lib)