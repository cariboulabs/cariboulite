#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOU_SMI_Main"

#define _GNU_SOURCE

#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <aio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include "zf_log/zf_log.h"
#include "caribou_smi.h"

#ifdef __cplusplus
extern "C" {
#endif
    #include "bcm2835_smi.h"
#ifdef __cplusplus
}
#endif

#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sched.h>
#include <pthread.h>


static char *error_strings[] = CARIBOU_SMI_ERROR_STRS;

static void caribou_smi_print_smi_settings(struct smi_settings *settings);
static void caribou_smi_setup_settings (struct smi_settings *settings);
static void caribou_smi_init_stream(caribou_smi_st* dev, caribou_smi_stream_type_en type, caribou_smi_channel_en ch);

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

    int fd = open(smi_file, O_RDWR);
    if (fd < 0)
    {
        ZF_LOGE("can't open smi driver file '%s'", smi_file);
        return -1;
    }

    dev->filedesc = fd;

    // Get the current settings
    int ret = ioctl(fd, BCM2835_SMI_IOC_GET_SETTINGS, &settings);
    if (ret != 0)
    {
        ZF_LOGE("failed reading ioctl from smi fd (settings)");
        close (fd);
        return -1;
    }

    // apply the new settings
    caribou_smi_setup_settings(&settings);
    ret = ioctl(fd, BCM2835_SMI_IOC_WRITE_SETTINGS, &settings);
    if (ret != 0)
    {
        ZF_LOGE("failed writing ioctl to the smi fd (settings)");
        close (fd);
        return -1;
    }

    //ZF_LOGD("Current SMI Settings:");
    //caribou_smi_print_smi_settings(&settings);

    // set the address to idle
    ret = ioctl(fd, BCM2835_SMI_IOC_ADDRESS, caribou_smi_address_idle);
    if (ret != 0)
    {
        ZF_LOGE("failed setting smi address (idle / %d) to device", caribou_smi_address_idle);
        close (fd);
        return -1;
    }
    dev->current_address = caribou_smi_address_idle;

    // initialize streams
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
int caribou_smi_close (caribou_smi_st* dev)
{
    //ZF_LOGI("closing caribou_smi");
    close (dev->filedesc);
    return 0;
}

//=========================================================================
int caribou_smi_read_async(
                            caribou_smi_st* dev, 
                            caribou_smi_address_en source, 
                            char* buffer, 
                            int size_of_buf,
                            struct aiocb *read_aiocb)
{
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
            printf("Set address to %d\n", source);
            dev->current_address = source;
        }
    }
    else
    {
        ZF_LOGE("the specified address is not a read address (%d)", source);
        return -1;
    }

    bzero((char *)read_aiocb, sizeof(struct aiocb));
    read_aiocb->aio_buf = buffer;
    read_aiocb->aio_fildes = dev->filedesc;
    read_aiocb->aio_nbytes = size_of_buf;
    read_aiocb->aio_offset = 0;
    int ret = aio_read(read_aiocb);
    if (ret < 0)
    {
        printf("aio_read failed!!\n");
        return -1;
    }
    return 0;
}

//=========================================================================
int caribou_smi_timeout_read(caribou_smi_st* dev, 
                            caribou_smi_address_en source, 
                            char* buffer, 
                            int size_of_buf, 
                            int timeout_num_millisec)
{
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
            printf("Set address to %d\n", source);
            dev->current_address = source;
        }
    }
    else
    {
        ZF_LOGE("the specified address is not a read address (%d)", source);
        return -1;
    }

    /*
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
    {*/
        return read(dev->filedesc, buffer, size_of_buf);
    /*}
    return -1;*/
}

