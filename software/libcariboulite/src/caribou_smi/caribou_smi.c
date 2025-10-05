#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOU_SMI"
#include "zf_log/zf_log.h"

// #ifndef _GNU_SOURCE
//     #define _GNU_SOURCE
// #endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/mman.h>   // for mlockall, MCL_CURRENT, MCL_FUTURE
#include <sched.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#include "caribou_smi.h"
#include "smi_utils.h"
#include "io_utils/io_utils.h"
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

#ifndef CARIBOU_SMI_BYTES_PER_SAMPLE
    #define CARIBOU_SMI_BYTES_PER_SAMPLE  (sizeof(caribou_smi_sample_complex_int16))
#endif


//=========================================================================
// int caribou_smi_set_driver_streaming_state(caribou_smi_st* dev, smi_stream_state_en state)
// {
//     int ret = ioctl(dev->filedesc, SMI_STREAM_IOC_SET_STREAM_STATUS, state);
//     if (ret != 0)
//     {
//         ZF_LOGE("failed setting smi stream state (%d)", state);
//         return -1;
//     }
//     dev->state = state;
//     return 0;
// }

/* --- TX leftover accumulator (shared between write/start/stop) --- */
static caribou_smi_sample_complex_int16 g_tx_accum[16384];
static size_t g_tx_accum_n = 0;

static inline void caribou_smi_tx_accum_reset(void)
{
    g_tx_accum_n = 0;
}

int caribou_smi_set_driver_streaming_state(caribou_smi_st* dev, smi_stream_state_en state)
{
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    static smi_stream_state_en last = smi_stream_idle;

    pthread_mutex_lock(&mtx);
    if (state == last) {
        pthread_mutex_unlock(&mtx);
        return 0;            // no-op: desired == current (cached)
    }

    int r = ioctl(dev->filedesc, SMI_STREAM_IOC_SET_STREAM_STATUS, state);
    if (r == 0) {
        last = state;        // update only on success
    }
    pthread_mutex_unlock(&mtx);
    return r;
}

//=========================================================================
smi_stream_state_en caribou_smi_get_driver_streaming_state(caribou_smi_st* dev)
{
    return dev->state;
}


//=========================================================================
/* --- helpers for “exact quarter” writes and priming ---*/  
static void caribou_smi_boost_sched(void)
{
#ifdef MCL_CURRENT
    // lock current+future pages in RAM
    (void)mlockall(MCL_CURRENT | MCL_FUTURE);
#endif

    // switch to SCHED_FIFO if permitted
    struct sched_param sp = { .sched_priority = 50 };
    (void)sched_setscheduler(0, SCHED_FIFO, &sp);
}

static inline size_t smi_quarter_bytes(const caribou_smi_st *dev)
{
    return (size_t)(dev->native_batch_len / 4);
}

/* Block until exactly 'len' bytes are written (handles EINTR/EAGAIN). */
static int write_all(int fd, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        ssize_t r = write(fd, buf + off, len - off);
        if (r > 0) { off += (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            (void)poll(&(struct pollfd){ .fd = fd, .events = POLLOUT }, 1, 1);
            continue;
        }
        return -1; // real error
    }
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
    printf("    native kernel chunk size: %ld bytes\n", dev->native_batch_len);
}

//=========================================================================
static int caribou_smi_get_smi_settings(caribou_smi_st *dev, struct smi_settings *settings, bool print)
{
    int ret = 0;

    ret = ioctl(dev->filedesc, BCM2835_SMI_IOC_GET_SETTINGS, settings);
    if (ret != 0)
    {
        ZF_LOGE("failed reading ioctl from smi fd (settings)");
        return -1;
    }

    ret = ioctl(dev->filedesc, SMI_STREAM_IOC_GET_NATIVE_BUF_SIZE, &dev->native_batch_len);
    if (ret != 0)
    {
        ZF_LOGE("failed reading native batch length, setting the default - this error is not fatal but we have wrong kernel drivers");
        dev->native_batch_len = (1024)*(1024)/2;
    }
    
    //printf("DEBUG: native batch len: %lu\n", dev->native_batch_len);

    if (print)
    {
        caribou_smi_print_smi_settings(dev, settings);
    }
    return ret;
}

