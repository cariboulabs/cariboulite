cmake_minimum_required(VERSION 3.2)
project(cariboulite_util)

# Find the package using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(CARIBOULITE REQUIRED cariboulite)

find_package(libsigmf REQUIRED)

# Add the executable
add_executable(cariboulite_util main.cpp)

# Include directories from the cariboulite package
target_include_directories(cariboulite_util PRIVATE ${CARIBOULITE_INCLUDE_DIRS})

# Link against the cariboulite library
target_link_libraries(cariboulite_util PRIVATE ${CARIBOULITE_LIBRARIES} -lcariboulite libsigmf::libsigmf)
