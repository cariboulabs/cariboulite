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
#include "datatypes/tsqueue.h"
#include "cariboulite_setup.h"
#include "cariboulite_radios.h"

enum Cariboulite_Format 
{
	CARIBOULITE_FORMAT_FLOAT32	= 0,
	CARIBOULITE_FORMAT_INT16	= 1,
	CARIBOULITE_FORMAT_INT8	        = 2,
	CARIBOULITE_FORMAT_FLOAT64      = 3,
};

class SampleQueue
{
public:
        SampleQueue(int mtu_size_bytes, int num_buffers);
        ~SampleQueue();
        
private:
        tsqueue_st queue;
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

        /*******************************************************************
         * Sensors API
         ******************************************************************/
        /*!
        * List the available channel readback sensors.
        * A sensor can represent a reference lock, RSSI, temperature.
        * \param direction the channel direction RX or TX
        * \param channel an available channel on the device
        * \return a list of available sensor string names
        */
        virtual std::vector<std::string> listSensors(const int direction, const size_t channel) const;

        /*!
        * Get meta-information about a channel sensor.
        * Example: displayable name, type, range.
        * \param direction the channel direction RX or TX
        * \param channel an available channel on the device
        * \param key the ID name of an available sensor
        * \return meta-information about a sensor
        */
        virtual SoapySDR::ArgInfo getSensorInfo(const int direction, const size_t channel, const std::string &key) const;

        /*!
        * Readback a channel sensor given the name.
        * The value returned is a string which can represent
        * a boolean ("true"/"false"), an integer, or float.
        * \param direction the channel direction RX or TX
        * \param channel an available channel on the device
        * \param key the ID name of an available sensor
        * \return the current value of the sensor
        */
        virtual std::string readSensor(const int direction, const size_t channel, const std::string &key) const;

        /*!
        * Readback a channel sensor given the name.
        * \tparam Type the return type for the sensor value
        * \param direction the channel direction RX or TX
        * \param channel an available channel on the device
        * \param key the ID name of an available sensor
        * \return the current value of the sensor as the specified type
        */
        template <typename Type>
        Type readSensor(const int direction, const size_t channel, const std::string &key) const;

private:
        cariboulite_st cariboulite_sys;
        cariboulite_radios_st radios;
};