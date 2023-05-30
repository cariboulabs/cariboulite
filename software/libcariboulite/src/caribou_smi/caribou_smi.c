#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOU_SMI"
#include "zf_log/zf_log.h"

#define _GNU_SOURCE

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
#include <sched.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#include "caribou_smi.h"
#include "smi_utils.h"
#include "io_utils/io_utils.h"

//=========================================================================
int caribou_smi_set_driver_streaming_state(caribou_smi_st* dev, smi_stream_state_en state)
{
    int ret = ioctl(dev->filedesc, SMI_STREAM_IOC_SET_STREAM_STATUS, state);
    if (ret != 0)
    {
        ZF_LOGE("failed setting smi stream state (%d)", state);
        return -1;
    }
    dev->state = state;
    return 0;
}

//=========================================================================
smi_stream_state_en caribou_smi_get_driver_streaming_state(caribou_smi_st* dev)
{
    return dev->state;
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
    settings->read_strobe_time = 4;
    settings->read_hold_time = 0;
    settings->read_pace_time = 0;

    settings->write_setup_time = 1;
    settings->write_strobe_time = 4;
    settings->write_hold_time = 0;
    settings->write_pace_time = 0;

	// 8 bit on each transmission (4 TRX per sample)
    settings->data_width = SMI_WIDTH_8BIT;
	
	// Enable DMA
    settings->dma_enable = 1;
	
	// Whether or not to pack multiple SMI transfers into a single 32 bit FIFO word
    settings->pack_data = 1;
	
	// External DREQs enabled
    settings->dma_passthrough_enable = 1;
	
    // RX DREQ Threshold Level. 
    // A RX DREQ will be generated when the RX FIFO exceeds this threshold level. 
    // This will instruct an external AXI RX DMA to read the RX FIFO. 
    // If the DMA is set to perform burst reads, the threshold must ensure that there is 
    // sufficient data in the FIFO to satisfy the burst
    // Instruction: Lower is faster response
    settings->dma_read_thresh = 1;
    
    // TX DREQ Threshold Level. 
    // A TX DREQ will be generated when the TX FIFO drops below this threshold level. 
    // This will instruct an external AXI TX DMA to write more data to the TX FIFO.
    // Instruction: Higher is faster response
    settings->dma_write_thresh = 254;
    
    // RX Panic Threshold level.
    // A RX Panic will be generated when the RX FIFO exceeds this threshold level. 
    // This will instruct the AXI RX DMA to increase the priority of its bus requests.
    // Instruction: Lower is more aggressive
    settings->dma_panic_read_thresh = 16;
    
    // TX Panic threshold level.
    // A TX Panic will be generated when the TX FIFO drops below this threshold level. 
    // This will instruct the AXI TX DMA to increase the priority of its bus requests.
    // Instruction: Higher is more aggresive
    settings->dma_panic_write_thresh = 224;

    if (print)
    {
        caribou_smi_print_smi_settings(dev, settings);
    }

    if (ioctl(dev->filedesc, BCM2835_SMI_IOC_WRITE_SETTINGS, settings) != 0)
    {
        ZF_LOGE("failed writing ioctl to the smi fd (settings)");
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

    if (len <= 4)
    {
        return 0;
    }

    if (dev->debug_mode == caribou_smi_none)
    {
        for (offs = 0; offs<(len-(CARIBOU_SMI_BYTES_PER_SAMPLE*3)); offs++)
        {
            uint32_t s1 = *((uint32_t*)(&buffer[offs]));
            uint32_t s2 = *((uint32_t*)(&buffer[offs+4]));
            uint32_t s3 = *((uint32_t*)(&buffer[offs+8]));
			
            //printf("%d => %08X\n", offs, s);
            if ((s1 & 0xC001C000) == 0x80004000 &&
                (s2 & 0xC001C000) == 0x80004000 &&
                (s3 & 0xC001C000) == 0x80004000)
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
	                cmplx_vec[i].q = s & 0x00001FFF; s >>= 13;
	                s >>= 3;
	                cmplx_vec[i].i = s & 0x00001FFF; s >>= 13;
					
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

//=========================================================================
static int caribou_smi_timeout_write(caribou_smi_st* dev,
                            uint8_t* buffer,
                            size_t len,
                            uint32_t timeout_num_millisec)
{
    int res = caribou_smi_poll(dev, timeout_num_millisec, smi_stream_dir_smi_to_device);

    if (res < 0)
    {
        ZF_LOGD("poll error");
        return -1;
    }
    else if (res == 0)  // timeout
    {
        //ZF_LOGD("===> smi write fd timeout");
        return 0;
    }

    return write(dev->filedesc, buffer, len);
}

//=========================================================================
static int caribou_smi_timeout_read(caribou_smi_st* dev,
                                uint8_t* buffer,
                                size_t len,
                                uint32_t timeout_num_millisec)
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

    int ret = read(dev->filedesc, buffer, len);

    /*if (ret > 16)
    {
        smi_utils_dump_hex(buffer, 16);
    }*/
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

    ZF_LOGI("initializing caribou_smi");

    // start from a defined state
    memset(dev, 0, sizeof(caribou_smi_st));

    // checking the loaded modules
    // --------------------------------------------
    if (caribou_smi_check_modules(true) < 0)
    {
        ZF_LOGE("Problem reloading SMI kernel modules");
        return -1;
    }

    // open the smi device file
    // --------------------------------------------
    int fd = open(smi_file, O_RDWR);
    if (fd < 0)
    {
        ZF_LOGE("couldn't open smi driver file '%s' (%s)", smi_file, strerror(errno));
        return -1;
    }
    dev->filedesc = fd;

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

    if (caribou_smi_setup_settings(dev, &settings, true) != 0)
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
void caribou_smi_set_debug_mode(caribou_smi_st* dev, caribou_smi_debug_mode_en mode)
{
    dev->debug_mode = mode;
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
    uint32_t to_millisec = (2 * dev->native_batch_len * 1000) / CARIBOU_SMI_SAMPLE_RATE;
    if (to_millisec < 2) to_millisec = 2;

    while (left_to_read)
    {
        if (sample_offset) sample_offset = samples + read_so_far;
        if (meta_offset) meta_offset = metadata + read_so_far;

        // current_read_len in bytes
        size_t current_read_len = ((left_to_read > dev->native_batch_len) ? dev->native_batch_len : left_to_read);
        int ret = caribou_smi_timeout_read(dev, dev->read_temp_buffer, current_read_len, to_millisec);
        if (ret < 0)
        {
            return -1;
        }
        else if (ret == 0)
        {
            printf("caribou_smi_read -> Timeout\n");
            break;
        }
        else
        {
            int data_affset = caribou_smi_rx_data_analyze(dev, channel, dev->read_temp_buffer, ret, sample_offset, meta_offset);
            if (data_affset < 0)
            {
                return -1;
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
//=========================================================================
static void caribou_smi_generate_data(caribou_smi_st* dev, uint8_t* data, size_t data_length, caribou_smi_sample_complex_int16* sample_offset)
{
    caribou_smi_sample_complex_int16* cmplx_vec = sample_offset;  
    uint32_t *samples = (uint32_t*)(data);
	
    for (unsigned int i = 0; i < (data_length / CARIBOU_SMI_BYTES_PER_SAMPLE); i++)
    {                    
        int32_t ii = cmplx_vec[i].i;
        int32_t qq = cmplx_vec[i].q;
		
        uint32_t s = SMI_TX_SAMPLE_SOF | SMI_TX_SAMPLE_MODEM_TX_CTRL | SMI_TX_SAMPLE_COND_TX_CTRL; s <<= 5;
        s |= (ii >> 8) & 0x1F; s <<= 8;
        s |= (ii >> 1) & 0x7F; s <<= 2;
        s |= (ii & 0x1); s <<= 6;
        s |= (qq >> 7) & 0x3F; s <<= 8;
        s |= (qq & 0x7F);
		
		//if (i < 2) printf("0x%08X\n", s);
		
        samples[i] = __builtin_bswap32(s);
    }
}

//=========================================================================
int caribou_smi_write(caribou_smi_st* dev, caribou_smi_channel_en channel,
                        caribou_smi_sample_complex_int16* samples, size_t length_samples)
{
    size_t left_to_write = length_samples * CARIBOU_SMI_BYTES_PER_SAMPLE;   // in bytes
    size_t written_so_far = 0;                                      // in samples
    uint32_t to_millisec = (2 * length_samples * 1000) / CARIBOU_SMI_SAMPLE_RATE;
    if (to_millisec < 2) to_millisec = 2;

    smi_stream_state_en state = smi_stream_tx;

    // apply the state
    if (caribou_smi_set_driver_streaming_state(dev, state) != 0)
    {
		printf("caribou_smi_set_driver_streaming_state -> Failed\n");
        return -1;
    }

    while (left_to_write)
    {
        // prepare the buffer
        caribou_smi_sample_complex_int16* sample_offset = samples + written_so_far;
        size_t current_write_len = (left_to_write > dev->native_batch_len) ? dev->native_batch_len : left_to_write;
		
        // make sure the written bytes length is a whole sample multiplication
        // if the number of remaining bytes is smaller than sample size -> finish;
        current_write_len &= 0xFFFFFFFC;
        if (!current_write_len) break;

        caribou_smi_generate_data(dev, dev->write_temp_buffer, current_write_len, sample_offset);

        int ret = caribou_smi_timeout_write(dev, dev->write_temp_buffer, current_write_len, to_millisec);
        if (ret < 0)
        {
            return -1;
        }
        else if (ret == 0) break;

        written_so_far += current_write_len / CARIBOU_SMI_BYTES_PER_SAMPLE;
        left_to_write -= ret;
    }

    return written_so_far;
}

//=========================================================================
size_t caribou_smi_get_native_batch_samples(caribou_smi_st* dev)
{
    return dev->native_batch_len / CARIBOU_SMI_BYTES_PER_SAMPLE;
}
