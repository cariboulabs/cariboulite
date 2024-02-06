/* -*- c++ -*- */
/*
 * Copyright 2023 CaribouLabs LTD.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef INCLUDED_CARIBOULITE_CARIBOULITESOURCE_H
#define INCLUDED_CARIBOULITE_CARIBOULITESOURCE_H

#include <gnuradio/caribouLite/api.h>
#include <gnuradio/sync_block.h>

namespace gr {
  namespace caribouLite {

    /*!
     * \brief <+description of block+>
     * \ingroup caribouLite
     *
     */
    class CARIBOULITE_API caribouLiteSource : virtual public gr::sync_block
    {
     public:
      typedef std::shared_ptr<caribouLiteSource> sptr;

      /*!
       * \brief Return a shared_ptr to a new instance of caribouLite::caribouLiteSource.
       *
       * To avoid accidental use of raw pointers, caribouLite::caribouLiteSource's
       * constructor is in a private implementation
       * class. caribouLite::caribouLiteSource::make is the public interface for
       * creating new instances.
       */
      static sptr make(int channel=0,
                      bool enable_agc=false,
                      float rx_gain=40,
                      float rx_bw=2500000,
                      float sample_rate=4000000,
                      float freq=900000000,
                      bool provide_sync = false);
    };

  } // namespace caribouLite
} // namespace gr

#endif /* INCLUDED_CARIBOULITE_CARIBOULITESOURCE_H */