//=========================================================================
static int caribou_smi_setup_settings (caribou_smi_st* dev, struct smi_settings *settings, bool print)
{
    settings->read_setup_time = 1;
    settings->read_strobe_time = 3; // orginal value: 5
    settings->read_hold_time = 1;
    settings->read_pace_time = 0;

    settings->write_setup_time = 1;
    settings->write_strobe_time = 3; // orginal value: 5
    settings->write_hold_time = 1;
    settings->write_pace_time = 0;

	// 8 bit on each transmission (4 TRX per sample)
    settings->data_width = SMI_WIDTH_8BIT;
	
	// Enable DMA
    settings->dma_enable = 1;
	
	// Whether or not to pack multiple SMI transfers into a single 32 bit FIFO word
    settings->pack_data = 1;
	
	// External DREQs enabled
    settings->dma_passthrough_enable = 1; // orginal: 1
	
    // RX DREQ Threshold Level. 
    // A RX DREQ will be generated when the RX FIFO exceeds this threshold level. 
    // This will instruct an external AXI RX DMA to read the RX FIFO. 
    // If the DMA is set to perform burst reads, the threshold must ensure that there is 
    // sufficient data in the FIFO to satisfy the burst
    // Instruction: Lower is faster response
    settings->dma_read_thresh = 1; // 0 - 511 range (9 bit)
    
    // TX DREQ Threshold Level. 
    // A TX DREQ will be generated when the TX FIFO drops below this threshold level. 
    // This will instruct an external AXI TX DMA to write more data to the TX FIFO.
    // Instruction: Higher is faster response
    settings->dma_write_thresh = 254; //381; // orginal: 254
    
    // RX Panic Threshold level.
    // A RX Panic will be generated when the RX FIFO exceeds this threshold level. 
    // This will instruct the AXI RX DMA to increase the priority of its bus requests.
    // Instruction: Lower is more aggressive
    settings->dma_panic_read_thresh = 16;
    
    // TX Panic threshold level.
    // A TX Panic will be generated when the TX FIFO drops below this threshold level. 
    // This will instruct the AXI TX DMA to increase the priority of its bus requests.
    // Instruction: Higher is more aggresive
    settings->dma_panic_write_thresh = 495; // 397; // orginal: 224

    if (print)
    {
        caribou_smi_print_smi_settings(dev, settings);
    }

    if (ioctl(dev->filedesc, BCM2835_SMI_IOC_WRITE_SETTINGS, settings) != 0)
    {
        ZF_LOGE("failed writing ioctl to the smi fd (settings)");
        return -1;
    }
    
    // set the address line parameters
    int address_dir_offset = 2;
    if (ioctl(dev->filedesc, SMI_STREAM_IOC_SET_ADDR_DIR_OFFSET, address_dir_offset) != 0)
    {
        ZF_LOGE("failed writing ioctl to the smi fd (address_dir_offset)");
        return -1;
    }
    
    // set the address line parameters
    int address_channel_offset = 3;
    if (ioctl(dev->filedesc, SMI_STREAM_IOC_SET_ADDR_CH_OFFSET, address_channel_offset) != 0)
    {
        ZF_LOGE("failed writing ioctl to the smi fd (address_channel_offset)");
        return -1;
    }
    
    return 0;
}

//=========================================================================
static void caribou_smi_anayze_smi_debug(caribou_smi_st* dev, uint8_t *data, size_t len)
{
    uint32_t error_counter_current = 0;
    int first_error = -1;
    uint32_t *values = (uint32_t*)data;

    //smi_utils_dump_hex(buffer, 12);

    if (dev->debug_mode == caribou_smi_lfsr)
    {
        for (size_t i = 0; i < len; i++)
        {
            if (data[i] != smi_utils_lfsr(dev->debug_data.last_correct_byte) || data[i] == 0)
            {
                if (first_error == -1) first_error = i;

                dev->debug_data.error_accum_counter ++;
                error_counter_current ++;
            }
            dev->debug_data.last_correct_byte = data[i];
        }
    }

    else if (dev->debug_mode == caribou_smi_push || dev->debug_mode == caribou_smi_pull)
    {
        for (size_t i = 0; i < len / 4; i++)
        {
            if (values[i] != CARIBOU_SMI_DEBUG_WORD)
            {
                if (first_error == -1) first_error = i * 4;

                dev->debug_data.error_accum_counter += 4;
                error_counter_current += 4;
            }
        }
    }

    dev->debug_data.cur_err_cnt = error_counter_current;
    dev->debug_data.bitrate = smi_calculate_performance(len, &dev->debug_data.last_time, dev->debug_data.bitrate);

    dev->debug_data.error_rate = dev->debug_data.error_rate * 0.9 + (double)(error_counter_current) / (double)(len) * 0.1;
    if (dev->debug_data.error_rate < 1e-8)
        dev->debug_data.error_rate = 0.0;
}

//=========================================================================
static void caribou_smi_print_debug_stats(caribou_smi_st* dev, uint8_t *buffer, size_t len)
{
    static unsigned int count = 0;

    count ++;
    if (count % 10 == 0)
    {
        printf("SMI DBG: ErrAccumCnt: %d, LastErrCnt: %d, ErrorRate: %.4g, bitrate: %.2f Mbps\n",
                dev->debug_data.error_accum_counter,
                dev->debug_data.cur_err_cnt,
                dev->debug_data.error_rate,
                dev->debug_data.bitrate);
    }
    //smi_utils_dump_hex(buffer, 16);
}

