#include <SoapySDR/Errors.h>
#include "Cariboulite.hpp"
#include <Iir.h>
#include <byteswap.h>
#include <chrono>



SoapySDR::Stream::Stream(cariboulite_radio_state_st *radio)
{
    stream_active = 0;
    native_dir = cariboulite_channel_dir_rx;
    // init pointers
    interm_native_buffer2 = NULL;
    interm_native_meta = NULL;
    filter_i = NULL;
	filter_q = NULL;
    
    // stream init
    this->radio = radio;
    mtu_size = getMTUSizeElements();
    
    SoapySDR_logf(SOAPY_SDR_INFO, "Creating stream MTU: %d I/Q samples (%d bytes)",
				mtu_size, mtu_size * sizeof(cariboulite_sample_complex_int16));


	format = CARIBOULITE_FORMAT_INT16;

	// Init the internal IIR filters
    // a buffer for conversion between native and emulated formats
    interm_native_buffer2 = new cariboulite_sample_complex_int16[mtu_size];
    interm_native_meta = new cariboulite_sample_meta[mtu_size];
    
	filterType = DigitalFilter_None;
	filt20_i.setup(4e6, 20e3/2);
	filt50_i.setup(4e6, 50e3/2);
	filt100_i.setup(4e6, 100e3/2);

	filt20_q.setup(4e6, 20e3/2);
	filt50_q.setup(4e6, 50e3/2);
	filt100_q.setup(4e6, 100e3/2);
    
}

//=================================================================
SoapySDR::Stream::~Stream()
{
    filterType = DigitalFilter_None;
	filter_i = NULL;
	filter_q = NULL;
    
    
    if (interm_native_buffer2) delete[] interm_native_buffer2;
    if (interm_native_meta) delete[] interm_native_meta;
}

//=================================================================
size_t SoapySDR::Stream::getMTUSizeElements(void)
{
    return cariboulite_radio_get_native_mtu_size_samples(radio);
}

//=================================================================
void SoapySDR::Stream::setDigitalFilter(DigitalFilterType type)
{
	switch (type)
	{
		case DigitalFilter_20KHz: filter_i = &filt20_i; filter_q = &filt20_q; break;
		case DigitalFilter_50KHz: filter_i = &filt50_i; filter_q = &filt50_q; break;
		case DigitalFilter_100KHz: filter_i = &filt100_i; filter_q = &filt100_q; break;
		case DigitalFilter_None:
		default: 
			filter_i = NULL;
			filter_q = NULL;
			break;
	}
	filterType = type;
}



//=================================================================
cariboulite_channel_dir_en SoapySDR::Stream::getInnerStreamType(void)
{
	return native_dir;
}

//=================================================================
void SoapySDR::Stream::setInnerStreamType(cariboulite_channel_dir_en direction)
{
    native_dir = direction;
}

//=================================================================
int SoapySDR::Stream::setFormat(const std::string &fmt)
{
	if (!fmt.compare(SOAPY_SDR_CS16))
		format = CARIBOULITE_FORMAT_INT16;
	else if (!fmt.compare(SOAPY_SDR_CS8))
		format = CARIBOULITE_FORMAT_INT8;
	else if (!fmt.compare(SOAPY_SDR_CF32))
		format = CARIBOULITE_FORMAT_FLOAT32;
	else if (!fmt.compare(SOAPY_SDR_CF64))
		format = CARIBOULITE_FORMAT_FLOAT64;
	else
	{
		return -1;
	}
	return 0;
}

//=================================================================
int SoapySDR::Stream::Write(cariboulite_sample_complex_int16 *buffer, size_t num_samples, uint8_t* meta, long timeout_us)
{
    if (!num_samples) return 0;
    if (!stream_active) {
        if (timeout_us > 0) std::this_thread::sleep_for(std::chrono::microseconds(timeout_us));
        return SOAPY_SDR_TIMEOUT;
    }
    int ret = cariboulite_radio_write_samples_timed(radio, buffer, num_samples, timeout_us);
    return ret > 0 ? ret : (ret == 0 ? SOAPY_SDR_TIMEOUT : SOAPY_SDR_STREAM_ERROR);
}

//=================================================================
int SoapySDR::Stream::WriteSamples(cariboulite_sample_complex_int16* buffer, size_t num_elements, long timeout_us)
{
    return Write(buffer, num_elements, NULL, timeout_us);
}

//=================================================================
int SoapySDR::Stream::WriteSamples(sample_complex_float* buffer, size_t num_elements, long timeout_us)
{
    num_elements = num_elements > mtu_size ? mtu_size : num_elements;
    float max_val = 4096.0;

    for (size_t i = 0; i < num_elements; i++)
    {
        interm_native_buffer2[i].i = (int16_t)(buffer[i].i * max_val);
        interm_native_buffer2[i].q = (int16_t)(buffer[i].q * max_val);

    }

    return WriteSamples(interm_native_buffer2, num_elements, timeout_us);
}

//=================================================================
int SoapySDR::Stream::WriteSamples(sample_complex_double* buffer, size_t num_elements, long timeout_us)
{
    num_elements = num_elements > mtu_size ? mtu_size : num_elements;
    double max_val = 4096.0;

    for (size_t i = 0; i < num_elements; i++)
    {
        interm_native_buffer2[i].i = (int16_t)(buffer[i].i * max_val);
        interm_native_buffer2[i].q = (int16_t)(buffer[i].q * max_val);

    }

    return WriteSamples(interm_native_buffer2, num_elements, timeout_us);
}


