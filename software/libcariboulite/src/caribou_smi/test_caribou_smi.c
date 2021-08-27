#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOU_SMI_Test"

#include "zf_log/zf_log.h"

#include <stdio.h>
#include <stdint.h>
#include "caribou_smi.h"

caribou_smi_st dev = {0};

void caribou_smi_data_event(void *ctx, caribou_smi_stream_type_en type, caribou_smi_channel_en ch,
                            uint32_t byte_count, uint8_t *buffer, uint32_t buffer_len_bytes)
{
    static int c = 1;
    static uint8_t last_byte = 0;
    static int err_count = 0;
    switch(type)
    {
        //-------------------------------------------------------
        case caribou_smi_stream_type_read:
            {
                //ZF_LOGD("data event: stream channel %d, received %d bytes\n", ch, byte_count);
                for (int i = 0; i< byte_count; i++)
                {
                    uint8_t dist = (uint8_t)(buffer[i] - last_byte);
                    if ( dist > 1)
                    {
                        err_count ++;
                        float bER = (float)(err_count) / (float)(c);
                        printf("ERR > at %d, dist: %d, V: %d, bER: %.7f\n", c, dist, buffer[i], bER);
                    }
                    /*printf("CUR CHUNK >  %d %d %d %d ... %d %d %d %d\n", buffer[0],buffer[1],buffer[2],buffer[3],
                                                                        buffer[byte_count-4],
                                                                        buffer[byte_count-3],
                                                                        buffer[byte_count-2],
                                                                        buffer[byte_count-1]);
                    */
                    last_byte = buffer[i];
                    c++;
                }
            }
            break;

        //-------------------------------------------------------
        case caribou_smi_stream_type_write:
            {

            }
            break;

        //-------------------------------------------------------
        case caribou_smi_stream_start:
            {
                ZF_LOGD("start event: stream channel %d\n", ch);
            }
            break;

        //-------------------------------------------------------
        case caribou_smi_stream_end:
            {
                ZF_LOGD("end event: stream channel %d\n", ch);
            }
            break;

        //-------------------------------------------------------
        default:
            break;
    }
}

void caribou_smi_error_event( void *ctx, caribou_smi_channel_en ch, caribou_smi_error_en err)
{
    ZF_LOGD("Error (from %s) occured in channel %d, err# %d (%s)\n", (char*)ctx, ch, err, caribou_smi_get_error_string(err));
}

char program_name[] = "test_caribou_smi.c";

int main()
{
    int count = 4096*32;
    int num_of_rounds = 1;
    uint32_t buffer[count];
    uint8_t* b8 = (uint8_t*)buffer;
    int hist[256] = {0};

    caribou_smi_init(&dev, caribou_smi_error_event, program_name);

    int stream_id = caribou_smi_setup_stream(&dev,
                                caribou_smi_stream_type_read,
                                caribou_smi_channel_900,
                                4096, 4,
                                caribou_smi_data_event);

    printf("Press ENTER to exit...\n");
    getchar();

    printf("ENTER pressed...\n");

    caribou_smi_destroy_stream(&dev, stream_id);

    for (int j = 0; j < num_of_rounds; j++)
    {
        int ret = caribou_smi_timeout_read(&dev, caribou_smi_address_read_900, (uint8_t*)buffer, count*sizeof(uint32_t), 100);
        if (ret > 0)
        {
            for (int i = 1; i<count*sizeof(uint32_t); i++)
            {
                hist[(uint8_t)(b8[i] - b8[i-1])] ++;
            }
        }
        else
        {
            printf("'caribou_smi_timeout_read' returned %d\n", ret);
        }
    }

    printf("Histogram , buffer[0] = %d, %d, %d, %d\n", ((uint8_t*)buffer)[0], ((uint8_t*)buffer)[1], ((uint8_t*)buffer)[2], ((uint8_t*)buffer)[3]);
    int error_bytes = 0;
    int total_bytes = 0;
    for (int i =0; i<256; i++)
    {
        if (hist[i]>0)
        {
            if (i != 1) error_bytes += hist[i];
            total_bytes += hist[i];
            printf("  %d: %d\n", i, hist[i]);
        }
    }
    printf(" Byte Error Rate: %.10g, %d total, %d errors\n", (float)(error_bytes) / (float)(total_bytes), total_bytes, error_bytes);

    caribou_smi_close (&dev);
}