//=========================================================================
static int allocate_buffer_vec(uint8_t*** mat, int num_buffers, int buffer_size)
{
    ZF_LOGI("Allocating buffer vectors");
    (*mat) = (uint8_t**) malloc ( num_buffers * sizeof(uint8_t*) );
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
static void release_buffer_vec(uint8_t** mat, int num_buffers, int buffer_size)
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
static void set_realtime_priority(int priority_deter)
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
void* caribou_smi_analyze_thread(void* arg)
{
    pthread_t tid = pthread_self();

    struct timeval tv_pre = {0};
    struct timeval tv_post = {0};
    long long total_samples = 0;
    double time_pre = 0, batch_time = 0, sample_rate = 0;
    double time_post = 0, process_time = 0;
    double temp_pre;
    double num_samples = 0, num_samples_avg = 0;
    
    caribou_smi_stream_st* st = (caribou_smi_stream_st*)arg;
    caribou_smi_st* dev = (caribou_smi_st*)st->parent_dev;
    caribou_smi_stream_type_en type = (caribou_smi_stream_type_en)(st->stream_id>>1 & 0x1);
    caribou_smi_channel_en ch = (caribou_smi_channel_en)(st->stream_id & 0x1);

    ZF_LOGD("Entered SMI analysis thread id %lu, running = %d", tid, st->read_analysis_thread_running);
    set_realtime_priority(2);

    while (st->read_analysis_thread_running)
    {
        pthread_mutex_lock(&st->read_analysis_lock);
        gettimeofday(&tv_pre, NULL);

        if (!st->read_analysis_thread_running) break;

        if (st->data_cb) st->data_cb(dev->cb_context,
                                    st->service_context,
                                    type,
                                    ch,
                                    st->read_ret_value,
                                    st->current_app_buffer,
                                    st->batch_length);
        
        gettimeofday(&tv_post, NULL);

        // benchmarking
        num_samples = (double)(st->read_ret_value) / 4.0;
        num_samples_avg = num_samples_avg*0.1 + num_samples*0.9;
        temp_pre = tv_pre.tv_sec + ((double)(tv_pre.tv_usec)) / 1e6;
        time_post = tv_post.tv_sec + ((double)(tv_post.tv_usec)) / 1e6;

        batch_time = temp_pre - time_pre;
        sample_rate = sample_rate*0.1 + (num_samples / batch_time) * 0.9;
        process_time = process_time*0.1 + (time_post - temp_pre)*0.9;

        time_pre = temp_pre;
        total_samples += st->read_ret_value;
        if (total_samples % (4*4000000) == 0)
        {
            printf("sample_rate = %.2f SPS, process_time = %.2f usec, num_samples_avg = %.1f\n", sample_rate, process_time * 1e6, num_samples_avg);
        }
    }

    ZF_LOGD("Exitting SMI analysis thread id %lu, running = %d", tid, st->read_analysis_thread_running);
    return NULL;
}

//=========================================================================
/*void* caribou_smi_thread(void *arg)
{
    pthread_t tid = pthread_self();
    caribou_smi_stream_st* st = (caribou_smi_stream_st*)arg;
    caribou_smi_st* dev = (caribou_smi_st*)st->parent_dev;
    caribou_smi_stream_type_en type = (caribou_smi_stream_type_en)(st->stream_id>>1 & 0x1);
    caribou_smi_channel_en ch = (caribou_smi_channel_en)(st->stream_id & 0x1);

    ZF_LOGD("Entered thread id %lu, running = %d", tid, st->running);
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
    if (st->data_cb != NULL) st->data_cb(dev->cb_context, st->service_context, 
                                        caribou_smi_stream_start,
                                        ch,
                                        0,
                                        st->current_app_buffer,
                                        st->batch_length);

    // thread main loop
    while (st->active)
    {
        if (!st->running)
        {
            usleep(1000);
            continue;
        }

        int ret = caribou_smi_timeout_read(dev, st->addr, (char*)st->current_smi_buffer, st->batch_length, 200);
        if (ret < 0)
        {
            ZF_LOGE("caribou_smi_timeout_read failed");
            if (dev->error_cb) dev->error_cb(dev->cb_context, st->stream_id & 0x1, caribou_smi_error_read_failed);
            break;
        }
        else if (ret == 0)  // timeout
        {
            printf("caribou_smi_timeout\n");
            continue;
        }

        if (st->batch_length > ret)
        {
            printf("partial read %d\n", ret);
        }

        st->read_ret_value = ret;
        st->current_app_buffer = st->current_smi_buffer;   
        pthread_mutex_unlock(&st->read_analysis_lock);      

        st->current_smi_buffer_index ++;
        if (st->current_smi_buffer_index >= (int)(st->num_of_buffers)) st->current_smi_buffer_index = 0;
        st->current_smi_buffer = st->buffers[st->current_smi_buffer_index];
    }

    st->read_analysis_thread_running = 0;
    pthread_mutex_unlock(&st->read_analysis_lock);  
    pthread_join(st->read_analysis_thread, NULL);   // check if cancel is needed
    pthread_mutex_destroy(&st->read_analysis_lock);

    // exit thread notification
    if (st->data_cb != NULL) st->data_cb(dev->cb_context,
                                        st->service_context,
                                        caribou_smi_stream_end,
                                        (caribou_smi_channel_en)(st->stream_id>>1),
                                        0,
                                        st->current_app_buffer,
                                        st->batch_length);

    ZF_LOGD("Exiting thread id %lu", tid);
    return NULL;
}*/

//=========================================================================
void* caribou_smi_thread_async(void *arg)
{
    struct timeval tv_pre = {0};
    struct timeval tv_post = {0};
    long long total_samples = 0;
    double time_pre = 0, batch_time = 0, sample_rate = 0;
    double time_post = 0, process_time = 0;
    double temp_pre;
    double num_samples = 0, num_samples_avg = 0;

    pthread_t tid = pthread_self();
    struct aiocb read_aiocb = {0};
    caribou_smi_stream_st* st = (caribou_smi_stream_st*)arg;
    caribou_smi_st* dev = (caribou_smi_st*)st->parent_dev;
    caribou_smi_stream_type_en type = (caribou_smi_stream_type_en)(st->stream_id>>1 & 0x1);
    caribou_smi_channel_en ch = (caribou_smi_channel_en)(st->stream_id & 0x1);

    ZF_LOGD("Entered thread id %lu, running = %d", tid, st->running);
    set_realtime_priority(0);

    st->active = 1;

    // start thread notification
    if (st->data_cb != NULL) st->data_cb(dev->cb_context, st->service_context, 
                                        caribou_smi_stream_start,
                                        ch,
                                        0,
                                        st->current_app_buffer,
                                        st->batch_length);

    // thread main loop
    while (st->active)
    {
        if (!st->running)
        {
            usleep(1000);
            continue;
        }
        gettimeofday(&tv_pre, NULL);

        // shoot the async read
        int ret = caribou_smi_read_async(dev, st->addr,
                            (char*)st->current_smi_buffer, 
                            st->batch_length,
                            &read_aiocb);

        if (ret < 0)
        {
            ZF_LOGE("async read failed");
            if (dev->error_cb) dev->error_cb(dev->cb_context, st->stream_id & 0x1, caribou_smi_error_read_failed);
            break;
        }

        // analyze the last buffer that was accepted
        if (st->current_app_buffer)
        {
            if (st->data_cb) st->data_cb(dev->cb_context,
                                    st->service_context,
                                    type,
                                    ch,
                                    st->read_ret_value,
                                    st->current_app_buffer,
                                    st->batch_length);
        }

        gettimeofday(&tv_post, NULL);

        // benchmarking
        num_samples = (double)(st->read_ret_value) / 4.0;
        num_samples_avg = num_samples_avg*0.1 + num_samples*0.9;
        temp_pre = tv_pre.tv_sec + ((double)(tv_pre.tv_usec)) / 1e6;
        time_post = tv_post.tv_sec + ((double)(tv_post.tv_usec)) / 1e6;

        batch_time = temp_pre - time_pre;
        sample_rate = sample_rate*0.1 + (num_samples / batch_time) * 0.9;
        process_time = process_time*0.1 + (time_post - temp_pre)*0.9;

        time_pre = temp_pre;
        total_samples += st->read_ret_value;
        if (total_samples % (4*4000000) == 0)
        {
            printf("sample_rate = %.2f SPS, process_time = %.2f usec, num_samples_avg = %.1f\n", sample_rate, process_time * 1e6, num_samples_avg);
        }

        // wait for the async to complete
        struct aiocb* read_aiocb_vec[1];
        read_aiocb_vec[0] = &read_aiocb;
        ret = aio_suspend(read_aiocb_vec, 1, NULL);
        if (ret >= 0)
        {
            st->read_ret_value = st->batch_length;
            st->current_app_buffer = st->current_smi_buffer;  
        }

        st->current_smi_buffer_index ++;
        if (st->current_smi_buffer_index >= (int)(st->num_of_buffers)) st->current_smi_buffer_index = 0;
        st->current_smi_buffer = st->buffers[st->current_smi_buffer_index];
    }

    // exit thread notification
    if (st->data_cb != NULL) st->data_cb(dev->cb_context,
                                        st->service_context,
                                        caribou_smi_stream_end,
                                        (caribou_smi_channel_en)(st->stream_id>>1),
                                        0,
                                        st->current_app_buffer,
                                        st->batch_length);

    ZF_LOGD("Exiting thread id %lu", tid);
    return NULL;
}

//=========================================================================
int caribou_smi_setup_stream(caribou_smi_st* dev,
                                caribou_smi_stream_type_en type,
                                caribou_smi_channel_en channel,
                                int batch_length, 
                                int num_buffers,
                                caribou_smi_data_callback cb,
                                void* serviced_context)
{
    int stream_id = CARIBOU_SMI_GET_STREAM_ID(type, channel);
    caribou_smi_stream_st* st = &dev->streams[stream_id];
    if (st->active)
    {
        ZF_LOGE("the requested read stream channel (%d) of type (%d) is already active", channel, type);
        return -1;
    }

    st->batch_length = batch_length;
    st->num_of_buffers = num_buffers;
    st->data_cb = cb;

    // allocate the buffer vector
    if (allocate_buffer_vec(&st->buffers, st->num_of_buffers, st->batch_length) != 0)
    {
        ZF_LOGE("read buffer-vector allocation failed");
        return -1;
    }

    st->current_smi_buffer_index = 0;
    st->current_smi_buffer = st->buffers[0];
    st->current_app_buffer = NULL;
    st->service_context = serviced_context;
    st->running = 0;

    // create the reading thread
    st->stream_id = stream_id;
    int ret = pthread_create(&st->stream_thread, NULL, &caribou_smi_thread_async, st);
    if (ret != 0)
    {
        ZF_LOGE("read stream thread creation failed");
        release_buffer_vec(st->buffers, st->num_of_buffers, st->batch_length);
        st->buffers = NULL;
        st->active = 0;
        st->running = 0;
        return -1;
    }

    while (!st->active) usleep(1000);

    ZF_LOGI("successfully created read stream for channel %s", channel==caribou_smi_channel_900?"900MHz":"2400MHz");
    return stream_id;
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

    //pthread_join(dev->streams[id].stream_thread, NULL);

    release_buffer_vec(dev->streams[id].buffers, dev->streams[id].num_of_buffers, dev->streams[id].batch_length);
    
    dev->streams[id].buffers = NULL;
    dev->streams[id].current_smi_buffer = NULL;
    dev->streams[id].current_app_buffer = NULL;

    ZF_LOGD("sucessfully desroyed SMI stream %d", id);
    return 0;
}

//=========================================================================
static void caribou_smi_init_stream(caribou_smi_st* dev, caribou_smi_stream_type_en type, caribou_smi_channel_en ch)
{
    caribou_smi_address_en addr = ((type << 2) | (ch + 1)) << 1;
    caribou_smi_stream_st* st = &dev->streams[CARIBOU_SMI_GET_STREAM_ID(type, ch)];
    st->stream_id = CARIBOU_SMI_GET_STREAM_ID(type, ch);

    ZF_LOGD("initializing stream type: %s, ch: %s, addr: %d, stream_id: %d",
                    type==caribou_smi_stream_type_write?"write":"read", ch==caribou_smi_channel_900?"900MHz":"2400MHz", addr, st->stream_id);

    st->addr = addr;
    st->batch_length = 1024;
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
}


//=========================================================================
static void caribou_smi_print_smi_settings(struct smi_settings *settings)
{
    printf("SMI SETTINGS:\n");
    printf("    width: %d\n", settings->data_width);
    printf("    pack: %c\n", settings->pack_data ? 'Y' : 'N');
    printf("    read setup: %d, strobe: %d, hold: %d, pace: %d\n", settings->read_setup_time, settings->read_strobe_time, settings->read_hold_time, settings->read_pace_time);
    printf("    write setup: %d, strobe: %d, hold: %d, pace: %d\n", settings->write_setup_time, settings->write_strobe_time, settings->write_hold_time, settings->write_pace_time);
    printf("    dma enable: %c, passthru enable: %c\n", settings->dma_enable ? 'Y':'N', settings->dma_passthrough_enable ? 'Y':'N');
    printf("    dma threshold read: %d, write: %d\n", settings->dma_read_thresh, settings->dma_write_thresh);
    printf("    dma panic threshold read: %d, write: %d\n", settings->dma_panic_read_thresh, settings->dma_panic_write_thresh);
}

//=========================================================================
static void caribou_smi_setup_settings (struct smi_settings *settings)
{
    settings->read_setup_time = 0;
    settings->read_strobe_time = 5;
    settings->read_hold_time = 0;
    settings->read_pace_time = 0;
    settings->write_setup_time = 1;
    settings->write_hold_time = 1;
    settings->write_pace_time = 2;
    settings->write_strobe_time = 3;
    settings->data_width = SMI_WIDTH_8BIT;
    settings->dma_enable = 1;
    settings->pack_data = 1;
    settings->dma_passthrough_enable = 1;
}

//=========================================================================
void dump_hex(const void* data, size_t size)
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