//=================================================================
int SoapySDR::Stream::WriteSamples(sample_complex_int8* buffer, size_t num_elements, long timeout_us)
{
    num_elements = num_elements > mtu_size ? mtu_size : num_elements;

    for (size_t i = 0; i < num_elements; i++)
    {
        interm_native_buffer2[i].i = ((int16_t)(buffer[i].i)) << 5;
        interm_native_buffer2[i].q = ((int16_t)(buffer[i].q)) << 5;

    }

    return WriteSamples(interm_native_buffer2, num_elements, timeout_us);
}

//=================================================================
int SoapySDR::Stream::WriteSamplesGen(void* buffer, size_t num_elements, long timeout_us)
{
	switch (format)
	{
		case CARIBOULITE_FORMAT_FLOAT32: return WriteSamples((sample_complex_float*)buffer, num_elements, timeout_us); break;
	    case CARIBOULITE_FORMAT_INT16: return WriteSamples((cariboulite_sample_complex_int16*)buffer, num_elements, timeout_us); break;
	    case CARIBOULITE_FORMAT_INT8: return WriteSamples((sample_complex_int8*)buffer, num_elements, timeout_us); break;
	    case CARIBOULITE_FORMAT_FLOAT64: return WriteSamples((sample_complex_double*)buffer, num_elements, timeout_us); break;
		default: return WriteSamples((cariboulite_sample_complex_int16*)buffer, num_elements, timeout_us); break;
	}
	return 0;
}
//=================================================================
int SoapySDR::Stream::Read(cariboulite_sample_complex_int16 *buffer, size_t num_samples, uint8_t *meta, long timeout_us)
{
    if (!num_samples) return 0;
    if (!stream_active) {
        if (timeout_us > 0) std::this_thread::sleep_for(std::chrono::microseconds(timeout_us));
        return SOAPY_SDR_TIMEOUT;
    }
    int ret = cariboulite_radio_read_samples_timed(radio, buffer,
        (cariboulite_sample_meta*)meta, num_samples, timeout_us);
    if (ret == -3) return SOAPY_SDR_CORRUPTION;
    return ret > 0 ? ret : (ret == 0 ? SOAPY_SDR_TIMEOUT : SOAPY_SDR_STREAM_ERROR);
}

//=================================================================
int SoapySDR::Stream::ReadSamples(cariboulite_sample_complex_int16* buffer, size_t num_elements, long timeout_us)
{
    int res = Read(buffer, num_elements, NULL, timeout_us);
    if (res < 0)
    {
        //SoapySDR_logf(SOAPY_SDR_ERROR, "Reading %d elements failed from queue", num_elements); 
        return res;
    }
    
	if (filterType != DigitalFilter_None && filter_i != NULL && filter_q != NULL)
	{
		for (int i = 0; i < res; i++)
		{
			buffer[i].i = (int16_t)filter_i->filter((float)buffer[i].i);
			buffer[i].q = (int16_t)filter_q->filter((float)buffer[i].q);
		}
	}

    return res;  
}

//=================================================================
int SoapySDR::Stream::ReadSamples(sample_complex_float* buffer, size_t num_elements, long timeout_us)
{
    num_elements = num_elements > mtu_size ? mtu_size : num_elements;

    // read out the native data type
    int res = ReadSamples(interm_native_buffer2, num_elements, timeout_us);
    if (res < 0)
    {
        return res;
    }

    float max_val = 4096.0f;

    for (int i = 0; i < res; i++)
    {
        buffer[i].i = (float)(interm_native_buffer2[i].i) / max_val;
        buffer[i].q = (float)(interm_native_buffer2[i].q) / max_val;
    }
    return res;
}

//=================================================================
int SoapySDR::Stream::ReadSamples(sample_complex_double* buffer, size_t num_elements, long timeout_us)
{
    num_elements = num_elements > mtu_size ? mtu_size : num_elements;

    // read out the native data type
    int res = ReadSamples(interm_native_buffer2, num_elements, timeout_us);
    if (res < 0)
    {
        return res;
    }

    double max_val = 4096.0;

    for (int i = 0; i < res; i++)
    {
        buffer[i].i = (double)(interm_native_buffer2[i].i) / max_val;
        buffer[i].q = (double)(interm_native_buffer2[i].q) / max_val;
    }

    return res;
}

//=================================================================
int SoapySDR::Stream::ReadSamples(sample_complex_int8* buffer, size_t num_elements, long timeout_us)
{
    num_elements = num_elements > mtu_size ? mtu_size : num_elements;

    // read out the native data type
    int res = ReadSamples(interm_native_buffer2, num_elements, timeout_us);
    if (res < 0)
    {
        return res;
    }

    for (int i = 0; i < res; i++)
    {
        buffer[i].i = (int8_t)((interm_native_buffer2[i].i >> 5)&0x00FF);
        buffer[i].q = (int8_t)((interm_native_buffer2[i].q >> 5)&0x00FF);
    }

    return res;
}

//=================================================================
int SoapySDR::Stream::ReadSamplesGen(void* buffer, size_t num_elements, long timeout_us)
{
    //printf("reading ne=%d\n", num_elements);
	switch (format)
	{
		case CARIBOULITE_FORMAT_FLOAT32: return ReadSamples((sample_complex_float*)buffer, num_elements, timeout_us); break;
	    case CARIBOULITE_FORMAT_INT16: return ReadSamples((cariboulite_sample_complex_int16*)buffer, num_elements, timeout_us); break;
	    case CARIBOULITE_FORMAT_INT8: return ReadSamples((sample_complex_int8*)buffer, num_elements, timeout_us); break;
	    case CARIBOULITE_FORMAT_FLOAT64: return ReadSamples((sample_complex_double*)buffer, num_elements, timeout_us); break;
		default: return ReadSamples((cariboulite_sample_complex_int16*)buffer, num_elements, timeout_us); break;
	}
	return 0;
}