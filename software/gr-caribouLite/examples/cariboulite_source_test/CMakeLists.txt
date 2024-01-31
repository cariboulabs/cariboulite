# GNU Radio C++ Flow Graph CMakeLists.txt
#
# Title: Cariboulite Source Test
# Author: Alon and David
# GNU Radio version: v3.11.0.0git-604-gd7f88722


cmake_minimum_required(VERSION 3.8)
set(CMAKE_CXX_STANDARD 14)

project(cariboulite_source_test)

find_package(Gnuradio "3.11" COMPONENTS
    blocks
    fec
    fft
    filter
    analog
    digital
    dtv
    audio
    channels
    qtgui
    trellis
    uhd
    video-sdl
    vocoder
    wavelet
    zeromq
)




find_package(gnuradio-caribouLite)
add_executable(cariboulite_source_test cariboulite_source_test.cpp)
target_link_libraries(cariboulite_source_test
    gnuradio::gnuradio-blocks
    gnuradio::gnuradio-caribouLite
)

