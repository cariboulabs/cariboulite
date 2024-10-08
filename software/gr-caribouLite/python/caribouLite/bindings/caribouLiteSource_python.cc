/*
 * Copyright 2024 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

/***********************************************************************************/
/* This file is automatically generated using bindtool and can be manually edited  */
/* The following lines can be configured to regenerate this file during cmake      */
/* If manual edits are made, the following tags should be modified accordingly.    */
/* BINDTOOL_GEN_AUTOMATIC(0)                                                       */
/* BINDTOOL_USE_PYGCCXML(0)                                                        */
/* BINDTOOL_HEADER_FILE(caribouLiteSource.h)                                        */
/* BINDTOOL_HEADER_FILE_HASH(08b8583f23bc4fa60b6251c2a4b0fb9f)                     */
/***********************************************************************************/

#include <pybind11/complex.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

#include <gnuradio/caribouLite/caribouLiteSource.h>
// pydoc.h is automatically generated in the build directory
#include <caribouLiteSource_pydoc.h>

void bind_caribouLiteSource(py::module& m)
{

    using caribouLiteSource = ::gr::caribouLite::caribouLiteSource;


    py::class_<caribouLiteSource,
               gr::sync_block,
               gr::block,
               gr::basic_block,
               std::shared_ptr<caribouLiteSource>>(
        m, "caribouLiteSource", D(caribouLiteSource))

        .def(py::init(&caribouLiteSource::make),
             py::arg("channel") = 0,
             py::arg("enable_agc") = false,
             py::arg("rx_gain") = 40,
             py::arg("rx_bw") = 2000000,
             py::arg("sample_rate") = 4000000,
             py::arg("freq") = 900000000,
             py::arg("provide_meta") = false,
             py::arg("pmod_state") = 0,
             D(caribouLiteSource, make))


        ;
}
