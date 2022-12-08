#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOU_SMI"

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <aio.h>
#include <signal.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>

#include "zf_log/zf_log.h"
#include "caribou_smi.h"
#include "utils.h"
#include "io_utils/io_utils.h"

#ifdef __cplusplus
extern "C" {
#endif
    #include "kernel/smi_stream_dev.h"
	#include "kernel/bcm2835_smi.h"
#ifdef __cplusplus
}
#endif


static char *error_strings[] = CARIBOU_SMI_ERROR_STRS;

static void caribou_smi_print_smi_settings(caribou_smi_st* dev, struct smi_settings *settings);
static void caribou_smi_setup_settings (caribou_smi_st* dev, struct smi_settings *settings);
static int caribou_smi_init_stream(caribou_smi_st* dev, caribou_smi_stream_type_en type, caribou_smi_channel_en ch);


#define TIMING_PERF_SYNC  (0)

#if (TIMING_PERF_SYNC)
	#define TIMING_PERF_SYNC_VARS									\
			struct timeval tv_pre = {0};							\
			struct timeval tv_post = {0};							\
			long long total_samples = 0,last_total_samples = 0;		\
			double time_pre = 0, batch_time = 0, sample_rate = 0;	\
			double time_post = 0, process_time = 0;					\
			double temp_pre;										\
			double num_samples = 0, num_samples_avg = 0;

	#define TIMING_PERF_SYNC_TICK									\
			gettimeofday(&tv_pre, NULL);

	#define TIMING_PERF_SYNC_TOCK													\
			gettimeofday(&tv_post, NULL);											\
			num_samples = (double)(st->read_ret_value) / 4.0;						\
			num_samples_avg = num_samples_avg*0.1 + num_samples*0.9;				\
			temp_pre = tv_pre.tv_sec + ((double)(tv_pre.tv_usec)) / 1e6;			\
			time_post = tv_post.tv_sec + ((double)(tv_post.tv_usec)) / 1e6;			\
			batch_time = temp_pre - time_pre;										\
			sample_rate = sample_rate*0.1 + (num_samples / batch_time) * 0.9;		\
			process_time = process_time*0.1 + (time_post - temp_pre)*0.9;			\
			time_pre = temp_pre;													\
			total_samples += st->read_ret_value;									\
			if ((total_samples - last_total_samples) > 4000000*4)					\
			{																		\
				last_total_samples = total_samples;									\
				ZF_LOGD("sample_rate = %.2f SPS, process_time = %.2f usec"			\
						", num_samples_avg = %.1f", 								\
						sample_rate, process_time * 1e6, num_samples_avg);			\
			}
#else
	#define TIMING_PERF_SYNC_VARS
	#define TIMING_PERF_SYNC_TICK
	#define TIMING_PERF_SYNC_TOCK
#endif


//=========================================================================
char* caribou_smi_get_error_string(caribou_smi_error_en err)
{
    return error_strings[err];
}

