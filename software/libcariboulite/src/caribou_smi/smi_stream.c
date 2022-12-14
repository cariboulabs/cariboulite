#ifndef ZF_LOG_LEVEL
	#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "SMI_STREAM"
#include "zf_log/zf_log.h"

#define _GNU_SOURCE

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sched.h>
#include <pthread.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#include "utils.h"
#include "io_utils/io_utils.h"
#include "smi_stream.h"

//=========================================================================
static int smi_stream_get_native_buffer_length(int filedesc)
{
	uint32_t len = 0;
	int ret = ioctl(filedesc, SMI_STREAM_IOC_GET_NATIVE_BUF_SIZE, &len);
	if (ret < 0) return ret;
	return len;
}

//=========================================================================
static int smi_stream_set_address(int filedesc, int address)
{
	int ret = ioctl(filedesc, BCM2835_SMI_IOC_ADDRESS, address);
	if (ret != 0)
	{
		ZF_LOGE("failed setting smi address (%d) to device", address);
		return -1;
	}
	return 0;
}

//=========================================================================
static int smi_stream_set_channel(int filedesc, smi_stream_channel_en ch)
{
	int ret = ioctl(filedesc, SMI_STREAM_IOC_SET_STREAM_IN_CHANNEL, (uint32_t)ch);
	if (ret != 0)
	{
		ZF_LOGE("failed setting smi stream channel (%d) to device", ch);
		return -1;
	}
	return 0;
}

//=========================================================================
static int smi_stream_poll(smi_stream_st* st, uint32_t timeout_num_millisec, smi_stream_direction_en dir)
{
	int rv = 0;
	
	// Calculate the timeout
	struct timeval timeout = {0};
	int num_sec = timeout_num_millisec / 1000;
    timeout.tv_sec = num_sec;
    timeout.tv_usec = (timeout_num_millisec - num_sec * 1000) * 1000;

	// Poll setting
    fd_set set;
    FD_ZERO(&set);                 // clear the set mask
    FD_SET(st->filedesc, &set);    // add only our file descriptor to the set

again:
	if (dir == smi_stream_dir_device_to_smi)		rv = select(st->filedesc + 1, &set, NULL, NULL, &timeout);
	else if (dir == smi_stream_dir_smi_to_device)	rv = select(st->filedesc + 1, NULL, &set, NULL, &timeout);
	else return -1;

    if(rv == -1)
    {
        int error = errno;
        switch(error)
        {
            case EBADF:         // An invalid file descriptor was given in one of the sets.
                                // (Perhaps a file descriptor that was already closed, or one on which an error has occurred.)
                ZF_LOGE("SMI filedesc select error - invalid file descriptor in one of the sets");
                break;
            case EINTR:	        // A signal was caught.
                ZF_LOGD("SMI filedesc select error - caught an interrupting signal");
                goto again;
                break;
            case EINVAL:        // nfds is negative or the value contained within timeout is invalid.
                ZF_LOGE("SMI filedesc select error - nfds is negative or invalid timeout");
                break;
            case ENOMEM:        // unable to allocate memory for internal tables.
                ZF_LOGE("SMI filedesc select error - internal tables allocation failed");
                break;
            default: break;
        };

        return -1;
    }
    else if(rv == 0)
    {
        return 0;
    }

	return FD_ISSET(st->filedesc, &set);
}

//=========================================================================
static int smi_stream_timeout_write(smi_stream_st* st,
                            uint8_t* buffer,
                            size_t len,
                            uint32_t timeout_num_millisec)
{
    // Set the address - no need to do it, as the TX size has 
	// anyway a single channel
    // smi_stream_set_channel(st->filedesc, st->channel);

	int res = smi_stream_poll(st, timeout_num_millisec, smi_stream_dir_device_to_smi);

	if (res < 0)
	{
		ZF_LOGD("select error");
		return -1;
	}
	else if (res == 0)	// timeout
	{
		ZF_LOGD("smi write fd timeout");
		return 0;
	}

	return write(st->filedesc, buffer, len);
}

