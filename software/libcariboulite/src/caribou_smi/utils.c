#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOU_SMI_Utils"

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "zf_log/zf_log.h"


//=========================================================================
void smi_utils_set_realtime_priority(int priority_deter)
{
    int ret;

    // We'll operate on the currently running thread.
    pthread_t this_thread = pthread_self();
    // struct sched_param is used to store the scheduling priority
    struct sched_param params;

    // We'll set the priority to the maximum.
    params.sched_priority = sched_get_priority_max(SCHED_FIFO) - priority_deter;
    ZF_LOGI("Trying to set thread realtime prio = %d", params.sched_priority);

    // Attempt to set thread real-time priority to the SCHED_FIFO policy
    ret = pthread_setschedparam(this_thread, SCHED_FIFO, &params);
    if (ret != 0)
    {
        // Print the error
        ZF_LOGE("Unsuccessful in setting thread realtime prio");
        return;
    }
    // Now verify the change in thread priority
    int policy = 0;
    ret = pthread_getschedparam(this_thread, &policy, &params);
    if (ret != 0)
    {
        ZF_LOGE("Couldn't retrieve real-time scheduling paramers");
        return;
    }

    // Check the correct policy was applied
    if(policy != SCHED_FIFO)
    {
        ZF_LOGE("Scheduling is NOT SCHED_FIFO!");
    } else {
        ZF_LOGI("SCHED_FIFO OK");
    }

    // Print thread scheduling priority
    ZF_LOGI("Thread priority is %d", params.sched_priority);
}

//=========================================================================
void smi_utils_dump_hex(const void* data, size_t size)
{
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';

	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~')
        {
			ascii[i % 16] = ((unsigned char*)data)[i];
		}
        else
        {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size)
        {
			printf(" ");
			if ((i+1) % 16 == 0)
            {
				printf("|  %s \n", ascii);
			}
            else if (i+1 == size)
            {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8)
                {
					printf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j)
                {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
}

//=========================================================================
void smi_utils_dump_bin(const uint8_t* data, size_t size)
{
	char str[16] = {0};

	for (size_t i = 0; i < size; i++)
	{
		if (i % 8 == 0) printf("\n");
		int k = 0;
		uint8_t b = data[i];
		for (k = 0; k < 8; k++)
		{
			str[k] = (b&0x80)==0?'0':'1';
			b <<= 1;
		}
		str[k] = ' ';
		printf("%s", str);
	}
	printf("\n");
}

//=========================================================================
void smi_utils_print_bin(uint32_t v)
{
	char str[48] = {0};
	int i = 0;
	for (int k = 0; k < 32; k++)
	{
		if (k%8==0) str[i++]=' ';
		str[i++] = (v&0x80000000)==0?'0':'1';
		v <<= 1;
	}
	printf("%s\n", str);
}

//=========================================================================
int smi_utils_allocate_buffer_vec(uint8_t*** mat, int num_buffers, int buffer_size)
{
    ZF_LOGI("Allocating buffer vectors");
    (*mat) = (uint8_t**) malloc( num_buffers * sizeof(uint8_t*) );
    if ((*mat) == NULL)
    {
        ZF_LOGE("buffer vector allocation failed");
        return -1;
    }

    memset( (*mat), 0, num_buffers * sizeof(uint8_t*) );

    int failed = 0;
    int i;
    for (i = 0; i < num_buffers; i++)
    {
        (*mat)[i] = (uint8_t*)calloc( buffer_size, sizeof(uint8_t) );
        if ((*mat)[i] == NULL)
        {
            failed = 1;
            break;
        }
    }
    if (failed)
    {
        for (int j = 0; j < i; j++)
        {
            free((*mat)[j]);
        }
        free((*mat));

        ZF_LOGE("buffer (%d) allocation failed", i);
        return -1;
    }

    return 0;
}

//=========================================================================
void smi_utils_release_buffer_vec(uint8_t** mat, int num_buffers, int buffer_size)
{
    ZF_LOGI("Releasing buffer vectors");
    if (mat == NULL)
        return;

    for (int i = 0; i < num_buffers; i ++)
    {
        if (mat[i] != NULL) free (mat[i]);
    }

    free(mat);
}

//=========================================================================
int smi_utils_search_offset_in_buffer(uint8_t *buff, int len)
{
	bool succ = false;
	int off = 0;
	while (!succ)
	{
		if ( (buff[off + 0] & 0xC0) == 0xC0 && 
			 (buff[off + 4] & 0xC0) == 0xC0 &&
			 (buff[off + 8] & 0xC0) == 0xC0 &&
			 (buff[off + 12] & 0xC0) == 0xC0 )
			 return off;
		off ++;
	}
	return -1;
}

//=========================================================================
uint8_t smi_utils_lfsr(uint8_t n)
{
	uint8_t bit = ((n >> 2) ^ (n >> 3)) & 1;
	return (n >> 1) | (bit << 7);
}

//=========================================================================
double smi_calculate_performance(size_t bytes, struct timeval *old_time, double old_mbps)
{
	struct timeval current_time = {0,0};
	
	gettimeofday(&current_time, NULL);
	
	double elapsed_us = (current_time.tv_sec - old_time->tv_sec) + ((double)(current_time.tv_usec - old_time.tv_usec)) / 1000000.0;
	double speed_mbps = (double)(bytes * 8) / elapsed_us / 1e6;
	old_time->tv_sec = current_time.tv_sec;
	old_time->tv_usec = current_time.tv_usec;
	return old_mbps * 0.98 + speed_mbps * 0.02;
}
