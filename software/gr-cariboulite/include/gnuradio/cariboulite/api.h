/*
 * gr-soapy: Soapy SDR Radio Module
 *
 *  Copyright (C) 2018
 *  Libre Space Foundation <http://librespacefoundation.org/>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 */

#ifndef INCLUDED_CARIBOULITE_API_H
#define INCLUDED_CARIBOULITE_API_H

#include <gnuradio/attributes.h>

#ifdef gnuradio_cariboulite_EXPORTS
#define CARIBOULITE_API __GR_ATTR_EXPORT
#else
#define CARIBOULITE_API __GR_ATTR_IMPORT
#endif

#endif /* INCLUDED_CARIBOULITE_API_H */
