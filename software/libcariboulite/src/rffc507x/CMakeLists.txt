cmake_minimum_required(VERSION 3.15)
project(cariboulite)
set(CMAKE_BUILD_TYPE Release)

#Bring the headers, such as Student.h into the project
set(SUPER_DIR ${PROJECT_SOURCE_DIR}/..)
include_directories(/.)
include_directories(${SUPER_DIR})

#However, the file(GLOB...) allows for wildcard additions:

set(SOURCES_LIB rffc507x.c)
set(SOURCES rffc507x.c test_rffc507x.c)
set(EXTERN_LIBS ${SUPER_DIR}/io_utils/build/libio_utils.a ${SUPER_DIR}/zf_log/build/libzf_log.a)

#Generate the static library from the sources
add_library(rffc507x STATIC ${SOURCES_LIB})
add_executable(test_rffc507x ${SOURCES})
target_link_libraries(test_rffc507x rt m pthread ${EXTERN_LIBS})

#Set the location for library installation -- i.e., /usr/lib in this case
# not really necessary in this example. Use "sudo make install" to apply
install(TARGETS rffc507x DESTINATION /usr/lib)