//=========================================================================
static int caribou_smi_find_buffer_offset(caribou_smi_st* dev, uint8_t *buffer, size_t len)
{
    size_t offs = 0;
    bool found = false;

    if (len <= (CARIBOU_SMI_BYTES_PER_SAMPLE*4))
    {
        return 0;
    }
    //smi_utils_dump_hex(buffer, 16);

    if (dev->debug_mode == caribou_smi_none)
    {
        for (offs = 0; offs<(len-(CARIBOU_SMI_BYTES_PER_SAMPLE*4)); offs++)
        {
            uint32_t s1 = *((uint32_t*)(&buffer[offs]));
            uint32_t s2 = *((uint32_t*)(&buffer[offs+4]));
            uint32_t s3 = *((uint32_t*)(&buffer[offs+8]));
            uint32_t s4 = *((uint32_t*)(&buffer[offs+12]));
			
            //printf("%d => %08X\n", offs, s);
            if ((s1 & 0xC001C000) == 0x80004000 &&
                (s2 & 0xC001C000) == 0x80004000 &&
                (s3 & 0xC001C000) == 0x80004000 &&
                (s4 & 0xC001C000) == 0x80004000)
            {
                found = true;
                break;
            }
        }
    }
    else if (dev->debug_mode == caribou_smi_push || dev->debug_mode == caribou_smi_pull)
    {
        for (offs = 0; offs<(len-CARIBOU_SMI_BYTES_PER_SAMPLE); offs++)
        {
            uint32_t s = /*__builtin_bswap32*/(*((uint32_t*)(&buffer[offs])));
            //printf("%d => %08X, %08X\n", offs, s, caribou_smi_count_bit(s^CARIBOU_SMI_DEBUG_WORD));
            if (smi_utils_count_bit(s^CARIBOU_SMI_DEBUG_WORD) < 4)
            {
                found = true;
                break;
            }
        }
    }
    else
    {
        // the lfsr option
        return 0;
    }

    if (found == false)
    {
        smi_utils_dump_hex(buffer, 16);
        return -1;
    }

    return (int)offs;
}

//=========================================================================
static int caribou_smi_rx_data_analyze(caribou_smi_st* dev,
                                caribou_smi_channel_en channel,
                                uint8_t* data, size_t data_length,
                                caribou_smi_sample_complex_int16* samples_out,
                                caribou_smi_sample_meta* meta_offset)
{
    int offs = 0;
    size_t actual_length = data_length;                 // in bytes
    int size_shortening_samples = 0;                    // in samples
    uint32_t *actual_samples = (uint32_t*)(data);

    caribou_smi_sample_complex_int16* cmplx_vec = samples_out;

    // find the offset and adjust
    offs = caribou_smi_find_buffer_offset(dev, data, data_length);
    if (offs > 0)
    {
        //printf("OFFSET = %d\n", offs);
    }
    if (offs < 0)
    {
        return -1;
    }

    // adjust the lengths accroding to the sample mismatch
    // this may be accompanied by a few samples losses (sphoradic OS
    // scheduling) thus trying to stitch buffers one to another may
    // be not effective. The single sample is interpolated
    size_shortening_samples = (offs > 0) ? (offs / CARIBOU_SMI_BYTES_PER_SAMPLE + 1) : 0;
    actual_length -= size_shortening_samples * CARIBOU_SMI_BYTES_PER_SAMPLE;
    actual_samples = (uint32_t*)(data + offs);

    // analyze the data
    if (dev->debug_mode != caribou_smi_none)
    {
        caribou_smi_anayze_smi_debug(dev, (uint8_t*)actual_samples, actual_length);
    }
    else
    {
        unsigned int i = 0;
        // Print buffer
        //smi_utils_dump_bin(buffer, 16);

        // Data Structure:
        //  [31:30] [   29:17   ]   [ 16  ]     [ 15:14 ]   [   13:1    ]   [   0   ]
        //  [ '10'] [ I sample  ]   [ '0' ]     [  '01' ]   [  Q sample ]   [  'S'  ]

        if (channel != caribou_smi_channel_2400)
        {   /* S1G */
            for (i = 0; i < actual_length / CARIBOU_SMI_BYTES_PER_SAMPLE; i++)
            {
                uint32_t s = /*__builtin_bswap32*/(actual_samples[i]);

                if (meta_offset) meta_offset[i].sync = s & 0x00000001;
                if (cmplx_vec)
                {
                    s >>= 1;
	                cmplx_vec[i].q = s & 0x00001FFF; s >>= 13;
	                s >>= 3;
	                cmplx_vec[i].i = s & 0x00001FFF; s >>= 13;
					
					if (cmplx_vec[i].i >= (int16_t)0x1000) cmplx_vec[i].i -= (int16_t)0x2000;
                	if (cmplx_vec[i].q >= (int16_t)0x1000) cmplx_vec[i].q -= (int16_t)0x2000;
                }
            }
        }
        else
        {   /* HiF */
            for (i = 0; i < actual_length / CARIBOU_SMI_BYTES_PER_SAMPLE; i++)
            {
                uint32_t s = /*__builtin_bswap32*/(actual_samples[i]);

                if (meta_offset) meta_offset[i].sync = s & 0x00000001;
                if (cmplx_vec)
                {   
				 	s >>= 1;
	                cmplx_vec[i].i = s & 0x00001FFF; s >>= 13;
	                s >>= 3;
	                cmplx_vec[i].q = s & 0x00001FFF; s >>= 13;
					
					if (cmplx_vec[i].i >= (int16_t)0x1000) cmplx_vec[i].i -= (int16_t)0x2000;
                	if (cmplx_vec[i].q >= (int16_t)0x1000) cmplx_vec[i].q -= (int16_t)0x2000;
                }
            }
        }

        // last sample interpolation (linear for I and Q or preserve)
        if (size_shortening_samples > 0)
        {
            //cmplx_vec[i].i = 2*cmplx_vec[i-1].i - cmplx_vec[i-2].i;
            //cmplx_vec[i].q = 2*cmplx_vec[i-1].q - cmplx_vec[i-2].q;

            cmplx_vec[i].i = 110*cmplx_vec[i-1].i/100 - cmplx_vec[i-2].i/10;
            cmplx_vec[i].q = 110*cmplx_vec[i-1].q/100 - cmplx_vec[i-2].q/10;
        }
    }

    return offs;
}

