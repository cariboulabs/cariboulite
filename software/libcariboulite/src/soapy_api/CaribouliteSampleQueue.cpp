#include "Cariboulite.hpp"
#include <Iir.h>


class CircFifo
{
private:
    sample_complex_int16 *buffer;
    uint32_t in;
    uint32_t out;
    uint32_t size;

public:
	CircFifo(uint32_t _size)
	{
		if (!is_power_of_2(_size))
			_size = roundup_power_of_2(_size);

		buffer = new sample_complex_int16[_size];
		in = 0;
		out = 0;
		size = _size;
	}

	~CircFifo()
	{
		delete []buffer;
	}

	uint32_t put(const uint8_t *data, uint32_t len)
	{
		len = min(len, size - in + out);
		auto l = min(len, size - (in  & (size - 1)));
		memcpy(buffer + (in & (size - 1)), data, l);
		memcpy(buffer, data + l, len - l);
		in += len;
		return len;
	}

	uint32_t get(uint8_t *data, uint32_t len)
	{
		len = min(len, in - out);
		auto l = min(len, size - (out & (size - 1)));
		memcpy(data, buffer + (out & (size - 1)), l);
		memcpy(data + l, buffer, len - l);
		out += len;
		return len;
	}
}

//==============================================
void print_iq(uint32_t* array, int len)
{
    //printf("Values I/Q:\n");
    for (int i=0; i<len; i++)
    {
        unsigned int v = array[i];
        //swap_little_big (&v);

        int16_t q_val = (v>> 1) & (0x1FFF);
        int16_t i_val = (v>>17) & (0x1FFF);
        if (q_val >= 0x1000) q_val-=0x2000;
        if (i_val >= 0x1000) i_val-=0x2000;
        float fi = i_val, fq = q_val;
        float mod = sqrt(fi*fi + fq*fq);
        float arg = atan2(fq, fi);
        printf("(%d, %d), ", i_val, q_val);
        if ((i % 32) == 0) printf("\n");
    }
}

