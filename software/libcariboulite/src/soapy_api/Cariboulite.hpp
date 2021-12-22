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

//#define BUFFER_SIZE_MS                  ( 10 )
#define NUM_SAMPLEQUEUE_BUFS            ( 10 )
#define NUM_BYTES_PER_CPLX_ELEM         ( 4 )
//#define GET_MTU_MS(ms)                  ( 4000*(ms) )
//#define GET_MTU_MS_BYTES(ms)            ( GET_MTU_MS(ms) * NUM_BYTES_PER_CPLX_ELEM )

#pragma pack(1)
// associated with CS8 - total 2 bytes / element
typedef struct
{
        int8_t i;                       // LSB
        int8_t q;                       // MSB
} sample_complex_int8;

// associated with CS12 - total 3 bytes / element
typedef struct
{
        int16_t i :12;                  // LSB
        int16_t q :12;                  // MSB
} sample_complex_int12;

// associated with CS16 - total 4 bytes / element
typedef struct
{
        int16_t i;                      // LSB
        int16_t q;                      // MSB
} sample_complex_int16;

// associated with CS32 - total 8 bytes / element
typedef struct
{
        int32_t i;                      // LSB
        int32_t q;                      // MSB
} sample_complex_int32;

// associated with CF32 - total 8 bytes / element
typedef struct
{
        float i;                        // LSB
        float q;                        // MSB
} sample_complex_float;

// associated with CF64 - total 16 bytes / element
typedef struct
{
        double i;                       // LSB
        double q;                       // MSB
} sample_complex_double;
#pragma pack()

class SoapyCaribouliteSession
{
public:
	SoapyCaribouliteSession(void);
	~SoapyCaribouliteSession(void);

public:
        static cariboulite_st cariboulite_sys;
        static std::mutex sessionMutex;
        static size_t sessionCount;
};


class SampleQueue
{
public:
        SampleQueue(int mtu_bytes, int num_buffers);
        ~SampleQueue();
        int AttachStreamId(int id, int dir, int channel);
        int Write(uint8_t *buffer, size_t length, uint32_t meta, long timeout_us);
        int Read(uint8_t *buffer, size_t length, uint32_t *meta, long timeout_us);

        int ReadSamples(sample_complex_int16* buffer, size_t num_elements, long timeout_us);
        int ReadSamples(sample_complex_float* buffer, size_t num_elements, long timeout_us);
        int ReadSamples(sample_complex_double* buffer, size_t num_elements, long timeout_us);
        int ReadSamples(sample_complex_int8* buffer, size_t num_elements, long timeout_us);

        int stream_id;
        int stream_dir;
        int stream_channel;
        int is_cw;
        Cariboulite_Format chosen_format;
private:
        tsqueue_st queue;
        size_t mtu_size_bytes;
        uint8_t *partial_buffer;
        int partial_buffer_start;
        int partial_buffer_length;

        sample_complex_int16 *interm_native_buffer;
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

        int findSampleQueue(const int direction, const size_t channel);
        int findSampleQueueById(int stream_id);

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
        cariboulite_radios_st radios;
        SampleQueue* sample_queues[4];

        // Static load time initializations
        static SoapyCaribouliteSession sess;
};