//=========================================================================
static int caribou_smi_poll(caribou_smi_st* dev, uint32_t timeout_num_millisec, smi_stream_direction_en dir)
{
    int ret = 0;
    struct pollfd fds;
    fds.fd = dev->filedesc;

    if (dir == smi_stream_dir_device_to_smi) fds.events = POLLIN;
    else if (dir == smi_stream_dir_smi_to_device) fds.events = POLLOUT;
    else return -1;

again:
    ret = poll(&fds, 1, timeout_num_millisec);
    if (ret == -1)
    {
        int error = errno;
        switch(error)
        {
            case EFAULT:
                ZF_LOGE("fds points outside the process's accessible address space");
                break;

            case EINTR:
            case EAGAIN:
                ZF_LOGD("SMI filedesc select error - caught an interrupting signal");
                goto again;
                break;

            case EINVAL:
                ZF_LOGE("The nfds value exceeds the RLIMIT_NOFILE value");
                break;

            case ENOMEM:
                ZF_LOGE("Unable to allocate memory for kernel data structures.");
                break;

            default: break;
        };
        return -1;
    }
    else if(ret == 0)
    {
        return 0;
    }

    return fds.revents & POLLIN || fds.revents & POLLOUT;
}

//==old=======================================================================
// static int caribou_smi_timeout_write(caribou_smi_st* dev,
//                             uint8_t* buffer,
//                             size_t len,
//                             uint32_t timeout_num_millisec)
// {
//     int res = caribou_smi_poll(dev, timeout_num_millisec, smi_stream_dir_smi_to_device);

//     if (res < 0)
//     {
//         ZF_LOGD("poll error");
//         return -1;
//     }
//     else if (res == 0)  // timeout
//     {
//         ZF_LOGD("===> smi write fd timeout");
//         return 0;
//     }

//     return write(dev->filedesc, buffer, len);
// }

//==new=======================================================================
static int caribou_smi_timeout_write(caribou_smi_st* dev,
                                     uint8_t* buffer,
                                     size_t len,
                                     uint32_t timeout_ms)
{
    uint8_t *p = buffer;
    size_t   left = len;
    int      wrote_total = 0;

    while (left > 0) {
        ssize_t r = write(dev->filedesc, p, left);
        if (r > 0) {
            p += r;
            left -= (size_t)r;
            wrote_total += (int)r;
            continue;
        }
        if (r < 0) {
            if (errno == EINTR) continue; // retry immediately
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // wait for POLLOUT briefly
                int ready = caribou_smi_poll(dev, timeout_ms, smi_stream_dir_smi_to_device);
                if (ready > 0) continue;          // try write again
                // timeout or poll error → return what we have so far
                return wrote_total;
            }
            // other error
            return (wrote_total > 0) ? wrote_total : -1;
        }

        // r == 0 should not happen for a blocking char device; treat as “not ready”
        int ready = caribou_smi_poll(dev, timeout_ms, smi_stream_dir_smi_to_device);
        if (ready <= 0) return wrote_total; // timeout or error: partial is fine
    }

    return wrote_total;
}

//=========================================================================
static int caribou_smi_timeout_read(caribou_smi_st* dev,
                                uint8_t* buffer,
                                size_t len,
                                uint32_t timeout_num_millisec)
{
    // try reading the file
    int ret = read(dev->filedesc, buffer, len);
    if (ret <= 0)
    {    
        int res = caribou_smi_poll(dev, timeout_num_millisec, smi_stream_dir_device_to_smi);

        if (res < 0)
        {
            ZF_LOGD("poll error");
            return -1;
        }
        else if (res == 0)  // timeout
        {
            //ZF_LOGD("===> smi read fd timeout");
            return 0;
        }

        return read(dev->filedesc, buffer, len);
    }
    
    return ret;
}

//=========================================================================
void caribou_smi_setup_ios(caribou_smi_st* dev)
{
	// setup the addresses
    io_utils_set_gpio_mode(2, io_utils_alt_1);  // addr
    io_utils_set_gpio_mode(3, io_utils_alt_1);  // addr
	
	// Setup the bus I/Os
	// --------------------------------------------
	for (int i = 6; i <= 15; i++)
	{
		io_utils_set_gpio_mode(i, io_utils_alt_1);  // 8xData + SWE + SOE
	}
	
	io_utils_set_gpio_mode(24, io_utils_alt_1); // rwreq
	io_utils_set_gpio_mode(25, io_utils_alt_1); // rwreq
}

