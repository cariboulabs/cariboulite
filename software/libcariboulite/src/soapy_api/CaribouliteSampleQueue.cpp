#include "Cariboulite.hpp"
#include <Iir.h>
#include <byteswap.h>

//=================================================================
SampleQueue::SampleQueue(int mtu_bytes, int num_buffers)
{
    SoapySDR_logf(SOAPY_SDR_INFO, "Creating SampleQueue MTU: %d bytes, NumBuffers: %d", mtu_bytes, num_buffers);
	queue = new circular_buffer<caribou_smi_sample_complex_int16>(mtu_bytes / 4 * num_buffers*10);
    mtu_size_bytes = mtu_bytes;
    stream_id = -1;
    stream_dir = -1;
    stream_channel = -1;

    partial_buffer = new uint8_t[mtu_size_bytes];
    partial_buffer_start = mtu_size_bytes;
    partial_buffer_length = 0;
	dig_filt = 0;

    // a buffer for conversion betwen native and emulated formats
    // the maximal size is the 2*(mtu_size in bytes)
    interm_native_buffer = new caribou_smi_sample_complex_int16[2*mtu_size_bytes];
    is_cw = 0;

	filt20_i.setup(4e6, 20e3/2);
	filt50_i.setup(4e6, 50e3/2);
	filt100_i.setup(4e6, 100e3/2);
	filt200_i.setup(4e6, 200e3/2);
	filt2p5M_i.setup(4e6, 2.5e6/2);

	filt20_q.setup(4e6, 20e3/2);
	filt50_q.setup(4e6, 50e3/2);
	filt100_q.setup(4e6, 100e3/2);
	filt200_q.setup(4e6, 200e3/2);
	filt2p5M_q.setup(4e6, 2.5e6/2);
}

//=================================================================
SampleQueue::~SampleQueue()
{
    stream_id = -1;
    stream_dir = -1;
    stream_channel = -1;
    delete[] partial_buffer;
    delete[] interm_native_buffer;
    delete queue;
}

//=================================================================
int SampleQueue::AttachStreamId(int id, int dir, int channel)
{
    //printf("SampleQueue::AttachStreamId\n");
    if (stream_id != -1)
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "Cannot attach stream_id - already attached to %d", stream_id);
        return -1;
    }
    stream_id = id;
    stream_dir = dir;
    stream_channel = channel;
    return 0;
}

//=================================================================
int SampleQueue::Write(caribou_smi_sample_complex_int16 *buffer, size_t num_samples, uint8_t* meta, long timeout_us)
{
	return queue->put(buffer, num_samples);
}

//=================================================================
int SampleQueue::Read(caribou_smi_sample_complex_int16 *buffer, size_t num_samples, uint8_t *meta, long timeout_us)
{
    return queue->get(buffer, num_samples);
}

//=================================================================
int SampleQueue::ReadSamples(caribou_smi_sample_complex_int16* buffer, size_t num_elements, long timeout_us)
{
    static int once = 100;
    static uint16_t last_q = 0;

    int res = Read(buffer, num_elements, NULL, timeout_us);
    if (res < 0)
    {
        // todo!!
        return res;
    }
    
    int tot_read_elements = res;

	return tot_read_elements;  

	// digital filters - TBD
	if (dig_filt == 0)
	{
		for (int i = 0; i < res; i++)
		{
			buffer[i].i = (int16_t)filt2p5M_i.filter((float)buffer[i].i);
			buffer[i].q = (int16_t)filt2p5M_q.filter((float)buffer[i].q);
		}
	} 
	else if (dig_filt == 1)
	{
		for (int i = 0; i < res; i++)
		{
			buffer[i].i = (int16_t)filt20_i.filter((float)buffer[i].i);
			buffer[i].q = (int16_t)filt20_q.filter((float)buffer[i].q);
		}
	}
	else if (dig_filt == 2)
	{
		for (int i = 0; i < res; i++)
		{
			buffer[i].i = (int16_t)filt50_i.filter((float)buffer[i].i);
			buffer[i].q = (int16_t)filt50_q.filter((float)buffer[i].q);
		}
	}
	else if (dig_filt == 3)
	{
		for (int i = 0; i < res; i++)
		{
			buffer[i].i = (int16_t)filt100_i.filter((float)buffer[i].i);
			buffer[i].q = (int16_t)filt100_q.filter((float)buffer[i].q);
		}
	}
	else if (dig_filt == 4)
	{
		for (int i = 0; i < res; i++)
		{
			buffer[i].i = (int16_t)filt200_i.filter((float)buffer[i].i);
			buffer[i].q = (int16_t)filt200_q.filter((float)buffer[i].q);
		}
	}

    return tot_read_elements;  
}

