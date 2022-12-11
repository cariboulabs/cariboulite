cmake_minimum_required(VERSION 3.15)
project(cariboulite)
set(CMAKE_BUILD_TYPE Release)

#Bring the headers, such as Student.h into the project
set(SUPER_DIR ${PROJECT_SOURCE_DIR}/..)
include_directories(/.)
include_directories(${SUPER_DIR})

#However, the file(GLOB...) allows for wildcard additions:
set(SOURCES_LIB production_utils.c hat_powermon.c cariboulite_production.c)
#add_compile_options(-Wall -Wextra -pedantic -Werror)
add_compile_options(-Wall -Wextra -Wno-missing-braces)

#Generate the static library from the sources
add_library(production_utils STATIC ${SOURCES_LIB})
target_include_directories(production_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
