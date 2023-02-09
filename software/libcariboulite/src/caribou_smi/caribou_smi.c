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
static int caribou_smi_set_driver_streaming_state(caribou_smi_st* dev, smi_stream_state_en state)
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
	printf("	native kernel chunk size: %d bytes", dev->native_batch_len);
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
    settings->read_setup_time = 0;
    settings->read_strobe_time = 2;
    settings->read_hold_time = 0;
    settings->read_pace_time = 0;
    
    settings->write_setup_time = 1;
    settings->write_strobe_time = 4;
    settings->write_hold_time = 1;
    settings->write_pace_time = 0;
    
    settings->data_width = SMI_WIDTH_8BIT;
    settings->dma_enable = 1;
    settings->pack_data = 1;
    settings->dma_passthrough_enable = 1;
	
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
        printf("SMI DBG: ErrAccumCnt: %d, LastErrCnt: %d, ErrorRate: %.4g, bitrate: %.2f Mbps, ReadTail: %d\n", 
                dev->debug_data.error_accum_counter, 
                dev->debug_data.cur_err_cnt, 
                dev->debug_data.error_rate, 
                dev->debug_data.bitrate, 
                dev->read_tail_len);
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
    
    //printf("a\n");
    
    if (dev->debug_mode == caribou_smi_none)
    {
        for (offs = 0; offs<(len-4); offs++)
        {
            uint32_t s = __builtin_bswap32(*((uint32_t*)(&buffer[offs])));
            
            //printf("%d => %08X\n", offs, s);
            if ((s & 0xC001C000) == 0x80004000)
            {
                found = true;
                break;
            }
        }
    }
    else if (dev->debug_mode == caribou_smi_push || dev->debug_mode == caribou_smi_pull)
    {
        for (offs = 0; offs<(len-4); offs++)
        {
            uint32_t s = /*__builtin_bswap32*/(*((uint32_t*)(&buffer[offs])));
            //printf("%d => %08X, %08X\n", offs, s, caribou_smi_count_bit(s^CARIBOU_SMI_DEBUG_WORD));
            if (smi_utils_count_bit(s^CARIBOU_SMI_DEBUG_WORD) < 10)
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
    
    //printf("b\n");
    
    if (found == false)
    {
        return -1;
    }
    
    return (int)offs;
}

//=========================================================================
static int caribou_smi_rx_data_analyze(caribou_smi_st* dev,
                                uint8_t* data, size_t data_length, 
                                caribou_smi_sample_complex_int16* samples_out, 
                                caribou_smi_sample_meta* meta_offset)
{
    int offs = 0;
	size_t actual_length = data_length;
	uint32_t *actual_samples = (uint32_t*)(data);
    
	caribou_smi_sample_complex_int16* cmplx_vec = samples_out;
    
    //smi_utils_dump_hex_simple(actual_samples, 16, 32);
    // find the offset and adjust
    offs = caribou_smi_find_buffer_offset(dev, data, data_length);
    if (offs < 0)
    {
        //ZF_LOGE("incoming buffer synchronization failed");
        //smi_utils_dump_hex_simple(actual_samples, 4, 16);
        return -1;
    }
    else if (offs > 0)
    {
        actual_length -= (offs / 4 + 1) * 4;
        actual_samples = (uint32_t*)(data + offs);
    }
    
    // analyze the data
	if (dev->debug_mode != caribou_smi_none)
	{
		caribou_smi_anayze_smi_debug(dev, (uint8_t*)actual_samples, actual_length);
	}
	else
	{
        // Print buffer
        //smi_utils_dump_bin(buffer, 16);
        
		// Data Structure:
		//	[31:30]	[	29:17	] 	[ 16  ] 	[ 15:14 ] 	[	13:1	] 	[ 	0	]
		//	[ '10']	[ I sample	]	[ '0' ] 	[  '01'	]	[  Q sample	]	[  'S'	]
		
		for (unsigned int i = 0; i < actual_length / 4; i++)
		{
			uint32_t s = __builtin_bswap32(actual_samples[i]);

			meta_offset[i].sync = s & 0x00000001;
			s >>= 1;
			cmplx_vec[i].q = s & 0x00001FFF; s >>= 13;
			s >>= 3;
			cmplx_vec[i].i = s & 0x00001FFF; s >>= 13;

			if (cmplx_vec[i].i >= (int16_t)0x1000) cmplx_vec[i].i -= (int16_t)0x2000;
			if (cmplx_vec[i].q >= (int16_t)0x1000) cmplx_vec[i].q -= (int16_t)0x2000;
		}
	}
    
    return offs;
}

//=========================================================================
static void caribou_smi_generate_data(caribou_smi_st* dev, uint8_t* data, size_t data_length, caribou_smi_sample_complex_int16* sample_offset)
{
    caribou_smi_sample_complex_int16* cmplx_vec = sample_offset;  
    uint32_t *samples = (uint32_t*)(data);

    for (unsigned int i = 0; i < data_length / 4; i++)
    {
        uint32_t s = (((uint32_t)(cmplx_vec[i].i & 0x1FFF)) << 17) |
                     (((uint32_t)(cmplx_vec[i].q & 0x1FFF)) << 1) |
                     ((uint32_t)(0x80004000));
        
        s = __builtin_bswap32(s);
        
        samples[i] = s;
    }
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
	else if (res == 0)	// timeout
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
	else if (res == 0)	// timeout
	{
		//ZF_LOGD("===> smi read fd timeout");
		return 0;
	}

	return read(dev->filedesc, buffer, len);
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
        ZF_LOGE("couldn't open smi driver file '%s'", smi_file);
        return -1;
    }
    dev->filedesc = fd;

	// Setup the bus I/Os
	// --------------------------------------------
	for (int i = 6; i <= 15; i++)
	{
		io_utils_set_gpio_mode(i, io_utils_alt_1);  // 8xData + SWE + SOE
	}
    io_utils_set_gpio_mode(2, io_utils_alt_1);  // addr
    io_utils_set_gpio_mode(3, io_utils_alt_1);  // addr
	io_utils_set_gpio_mode(24, io_utils_alt_1); // rwreq
	io_utils_set_gpio_mode(25, io_utils_alt_1); // rwreq

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
	dev->read_tail_len = 0;
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
                    caribou_smi_sample_complex_int16* buffer, caribou_smi_sample_meta* metadata, size_t length_samples)
{
    size_t left_to_read = length_samples * CARIBOU_SMI_BYTES_PER_SAMPLE;        // in bytes
    size_t read_so_far = 0;                                                     // in samples
    uint32_t to_millisec = (2 * length_samples * 1000) / CARIBOU_SMI_SAMPLE_RATE;
    if (to_millisec < 2) to_millisec = 2;
    
    // choose the state
    smi_stream_state_en state = smi_stream_idle;
    if (channel == caribou_smi_channel_900)
        state = smi_stream_rx_channel_0;
    else if (channel == caribou_smi_channel_2400)
        state = smi_stream_rx_channel_1;
    
    // apply the state
    if (caribou_smi_set_driver_streaming_state(dev, state) != 0)
    {
        return -1;
    }
    
    while (left_to_read)
    {
        caribou_smi_sample_complex_int16* sample_offset = buffer + read_so_far;
        caribou_smi_sample_meta* meta_offset = metadata + read_so_far;
        
        // current_read_len in bytes
        size_t current_read_len = ((left_to_read > dev->native_batch_len) ? dev->native_batch_len : left_to_read);
        int ret = caribou_smi_timeout_read(dev, dev->read_temp_buffer + dev->read_tail_len, current_read_len, to_millisec);
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
            ret += dev->read_tail_len;
            int data_affset = caribou_smi_rx_data_analyze(dev, dev->read_temp_buffer, ret, sample_offset, meta_offset);
            
            //printf("data_affset = %d\n", data_affset);
            if (data_affset < 0)
            {
                dev->read_tail_len = 0;
                return -1;
            }
            if (data_affset > 0)
            {
                //dev->read_tail_len = 4 - (data_affset % 4);
                //ret -= data_affset;
                //memcpy(dev->read_temp_buffer, dev->read_temp_buffer + ret, dev->read_tail_len);
            }
            else
            {
                dev->read_tail_len = 0;
            }
            
            // A special functionality for debug modes
            if (dev->debug_mode != caribou_smi_none)
            {
                caribou_smi_print_debug_stats(dev, dev->read_temp_buffer, ret);
                return -2;  // special for debug
            }
            
            
        }
        read_so_far += ret / CARIBOU_SMI_BYTES_PER_SAMPLE;
        left_to_read -= ret;
    }
    
    return read_so_far;
}

//=========================================================================
int caribou_smi_write(caribou_smi_st* dev, caribou_smi_channel_en channel, 
                        caribou_smi_sample_complex_int16* buffer, size_t length_samples)
{
    size_t left_to_write = length_samples * CARIBOU_SMI_BYTES_PER_SAMPLE;   // in bytes
    size_t written_so_far = 0;                                      // in samples
    uint32_t to_millisec = (2 * length_samples * 1000) / CARIBOU_SMI_SAMPLE_RATE;
    if (to_millisec < 2) to_millisec = 2;
    
    smi_stream_state_en state = smi_stream_tx;
    
    // apply the state
    if (caribou_smi_set_driver_streaming_state(dev, state) != 0)
    {
        return -1;
    }
    
    while (left_to_write)
    {
        // prepare the buffer
        caribou_smi_sample_complex_int16* sample_offset = buffer + written_so_far;
        size_t current_write_len = (left_to_write > dev->native_batch_len) ? dev->native_batch_len : left_to_write;

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
    
    return 0;
}

//=========================================================================
size_t caribou_smi_get_native_batch_samples(caribou_smi_st* dev)
{
    return dev->native_batch_len / CARIBOU_SMI_BYTES_PER_SAMPLE;
}