//=========================================================================
static int smi_stream_timeout_read(smi_stream_st* st,
								uint8_t* buffer,
								size_t len,
								uint32_t timeout_num_millisec)
{
    // Set the rx address
    smi_stream_set_channel(st->filedesc, st->channel);

	int res = smi_stream_poll(st, timeout_num_millisec, smi_stream_dir_smi_to_device);

	if (res < 0)
	{
		ZF_LOGD("select error");
		return -1;
	}
	else if (res == 0)	// timeout
	{
		ZF_LOGD("smi read fd timeout");
		return 0;
	}

	return read(st->filedesc, buffer, len);
}

//=========================================================================
static void smi_stream_swap_buffers(smi_stream_st* st, smi_stream_direction_en dir)
{
	uint8_t * temp = NULL;

	if (dir == smi_stream_dir_device_to_smi)
	{
		temp = st->smi_read_point;
		st->smi_read_point = st->app_read_point;
		st->app_read_point = temp;
	}
	else if (dir == smi_stream_dir_smi_to_device)
	{
		temp = st->smi_write_point;
		st->smi_write_point = st->app_write_point;
		st->app_write_point = temp;
	}
	else {}
}

//=========================================================================
static void* smi_stream_reader_thread(void* arg)
{
	smi_stream_st* st = (smi_stream_st*)arg;
    pthread_t tid = pthread_self();
	smi_utils_set_realtime_priority(2);

	// performance
	struct timeval current_time = {0,0};

    ZF_LOGD("Entered SMI reader thread id %lu", tid);

	// ****************************************
	//  MAIN LOOP
    // ****************************************
    while (st->active)
    {
        //pthread_mutex_lock(&st->reader_lock);
        if (!st->active) break;

		// switch to this channel
		
		
		// try read
		int ret = smi_stream_timeout_read(st, st->smi_read_point, st->buffer_length, 200);
        if (ret < 0)
        {
            if (st->event_cb) st->event_cb(st->channel, smi_stream_error, NULL, st->context);
			continue;
            //break;
        }
        else if (ret == 0)  // timeout
        {
            continue;
        }

		// Swap the buffers
		smi_stream_swap_buffers(st, smi_stream_dir_device_to_smi);

		// Notify app
		if (st->rx_data_cb) st->rx_data_cb(st->channel, st->app_read_point, ret, st->context);

		// Performance
		st->rx_bitrate_mbps = smi_calculate_performance(ret, &current_time, st->rx_bitrate_mbps);
    }

    ZF_LOGD("Leaving SMI reader thread id %lu", tid);
    return NULL;
}

//=========================================================================
static void* smi_stream_writer_thread(void* arg)
{
	int ret = 0;
	smi_stream_st* st = (smi_stream_st*)arg;
	size_t data_len_to_write = 0;
    pthread_t tid = pthread_self();
	smi_utils_set_realtime_priority(2);

	// performance
	struct timeval current_time = {0,0};

    ZF_LOGD("Entered SMI writer thread id %lu", tid);

	// ****************************************
	//  MAIN LOOP
    // ****************************************
    while (st->active)
    {
        //pthread_mutex_lock(&st->writer_lock);
        if (!st->active) break;

		// Notify app
		if (st->tx_data_cb)
		{
			size_t buffer_len = st->buffer_length;
			data_len_to_write = st->tx_data_cb(st->channel, st->app_write_point, &buffer_len, st->context);
		}

		// check if there is something to write
		if (data_len_to_write == 0)
		{
			io_utils_usleep(10000);
			continue;
		}

		ret = smi_stream_timeout_write(st, st->smi_write_point, data_len_to_write, 200);
        if (ret < 0)
        {
            if (st->event_cb) st->event_cb(st->channel, smi_stream_error, NULL, st->context);
			continue;
            //break;
        }
        else if (ret == 0)  // timeout
        {
            continue;
        }

		// Performance
		st->tx_bitrate_mbps = smi_calculate_performance(ret, &current_time, st->tx_bitrate_mbps);

		// Swap the buffers
		smi_stream_swap_buffers(st, smi_stream_dir_smi_to_device);
    }

    ZF_LOGD("Leaving SMI writer thread id %lu", tid);
    return NULL;
}

