cmake_minimum_required(VERSION 3.2)
project(test_app)

# Find the package using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(CARIBOULITE REQUIRED cariboulite)

# Add the executable
add_executable(test_app main.cpp)

# Include directories from the cariboulite package
target_include_directories(test_app PRIVATE ${CARIBOULITE_INCLUDE_DIRS})

# Link against the cariboulite library
target_link_libraries(test_app PRIVATE ${CARIBOULITE_LIBRARIES} -lcariboulite)