//=========================================================================
int caribou_smi_init(caribou_smi_st* dev, caribou_smi_error_callback error_cb, void* context)
{
    char smi_file[] = "/dev/smi";
    struct smi_settings settings = {0};

	ZF_LOGI("initializing caribou_smi");

	// checking the loaded modules
	if (caribou_smi_check_modules(true) < 0)
	{
		ZF_LOGE("Problem reloading SMI kernel modules");
		return -1;
	}
	
    // open the smi device file
    int fd = open(smi_file, O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        ZF_LOGE("couldn't open smi driver file '%s'", smi_file);
        return -1;
    }
    dev->filedesc = fd;

	// setup the IOs
	for (int i = 4; i <= 15; i++)
	{
		io_utils_set_gpio_mode(i, io_utils_alt_1);
	}
	io_utils_set_gpio_mode(24, io_utils_alt_1);
	io_utils_set_gpio_mode(25, io_utils_alt_1);

    // Retrieve the current settings
    int ret = ioctl(fd, BCM2835_SMI_IOC_GET_SETTINGS, &settings);
    if (ret != 0)
    {
        ZF_LOGE("failed reading ioctl from smi fd (settings)");
        close (fd);
        return -1;
    }

    // Apply the new settings
    caribou_smi_setup_settings(dev, &settings);
    ret = ioctl(fd, BCM2835_SMI_IOC_WRITE_SETTINGS, &settings);
    if (ret != 0)
    {
        ZF_LOGE("failed writing ioctl to the smi fd (settings)");
        close (fd);
        return -1;
    }

    // set the address to first channel
    ret = ioctl(fd, BCM2835_SMI_IOC_ADDRESS, caribou_smi_address_read_900);
    if (ret != 0)
    {
        ZF_LOGE("failed setting smi address (idle / %d) to device", caribou_smi_address_idle);
        close (fd);
        return -1;
    }
    dev->current_address = caribou_smi_address_read_900;

	// get the native batch length in bytes
	ret = ioctl(fd, SMI_STREAM_IOC_GET_NATIVE_BUF_SIZE, &dev->native_batch_length_bytes);
    if (ret != 0)
    {
        ZF_LOGE("failed reading native batch length, setting the default - this error is not fatal but we have wrong kernel drivers");
		dev->native_batch_length_bytes = (1024)*(1024)/2;
        //close (fd);
        //return -1;
    }
	ZF_LOGI("Finished interogating 'smi' driver. Native batch length (bytes) = %d", dev->native_batch_length_bytes);

	ZF_LOGD("Current SMI Settings:");
    caribou_smi_print_smi_settings(dev, &settings);

	caribou_smi_set_debug_mode(dev, false, false, false);

    // initialize streams (TODO: add error check)
    caribou_smi_init_stream(dev, caribou_smi_stream_type_write, caribou_smi_channel_900);
    caribou_smi_init_stream(dev, caribou_smi_stream_type_write, caribou_smi_channel_2400);
    caribou_smi_init_stream(dev, caribou_smi_stream_type_read, caribou_smi_channel_900);
    caribou_smi_init_stream(dev, caribou_smi_stream_type_read, caribou_smi_channel_2400);

    dev->error_cb = error_cb;
    dev->cb_context = context;
    dev->initialized = 1;

    return 0;
}


//=========================================================================
void caribou_smi_set_debug_mode(caribou_smi_st* dev, bool smi_Debug, bool fifo_push_debug, bool fifo_pull_debug)
{
	dev->smi_debug = smi_Debug;
	dev->fifo_push_debug = fifo_push_debug;
	dev->fifo_pull_debug = fifo_pull_debug;
}

//=========================================================================
int caribou_smi_close (caribou_smi_st* dev)
{
    close (dev->filedesc);
    return 0;
}

//=========================================================================
int caribou_smi_timeout_read(caribou_smi_st* dev, 
                            caribou_smi_address_en source, 
                            char* buffer, 
                            int size_of_buf, 
                            int timeout_num_millisec)
{
	//ZF_LOGI("Requesting timeout read for %d bytes", size_of_buf);
	
    // set the address
    if (source > 0 && CARIBOU_SMI_READ_ADDR(source))
    {
        if (source != dev->current_address)
        {
            int ret = ioctl(dev->filedesc, BCM2835_SMI_IOC_ADDRESS, source);
            if (ret != 0)
            {
                ZF_LOGE("failed setting smi address (idle / %d) to device", source);
                return -1;
            }
            //printf("Set address to %d\n", source);
            dev->current_address = source;
        }
    }
    else
    {
        ZF_LOGE("the specified address is not a read address (%d)", source);
        return -1;
    }

    fd_set set;
    struct timeval timeout = {0};
    int rv;
    FD_ZERO(&set);                  // clear the set mask
    FD_SET(dev->filedesc, &set);    // add our file descriptor to the set - and only it

    int num_sec = timeout_num_millisec / 1000;
    timeout.tv_sec = num_sec;
    timeout.tv_usec = (timeout_num_millisec - num_sec*1000) * 1000;
    //printf("tv_sec = %d, tv_usec = %d\n", timeout.tv_sec, timeout.tv_usec);

again:
    rv = select(dev->filedesc + 1, &set, NULL, NULL, &timeout);
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
        ZF_LOGD("smi fd timeout");
        return 0;
    }
    else if (FD_ISSET(dev->filedesc, &set))
    {
        return read(dev->filedesc, buffer, size_of_buf);
    }
    return -1;
}

//=========================================================================
uint8_t caribou_smi_lfsr(uint8_t n)
{
	uint8_t bit = ((n >> 2) ^ (n >> 3)) & 1;
	return (n >> 1) | (bit << 7);
}


