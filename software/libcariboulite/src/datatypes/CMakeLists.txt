cmake_minimum_required(VERSION 3.15)
project(cariboulite)
set(CMAKE_BUILD_TYPE Release)

#Bring the headers, such as Student.h into the project
set(SUPER_DIR ${PROJECT_SOURCE_DIR}/..)
include_directories(/.)
include_directories(${SUPER_DIR})

#However, the file(GLOB...) allows for wildcard additions:
set(SOURCES_LIB tsqueue.c tiny_list.c circular_buffer.cpp entropy.c)
#add_compile_options(-Wall -Wextra -pedantic -Werror)
add_compile_options(-Wall -Wextra -pedantic -Wno-missing-braces)

#Generate the static library from the sources
add_library(datatypes STATIC ${SOURCES_LIB})
target_include_directories(datatypes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(test_tsqueue test_tsqueue.c)
target_link_libraries(test_tsqueue datatypes pthread)

add_executable(test_circular_buffer test_circular_buffer.cpp)
target_link_libraries(test_circular_buffer datatypes pthread)

add_executable(test_tiny_list test_tiny_list.c)
target_link_libraries(test_tiny_list datatypes pthread)

#Set the location for library installation -- i.e., /usr/lib in this case
# not really necessary in this example. Use "sudo make install" to apply
install(TARGETS datatypes DESTINATION /usr/lib)