//=========================================================================
int caribou_smi_init(caribou_smi_st* dev,
                    void* context)
{
    char smi_file[] = "/dev/smi";
    struct smi_settings settings = {0};
    dev->read_temp_buffer = NULL;
    dev->write_temp_buffer = NULL;

    ZF_LOGD("initializing caribou_smi");

    // start from a defined state
    memset(dev, 0, sizeof(caribou_smi_st));

    // checking the loaded modules
    // --------------------------------------------
    /*if (caribou_smi_check_modules(true) < 0)
    {
        ZF_LOGE("Problem reloading SMI kernel modules");
        return -1;
    }*/

    // open the smi device file
    // --------------------------------------------
    int fd = open(smi_file, O_RDWR);
    if (fd < 0)
    {
        ZF_LOGE("couldn't open smi driver file '%s' (%s)", smi_file, strerror(errno));
        return -1;
    }
    dev->filedesc = fd;
    caribou_smi_boost_sched();   // optional: reduces micro-stalls

    // Setup the bus I/Os
    // --------------------------------------------
    caribou_smi_setup_ios(dev);

    // Retrieve the current settings and modify
    // --------------------------------------------
    if (caribou_smi_get_smi_settings(dev, &settings, false) != 0)
    {
        caribou_smi_close (dev);
        return -1;
    }

    if (caribou_smi_setup_settings(dev, &settings, false) != 0)
    {
        caribou_smi_close (dev);
        return -1;
    }

    // Initialize temporary buffers
    // we add additional bytes to allow data synchronization corrections
    dev->read_temp_buffer = malloc (dev->native_batch_len + 1024);
    dev->write_temp_buffer = malloc (dev->native_batch_len + 1024);

    if (dev->read_temp_buffer == NULL || dev->write_temp_buffer == NULL)
    {
        ZF_LOGE("smi temporary buffers allocation failed");
        caribou_smi_close (dev);
        return -1;
    }
    memset(&dev->debug_data, 0, sizeof(caribou_smi_debug_data_st));

    dev->debug_mode = caribou_smi_none;
    dev->invert_iq = false;
    dev->sample_rate = CARIBOU_SMI_SAMPLE_RATE;
    dev->initialized = 1;

    return 0;
}

//=========================================================================
int caribou_smi_close (caribou_smi_st* dev)
{
    // release temporary buffers
    if (dev->read_temp_buffer) free(dev->read_temp_buffer);
    if (dev->write_temp_buffer) free(dev->write_temp_buffer);

    // close smi device file
    return close (dev->filedesc);
}

//=========================================================================
void caribou_smi_set_sample_rate(caribou_smi_st* dev, uint32_t sample_rate)
{
    if (sample_rate < 100000)
    {
        dev->sample_rate = 100000;
    }
    else if (sample_rate > CARIBOU_SMI_SAMPLE_RATE)
    {
        dev->sample_rate = CARIBOU_SMI_SAMPLE_RATE;
    }
    else
    {
        dev->sample_rate = sample_rate;
    }
}

//=========================================================================
void caribou_smi_set_debug_mode(caribou_smi_st* dev, caribou_smi_debug_mode_en mode)
{
    dev->debug_mode = mode;
}

//=========================================================================
void caribou_smi_invert_iq(caribou_smi_st* dev, bool invert)
{
    dev->invert_iq = invert;
}

//=========================================================================
static int caribou_smi_calc_read_timeout(uint32_t sample_rate, size_t len)
{
    uint32_t to_millisec = (2 * len * 1000) / sample_rate;
    if (to_millisec < 1) to_millisec = 1;
    return to_millisec * 2;
}

//=========================================================================
int caribou_smi_read(caribou_smi_st* dev, caribou_smi_channel_en channel,
                    caribou_smi_sample_complex_int16* samples,
                    caribou_smi_sample_meta* metadata,
                    size_t length_samples)
{
    caribou_smi_sample_complex_int16* sample_offset = samples;
    caribou_smi_sample_meta* meta_offset = metadata;
    size_t left_to_read = length_samples * CARIBOU_SMI_BYTES_PER_SAMPLE;        // in bytes
    size_t read_so_far = 0;                                                     // in samples
    uint32_t to_millisec = caribou_smi_calc_read_timeout(dev->sample_rate, dev->native_batch_len);
  
    while (left_to_read)
    {
        if (sample_offset) sample_offset = samples + read_so_far;
        if (meta_offset) meta_offset = metadata + read_so_far;

        // current_read_len in bytes
        size_t current_read_len = ((left_to_read > dev->native_batch_len) ? dev->native_batch_len : left_to_read);
        
        to_millisec = caribou_smi_calc_read_timeout(dev->sample_rate, current_read_len);
        int ret = caribou_smi_timeout_read(dev, dev->read_temp_buffer, current_read_len, to_millisec);
        if (ret < 0)
        {
            return -1;
        }
        else if (ret == 0)
        {
            ZF_LOGD("Reading timed-out");
            break;
        }
        else
        {
            int data_affset = caribou_smi_rx_data_analyze(dev, channel, dev->read_temp_buffer, ret, sample_offset, meta_offset);
            if (data_affset < 0)
            {
                return -3;
            }

            // A special functionality for debug modes
            if (dev->debug_mode != caribou_smi_none)
            {
                caribou_smi_print_debug_stats(dev, dev->read_temp_buffer, ret);
                return -2;
            }
        }
        read_so_far += ret / CARIBOU_SMI_BYTES_PER_SAMPLE;
        left_to_read -= ret;
    }

    return read_so_far;
}

