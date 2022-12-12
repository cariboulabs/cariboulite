cmake_minimum_required(VERSION 3.15)
project(cariboulite)
set(CMAKE_BUILD_TYPE Release)

#Bring the headers, such as Student.h into the project
set(SUPER_DIR ${PROJECT_SOURCE_DIR}/..)
include_directories(/.)
include_directories(${SUPER_DIR})

#However, the file(GLOB...) allows for wildcard additions:
set(SOURCES lcd.c test_lcd.c)
set(SOURCES_LIB production_utils.c hat_powermon.c lcd.c production_testing.c)
#add_compile_options(-Wall -Wextra -pedantic -Werror)
add_compile_options(-Wall -Wextra -Wno-missing-braces)

add_executable(test_lcd ${SOURCES})
target_link_libraries(test_lcd rt m pthread zmq ${SUPER_DIR}/zf_log/build/libzf_log.a ${SUPER_DIR}/io_utils/build/libio_utils.a ${EXTERN_LIBS})

#Generate the static library from the sources
add_library(production_utils STATIC ${SOURCES_LIB})
target_include_directories(production_utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
