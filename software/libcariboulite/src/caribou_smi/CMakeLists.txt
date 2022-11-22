cmake_minimum_required(VERSION 3.15)
project(cariboulite)
set(CMAKE_BUILD_TYPE Release)

# Bring the headers
#add_subdirectory(kernel "${CMAKE_CURRENT_BINARY_DIR}/kernel_build")

set(SUPER_DIR ${PROJECT_SOURCE_DIR}/..)
include_directories(/.)
include_directories(${SUPER_DIR})

# allows for wildcard additions:
set(SOURCES_LIB caribou_smi.c utils.c caribou_smi_modules.c)
set(SOURCES ${SOURCES_LIB} test_caribou_smi.c)
set(EXTERN_LIBS ${SUPER_DIR}/io_utils/build/libio_utils.a ${SUPER_DIR}/zf_log/build/libzf_log.a -lpthread)
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-braces -Wno-unused-function -O3)

# Generate the static library from the sources
add_library(caribou_smi STATIC ${SOURCES_LIB})
#add_dependencies(caribou_smi smi_modules)

add_executable(test_caribou_smi ${SOURCES})
target_link_libraries(test_caribou_smi ${EXTERN_LIBS} m rt pthread)