#define SMI_TX_SAMPLE_SOF               (1<<2)
#define SMI_TX_SAMPLE_MODEM_TX_CTRL     (1<<1)
#define SMI_TX_SAMPLE_COND_TX_CTRL      (1<<0)
//======helper function====================================================
void print_binairy32(uint32_t value) {
    for (int i = 31; i >= 0; --i) {
        putchar((value & (1 << i)) ? '1' : '0');
        if (i % 8 == 0 && i != 0) putchar(' ');
    }
    putchar('\n');
}
//=========================================================================
static void caribou_smi_generate_data(caribou_smi_st* dev, uint8_t* data, size_t data_length, const caribou_smi_sample_complex_int16* sample_offset)
{
    const caribou_smi_sample_complex_int16* cmplx_vec = sample_offset;  
    uint32_t *samples = (uint32_t*)(data);
    
    // Sample Structure
    // [                 BYTE 0      ] [           BYTE 1     ] [           BYTE 2        ] [          BYTE 3      ]
    // [SOF TXC CTX I12 I11 I10 I9 I8] [0 I7 I6 I5 I4 I3 I2 I1] [0 I0 Q12 Q11 Q10 Q9 Q8 Q7] [0 Q6 Q5 Q4 Q3 Q2 Q1 Q0]
	//   1  0/1 0/1
    
    for (unsigned int i = 0; i < (data_length / CARIBOU_SMI_BYTES_PER_SAMPLE); i++)
    {                    
        int32_t ii = cmplx_vec[i].i;
        int32_t qq = cmplx_vec[i].q;
        ii &= 0x1FFF;
        qq &= 0x1FFF;
		
        uint32_t s = SMI_TX_SAMPLE_SOF | SMI_TX_SAMPLE_MODEM_TX_CTRL | SMI_TX_SAMPLE_COND_TX_CTRL; s <<= 5;
        s |= (ii >> 8) & 0x1F; s <<= 8;
        s |= (ii >> 1) & 0x7F; s <<= 2;
        s |= (ii & 0x1); s <<= 6;
        s |= (qq >> 7) & 0x3F; s <<= 8;
        s |= (qq & 0x7F);
        //s = 0x80000000; // we need only one bit to be correct...
		
		//if (i < 2) 
        //{
        //    printf("0x%08X   ", s);
        //    print_binairy32(s);
        //}  
        
        //samples[i] = __builtin_bswap32(s);
        samples[i] = s; // like this we have 0 missed 'programmed write'.
    }
}

//==old======================================================================
// int caribou_smi_write(caribou_smi_st* dev, caribou_smi_channel_en channel,
//                         caribou_smi_sample_complex_int16* samples, size_t length_samples)
// {
//     size_t left_to_write = length_samples * CARIBOU_SMI_BYTES_PER_SAMPLE;   // in bytes
//     size_t written_so_far = 0;                                      // in samples
//     //uint32_t to_millisec = (2 * length_samples * 1000) / CARIBOU_SMI_SAMPLE_RATE;
//     uint32_t to_millisec = (2 * length_samples * 1000) / dev->sample_rate;
//     if (to_millisec < 2) to_millisec = 2;

//     smi_stream_state_en state = smi_stream_tx_channel;

//     // apply the state
//     // if (caribou_smi_set_driver_streaming_state(dev, state) != 0)
//     // {
// 	// 	printf("caribou_smi_set_driver_streaming_state -> Failed\n");
//     //     return -1;
//     // }

//     while (left_to_write)
//     {
//         // prepare the buffer
//         caribou_smi_sample_complex_int16* sample_offset = samples + written_so_far;
//         size_t current_write_len = (left_to_write > dev->native_batch_len) ? dev->native_batch_len : left_to_write;
		
//         // make sure the written bytes length is a whole sample multiplication
//         // if the number of remaining bytes is smaller than sample size -> finish;
//         current_write_len &= 0xFFFFFFFC;
//         if (!current_write_len) break;

//         caribou_smi_generate_data(dev, dev->write_temp_buffer, current_write_len, sample_offset);

//         int ret = caribou_smi_timeout_write(dev, dev->write_temp_buffer, current_write_len, to_millisec);
//         if (ret < 0)
//         {
//             return -1;
//         }
//         else if (ret == 0) break;

//         left_to_write  -= (size_t)ret;   // subtract *actual* bytes written
//         written_so_far += (size_t)ret / CARIBOU_SMI_BYTES_PER_SAMPLE;
//     }

//     return written_so_far;
// }

