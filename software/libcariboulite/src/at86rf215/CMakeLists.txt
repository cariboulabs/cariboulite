cmake_minimum_required(VERSION 3.15)
project(cariboulite)
set(CMAKE_BUILD_TYPE Release)

#Bring the headers, such as Student.h into the project
set(SUPER_DIR ${PROJECT_SOURCE_DIR}/..)
include_directories(/.)
include_directories(${SUPER_DIR})

#However, the file(GLOB...) allows for wildcard additions:
set(SOURCES_LIB at86rf215.c at86rf215_events.c at86rf215_radio.c at86rf215_baseband.c)
set(SOURCES ${SOURCES_LIB} test_at86rf215.c)
set(EXTERN_LIBS ${SUPER_DIR}/io_utils/build/libio_utils.a ${SUPER_DIR}/zf_log/build/libzf_log.a)

#Generate the static library from the sources
add_library(at86rf215 STATIC ${SOURCES_LIB})
add_executable(test_at86rf215 ${SOURCES})
target_link_libraries(test_at86rf215 rt pthread ${EXTERN_LIBS})

#Set the location for library installation -- i.e., /usr/lib in this case
# not really necessary in this example. Use "sudo make install" to apply
install(TARGETS at86rf215 DESTINATION /usr/lib)