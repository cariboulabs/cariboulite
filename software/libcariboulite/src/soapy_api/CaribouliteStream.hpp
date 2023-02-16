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
#include <Iir.h>

//#define ZF_LOG_LEVEL ZF_LOG_ERROR
#define ZF_LOG_LEVEL ZF_LOG_VERBOSE

#include "datatypes/circular_buffer.h"
#include "cariboulite_setup.h"
#include "cariboulite_radio.h"

#define DIG_FILT_ORDER		6

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

// The forward declared Stream Class
class SoapySDR::Stream 
{
public:
	enum CaribouliteFormat 
	{
		CARIBOULITE_FORMAT_FLOAT32	= 0,
		CARIBOULITE_FORMAT_INT16	= 1,
		CARIBOULITE_FORMAT_INT8	    = 2,
		CARIBOULITE_FORMAT_FLOAT64  = 3,
	};
	CaribouliteFormat format;
	
	enum DigitalFilterType
	{
		DigitalFilter_None = 0,
		DigitalFilter_20KHz,
		DigitalFilter_50KHz,
		DigitalFilter_100KHz,
		DigitalFilter_200KHz,
	};

public:
	Stream(cariboulite_radio_state_st *radio);
	~Stream();
	int Write(caribou_smi_sample_complex_int16 *buffer, size_t num_samples, uint8_t* meta, long timeout_us);
	int Read(caribou_smi_sample_complex_int16 *buffer, size_t num_samples, uint8_t *meta, long timeout_us);

	int ReadSamples(caribou_smi_sample_complex_int16* buffer, size_t num_elements, long timeout_us);
	int ReadSamples(sample_complex_float* buffer, size_t num_elements, long timeout_us);
	int ReadSamples(sample_complex_double* buffer, size_t num_elements, long timeout_us);
	int ReadSamples(sample_complex_int8* buffer, size_t num_elements, long timeout_us);
	int ReadSamplesGen(void* buffer, size_t num_elements, long timeout_us);

	cariboulite_channel_dir_en getInnerStreamType(void);
    void setInnerStreamType(cariboulite_channel_dir_en dir);
	void setDigitalFilter(DigitalFilterType type);
	int setFormat(const std::string &fmt);
	inline int readerThreadRunning() {return reader_thread_running;}
    void activateStream(int active) {stream_active = active;}
    
public:
    cariboulite_radio_state_st *radio;
    cariboulite_channel_dir_en native_dir;
    size_t mtu_size;
    std::thread *reader_thread;
    int stream_active;
    int reader_thread_running;
	circular_buffer<caribou_smi_sample_complex_int16> *rx_queue;
    
	caribou_smi_sample_complex_int16 *interm_native_buffer1;
    caribou_smi_sample_complex_int16 *interm_native_buffer2;
    caribou_smi_sample_meta* interm_native_meta;
	DigitalFilterType filterType;
	Iir::Butterworth::LowPass<DIG_FILT_ORDER>* filter_i;
	Iir::Butterworth::LowPass<DIG_FILT_ORDER>* filter_q;
	Iir::Butterworth::LowPass<DIG_FILT_ORDER> filt20_i;
	Iir::Butterworth::LowPass<DIG_FILT_ORDER> filt20_q;
	Iir::Butterworth::LowPass<DIG_FILT_ORDER> filt50_i;
	Iir::Butterworth::LowPass<DIG_FILT_ORDER> filt50_q;
	Iir::Butterworth::LowPass<DIG_FILT_ORDER> filt100_i;
	Iir::Butterworth::LowPass<DIG_FILT_ORDER> filt100_q;

public:
	size_t getMTUSizeElements(void);
};