//==new====================================================================
int caribou_smi_write(caribou_smi_st* dev, caribou_smi_channel_en channel,
                      caribou_smi_sample_complex_int16* samples, size_t length_samples)
{
    const size_t q_bytes = smi_quarter_bytes(dev);                  // exact DMA period (bytes)
    const size_t bytes_total = length_samples * CARIBOU_SMI_BYTES_PER_SAMPLE;

    // We only transmit whole quarters; keep a static “last” IQ for padding tails
    static caribou_smi_sample_complex_int16 last = {0, 0};

    size_t bytes_left  = bytes_total;
    size_t wrote_samps = 0;

    // 1) Send as many *full* quarters as we can
    while (bytes_left >= q_bytes) {
        // Pack exactly one quarter into write_temp_buffer
        const size_t this_samp = q_bytes / CARIBOU_SMI_BYTES_PER_SAMPLE;
        caribou_smi_generate_data(dev, dev->write_temp_buffer, q_bytes, samples + wrote_samps);

        // Blocking write of the whole quarter
        if (write_all(dev->filedesc, (uint8_t*)dev->write_temp_buffer, q_bytes) != 0)
            return (int)wrote_samps; // short on error (never return a partial quarter)

        // Track the last IQ we sent (for tail padding later)
        last = samples[wrote_samps + this_samp - 1];

        wrote_samps += this_samp;
        bytes_left  -= q_bytes;
    }

    // 2) Handle tail < one quarter: pad with the last sample to complete a clean quarter
    if (bytes_left > 0) {
        const size_t this_samp = q_bytes / CARIBOU_SMI_BYTES_PER_SAMPLE;

        // Build a temporary quarter of IQ: the real tail first, then pad with 'last'
        // We reuse write_temp_buffer as a staging area.
        // Step A: copy real tail
        const size_t tail_samp = bytes_left / CARIBOU_SMI_BYTES_PER_SAMPLE;
        if (tail_samp) {
            caribou_smi_generate_data(dev, dev->write_temp_buffer,
                                      tail_samp * CARIBOU_SMI_BYTES_PER_SAMPLE,
                                      samples + wrote_samps);
            last = samples[wrote_samps + tail_samp - 1];
        }

        // Step B: synthesize padding samples = 'last'
        if (tail_samp < this_samp) {
            // Create a tiny view of identical samples for padding
            caribou_smi_sample_complex_int16 pad = last;
            // We can re-use the same last sample repeatedly:
            size_t pad_samp = this_samp - tail_samp;
            // Lay the pad samples into a small local stack array in chunks
            // to avoid big allocations; 64 is enough since we only need to
            // generate data into the *remaining* bytes of the quarter.
            caribou_smi_sample_complex_int16 chunk[64];
            size_t produced = 0;
            size_t dst_off_bytes = tail_samp * CARIBOU_SMI_BYTES_PER_SAMPLE;

            while (produced < pad_samp) {
                size_t n = (pad_samp - produced) < 64 ? (pad_samp - produced) : 64;
                for (size_t i = 0; i < n; i++) chunk[i] = pad;

                caribou_smi_generate_data(dev,
                                          ((uint8_t*)dev->write_temp_buffer) + dst_off_bytes,
                                          n * CARIBOU_SMI_BYTES_PER_SAMPLE,
                                          chunk);
                produced += n;
                dst_off_bytes += n * CARIBOU_SMI_BYTES_PER_SAMPLE;
            }
        }

        // Write the completed quarter
        if (write_all(dev->filedesc, (uint8_t*)dev->write_temp_buffer, q_bytes) != 0)
            return (int)wrote_samps;

        wrote_samps += this_samp;   // we advanced one full quarter worth of samples
        bytes_left   = 0;
    }

    return (int)wrote_samps;
}

// int caribou_smi_write_samples(caribou_smi_st *dev,
//                               caribou_smi_channel_en ch,
//                               const caribou_smi_sample_complex_int16 *samples,
//                               int n_samples)
// {
//     (void)ch; // channel via driver state

//     if (!dev || dev->filedesc < 0 || !samples || n_samples <= 0)
//         return -EINVAL;

//     const size_t bps = (size_t)CARIBOU_SMI_BYTES_PER_SAMPLE;
//     const size_t total_bytes = (size_t)n_samples * bps;

//     size_t bytes_left = total_bytes;
//     size_t consumed_samples = 0;

//     // Use a small write timeout; driver/DMA will pull as ready.
//     const uint32_t to_ms = 5;

//     while (bytes_left) {
//         // write at most native chunk; keep it whole-sample aligned
//         size_t cur = (bytes_left > dev->native_batch_len) ? dev->native_batch_len : bytes_left;
//         cur &= ~(bps - 1);                  // round down to whole sample
//         if (!cur) break;

//         // Pack caller’s samples for this chunk into write_temp_buffer
//         size_t cur_samp = cur / bps;
//         caribou_smi_generate_data(dev,
//                                   (uint8_t*)dev->write_temp_buffer,
//                                   cur,
//                                   (caribou_smi_sample_complex_int16*)(samples + consumed_samples));

//         // Write (may be partial); returns bytes actually written
//         int w = caribou_smi_timeout_write(dev,
//                                           (uint8_t*)dev->write_temp_buffer,
//                                           cur,
//                                           to_ms);
//         if (w < 0) {
//             // hard error
//             return (consumed_samples > 0) ? (int)consumed_samples : w;
//         }
//         if (w == 0) {
//             // timed out this round; report what we consumed so far
//             break;
//         }

//         // Convert bytes written back to samples *we can advance the source by*
//         size_t w_whole = (size_t)w & ~(bps - 1);
//         size_t w_samp  = w_whole / bps;

//         consumed_samples += w_samp;
//         bytes_left       -= w_whole;

//         if (w_whole < cur) {
//             // short write; let caller feed again later
//             break;
//         }
//     }

//     return (int)consumed_samples;
// }

