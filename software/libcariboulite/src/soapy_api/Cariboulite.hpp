#pragma once

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Logger.h>
#include <SoapySDR/Types.h>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <string>
#include <cstring>
#include <algorithm>
#include <atomic>
#include "cariboulite_setup.h"
#include "cariboulite_radios.h"

#define BUF_LEN			262144
#define BUF_NUM			15
#define BYTES_PER_SAMPLE	2

enum Cariboulite_Format 
{
	CARIBOULITE_FORMAT_FLOAT32	= 0,
	CARIBOULITE_FORMAT_INT16	= 1,
	CARIBOULITE_FORMAT_INT8	        = 2,
	CARIBOULITE_FORMAT_FLOAT64      = 3,
};

/***********************************************************************
 * Device interface
 **********************************************************************/
class Cariboulite : public SoapySDR::Device
{
public:
        /*******************************************************************
         * Constructor / Destructor
         ******************************************************************/
        Cariboulite(const SoapySDR::Kwargs &args);
        virtual ~Cariboulite(void);

        /*******************************************************************
         * Identification API
         ******************************************************************/
        std::string getDriverKey(void) const { return "Cariboulite"; }
        std::string getHardwareKey(void) const { return "Cariboulite Rev2"; }
        SoapySDR::Kwargs getHardwareInfo(void) const;

        /*******************************************************************
         * Channels API
         ******************************************************************/
        size_t getNumChannels(const int direction) const { return 2; }
        bool getFullDuplex(const int direction, const size_t channel) const { return (false); }

        /*******************************************************************
         * Stream API
         ******************************************************************/
        std::vector<std::string> getStreamFormats(const int direction, const size_t channel) const;
        std::string getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const;
        SoapySDR::ArgInfoList getStreamArgsInfo(const int direction, const size_t channel) const;
        SoapySDR::Stream *setupStream(  const int direction, 
                                        const std::string &format, 
                                        const std::vector<size_t> &channels = std::vector<size_t>(), 
                                        const SoapySDR::Kwargs &args = SoapySDR::Kwargs());
        void closeStream(SoapySDR::Stream *stream);
        size_t getStreamMTU(SoapySDR::Stream *stream) const;
        int activateStream(     SoapySDR::Stream *stream,
                                const int flags = 0,
                                const long long timeNs = 0,
                                const size_t numElems = 0);
        int deactivateStream(SoapySDR::Stream *stream, const int flags = 0, const long long timeNs = 0);

        int readStream( SoapySDR::Stream *stream,
                        void * const *buffs,
                        const size_t numElems,
                        int &flags,
                        long long &timeNs,
                        const long timeoutUs = 100000);

        /*******************************************************************
         * Antenna API
         ******************************************************************/
        std::vector<std::string> listAntennas( const int direction, const size_t channel ) const;
        std::string getAntenna( const int direction, const size_t channel ) const;


        /*******************************************************************
         * Frontend corrections API
         ******************************************************************/
        bool hasDCOffsetMode( const int direction, const size_t channel ) const { return(false); }

        /*******************************************************************
         * Gain API
         ******************************************************************/
        std::vector<std::string> listGains(const int direction, const size_t channel) const;
        void setGain(const int direction, const size_t channel, const double value);
        void setGain(const int direction, const size_t channel, const std::string &name, const double value);
        double getGain(const int direction, const size_t channel) const;
        double getGain(const int direction, const size_t channel, const std::string &name) const;
        SoapySDR::Range getGainRange(const int direction, const size_t channel) const;
        SoapySDR::Range getGainRange(const int direction, const size_t channel, const std::string &name) const;
        void setGainMode( const int direction, const size_t channel, const bool automatic );
        bool hasGainMode(const int direction, const size_t channel) const;
        bool getGainMode( const int direction, const size_t channel ) const;

        /*******************************************************************
         * Frequency API
         ******************************************************************/
        void setFrequency( const int direction, const size_t channel, const std::string &name, const double frequency, const SoapySDR::Kwargs &args );
        double getFrequency( const int direction, const size_t channel, const std::string &name ) const;
        SoapySDR::ArgInfoList getFrequencyArgsInfo(const int direction, const size_t channel) const;
        std::vector<std::string> listFrequencies( const int direction, const size_t channel ) const;
        SoapySDR::RangeList getFrequencyRange( const int direction, const size_t channel, const std::string &name ) const;

        /*******************************************************************
         * Sample Rate API
         ******************************************************************/
        void setSampleRate( const int direction, const size_t channel, const double rate );
        double getSampleRate( const int direction, const size_t channel ) const;
        std::vector<double> listSampleRates( const int direction, const size_t channel ) const;
        void setBandwidth( const int direction, const size_t channel, const double bw );
        double getBandwidth( const int direction, const size_t channel ) const;
        std::vector<double> listBandwidths( const int direction, const size_t channel ) const;

private:
        cariboulite_st cariboulite_sys;
        cariboulite_radios_st radios;

        SoapySDR::Stream* const TX_STREAM09 = (SoapySDR::Stream*) 0x1;
	SoapySDR::Stream* const RX_STREAM09 = (SoapySDR::Stream*) 0x2;
        SoapySDR::Stream* const TX_STREAM24 = (SoapySDR::Stream*) 0x1;
	SoapySDR::Stream* const RX_STREAM24 = (SoapySDR::Stream*) 0x2;

	struct Stream {
		Stream(): 
                        opened(false), 
                        buf_num(BUF_NUM), 
                        buf_len(BUF_LEN), 
                        buf(nullptr),
                        buf_head(0), 
                        buf_tail(0), 
                        buf_count(0),
                        remainderHandle(-1), 
                        remainderSamps(0), 
                        remainderOffset(0), 
                        remainderBuff(nullptr),
                        format(CARIBOULITE_FORMAT_INT16) {}

		bool opened;
		uint32_t	buf_num;
		uint32_t	buf_len;
		int8_t		**buf;
		uint32_t	buf_head;
		uint32_t	buf_tail;
		uint32_t	buf_count;

		int32_t remainderHandle;
		size_t remainderSamps;
		size_t remainderOffset;
		int8_t* remainderBuff;
		uint32_t format;

		~Stream() { clear_buffers(); }
		void clear_buffers() {}
		void allocate_buffers() {}
	};

	struct RXStream:Stream
        {
		uint32_t vga_gain;
		uint32_t lna_gain;
		uint8_t amp_gain;
		double samplerate;
		uint32_t bandwidth;
		uint64_t frequency;
		bool overflow;
	};

	struct TXStream:Stream
        {
		uint32_t vga_gain;
		uint8_t amp_gain;
		double samplerate;
		uint32_t bandwidth;
		uint64_t frequency;
		bool bias;
		bool underflow;
		bool burst_end;
		int32_t burst_samps;
	} ;

	RXStream _rx_stream;
	TXStream _tx_stream;
	bool _auto_bandwidth;
	uint64_t _current_frequency;
	double _current_samplerate;
	uint32_t _current_bandwidth;
	uint8_t _current_amp;
};