//=================================================================
int SampleQueue::ReadSamples(sample_complex_float* buffer, size_t num_elements, long timeout_us)
{
    uint32_t max_native_samples = 2*(mtu_size_bytes / sizeof(caribou_smi_sample_complex_int16));

    // do not allow to store more than is possible in the intermediate buffer
    if (num_elements > max_native_samples)
    {
        num_elements = max_native_samples;
    }

    // read out the native data type
    int res = ReadSamples(interm_native_buffer, num_elements, timeout_us);
    if (res < 0)
    {
        // todo!!
        return res;
    }

    float max_val = (float)(4095);

    for (int i = 0; i < res; i++)
    {
        buffer[i].i = (float)interm_native_buffer[i].i / max_val;
        buffer[i].q = (float)interm_native_buffer[i].q / max_val;
    }

    double sumI = 0.0;
    double sumQ = 0.0;
    for (int i = 0; i < res; i++)
    {
        sumI += buffer[i].i;
        sumQ += buffer[i].q;
    }
    sumI /= (double)res;
    sumQ /= (double)res;

    for (int i = 0; i < res; i++)
    {
        buffer[i].i -= sumI;
        buffer[i].q -= sumQ;
    }

    /*double theta1 = 0.0;
    double theta2 = 0.0;
    double theta3 = 0.0;
    for (int i = 0; i < res; i++)
    {
        int sign_I = (buffer[i].i > 0 ? 1 : -1);
        int sign_Q = (buffer[i].q > 0 ? 1 : -1);
        theta1 += sign_I * buffer[i].q;
        theta2 += sign_I * buffer[i].i;
        theta3 += sign_Q * buffer[i].q;
    }
    theta1 = - theta1 / (double)res;
    theta2 /= (double)res;
    theta3 /= (double)res;

    double c1 = theta1 / theta2;
    double c2 = sqrt( (theta3*theta3 - theta1*theta1) / (theta2*theta2) );

    for (int i = 0; i < res; i++)
    {
        float ii = buffer[i].i;
        float qq = buffer[i].q;
        buffer[i].i = c2 * ii;
        buffer[i].q = c1 * ii + qq;
    }*/

    return res;
}

//=================================================================
int SampleQueue::ReadSamples(sample_complex_double* buffer, size_t num_elements, long timeout_us)
{
    uint32_t max_native_samples = 2*(mtu_size_bytes / sizeof(caribou_smi_sample_complex_int16));

    // do not allow to store more than is possible in the intermediate buffer
    if (num_elements > max_native_samples)
    {
        num_elements = max_native_samples;
    }

    // read out the native data type
    int res = ReadSamples(interm_native_buffer, num_elements, timeout_us);
    if (res < 0)
    {
        // todo!!
        return res;
    }

    double max_val = (double)(4095);

    for (int i = 0; i < res; i++)
    {
        buffer[i].i = (double)interm_native_buffer[i].i / max_val;
        buffer[i].q = (double)interm_native_buffer[i].q / max_val;
    }

    return res;
}

//=================================================================
int SampleQueue::ReadSamples(sample_complex_int8* buffer, size_t num_elements, long timeout_us)
{
    uint32_t max_native_samples = 2*(mtu_size_bytes / sizeof(caribou_smi_sample_complex_int16));

    // do not allow to store more than is possible in the intermediate buffer
    if (num_elements > max_native_samples)
    {
        num_elements = max_native_samples;
    }

    // read out the native data type
    int res = ReadSamples(interm_native_buffer, num_elements, timeout_us);
    if (res < 0)
    {
        // todo!!
        return res;
    }

    for (int i = 0; i < res; i++)
    {
        buffer[i].i = (int8_t)((interm_native_buffer[i].i >> 5)&0x00FF);
        buffer[i].q = (int8_t)((interm_native_buffer[i].q >> 5)&0x00FF);
    }

    return res;
}