#pragma once

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Logger.h>
#include <SoapySDR/Types.h>
#include <SoapySDR/Formats.hpp>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <string>
#include <cstring>
#include <algorithm>
#include <atomic>

//#define ZF_LOG_LEVEL ZF_LOG_ERROR
#define ZF_LOG_LEVEL ZF_LOG_VERBOSE

#include "CaribouliteStream.hpp"
#include "cariboulite_setup.h"
#include "cariboulite_radio.h"


class SoapyCaribouliteSession
{
public:
	SoapyCaribouliteSession(void);
	~SoapyCaribouliteSession(void);

public:
        static sys_st sys;
        static std::mutex sessionMutex;
        static size_t sessionCount;
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
        std::string getHardwareKey(void) const;
        SoapySDR::Kwargs getHardwareInfo(void) const;

        /*******************************************************************
         * Channels API
         ******************************************************************/
        size_t getNumChannels(const int direction) const { return 1; }
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
                        
        int writeStream(SoapySDR::Stream *stream,
                        void * const *buffs,
                        const size_t numElems,
                        int &flags,
                        long long &timeNs,
                        const long timeoutUs);

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

        /*******************************************************************
         * Sensors API
         ******************************************************************/
        virtual std::vector<std::string> listSensors(const int direction, const size_t channel) const;
        virtual SoapySDR::ArgInfo getSensorInfo(const int direction, const size_t channel, const std::string &key) const;
        virtual std::string readSensor(const int direction, const size_t channel, const std::string &key) const;
        template <typename Type>
        Type readSensor(const int direction, const size_t channel, const std::string &key) const;

public:
        cariboulite_radio_state_st *radio;
		SoapySDR::Stream* stream;

        // Static load time initializations
        static SoapyCaribouliteSession sess;
};
