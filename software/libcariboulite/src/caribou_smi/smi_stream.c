 #ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "SMI_STREAM"

#define _GNU_SOURCE

#ifdef __cplusplus
extern "C" {
#endif
	#include "kernel/bcm2835_smi.h"
#ifdef __cplusplus
}
#endif

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>

#include "zf_log/zf_log.h"

#include "utils.h"
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
static int smi_stream_poll(smi_stream_st* st, uint32_t timeout_num_millisec, smi_stream_direction_en dir)
{
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
	int rv = 0;
	if (dir == smi_stream_dir_rx) 		rv = select(st->filedesc + 1, &set, NULL, NULL, &timeout);
	else if ((dir == smi_stream_dir_tx) rv = select(st->filedesc + 1, NULL, &set, NULL, &timeout);
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
    // Set the address
    smi_stream_set_address(st->filedesc, st->stream_addr)
	
	int res = smi_stream_poll(st, timeout_num_millisec, smi_stream_dir_tx);
	
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
    // Set the address
    smi_stream_set_address(st->filedesc, st->stream_addr)
	
	int res = smi_stream_poll(st, timeout_num_millisec, smi_stream_dir_rx);
	
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
	
	if (dir == smi_stream_dir_rx)
	{
		temp = st->smi_read_point;
		st->smi_read_point = st->app_read_point;
		st->app_read_point = temp;
	}
	else if (dir == smi_stream_dir_rx)
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
        pthread_mutex_lock(&st->reader_lock);
        if (!st->active) break;
		
		int ret = smi_stream_timeout_read(st, st->smi_read_point, st->buffer_length, 200);
        if (ret < 0)
        {
            if (st->event_cb) dev->event_cb(st->stream_addr, smi_stream_error, NULL, st->context);
            break;
        }
        else if (ret == 0)  // timeout
        {
            continue;
        }
		
		// Swap the buffers
		smi_stream_swap_buffers(st, smi_stream_dir_rx);
		
		// Notify app
		if (st->data_cb) st->data_cb(st->stream_addr, smi_stream_dir_rx, st->app_read_point, ret, st->context);
		
		// Performance
		st->rx_bitrate_mbps = smi_calculate_performance(ret, &current_time, st->rx_bitrate_mbps);
    }

    ZF_LOGD("Leaving SMI reader thread id %lu", tid);
    return NULL;
}

//=========================================================================
static void* smi_stream_writer_thread(void* arg)
{
smi_stream_st* st = (smi_stream_st*)arg;
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
        pthread_mutex_lock(&st->writer_lock);
        if (!st->active) break;
		
		int ret = smi_stream_timeout_write(st, st->smi_write_point, st->buffer_length, 200);
        if (ret < 0)
        {
            if (st->event_cb) dev->event_cb(st->stream_addr, smi_stream_error, NULL, st->context);
            break;
        }
        else if (ret == 0)  // timeout
        {
            continue;
        }
		
		// Swap the buffers
		smi_stream_swap_buffers(st, smi_stream_dir_tx);
		
		// Notify app
		if (st->data_cb) st->data_cb(st->stream_addr, smi_stream_dir_tx, st->app_read_point, ret, st->context);
		
		// Performance
		st->tx_bitrate_mbps = smi_calculate_performance(ret, &current_time, st->tx_bitrate_mbps);
    }

    ZF_LOGD("Leaving SMI writer thread id %lu", tid);
    return NULL;
}
 
//=========================================================================
int smi_stream_init(smi_stream_st* st,
					int filedesc,
					int stream_addr,
					smi_stream_data_callback data_cb,
					smi_stream_event_callback event_cb,
					void* context)
{
	ZF_LOGI("Initializing smi stream address (%d) from filedesc (%d)", stream_addr, filedesc);
	
	if (st->initialized)
	{
		ZF_LOGE("The requested smi stream address is already initialized (%d)", stream_addr);
        return 1;
	}
	
	// A clean init
	// -------------------------------------------------------
	memset(st, 0, sizeof(smi_stream_st));
	
	// App data
	st->stream_addr = stream_addr;
	st->data_cb = data_cb;
	st->event_cb = event_cb;
	st->context = context;
	st->filedesc = filedesc;
	
	// Buffers allocation
	// -------------------------------------------------------
	st->buffer_length = smi_stream_get_native_buffer_length(st->filedesc);
	if (st->buffer_length < 0)
	{
		ZF_LOGE("Reading smi stream native buffer length failed, setting default");
		st->buffer_length = DMA_BOUNCE_BUFFER_SIZE;
	}
	
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
	st->smi_read_point = smi_read_buffers[0];
	st->app_read_point = smi_read_buffers[1];
	
	st->smi_write_point = smi_write_buffers[0];
	st->app_write_point = smi_write_buffers[1];
	
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
        return NULL;
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
        return NULL;
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
		if (smi_read_buffers[i] != NULL) free(smi_read_buffers[i]);
		if (smi_write_buffers[i] != NULL) free(smi_write_buffers[i]);
	}
	
	usleep(500);
	
	st->initialized = false;
	return 0;
}