double caribou_smi_analyze_data(uint8_t *buffer, 
							size_t length_bytes, 
							caribou_smi_sample_complex_int16* cmplx_vec, 
							caribou_smi_sample_meta* meta_vec,
							bool smi_debug_mode, bool pull_debug_mode, bool push_debug_mode)
{
	// analyze the speed
	static struct timeval prev = {0,0};
	struct timeval current;
	static double speed_bps_out = 128e6;

	gettimeofday(&current,NULL);
	double elapsed_us = (current.tv_sec - prev.tv_sec) + ((double)(current.tv_usec - prev.tv_usec)) / 1000000.0;
	double speed_mbps = (double)(length_bytes * 8) / elapsed_us;
	prev.tv_sec = current.tv_sec;
	prev.tv_usec = current.tv_usec;
	speed_bps_out = speed_bps_out * 0.98 + speed_mbps * 0.02;

	if (smi_debug_mode)
	{
		static int cnt = 0;
		static uint8_t last_correct_byte = 0;
		static uint32_t error_accum_counter = 0;
		uint32_t error_counter_current = 0;
		int first_error = -1;
		//int last_error = -1;
		static double error_rate = 0;

		//dump_hex(buffer, 12);
		for (size_t i = 0; i < length_bytes; i++)
		{
			if (buffer[i] != caribou_smi_lfsr(last_correct_byte) || buffer[i] == 0)
			{
				if (first_error == -1) first_error = i;
				//last_error = i;
				error_accum_counter ++;
				error_counter_current ++;
			}
			last_correct_byte = buffer[i];
		}
		error_rate = error_rate * 0.9 + (double)(error_counter_current) / (double)(length_bytes) * 0.1;

		if (cnt > 10)
		{
			printf("	[%02X, %02X, LAST:%02X] Received Debug Errors:  Curr. %d , Accum %d, FirstErr %d, BER: %.3g, BitRate[Mbps]: %.2f\n", 
						buffer[0], buffer[length_bytes-1], last_correct_byte,
						error_counter_current, error_accum_counter, first_error, error_rate,
						speed_bps_out / 1e6);
			cnt = 0;
		} else cnt++;

		memcpy((void*)cmplx_vec, buffer, length_bytes);
		return speed_bps_out;
	}

	if (pull_debug_mode || push_debug_mode)
	{
		//dump_hex(buffer, 64);
		uint32_t *values = (uint32_t*)buffer;
		uint32_t num_values = length_bytes / 4;
		static uint32_t error_accum_counter = 0;
		uint32_t error_counter_current = 0;
		int first_error = -1;

		for (size_t i = 0; i < num_values; i++)
		{
			if (values[i] != 0x01EFCDAB)
			{
				if (first_error == -1) first_error = i;
				error_accum_counter ++;
				error_counter_current ++;
			}
		}
		printf("	Received Debug Errors:  Curr. %d , Accum %d, First %d, BitRate[Mbps]: %.2f\n", 
						error_counter_current, error_accum_counter, first_error, speed_bps_out / 1e6);

		return speed_bps_out;
	}

	// the verilog struct looks as follows:
	//	[31:30]	[	29:17	] 	[ 16  ] 	[ 15:14 ] 	[	13:1	] 	[ 	0	]
	//	[ '00']	[ I sample	]	[ '0' ] 	[  '01'	]	[  Q sample	]	[  '0'	]

	// check data offset
	unsigned int offs = 0;
	//dump_bin(buffer, 16);
	for (offs = 0; offs<8; offs++)
	{
		uint32_t s = __builtin_bswap32(*((uint32_t*)(&buffer[offs])));
		//print_bin(s);
		if ((s & 0xC001C000) == 0x80004000) break;
		//if ((s & 0xC000C000) == 0x80004000) break;
	}
	
	if (offs > 0) 
	{
		length_bytes -= (offs/4 + 1) * 4;
	}
	//printf("data offset %d\n", offs);
	uint32_t *samples = (uint32_t*)(buffer + offs);

	for (unsigned int i = 0; i < length_bytes/4; i++)
	{
		uint32_t s = __builtin_bswap32(samples[i]);

		meta_vec[i].sync = s & 0x00000001;
		/*if (meta_vec[i].sync2)
		{
			printf("s");
		}*/
		s >>= 1;
		cmplx_vec[i].q = s & 0x00001FFF; s >>= 13;
		s >>= 3;
		cmplx_vec[i].i = s & 0x00001FFF; s >>= 13;
		
		if (cmplx_vec[i].i >= (int16_t)0x1000) cmplx_vec[i].i -= (int16_t)0x2000;
        if (cmplx_vec[i].q >= (int16_t)0x1000) cmplx_vec[i].q -= (int16_t)0x2000;
	}
	return speed_bps_out;
}

