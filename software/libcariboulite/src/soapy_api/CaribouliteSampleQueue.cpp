#include "Cariboulite.hpp"
#include <Iir.h>
#include <byteswap.h>

#define NUM_BYTES_PER_CPLX_ELEM         ( sizeof(caribou_smi_sample_complex_int16) )
#define NUM_NATIVE_MTUS_PER_QUEUE		( 10 )

// each MTU = 131072 samples
// Sized 524,288 bytes per MTU = 32.768 milliseconds
// In total, each queue contains ~5 MB = ~320 milliseconds of depth
size_t SoapySDR::Stream::mtu_size_elements = (1024 * 1024 / 2 / NUM_BYTES_PER_CPLX_ELEM );

//=================================================================
SoapySDR::Stream::Stream()
{
    SoapySDR_logf(SOAPY_SDR_INFO, "Creating SampleQueue MTU: %d I/Q samples (%d bytes), NumBuffers: %d", 
				mtu_size_elements, 
				mtu_size_elements * sizeof(caribou_smi_sample_complex_int16),
				NUM_NATIVE_MTUS_PER_QUEUE);

	// create the actual native queue
	queue = new circular_buffer<caribou_smi_sample_complex_int16>(
							mtu_size_elements * NUM_NATIVE_MTUS_PER_QUEUE);

	format = CARIBOULITE_FORMAT_INT16;
	is_cw = 0;
	smi_stream_id = -1;

	// Init the internal IIR filters
    // a buffer for conversion between native and emulated formats
    // the maximal size is the twice the MTU
    interm_native_buffer = new caribou_smi_sample_complex_int16[2 * mtu_size_elements];
	filterType = DigitalFilter_None;
	filter_i = NULL;
	filter_q = NULL;
	filt20_i.setup(4e6, 20e3/2);
	filt50_i.setup(4e6, 50e3/2);
	filt100_i.setup(4e6, 100e3/2);
	filt2p5M_i.setup(4e6, 2.5e6/2);

	filt20_q.setup(4e6, 20e3/2);
	filt50_q.setup(4e6, 50e3/2);
	filt100_q.setup(4e6, 100e3/2);
	filt2p5M_q.setup(4e6, 2.5e6/2);
}

//=================================================================
void SoapySDR::Stream::setDigitalFilter(DigitalFilterType type)
{
	switch (type)
	{
		case DigitalFilter_20KHz: filter_i = &filt20_i; filter_q = &filt20_q; break;
		case DigitalFilter_50KHz: filter_i = &filt50_i; filter_q = &filt50_q; break;
		case DigitalFilter_100KHz: filter_i = &filt100_i; filter_q = &filt100_q; break;
		case DigitalFilter_2500KHz: filter_i = &filt2p5M_i; filter_q = &filt2p5M_q; break;
		case DigitalFilter_None:
		default: 
			filter_i = NULL;
			filter_q = NULL;
			break;
	}
	filterType = type;
}

//=================================================================
SoapySDR::Stream::~Stream()
{
	smi_stream_id = -1;
    delete[] interm_native_buffer;
	filter_i = NULL;
	filter_q = NULL;
	filterType = DigitalFilter_None;
    delete queue;
}

//=================================================================
int SoapySDR::Stream::getInnerStreamType(void)
{
	if (smi_stream_id < 0)
	{
		return SOAPY_SDR_RX;
	}
	return CARIBOU_SMI_GET_STREAM_TYPE(smi_stream_id) == caribou_smi_stream_type_read ? SOAPY_SDR_RX : SOAPY_SDR_TX;
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
int SoapySDR::Stream::Write(caribou_smi_sample_complex_int16 *buffer, size_t num_samples, uint8_t* meta, long timeout_us)
{
	return queue->put(buffer, num_samples);
}

//=================================================================
int SoapySDR::Stream::Read(caribou_smi_sample_complex_int16 *buffer, size_t num_samples, uint8_t *meta, long timeout_us)
{
    return queue->get(buffer, num_samples);
}

//=================================================================
int SoapySDR::Stream::ReadSamples(caribou_smi_sample_complex_int16* buffer, size_t num_elements, long timeout_us)
{
    int res = Read(buffer, num_elements, NULL, timeout_us);
    if (res < 0)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "Reading %d elements failed from queue", num_elements); 
        return res;
    }
    
    int tot_read_elements = res;

	//return tot_read_elements;  

	if (filterType != DigitalFilter_None && filter_i != NULL && filter_q != NULL)
	{
		for (int i = 0; i < res; i++)
		{
			buffer[i].i = (int16_t)filter_i->filter((float)buffer[i].i);
			buffer[i].q = (int16_t)filter_q->filter((float)buffer[i].q);
		}
	}

    return tot_read_elements;  
}

//=================================================================
int SoapySDR::Stream::ReadSamples(sample_complex_float* buffer, size_t num_elements, long timeout_us)
{
    num_elements = num_elements > mtu_size_elements ? mtu_size_elements : num_elements;

    // read out the native data type
    int res = ReadSamples(interm_native_buffer, num_elements, timeout_us);
    if (res < 0)
    {
        return res;
    }

    float max_val = 4096.0f;

    for (int i = 0; i < res; i++)
    {
        buffer[i].i = (float)(interm_native_buffer[i].i) / max_val;
        buffer[i].q = (float)(interm_native_buffer[i].q) / max_val;
    }
    return res;
}

//=================================================================
int SoapySDR::Stream::ReadSamples(sample_complex_double* buffer, size_t num_elements, long timeout_us)
{
    num_elements = num_elements > mtu_size_elements ? mtu_size_elements : num_elements;

    // read out the native data type
    int res = ReadSamples(interm_native_buffer, num_elements, timeout_us);
    if (res < 0)
    {
        return res;
    }

    double max_val = 4096.0;

    for (int i = 0; i < res; i++)
    {
        buffer[i].i = (double)(interm_native_buffer[i].i) / max_val;
        buffer[i].q = (double)(interm_native_buffer[i].q) / max_val;
    }

    return res;
}

//=================================================================
int SoapySDR::Stream::ReadSamples(sample_complex_int8* buffer, size_t num_elements, long timeout_us)
{
    num_elements = num_elements > mtu_size_elements ? mtu_size_elements : num_elements;

    // read out the native data type
    int res = ReadSamples(interm_native_buffer, num_elements, timeout_us);
    if (res < 0)
    {
        return res;
    }

    for (int i = 0; i < res; i++)
    {
        buffer[i].i = (int8_t)((interm_native_buffer[i].i >> 5)&0x00FF);
        buffer[i].q = (int8_t)((interm_native_buffer[i].q >> 5)&0x00FF);
    }

    return res;
}

//=================================================================
int SoapySDR::Stream::ReadSamplesGen(void* buffer, size_t num_elements, long timeout_us)
{
	switch (format)
	{
		case CARIBOULITE_FORMAT_FLOAT32: return ReadSamples((sample_complex_float*)buffer, num_elements, timeout_us); break;
	    case CARIBOULITE_FORMAT_INT16: return ReadSamples((caribou_smi_sample_complex_int16*)buffer, num_elements, timeout_us); break;
	    case CARIBOULITE_FORMAT_INT8: return ReadSamples((sample_complex_int8*)buffer, num_elements, timeout_us); break;
	    case CARIBOULITE_FORMAT_FLOAT64: return ReadSamples((sample_complex_double*)buffer, num_elements, timeout_us); break;
		default: return ReadSamples((caribou_smi_sample_complex_int16*)buffer, num_elements, timeout_us); break;
	}
	return 0;
}