int caribou_smi_write_samples(caribou_smi_st *dev,
                              caribou_smi_channel_en ch,
                              const caribou_smi_sample_complex_int16 *samples,
                              int n_samples)
{
    (void)ch;

    if (!dev || dev->filedesc < 0 || !samples || n_samples <= 0)
        return -EINVAL;

    const size_t bps         = (size_t)CARIBOU_SMI_BYTES_PER_SAMPLE;
    const size_t total_bytes = (size_t)n_samples * bps;

    size_t bytes_left        = total_bytes;
    size_t consumed_samples  = 0;

    // Give the kernel/DMA a bit more breathing room per attempt.
    const uint32_t per_try_timeout_ms = 25;

    while (bytes_left) {
        // Choose a modest chunk to smooth out scheduling jitter.
        size_t cur = bytes_left;
        if (cur > dev->native_batch_len) cur = dev->native_batch_len;

        // round down to whole sample
        cur &= ~(bps - 1);
        if (!cur) break;

        // Generate payload for this chunk
        size_t cur_samp = cur / bps;
        caribou_smi_generate_data(dev,
                                  (uint8_t*)dev->write_temp_buffer,
                                  cur,
                                  (const caribou_smi_sample_complex_int16*)(samples + consumed_samples));

        // Try to push the entire chunk, tolerate partial/timeouts inside this loop
        size_t off = 0;
        int attempts = 0;

        while (off < cur) {
            int w = caribou_smi_timeout_write(dev,
                                              (uint8_t*)dev->write_temp_buffer + off,
                                              (int)(cur - off),
                                              per_try_timeout_ms);
            if (w < 0) {
                // hard error: return what we did manage to consume so far (in samples) or the error if nothing
                return (consumed_samples > 0) ? (int)consumed_samples : w;
            }
            if (w == 0) {
                // just a timeout; allow a few retries before giving up this call
                if (++attempts >= 4) {
                    // return what we've advanced so far; caller can call us again immediately
                    goto done;
                }
                continue; // retry same offset
            }

            // advance by whole samples only
            size_t w_whole = (size_t)w & ~(bps - 1);
            off += w_whole;

            // reset attempts after forward progress
            attempts = 0;

            // If the driver ever gave us a non-sample-aligned write (shouldn't happen),
            // discard the tail bytes from this chunk to preserve alignment.
            if ((size_t)w != w_whole) {
                break; // finish this chunk; we'll regenerate cleanly next call
            }
        }

        // We successfully pushed 'off' bytes (sample-aligned)
        size_t pushed_samp = off / bps;
        consumed_samples  += pushed_samp;
        bytes_left        -= off;

        // If we didn't finish the chunk (e.g., non-aligned w), fall out to return early.
        if (off < cur) break;
    }

done:
    return (int)consumed_samples;
}

// Optionally keep the older name as a thin wrapper:
// int caribou_smi_write(caribou_smi_st* dev, caribou_smi_channel_en ch,
//                       caribou_smi_sample_complex_int16* samples, size_t length_samples)
// {
//     int n = (int)length_samples;
//     return caribou_smi_write_samples(dev, ch, samples, n);
// }

//=========================================================================
size_t caribou_smi_get_native_batch_samples(caribou_smi_st* dev)
{
    //printf("DEBUG: native batch len: %lu\n", dev->native_batch_len / CARIBOU_SMI_BYTES_PER_SAMPLE);
    return (dev->native_batch_len / CARIBOU_SMI_BYTES_PER_SAMPLE);
}

//=========================================================================
int caribou_smi_flush_fifo(caribou_smi_st* dev)
{
    if (!dev) return -1;
    if (!dev->initialized) return -1;
    int ret = read(dev->filedesc, NULL, 0);
    if (ret != 0)
    {
        ZF_LOGE("failed flushing driver fifos");
        return -1;
    }
    return 0;
}

int caribou_smi_start_tx(caribou_smi_st *dev)
{
    if (!dev) return -EINVAL;
    if (dev->state == smi_stream_tx_channel) return 0;

    // 1) Arm TX in the kernel FIRST
    if (caribou_smi_set_driver_streaming_state(dev, smi_stream_tx_channel) != 0)
        return -1;
    dev->state = smi_stream_tx_channel;

    // 2) Prefill a few chunks of steady zeros using the SAME writer
    const size_t bps     = (size_t)CARIBOU_SMI_BYTES_PER_SAMPLE;
    const size_t q_bytes = (size_t)(dev->native_batch_len / 4);
    const size_t q_samp  = q_bytes / bps;

    caribou_smi_sample_complex_int16 zero = {0, 0};

    // Build one zero quarter in write_temp_buffer
    for (size_t i = 0; i < q_samp; ++i) {
        ((caribou_smi_sample_complex_int16*)dev->write_temp_buffer)[i] = zero;
    }
    // Pack it to device format
    caribou_smi_generate_data(dev, (uint8_t*)dev->write_temp_buffer, q_bytes,
                              (caribou_smi_sample_complex_int16*)dev->write_temp_buffer);

    // Push a few quarters (non-blocking, allow partials/timeouts)
    for (int i = 0; i < 8; ++i) {
        int w = caribou_smi_timeout_write(dev,
                                          (uint8_t*)dev->write_temp_buffer,
                                          q_bytes,
                                          5);
        if (w <= 0) break; // fine; DMA will pull as it goes
    }

    return 0;
}

int caribou_smi_stop(caribou_smi_st *dev) {

    if (!dev) return -EINVAL;

    /* Drop any partial block so next TX starts clean */
    //caribou_smi_tx_accum_reset();

    if (dev->state == smi_stream_idle) return 0;
    if (caribou_smi_set_driver_streaming_state(dev, smi_stream_idle) != 0) return -1;
    
    dev->state = smi_stream_idle;
    return 0;
}