//=================================================================
SampleQueue::SampleQueue(int mtu_bytes, int num_buffers)
{
    SoapySDR_logf(SOAPY_SDR_INFO, "Creating SampleQueue MTU: %d bytes, NumBuffers: %d", mtu_bytes, num_buffers);
    tsqueue_init(&queue, mtu_bytes, num_buffers);
    //printf("finished tsqueue\n");
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
    interm_native_buffer = new sample_complex_int16[2*mtu_size_bytes];
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
    //printf("~SampleQueue streamID: %d, dir: %d, channel: %d\n", stream_id, stream_dir, stream_channel);
    //SoapySDR_logf(SOAPY_SDR_INFO, "~SampleQueue streamID: %d, dir: %d, channel: %d", stream_id, stream_dir, stream_channel);

    stream_id = -1;
    stream_dir = -1;
    stream_channel = -1;
    delete[] partial_buffer;
    delete[] interm_native_buffer;
    tsqueue_release(&queue);
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
int SampleQueue::Write(uint8_t *buffer, size_t length, uint32_t meta, long timeout_us)
{
    int left_to_write = length;
    int offset = 0;
    int chunk = 0;

    //printf("Write: %dB\n", length);
    while (left_to_write)
    {
        int current_length = ( left_to_write < (int)mtu_size_bytes ) ? left_to_write : mtu_size_bytes;

        int res = tsqueue_insert_push_buffer(&queue, 
                                            buffer + offset, 
                                            current_length, 
                                            meta, timeout_us, 
                                            true);
        switch (res)
        {
            case TSQUEUE_NOT_INITIALIZED:
            case TSQUEUE_SEM_FAILED: 
                {
                    SoapySDR_logf(SOAPY_SDR_ERROR, "pushing buffer n %d failed", chunk);
                    return -1;
                } break;
            case TSQUEUE_TIMEOUT:
            case TSQUEUE_FAILED_FULL: return offset; break;
            default: break;
        }
        offset += current_length;
        left_to_write -= current_length;
        chunk ++;
    }
    return offset;
}

//=================================================================
int SampleQueue::Read(uint8_t *buffer, size_t length, uint32_t *meta, long timeout_us)
{
    tsqueue_item_st* item_ptr = NULL;
    int left_to_read = length;
    int read_so_far = 0;
    int chunk = 0;

    // first read out from partial buffer
    int amount_to_read_from_partial = (left_to_read <= partial_buffer_length) ? 
                                            left_to_read : partial_buffer_length;
    memcpy (buffer, partial_buffer + partial_buffer_start, amount_to_read_from_partial);
    left_to_read -= amount_to_read_from_partial;
    partial_buffer_length -= amount_to_read_from_partial;
    partial_buffer_start += amount_to_read_from_partial;
    read_so_far += amount_to_read_from_partial;

    // read from the queue
    while (left_to_read)
    {
        int res = tsqueue_pop_item(&queue, &item_ptr, timeout_us);
        switch (res)
        {
            case TSQUEUE_NOT_INITIALIZED:
            case TSQUEUE_SEM_FAILED: 
                {
                    SoapySDR_logf(SOAPY_SDR_ERROR, "popping buffer %d failed", chunk);
                    return -1;
                } break;
            case TSQUEUE_TIMEOUT:
            case TSQUEUE_FAILED_EMPTY: return read_so_far; break;
            default: break;
        }
        if (meta) *meta = item_ptr->metadata;

        // if we need more or exactly the mtu size
        if (left_to_read >= item_ptr->length)
        {
            memcpy(&buffer[read_so_far], item_ptr->data, item_ptr->length);
            left_to_read -= item_ptr->length;
            read_so_far += item_ptr->length;
        }
        // if we need less than the mtu size - store the residue for next time
        else
        {
            // copy out only the amount that is needed
            memcpy(&buffer[read_so_far], item_ptr->data, left_to_read);

            // we are left with "item_ptr->length - left_to_read" bytes
            // which will be stored for future requests

            // store the residue in the partial buffer - for the next time
            partial_buffer_length = item_ptr->length - left_to_read;
            partial_buffer_start = (int)mtu_size_bytes - partial_buffer_length;
            memcpy (partial_buffer + partial_buffer_start, 
                    item_ptr->data + left_to_read,
                    partial_buffer_length);
            
            read_so_far += left_to_read;
            left_to_read = 0;
        }
        chunk ++;
    }

    return read_so_far;
}

//=================================================================
int SampleQueue::ReadSamples(sample_complex_int16* buffer, size_t num_elements, long timeout_us)
{
    static int once = 100;
    static uint16_t last_q = 0;
    // this is the native method
    int tot_length = num_elements * sizeof(sample_complex_int16);
    int res = Read((uint8_t *)buffer, tot_length, NULL, timeout_us);
    if (res < 0)
    {
        // todo!!
        return res;
    }
    
    int tot_read_elements = res / sizeof(sample_complex_int16);

    // shift q
    //buffer[0].q = last_q;
    /*for (int i = 1; i < tot_read_elements; i++)
    {
        buffer[i-1].q = buffer[i].q;
    }*/

	/*for (int i = 1; i < tot_read_elements; i+=2)
    {
		short t = buffer[i].i;
		buffer[i].i = buffer[i+1].i;
		buffer[i+1].i = t;
		t = buffer[i].q;
		buffer[i].q = buffer[i+1].q;
		buffer[i+1].q = t;
    }*/

    /*for (int i = tot_read_elements-1; i >= 0; i--)
    {
        buffer[i].q = buffer[i-1].q;
    }*/

    for (int i = 0; i < tot_read_elements; i++)
    {        
        buffer[i].i >>= 1;
        buffer[i].q >>= 1;

        buffer[i].i = buffer[i].i & 0x1FFF;
        buffer[i].q = buffer[i].q & 0x1FFF;

        if (buffer[i].i >= (int16_t)0x1000) buffer[i].i -= (int16_t)0x2000;
        if (buffer[i].q >= (int16_t)0x1000) buffer[i].q -= (int16_t)0x2000;
    }

	return tot_read_elements;  

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
    uint32_t max_native_samples = 2*(mtu_size_bytes / sizeof(sample_complex_int16));

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
    uint32_t max_native_samples = 2*(mtu_size_bytes / sizeof(sample_complex_int16));

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
    uint32_t max_native_samples = 2*(mtu_size_bytes / sizeof(sample_complex_int16));

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