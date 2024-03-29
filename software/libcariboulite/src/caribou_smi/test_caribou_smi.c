#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOU_SMI_Test"

#include "zf_log/zf_log.h"

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include "caribou_smi.h"
#include "utils.h"

caribou_smi_st dev = {0};
char program_name[] = "test_caribou_smi.c";

#define NUM_MS          (5)
#define NUM_SAMPLES     (NUM_MS * 4096)
#define SIZE_OF_SAMPLE  (4)
#define NUM_OF_BUFFERS  (2)


//==============================================
void swap_little_big (uint32_t *v)
{
    uint8_t b[4];
    b[0] = (uint8_t) ((*v) >> 24u);
    b[1] = (uint8_t) ((*v) >> 16u);
    b[2] = (uint8_t) ((*v) >> 8u);
    b[3] = (uint8_t) ((*v) >> 0u);
    *v = *((uint32_t*)(b));
}

//==============================================
void print_iq(uint32_t* array, int len)
{
    printf("Values I/Q:\n");
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
        printf("%08X,   %d, %d, %.4f, %.2f\n", v, i_val, q_val, mod, arg);
    }
}

void caribou_smi_data_event(void *ctx,                              	// The context of the requesting application
							void *serviced_context,                 	// the context of the session within the app
							caribou_smi_stream_type_en type,        	// which type of stream is it? read / write?
							caribou_smi_event_type_en ev,					// the event (start / stop)
							caribou_smi_channel_en ch,              	// which channel (900 / 2400)
							size_t num_samples,                    		// for "read stream only" - number of read data bytes in buffer
							caribou_smi_sample_complex_int16 *cplx_vec, // for "read" - complex vector of samples to be analyzed
																		// for "write" - complex vector of samples to be written into
							caribou_smi_sample_meta *metadat_vec,		// for "read" - the metadata send by the receiver for each sample
																		// for "write" - the metadata to be written by app for each sample
							size_t total_length_samples)
{
	if (ev == caribou_smi_event_type_start)
	{
		ZF_LOGD("start event: stream batch length: %u samples\n", total_length_samples);
		return;
	}
	else if (ev == caribou_smi_event_type_end)
	{
		ZF_LOGD("end event: stream batch length: %u samples\n", total_length_samples);
		return;
	}

    switch(type)
    {
        //-------------------------------------------------------
        case caribou_smi_stream_type_read:
            {
                ZF_LOGD("data event: stream channel %d, received %u samples\n", ch, num_samples);
                //print_iq((uint32_t*)buffer, 8);
                /*for (int i = 0; i< byte_count; i++)
                {
                    uint8_t dist = (uint8_t)(buffer[i] - last_byte);
                    if ( dist > 1)
                    {
                        err_count ++;
                        float bER = (float)(err_count) / (float)(c);
                        printf("ERR > at %d, dist: %d, V: %d, bER: %.7f\n", c, dist, buffer[i], bER);
                    }

                    last_byte = buffer[i];
                    c++;
                }*/
                if ( num_samples > 0 )
                {
                    /*printf("CHUNK %d> %02x %02x %02x %02x...\n", c, buffer[0],
                                                                   buffer[1],
                                                                    buffer[2],
                                                                    buffer[3]);*/
/*
                    printf("CUR %d CHUNK >  %02x %02x %02x %02x ... %02x %02x %02x %02x\n", c,
                                                                        buffer[0],
                                                                        buffer[1],
                                                                        buffer[2],
                                                                        buffer[3],
                                                                        buffer[byte_count-4],
                                                                        buffer[byte_count-3],
                                                                        buffer[byte_count-2],
                                                                        buffer[byte_count-1]);*/
                }
            }
            break;

        //-------------------------------------------------------
        case caribou_smi_stream_type_write:
            {

            }
            break;

        //-------------------------------------------------------
        default:
            break;
    }
}

//==============================================
void caribou_smi_error_event( void *ctx, caribou_smi_channel_en ch, caribou_smi_error_en err)
{
    ZF_LOGD("Error (from %s) occured in channel %d, err# %d (%s)\n", (char*)ctx, ch, err, caribou_smi_get_error_string(err));
}

//==============================================
int main_single_read()
{
    int read_count = 4096;
    uint32_t buffer[read_count];
    char* b8 = (char*)buffer;

    caribou_smi_init(&dev, caribou_smi_error_event, program_name);

    caribou_smi_timeout_read(&dev, address, b8, read_count*sizeof(uint32_t), 1000);
    smi_utils_dump_hex(b8, read_count*sizeof(uint32_t));
    print_iq(buffer, read_count);
    caribou_smi_close (&dev);
    return 0;
}


//==============================================
int main_stream_read()
{
    caribou_smi_init(&dev, caribou_smi_error_event, program_name);
    int stream_id = caribou_smi_setup_stream(&dev,
                                caribou_smi_stream_type_read,
                                channel,
                                caribou_smi_data_event,
                                NULL);

    sleep(1);
    caribou_smi_run_pause_stream (&dev, stream_id, 1);
    printf("Listening on stream_id = %d\n", stream_id);
    printf("Press ENTER to exit...\n");
    getchar();

    printf("ENTER pressed...\n");

    //caribou_smi_run_pause_stream (&dev, stream_id, 0);
    caribou_smi_destroy_stream(&dev, stream_id);
    caribou_smi_close (&dev);
    return 0;
}

//==============================================
int main()
{
    main_stream_read();
    return 0;
}