//=========================================================================
void* caribou_smi_analyze_thread(void* arg)
{
	//static int a = 0;
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
void* caribou_smi_thread(void *arg)
{
	TIMING_PERF_SYNC_VARS;

    pthread_t tid = pthread_self();
    caribou_smi_stream_st* st = (caribou_smi_stream_st*)arg;
    caribou_smi_st* dev = (caribou_smi_st*)st->parent_dev;
    caribou_smi_channel_en ch = (caribou_smi_channel_en)(st->stream_id & 0x1);

    ZF_LOGD("Entered thread id %lu, running = %d, Perf-Verbosity = %d", tid, st->running, TIMING_PERF_SYNC);
    set_realtime_priority(0);

    // create the analysis thread and mutexes
    if (pthread_mutex_init(&st->read_analysis_lock, NULL) != 0)
    {
        ZF_LOGE("read_analysis_lock mutex creation failed");
        st->active = 0;
        st->running = 0;
        return NULL;
    }
    pthread_mutex_lock(&st->read_analysis_lock);
    st->read_analysis_thread_running = 1;
    
    int ret = pthread_create(&st->read_analysis_thread, NULL, &caribou_smi_analyze_thread, st);
    if (ret != 0)
    {
        ZF_LOGE("read analysis stream thread creation failed");
        st->active = 0;
        st->running = 0;
        return NULL;
    }
    st->active = 1;

    // start thread notification
    if (st->data_cb != NULL) st->data_cb(dev->cb_context, 
										st->service_context,
										caribou_smi_stream_type_read,
                                        caribou_smi_event_type_start, 
										ch, 
										0, NULL, NULL, 0);

    // ****************************************
	//  MAIN LOOP
    // ****************************************	
    while (st->active)
    {
        if (!st->running)
        {
            usleep(1000);
            continue;
        }

		//TIMING_PERF_SYNC_TICK;

        int ret = caribou_smi_timeout_read(dev, st->addr, (char*)st->current_smi_buffer, st->batch_length, 200);
        if (ret < 0)
        {
            ZF_LOGE("caribou_smi_timeout_read failed");
            if (dev->error_cb) dev->error_cb(dev->cb_context, st->stream_id & 0x1, caribou_smi_error_read_failed);
            break;
        }
        else if (ret == 0)  // timeout
        {
            ZF_LOGW("caribou_smi_timeout");
            continue;
        }

        if ((int)(st->batch_length) > ret)
        {
            ZF_LOGW("partial read %d", ret);
        }

        st->read_ret_value = ret;
        st->current_app_buffer = st->current_smi_buffer;   
        pthread_mutex_unlock(&st->read_analysis_lock);      

        st->current_smi_buffer_index ++;
        if (st->current_smi_buffer_index >= (int)(st->num_of_buffers)) st->current_smi_buffer_index = 0;
        st->current_smi_buffer = st->buffers[st->current_smi_buffer_index];

		//TIMING_PERF_SYNC_TOCK;
    }

    st->read_analysis_thread_running = 0;
    pthread_mutex_unlock(&st->read_analysis_lock);  
    pthread_join(st->read_analysis_thread, NULL);   // check if cancel is needed
    pthread_mutex_destroy(&st->read_analysis_lock);

    // exit thread notification
    if (st->data_cb != NULL) st->data_cb(dev->cb_context, st->service_context,
										caribou_smi_stream_type_read, 
                                        caribou_smi_event_type_end, 
										(caribou_smi_channel_en)(st->stream_id>>1),
                                        0, NULL, NULL, 0);

    ZF_LOGD("Leaving thread id %lu", tid);
    return NULL;
}

//=========================================================================
static int caribou_smi_set_driver_streaming_state(caribou_smi_st* dev, int state)
{
	int ret = ioctl(dev->filedesc, SMI_STREAM_IOC_SET_STREAM_STATUS, state);
	if (ret != 0)
	{
		ZF_LOGE("failed setting smi stream state (%d)", state);
		return -1;
	}
	return 0;
}

//=========================================================================
int caribou_smi_setup_stream(caribou_smi_st* dev,
                                caribou_smi_stream_type_en type,
                                caribou_smi_channel_en channel,
                                caribou_smi_data_callback cb,
                                void* serviced_context)
{
    int stream_id = CARIBOU_SMI_GET_STREAM_ID(type, channel);
	ZF_LOGI("Setting up stream channel (%d) of type (%d, %s)", channel, type, type==caribou_smi_stream_type_write?"WRITE":"READ");
    caribou_smi_stream_st* st = &dev->streams[stream_id];
    if (st->active)
    {
        ZF_LOGE("the requested read stream channel (%d) of type (%d) is already active", channel, type);
        return 1;
    }

	st->app_meta_vec = NULL;
	st->app_cmplx_vec = NULL;
    st->batch_length = dev->native_batch_length_bytes;
    st->num_of_buffers = 2;
    st->data_cb = cb;

	caribou_smi_set_driver_streaming_state(dev, 0);

    // allocate the buffer vector
    if (allocate_buffer_vec(&st->buffers, st->num_of_buffers, st->batch_length) != 0)
    {
        ZF_LOGE("read buffer-vector allocation failed");
        return -1;
    }

	// Allocate the complex vector and metadata vector
	st->app_cmplx_vec = 
		(caribou_smi_sample_complex_int16*)malloc(sizeof(caribou_smi_sample_complex_int16) * st->batch_length / 4);
	if (st->app_cmplx_vec == NULL)
	{
		ZF_LOGE("application complex buffer allocation failed");
		release_buffer_vec(st->buffers, st->num_of_buffers, st->batch_length);
        return -1;
	}

	st->app_meta_vec = 
				(caribou_smi_sample_meta*)malloc(sizeof(caribou_smi_sample_meta) * st->batch_length / 4);
	if (st->app_meta_vec == NULL)
	{
		ZF_LOGE("application meta-data buffer allocation failed");
		release_buffer_vec(st->buffers, st->num_of_buffers, st->batch_length);
		free(st->app_cmplx_vec);
        return -1;
	}

    st->current_smi_buffer_index = 0;
    st->current_smi_buffer = st->buffers[0];
    st->current_app_buffer = st->buffers[st->num_of_buffers-1];
    st->service_context = serviced_context;
    st->running = 0;

    // create the reading thread
    st->stream_id = stream_id;
    int ret = pthread_create(&st->stream_thread, NULL, &caribou_smi_thread, st);
    if (ret != 0)
    {
        ZF_LOGE("read stream thread creation failed");
        release_buffer_vec(st->buffers, st->num_of_buffers, st->batch_length);
		free(st->app_cmplx_vec);
		free(st->app_meta_vec);
        st->buffers = NULL;
        st->active = 0;
        st->running = 0;
        return -1;
    }

    while (!st->active) usleep(500);

    ZF_LOGI("successfully created read stream for channel %s", channel==caribou_smi_channel_900?"900MHz":"2400MHz");
    return stream_id;
}

//=========================================================================
int caribou_smi_read_stream_buffer_info(caribou_smi_st* dev, int id, size_t *batch_length_bytes, int* num_buffers)
{
	if (id >= CARIBOU_SMI_MAX_NUM_STREAMS)
    {
        ZF_LOGE("wrong parameter id = %d >= %d", id, CARIBOU_SMI_MAX_NUM_STREAMS);
        return -1;
    }
	if (dev->streams[id].active == 0)
    {
        ZF_LOGW("stream id = %d is not active", id);
    }

	if (batch_length_bytes) *batch_length_bytes = dev->streams[id].batch_length;
    if (num_buffers) *num_buffers = dev->streams[id].num_of_buffers;

	return 0;
}

//=========================================================================
int caribou_smi_run_pause_stream (caribou_smi_st* dev, int id, int run)
{
    ZF_LOGD("%s SMI stream %d", run?"RUNNING":"PAUSING", id);
    if (id >= CARIBOU_SMI_MAX_NUM_STREAMS)
    {
        ZF_LOGE("wrong parameter id = %d >= %d", id, CARIBOU_SMI_MAX_NUM_STREAMS);
        return -1;
    }
    if (dev->streams[id].active == 0)
    {
        ZF_LOGW("stream id = %d is not active", id);
        return 0;
    }

	caribou_smi_set_driver_streaming_state(dev, run);

    dev->streams[id].running = run;
    return 0;
}

//=========================================================================
int caribou_smi_destroy_stream(caribou_smi_st* dev, int id)
{
    ZF_LOGD("desroying SMI stream %d", id);
    if (id >= CARIBOU_SMI_MAX_NUM_STREAMS)
    {
        ZF_LOGE("wrong parameter id = %d >= %d", id, CARIBOU_SMI_MAX_NUM_STREAMS);
        return -1;
    }
    if (dev->streams[id].active == 0)
    {
        ZF_LOGW("stream id = %d is already not active", id);
        return 0;
    }

	caribou_smi_set_driver_streaming_state(dev, 0);

    dev->streams[id].running = 0;
    usleep(1000);

    ZF_LOGD("Joining thread");
    dev->streams[id].active = 0;

    struct timespec ts;
    int s;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;

    s = pthread_timedjoin_np(dev->streams[id].stream_thread, NULL, &ts);
    if (s != 0) 
    {
        ZF_LOGE("pthread timed_joid returned with error %d, timeout = %d", s, ETIMEDOUT);
        pthread_cancel(dev->streams[id].stream_thread);
        usleep(1000);
        ZF_LOGE("Killed with pthread_cancel");
    }

    release_buffer_vec(dev->streams[id].buffers, dev->streams[id].num_of_buffers, dev->streams[id].batch_length);
    free(dev->streams[id].app_cmplx_vec);
	free(dev->streams[id].app_meta_vec);

	dev->streams[id].app_cmplx_vec = NULL;
	dev->streams[id].app_meta_vec = NULL;
    dev->streams[id].buffers = NULL;
    dev->streams[id].current_smi_buffer = NULL;
    dev->streams[id].current_app_buffer = NULL;

    ZF_LOGD("sucessfully desroyed SMI stream %d", id);
    return 0;
}

//=========================================================================
static int caribou_smi_init_stream(caribou_smi_st* dev, caribou_smi_stream_type_en type, caribou_smi_channel_en ch)
{
    caribou_smi_address_en addr = ((type << 2) | (ch + 1)) << 1;
    caribou_smi_stream_st* st = &dev->streams[CARIBOU_SMI_GET_STREAM_ID(type, ch)];
    st->stream_id = CARIBOU_SMI_GET_STREAM_ID(type, ch);

    ZF_LOGD("initializing stream type: %s, ch: %s, addr: %d, stream_id: %d",
                    type==caribou_smi_stream_type_write?"write":"read", ch==caribou_smi_channel_900?"900MHz":"2400MHz", addr, st->stream_id);

    st->addr = addr;
    st->batch_length = dev->native_batch_length_bytes;
    st->num_of_buffers = 2;
    st->data_cb = NULL;
    st->service_context = NULL;

    st->buffers = NULL;
    st->current_smi_buffer_index = 0;
    st->current_smi_buffer = NULL;
    st->current_app_buffer = NULL;

    st->active = 0;
    st->running = 0;
    st->read_analysis_thread_running = 0;
    st->parent_dev = dev;
	return 0;
}

//=========================================================================
static void caribou_smi_print_smi_settings(caribou_smi_st* dev, struct smi_settings *settings)
{
    printf("SMI SETTINGS:\n");
    printf("    width: %d\n", settings->data_width);
    printf("    pack: %c\n", settings->pack_data ? 'Y' : 'N');
    printf("    read setup: %d, strobe: %d, hold: %d, pace: %d\n", settings->read_setup_time, settings->read_strobe_time, settings->read_hold_time, settings->read_pace_time);
    printf("    write setup: %d, strobe: %d, hold: %d, pace: %d\n", settings->write_setup_time, settings->write_strobe_time, settings->write_hold_time, settings->write_pace_time);
    printf("    dma enable: %c, passthru enable: %c\n", settings->dma_enable ? 'Y':'N', settings->dma_passthrough_enable ? 'Y':'N');
    printf("    dma threshold read: %d, write: %d\n", settings->dma_read_thresh, settings->dma_write_thresh);
    printf("    dma panic threshold read: %d, write: %d\n", settings->dma_panic_read_thresh, settings->dma_panic_write_thresh);
	printf("	native kernel chunk size: %d bytes", dev->native_batch_length_bytes);
}

//=========================================================================
static void caribou_smi_setup_settings (caribou_smi_st* dev, struct smi_settings *settings)
{
    settings->read_setup_time = 0;
    settings->read_strobe_time = 5;
    settings->read_hold_time = 0;
    settings->read_pace_time = 0;
    settings->write_setup_time = 0;
    settings->write_hold_time = 0;
    settings->write_pace_time = 0;
    settings->write_strobe_time = 4;
    settings->data_width = SMI_WIDTH_8BIT;
    settings->dma_enable = 1;
    settings->pack_data = 1;
    settings->dma_passthrough_enable = 1;
}
