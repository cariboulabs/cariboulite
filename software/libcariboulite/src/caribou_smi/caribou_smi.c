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

#include "caribou_smi.h"
#include "smi_stream.h"
#include "utils.h"
#include "io_utils/io_utils.h"


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
static void caribou_smi_anayze_smi_debug(caribou_smi_st* dev,
										 smi_stream_channel_en channel,
										 uint8_t *data,
										 size_t len)
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

	dev->debug_data.error_rate = dev->debug_data.error_rate * 0.9 + (double)(error_counter_current) / (double)(len) * 0.1;

	// print
	if (dev->debug_data.cnt > 10)
	{
		printf("	[%02X, %02X, LAST:%02X] Received Debug Errors:  Curr. %d , Accum %d, FirstErr %d, BER: %.3g, BitRate[Mbps]: %.2f\n",
					data[0], data[len-1], dev->debug_data.last_correct_byte,
					error_counter_current, dev->debug_data.error_accum_counter, first_error, dev->debug_data.error_rate,
					dev->streams[channel].rx_bitrate_mbps);
		dev->debug_data.cnt = 0;
	} else dev->debug_data.cnt++;
}
										 

//=========================================================================
void caribou_smi_stream_rx_data_callback(smi_stream_channel_en channel,
											uint8_t* data,
											size_t data_length,
											void* context)
{
	caribou_smi_st* dev = (caribou_smi_st*)context;
	caribou_smi_sample_complex_int16* cmplx_vec = dev->rx_cplx_buffer[channel];
	
	if (dev->debug_mode != caribou_smi_none)
	{
		caribou_smi_anayze_smi_debug(dev, channel, data, data_length);
	}
	else
	{
		// the verilog struct looks as follows:
		//	[31:30]	[	29:17	] 	[ 16  ] 	[ 15:14 ] 	[	13:1	] 	[ 	0	]
		//	[ '00']	[ I sample	]	[ '0' ] 	[  '01'	]	[  Q sample	]	[  '0'	]
		
		// check data offset
		unsigned int offs = 0;
		//smi_utils_dump_bin(buffer, 16);
		for (offs = 0; offs<8; offs++)
		{
			uint32_t s = __builtin_bswap32(*((uint32_t*)(&data[offs])));
			//smi_utils_print_bin(s);
			if ((s & 0xC001C000) == 0x80004000) break;
		}

		if (offs)
		{
			data_length -= (offs/4 + 1) * 4;
		}
		
		//printf("data offset %d\n", offs);
		uint32_t *samples = (uint32_t*)(data + offs);

		for (unsigned int i = 0; i < data_length/4; i++)
		{
			uint32_t s = __builtin_bswap32(samples[i]);

			//meta_vec[i].sync = s & 0x00000001;
			s >>= 1;
			cmplx_vec[i].q = s & 0x00001FFF; s >>= 13;
			s >>= 3;
			cmplx_vec[i].i = s & 0x00001FFF; s >>= 13;

			if (cmplx_vec[i].i >= (int16_t)0x1000) cmplx_vec[i].i -= (int16_t)0x2000;
			if (cmplx_vec[i].q >= (int16_t)0x1000) cmplx_vec[i].q -= (int16_t)0x2000;
		}
		
		if (dev->rx_cb) dev->rx_cb((caribou_smi_channel_en)channel, cmplx_vec, data_length/4, dev->context);
	}
}

//=========================================================================
size_t caribou_smi_stream_tx_data_callback(smi_stream_channel_en channel,
											uint8_t* data,
											size_t* data_length,
											void* context)
{
	return 0;
}

//=========================================================================
void caribou_smi_stream_event_callback(	smi_stream_channel_en channel,
										smi_stream_event_type_en event,
										void* metadata,
										void* context)
{
	caribou_smi_st* dev = (caribou_smi_st*)context;
	if (event == smi_stream_error)
	{
		if (dev->error_cb)
		{
			dev->error_cb((caribou_smi_channel_en)channel, dev->context);
		}
	}
}

//=========================================================================
int caribou_smi_init(caribou_smi_st* dev, 
					caribou_smi_error_callback error_cb,
					void* context)
{
	int ret = 0;
    char smi_file[] = "/dev/smi";
    struct smi_settings settings = {0};
	
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
    int fd = open(smi_file, O_RDWR | O_NONBLOCK);
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

	// create streams and complex buffers
	for (int ch = smi_stream_channel_0; ch < smi_stream_channel_max; ch++)
	{
		dev->rx_cplx_buffer[ch] = (caribou_smi_sample_complex_int16 *)malloc(sizeof(caribou_smi_sample_complex_int16) * dev->native_batch_len / 4);
		if (!dev->rx_cplx_buffer[ch])
		{
			ZF_LOGE("SMI RX complex buffer allocation for channel (%d) init failed", ch);
			caribou_smi_close (dev);
		}
		
		dev->tx_cplx_buffer[ch] = (caribou_smi_sample_complex_int16 *)malloc(sizeof(caribou_smi_sample_complex_int16) * dev->native_batch_len / 4);
		if (!dev->tx_cplx_buffer[ch])
		{
			ZF_LOGE("SMI TX complex buffer allocation for channel (%d) init failed", ch);
			caribou_smi_close (dev);
		}
		
		ret = smi_stream_init(	&dev->streams[ch],
								dev->filedesc,
								(smi_stream_channel_en)ch,
								caribou_smi_stream_rx_data_callback,
								caribou_smi_stream_tx_data_callback,
								caribou_smi_stream_event_callback,
								dev);
		if (ret != 0)
		{
			ZF_LOGE("SMI stream channel (%d) init failed", ch);
			caribou_smi_close (dev);
		}
	}

	dev->debug_mode = caribou_smi_none;
    dev->error_cb = error_cb;
	dev->rx_cb = NULL;
	dev->tx_cb = NULL;
    dev->context = context;
    dev->initialized = 1;

    return 0;
}

//=========================================================================
int caribou_smi_close (caribou_smi_st* dev)
{
	// release streams
	for (int ch = smi_stream_channel_0; ch < smi_stream_channel_max; ch++)
	{
		smi_stream_release(&dev->streams[ch]);
		
		if (dev->rx_cplx_buffer[ch]) free(dev->rx_cplx_buffer[ch]);
		if (dev->tx_cplx_buffer[ch]) free(dev->tx_cplx_buffer[ch]);
		dev->rx_cplx_buffer[ch] = NULL;
		dev->tx_cplx_buffer[ch] = NULL;
	}
	
	// close smi device file
    close (dev->filedesc);
    return 0;
}

//=========================================================================
int caribou_smi_setup_data_callbacks (caribou_smi_st* dev, 
									caribou_smi_rx_data_callback rx_cb, 
									caribou_smi_tx_data_callback tx_cb, 
									void *data_context)
{
	dev->rx_cb = rx_cb;
	dev->tx_cb = tx_cb;
}

//=========================================================================
void caribou_smi_set_debug_mode(caribou_smi_st* dev, caribou_smi_debug_mode_en mode)
{
	dev->debug_mode = mode;
}
