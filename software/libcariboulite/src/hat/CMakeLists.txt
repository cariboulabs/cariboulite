cmake_minimum_required(VERSION 3.15)
project(cariboulite)
set(CMAKE_BUILD_TYPE Release)

# Bring the headers
set(SUPER_DIR ${PROJECT_SOURCE_DIR}/..)
include_directories(/.)
include_directories(${SUPER_DIR})

# Source files
set(SOURCES_LIB hat.c eeprom_utils.c)
set(SOURCES ${SOURCES_LIB} test_hat.c)
set(EXTERN_LIBS ${SUPER_DIR}/io_utils/build/libio_utils.a ${SUPER_DIR}/zf_log/build/libzf_log.a -lpthread)
add_compile_options(-Wall -Wextra -Wmissing-braces)

#Generate the static library from the sources
add_library(hat STATIC ${SOURCES_LIB})
add_executable(test_hat ${SOURCES})
target_link_libraries(test_hat rt pthread ${EXTERN_LIBS})

# Set the location for library installation -- i.e., /usr/lib in this case
# not really necessary in this example. Use "sudo make install" to apply
install(TARGETS hat DESTINATION /usr/lib)