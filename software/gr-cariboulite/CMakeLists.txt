# Copyright 2011-2021 Free Software Foundation, Inc.
#
# This file is part of GNU Radio
#
# SPDX-License-Identifier: GPL-3.0-or-later
#

########################################################################
# Setup dependencies
########################################################################

########################################################################
# External dependencies
########################################################################
find_package(cariboulite)

########################################################################
# Register component
########################################################################
include(GrComponent)
GR_REGISTER_COMPONENT("gr-cariboulite" ENABLE_GR_CARIBOULITE
    CARIBOULITE_FOUND
    ENABLE_GNURADIO_RUNTIME
)
SET(GR_PKG_CARIBOULITE_EXAMPLES_DIR ${GR_PKG_DATA_DIR}/examples/cariboulite)

########################################################################
# Begin conditional configuration
########################################################################
if(ENABLE_GR_CARIBOULITE)
message(STATUS "  CariobouLite Version: ${CaribouLite_VERSION}")

########################################################################
# Add subdirectories
########################################################################
add_subdirectory(include/gnuradio/cariboulite)
add_subdirectory(lib)
add_subdirectory(docs)
if(ENABLE_PYTHON)
    add_subdirectory(python/cariboulite)
    if(ENABLE_EXAMPLES)
        add_subdirectory(examples/grc)
    endif(ENABLE_EXAMPLES)
endif(ENABLE_PYTHON)
if(ENABLE_GRC)
    add_subdirectory(grc)
endif(ENABLE_GRC)

########################################################################
# Create Pkg Config File
########################################################################
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/gnuradio-cariboulite.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/gnuradio-cariboulite.pc
@ONLY)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/gnuradio-cariboulite.pc
    DESTINATION ${GR_LIBRARY_DIR}/pkgconfig
)

endif(ENABLE_GR_CARIBOULITE)