//=========================================================================
int smi_stream_init(smi_stream_st* st,
					int filedesc,
					smi_stream_channel_en channel,
					smi_stream_rx_data_callback rx_data_cb,
					smi_stream_tx_data_callback tx_data_cb,
					smi_stream_event_callback event_cb,
					void* context)
{
	ZF_LOGI("Initializing smi stream channel (%d) from filedesc (%d)", channel, filedesc);

	if (st->initialized)
	{
		ZF_LOGE("The requested smi stream channel is already initialized (%d)", channel);
        return 1;
	}

	// A clean init
	// -------------------------------------------------------
	memset(st, 0, sizeof(smi_stream_st));

	// App data
	st->channel = channel;
	st->rx_data_cb = rx_data_cb;
	st->tx_data_cb = tx_data_cb;
	st->event_cb = event_cb;
	st->context = context;
	st->filedesc = filedesc;

	// Buffers allocation
	// -------------------------------------------------------
	int ret	= smi_stream_get_native_buffer_length(st->filedesc);
	if (ret < 0)
	{
		ZF_LOGE("Reading smi stream native buffer length failed, setting default");
		st->buffer_length = DMA_BOUNCE_BUFFER_SIZE;
	}
	else st->buffer_length = (size_t)ret;

	for (int i = 0; i < 2; i++)
	{
		st->smi_read_buffers[i] = malloc(st->buffer_length);
		st->smi_write_buffers[i] = malloc(st->buffer_length);
		if (st->smi_read_buffers[i] == NULL || st->smi_write_buffers[i] == NULL)
		{
			ZF_LOGE("SMI stream allocation of buffers failed");
			smi_stream_release(st);
			return -1;
		}
	}
	st->smi_read_point = st->smi_read_buffers[0];
	st->app_read_point = st->smi_read_buffers[1];

	st->smi_write_point = st->smi_write_buffers[0];
	st->app_write_point = st->smi_write_buffers[1];

	// Reader thread
	// -------------------------------------------------------
	st->active = true;
	if (pthread_mutex_init(&st->reader_lock, NULL) != 0)
    {
        ZF_LOGE("SMI stream reader mutex init failed");
		smi_stream_release(st);
		return -1;
    }
    pthread_mutex_lock(&st->reader_lock);

    if (pthread_create(&st->reader_thread, NULL, &smi_stream_reader_thread, st) != 0)
    {
        ZF_LOGE("SMI stream reader thread creation failed");
        smi_stream_release(st);
        return -1;
    }

	// Writer thread
	// -------------------------------------------------------
	if (pthread_mutex_init(&st->writer_lock, NULL) != 0)
    {
        ZF_LOGE("SMI stream writer mutex init failed");
		smi_stream_release(st);
		return -1;
    }
	pthread_mutex_lock(&st->writer_lock);

	if (pthread_create(&st->writer_thread, NULL, &smi_stream_writer_thread, st) != 0)
    {
        ZF_LOGE("SMI stream writer thread creation failed");
        smi_stream_release(st);
        return -1;
    }

	// declare initialized
	st->initialized = true;
	return 0;
}

//=========================================================================
int smi_stream_release(smi_stream_st* st)
{
	// Destroy thrreads
	st->active = false;
	pthread_mutex_unlock(&st->reader_lock);
    pthread_join(st->reader_thread, NULL);
    pthread_mutex_destroy(&st->reader_lock);

	pthread_mutex_unlock(&st->writer_lock);
    pthread_join(st->writer_thread, NULL);
    pthread_mutex_destroy(&st->writer_lock);

	// Release the buffers
	for (int i = 0; i < 2; i++)
	{
		if (st->smi_read_buffers[i] != NULL) free(st->smi_read_buffers[i]);
		if (st->smi_write_buffers[i] != NULL) free(st->smi_write_buffers[i]);
	}

	usleep(500);

	st->initialized = false;
	return 0;
}

//=========================================================================
void smi_stream_get_datarate(smi_stream_st* st, float *tx_mbps, float *rx_mbps)
{
	if (tx_mbps) *tx_mbps = st->tx_bitrate_mbps;
	if (rx_mbps) *rx_mbps = st->rx_bitrate_mbps;
}
