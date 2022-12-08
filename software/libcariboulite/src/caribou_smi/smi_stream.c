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
static void* smi_stream_reader_thread(void* arg)
{
	int current_data_size = 0;
    pthread_t tid = pthread_self();
	TIMING_PERF_SYNC_VARS;

    caribou_smi_stream_st* st = (caribou_smi_stream_st*)arg;
    caribou_smi_st* dev = (caribou_smi_st*)st->parent_dev;
    caribou_smi_stream_type_en type = (caribou_smi_stream_type_en)(st->stream_id>>1 & 0x1);
    caribou_smi_channel_en ch = (caribou_smi_channel_en)(st->stream_id & 0x1);

    ZF_LOGD("Entered SMI analysis thread id %lu, running = %d", tid, st->read_analysis_thread_running);
    set_realtime_priority(2);

	// ****************************************
	//  MAIN LOOP
    // ****************************************	
    while (st->read_analysis_thread_running)
    {
        pthread_mutex_lock(&st->read_analysis_lock);
		TIMING_PERF_SYNC_TICK;
        if (!st->read_analysis_thread_running) break;

		current_data_size = st->read_ret_value;
		//if (offset != 0) current_data_size -= 4;

		dev-> rx_bitrate_mbps = caribou_smi_analyze_data(st->current_app_buffer, 
								current_data_size, 
								st->app_cmplx_vec, 
								st->app_meta_vec,
								dev->smi_debug, dev->fifo_push_debug, dev->fifo_pull_debug);
		dev-> rx_bitrate_mbps /= 1e6;

        if (st->data_cb) st->data_cb(dev->cb_context, 
									st->service_context, 
									type,
									caribou_smi_event_type_data,
									ch,
                                    current_data_size / 4,
                                    st->app_cmplx_vec,
                                    st->app_meta_vec,
									st->batch_length / 4);
        
		TIMING_PERF_SYNC_TOCK;
    }

    ZF_LOGD("Leaving SMI analysis thread id %lu, running = %d", tid, st->read_analysis_thread_running);
    return NULL;
}

//=========================================================================
static void* smi_stream_writer_thread(void* arg)
{
	
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
	int buf_len = smi_stream_get_native_buffer_length(st->filedesc);
	if (buf_len < 0)
	{
		ZF_LOGE("Reading smi stream native buffer length failed, setting default");
		buf_len = DMA_BOUNCE_BUFFER_SIZE;
	}
	
	for (int i = 0; i < 2; i++)
	{
		st->smi_read_buffers[i] = malloc(buf_len);
		st->smi_write_buffers[i] = malloc(buf_len);
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
					
