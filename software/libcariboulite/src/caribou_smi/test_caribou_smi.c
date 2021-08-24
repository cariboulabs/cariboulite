#include <stdio.h>
#include <stdint.h>
#include "caribou_smi.h"

caribou_smi_st dev = {0};

int main()
{
    int count = 4096*32;
    int num_of_rounds = 1;
    uint32_t buffer[count];
    uint8_t* b8 = (uint8_t*)buffer;
    int hist[256] = {0};

    caribou_smi_init(&dev);

    for (int j = 0; j < num_of_rounds; j++)
    {
        caribou_smi_timeout_read(&dev, caribou_smi_address_read_900, (uint8_t*)buffer, count*sizeof(uint32_t), 100);

        for (int i = 1; i<count*sizeof(uint32_t); i++)
        {
            hist[(uint8_t)(b8[i] - b8[i-1])] ++;
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