#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOU_SMI_Test"

#include "zf_log/zf_log.h"

#include <stdio.h>
#include <stdint.h>
#include <math.h>
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
                if ( byte_count > 0 )
                {
                    printf("CHUNK %d> %02x %02x %02x %02x...\n", c, buffer[0],
                                                                    buffer[1],
                                                                    buffer[2],
                                                                    buffer[3]);
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
                else printf("CUR %d MISSED\n", c);
                c++;
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
                ZF_LOGD("start event: stream channel %d, batch length: %d bytes\n", ch, buffer_len_bytes);
            }
            break;

        //-------------------------------------------------------
        case caribou_smi_stream_end:
            {
                ZF_LOGD("end event: stream channel %d, batch length: %d bytes\n", ch, buffer_len_bytes);
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

void print_iq(uint32_t* array, int len)
{
    printf("Values I/Q:\n");
    for (int i=0; i<len; i++)
    {
        unsigned int v = array[i];
        uint8_t b[4];
        b[0] = (uint8_t) (v >> 24u);
        b[1] = (uint8_t) (v >> 16u);
        b[2] = (uint8_t) (v >> 8u);
        b[3] = (uint8_t) (v >> 0u);
        v = *((uint32_t*)(b));

        //printf("%08x\n", v);
        int cnt = (v >> 30)*4 + ((v >> 14) & 0x3);
        int16_t q_val = (v>> 1) & (0x1FFF);
        int16_t i_val = (v>>17) & (0x1FFF);
        if (q_val >= 0x1000) q_val-=0x2000;
        if (i_val >= 0x1000) i_val-=0x2000;
        float fi = i_val, fq = q_val;
        float mod = sqrt(fi*fi + fq*fq);
        float arg = atan(fq / fi);
        printf("%d, %d, %d, %.4f, %.2f\n", cnt, i_val, q_val, mod, arg);
    }
}

int main()
{
    int count = 128;
    uint32_t buffer[count];
    uint8_t* b8 = (uint8_t*)buffer;

    caribou_smi_init(&dev, caribou_smi_error_event, program_name);

    caribou_smi_timeout_read(&dev, caribou_smi_address_read_900, b8, count*sizeof(uint32_t), 1000);
    dump_hex(b8, count*sizeof(uint32_t));

    print_iq(buffer, count);


    caribou_smi_close (&dev);
    return 0;


    int stream_id = caribou_smi_setup_stream(&dev,
                                caribou_smi_stream_type_read,
                                caribou_smi_channel_900,
                                4096*4*10, 2,                      // ~10 milliseconds of I/Q sample (32 bit)
                                caribou_smi_data_event);

    printf("Press ENTER to exit...\n");
    getchar();

    printf("ENTER pressed...\n");

    caribou_smi_destroy_stream(&dev, stream_id);

    /*for (int j = 0; j < num_of_rounds; j++)
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
    */
    caribou_smi_close (&dev);
}