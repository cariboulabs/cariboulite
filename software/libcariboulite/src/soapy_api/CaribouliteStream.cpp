#include "Cariboulite.hpp"
#include <Iir.h>
#include <byteswap.h>
#include <chrono>


#define NUM_BYTES_PER_CPLX_ELEM         ( sizeof(caribou_smi_sample_complex_int16) )
#define NUM_NATIVE_MTUS_PER_QUEUE		( 10 )

#define USE_ASYNC                       ( 1 )
#define USE_ASYNC_OVERRIDE_WRITES       ( true )
#define USE_ASYNC_BLOCK_READS           ( true )

//=================================================================
void ReaderThread(SoapySDR::Stream* stream)
{
#if USE_ASYNC
    SoapySDR_logf(SOAPY_SDR_INFO, "Enterring Reader Thread");
    
    while (stream->readerThreadRunning())
    {
        if (!stream->stream_active)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        int ret = cariboulite_radio_read_samples(stream->radio, 
                                                    stream->interm_native_buffer1, 
                                                    stream->interm_native_meta, 
                                                    stream->mtu_size);
        if (ret < 0)
        {
            if (ret == -1)
            {
                printf("reader thread failed to read SMI!\n");
            }
            // a special case for debug streams which are not
            // taken care of in the soapy front-end (ret = -2)
            ret = 0;
        }
        
        if (ret) stream->rx_queue->put(stream->interm_native_buffer1, ret);
    }
    
    SoapySDR_logf(SOAPY_SDR_INFO, "Leaving Reader Thread");
#endif //USE_ASYNC
}

//=================================================================
SoapySDR::Stream::Stream(cariboulite_radio_state_st *radio)
{
    // init pointers
    reader_thread = NULL;
    rx_queue = NULL;
    interm_native_buffer1 = NULL;
    interm_native_buffer2 = NULL;
    interm_native_meta = NULL;
    filter_i = NULL;
	filter_q = NULL;
    
    // stream init
    this->radio = radio;
    mtu_size = getMTUSizeElements();
    
    SoapySDR_logf(SOAPY_SDR_INFO, "Creating SampleQueue MTU: %d I/Q samples (%d bytes)", 
				mtu_size, mtu_size * sizeof(caribou_smi_sample_complex_int16));

    #if USE_ASYNC
        rx_queue = new circular_buffer<caribou_smi_sample_complex_int16>(mtu_size * NUM_NATIVE_MTUS_PER_QUEUE, 
                                                                         USE_ASYNC_OVERRIDE_WRITES, 
                                                                         USE_ASYNC_BLOCK_READS);
        interm_native_buffer1 = new caribou_smi_sample_complex_int16[mtu_size];
    #endif //USE_ASYNC

	format = CARIBOULITE_FORMAT_INT16;

	// Init the internal IIR filters
    // a buffer for conversion between native and emulated formats
    interm_native_buffer2 = new caribou_smi_sample_complex_int16[mtu_size];
    interm_native_meta = new caribou_smi_sample_meta[mtu_size];
    
	filterType = DigitalFilter_None;
	filt20_i.setup(4e6, 20e3/2);
	filt50_i.setup(4e6, 50e3/2);
	filt100_i.setup(4e6, 100e3/2);

	filt20_q.setup(4e6, 20e3/2);
	filt50_q.setup(4e6, 50e3/2);
	filt100_q.setup(4e6, 100e3/2);
    
    #if USE_ASYNC
        reader_thread_running = 1;
        stream_active = 0;
        reader_thread = new std::thread(ReaderThread, this);
    #endif //USE_ASYNC
}

//=================================================================
SoapySDR::Stream::~Stream()
{
    filterType = DigitalFilter_None;
	filter_i = NULL;
	filter_q = NULL;
    
    #if USE_ASYNC
        stream_active = 0;
        reader_thread_running = 0;
        reader_thread->join();
        if (reader_thread) delete reader_thread;
        if (interm_native_buffer1) delete[] interm_native_buffer1;
        if (rx_queue) delete rx_queue;
    #endif //USE_ASYNC
    
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
int SoapySDR::Stream::Write(caribou_smi_sample_complex_int16 *buffer, size_t num_samples, uint8_t* meta, long timeout_us)
{
	return cariboulite_radio_write_samples(radio, buffer, num_samples);
}

//=================================================================
int SoapySDR::Stream::Read(caribou_smi_sample_complex_int16 *buffer, size_t num_samples, uint8_t *meta, long timeout_us)
{
    #if USE_ASYNC
        return rx_queue->get(buffer, num_samples, timeout_us);
    #else
        int ret = cariboulite_radio_read_samples(radio, buffer, (caribou_smi_sample_meta*)meta, num_samples);
        if (ret < 0)
        {
            if (ret == -1)
            {
                printf("reader thread failed to read SMI!\n");
            }
            // a special case for debug streams which are not
            // taken care of in the soapy front-end (ret = -2)
            ret = 0;
        }
        return ret;
    #endif //USE_ASYNC
}

//=================================================================
int SoapySDR::Stream::ReadSamples(caribou_smi_sample_complex_int16* buffer, size_t num_elements, long timeout_us)
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
	    case CARIBOULITE_FORMAT_INT16: return ReadSamples((caribou_smi_sample_complex_int16*)buffer, num_elements, timeout_us); break;
	    case CARIBOULITE_FORMAT_INT8: return ReadSamples((sample_complex_int8*)buffer, num_elements, timeout_us); break;
	    case CARIBOULITE_FORMAT_FLOAT64: return ReadSamples((sample_complex_double*)buffer, num_elements, timeout_us); break;
		default: return ReadSamples((caribou_smi_sample_complex_int16*)buffer, num_elements, timeout_us); break;
	}
	return 0;
}