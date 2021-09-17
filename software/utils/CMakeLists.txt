cmake_minimum_required(VERSION 3.15)
project(cariboulite_utils)
set(CMAKE_BUILD_TYPE Release)

#Bring the headers, such as Student.h into the project
set(SUPER_DIR ${PROJECT_SOURCE_DIR}/..)
include_directories(/.)
include_directories(${SUPER_DIR})

set(SOURCE_GEN_BLOB generate_bin_blob.c)
set(EXTERN_LIBS )

#Generate the static library from the sources
add_executable(generate_bin_blob ${SOURCE_GEN_BLOB})
target_link_libraries(generate_bin_blob ${EXTERN_LIBS})

#Set the location for library installation -- i.e., /usr/lib in this case
# not really necessary in this example. Use "sudo make install" to apply
install(TARGETS generate_bin_blob DESTINATION /usr/lib)