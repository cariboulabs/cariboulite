/* -*- c++ -*- */
/*
 * Copyright 2023 CaribouLabs LTD.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gnuradio/io_signature.h>
#include "caribouLiteSource_impl.h"

namespace gr {
    namespace caribouLite {
        
        #define NUM_NATIVE_MTUS_PER_QUEUE		( 10 )
        #define USE_ASYNC_OVERRIDE_WRITES       ( true )
        #define USE_ASYNC_BLOCK_READS           ( true )
        
        using output_type = gr_complex;

        void detectBoard()
        {
            CaribouLite::SysVersion ver;
            std::string name;
            std::string guid;
            
            if (CaribouLite::DetectBoard(&ver, name, guid))
            {
                std::cout << "Detected Version: " << CaribouLite::GetSystemVersionStr(ver) << ", Name: " << name << ", GUID: " << guid << std::endl;
            }
            else
            {
                std::cout << "Undetected CaribouLite!" << std::endl;
            }    
        }

        //-------------------------------------------------------------------------------------------------------------
        caribouLiteSource::sptr caribouLiteSource::make(int channel, bool enable_agc, float rx_gain, float rx_bw, float sample_rate, float freq)
        {
            return gnuradio::make_block_sptr<caribouLiteSource_impl>(channel, enable_agc, rx_gain, rx_bw, sample_rate, freq);
        }


        // private constructor
        //-------------------------------------------------------------------------------------------------------------
        caribouLiteSource_impl::caribouLiteSource_impl(int channel, bool enable_agc, float rx_gain, float rx_bw, float sample_rate, float freq)
                        : gr::sync_block("caribouLiteSource",
                          gr::io_signature::make(0, 0, 0),
                          gr::io_signature::make(1 /* min outputs */, 1 /*max outputs */, sizeof(output_type)))
        {
            _rx_queue = NULL;
            _channel = (CaribouLiteRadio::RadioType)channel;
            _enable_agc = enable_agc;
            _rx_gain = rx_gain;
            _rx_bw = rx_bw;
            _sample_rate = sample_rate;
            _frequency = freq;
            detectBoard();
            CaribouLite &cl = CaribouLite::GetInstance();
            _cl = &cl;
            _radio = cl.GetRadioChannel(_channel);
            
            _mtu_size = _radio->GetNativeMtuSample();
            
            _rx_queue = new circular_buffer<gr_complex>(_mtu_size * NUM_NATIVE_MTUS_PER_QUEUE, 
                                                         USE_ASYNC_OVERRIDE_WRITES, 
                                                         USE_ASYNC_BLOCK_READS);
            
            // setup parameters
            _radio->SetRxGain(rx_gain);
            _radio->SetAgc(enable_agc);
            _radio->SetRxBandwidth(rx_bw);
            _radio->SetRxSampleRate(sample_rate);
            _radio->SetFrequency(freq);
            _radio->StartReceiving([this](CaribouLiteRadio* radio, const std::complex<float>* samples, CaribouLiteMeta* sync, size_t num_samples) {
                receivedSamples(radio, samples, sync, num_samples);
            });
        }

        // virtual destructor
        //-------------------------------------------------------------------------------------------------------------
        caribouLiteSource_impl::~caribouLiteSource_impl()
        {
            _radio->StopReceiving();
            if (_rx_queue) delete _rx_queue;
        }
        
        // Receive samples callback
        //-------------------------------------------------------------------------------------------------------------
        void caribouLiteSource_impl::receivedSamples(CaribouLiteRadio* radio, const std::complex<float>* samples, CaribouLiteMeta* sync, size_t num_samples)
        {
            //std::cout << "Radio: " << radio->GetRadioName() << " Received " << std::dec << num_samples << " samples" << std::endl;
            _rx_queue->put(static_cast<const gr_complex*>(samples), num_samples);
        }

        //-------------------------------------------------------------------------------------------------------------
        int caribouLiteSource_impl::work(   int noutput_items,
                                            gr_vector_const_void_star &input_items,
                                            gr_vector_void_star &output_items)
        {
            auto out = static_cast<output_type*>(output_items[0]);
            size_t num_read = _rx_queue->get(out, noutput_items);
            return noutput_items;
        }

    } /* namespace caribouLite */
} /* namespace gr */
