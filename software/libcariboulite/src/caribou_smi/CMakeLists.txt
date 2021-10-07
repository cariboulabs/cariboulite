cmake_minimum_required(VERSION 3.15)
project(cariboulite)
set(CMAKE_BUILD_TYPE Release)

#Bring the headers, such as Student.h into the project
set(SUPER_DIR ${PROJECT_SOURCE_DIR}/..)
include_directories(/.)
include_directories(${SUPER_DIR})

#However, the file(GLOB...) allows for wildcard additions:
set(SOURCES_LIB caribou_smi.c)
set(SOURCES ${SOURCES_LIB} test_caribou_smi.c)
set(EXTERN_LIBS ${SUPER_DIR}/io_utils/build/libio_utils.a ${SUPER_DIR}/zf_log/build/libzf_log.a)
#add_compile_options(-Wall -Wextra -pedantic -Werror)
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-braces)

#Generate the static library from the sources
add_library(caribou_smi STATIC ${SOURCES_LIB})
add_executable(test_caribou_smi ${SOURCES})
target_link_libraries(test_caribou_smi ${EXTERN_LIBS} m rt pthread)

#Set the location for library installation -- i.e., /usr/lib in this case
# not really necessary in this example. Use "sudo make install" to apply
install(TARGETS caribou_smi DESTINATION /usr/lib)