#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "ICE40"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "zf_log/zf_log.h"
#include "latticeice40.h"


#define LATTICE_ICE40_BUFSIZE 512
#define LATTICE_ICE40_TO_COUNT 200

//---------------------------------------------------------------------------
static int latticeice40_check_if_programmed(latticeice40_st* dev)
{
	if (dev == NULL)
	{
		ZF_LOGE("device pointer NULL");
		return -1;
	}

	if (!dev->initialized)
	{
		ZF_LOGE("device not initialized");
		return -1;
	}

	return io_utils_read_gpio(dev->cdone_pin);
}

//---------------------------------------------------------------------------
int latticeice40_init(latticeice40_st *dev,
						io_utils_spi_st* io_spi)
{
	if (dev == NULL)
	{
		ZF_LOGE("device pointer NULL");
		return -1;
	}

	if (dev->initialized)
	{
		ZF_LOGE("device already initialized");
		return 0;
	}

	dev->io_spi = io_spi;

	// Init pin directions
	io_utils_setup_gpio(dev->cdone_pin, io_utils_dir_input, io_utils_pull_up);
	io_utils_setup_gpio(dev->cs_pin, io_utils_dir_output, io_utils_pull_up);
	io_utils_setup_gpio(dev->reset_pin, io_utils_dir_output, io_utils_pull_up);

	dev->io_spi_handle = io_utils_spi_add_chip(dev->io_spi, dev->cs_pin, 5000000, 0, 0,
                                        io_utils_spi_chip_ice40_prog, NULL);

	dev->initialized = 1;

	// check if the FPGA is already configures
	if (latticeice40_check_if_programmed(dev) == 1)
	{
		ZF_LOGI("FPGA is already configured and running");
	}

	ZF_LOGI("device init completed");

	return 0;
}

//---------------------------------------------------------------------------
int latticeice40_release(latticeice40_st *dev)
{
	if (dev == NULL)
	{
		ZF_LOGE("device pointer NULL");
		return -1;
	}

	if (!dev->initialized)
	{
		ZF_LOGE("device not initialized");
		return 0;
	}

	// Release the SPI device
	io_utils_spi_remove_chip(dev->io_spi, dev->io_spi_handle);

	// Release the GPIO functions
	io_utils_setup_gpio(dev->cdone_pin, io_utils_dir_input, io_utils_pull_up);
	io_utils_setup_gpio(dev->cs_pin, io_utils_dir_input, io_utils_pull_up);
	io_utils_setup_gpio(dev->reset_pin, io_utils_dir_input, io_utils_pull_up);

	dev->initialized = 0;
	ZF_LOGI("device release completed");

	return 0;
}

//---------------------------------------------------------------------------
int latticeice40_configure(latticeice40_st *dev, char *bitfilename)
{
	FILE *fd = NULL;
	long ct;
	int read;
	uint8_t byte = 0xFF;
	uint8_t rxbyte = 0;
	unsigned char dummybuf[LATTICE_ICE40_BUFSIZE];
	char readbuf[LATTICE_ICE40_BUFSIZE];
	int progress = 0;
	long file_length = 0;

	if (dev == NULL)
	{
		ZF_LOGE("device pointer NULL");
		return -1;
	}

	if (!dev->initialized)
	{
		ZF_LOGE("device not initialized");
		return -1;
	}

	// open file or return error
	if(!(fd = fopen(bitfilename, "r")))
	{
		ZF_LOGI("open file %s failed", bitfilename);
		return 0;
	}
	else
	{
		fseek(fd, 0L, SEEK_END);
		file_length = ftell(fd);
		ZF_LOGI("opened bitstream file %s", bitfilename);
		fseek(fd, 0L, SEEK_SET);
	}

	// set SS low for spi config
	io_utils_write_gpio_with_wait(dev->cs_pin, 0, 200);

	// pulse RST low min 200 us ns
	io_utils_write_gpio_with_wait(dev->reset_pin, 0, 200);
	io_utils_usleep(200);

	// Wait for DONE low
	ZF_LOGI("RESET low, Waiting for CDONE low");
	ct = LATTICE_ICE40_TO_COUNT;

	while(io_utils_read_gpio(dev->cdone_pin)==1 && ct--)
	{
		asm volatile ("nop");
	}
	if (!ct)
	{
		ZF_LOGE("CDONE didn't fall during RESET='0'");
		return -1;
	}
	io_utils_write_gpio_with_wait(dev->reset_pin, 1, 200);
	io_utils_usleep(1200);

	// Send 8 dummy clocks with SS high
	io_utils_write_gpio_with_wait(dev->cs_pin, 1, 200);
	io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle, &byte, &rxbyte, 1, io_utils_spi_write);

	// Read file & send bitstream to FPGA via SPI with CS LOW
	ZF_LOGI("Sending bitstream\n");
	ct = 0;
	io_utils_write_gpio_with_wait(dev->cs_pin, 0, 200);
	while( (read=fread(readbuf, sizeof(char), LATTICE_ICE40_BUFSIZE, fd)) > 0 )
	{
		// Send bitstream
		io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle,
								(unsigned char *)readbuf,
								dummybuf,
								read,
								io_utils_spi_write);
		ct += read;

		// progress
		progress = (ct * 100) / file_length;
		printf("[%2d%%]\r", progress); fflush(stdout);
	}
	io_utils_write_gpio_with_wait(dev->cs_pin, 1, 200);

	// Transmit at least 49 clock cycles of clock
	io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle,
								dummybuf,
								dummybuf,
								8,
								io_utils_spi_write);

	/* close file */
	ZF_LOGI("sent %ld bytes", ct);
	ZF_LOGI("bitstream sent, closing file");
	fclose(fd);

	/* send dummy data while waiting for DONE */
 	ZF_LOGI("sending dummy clocks, waiting for CDONE to rise (or fail)");

	ct = LATTICE_ICE40_TO_COUNT;
	while(latticeice40_check_if_programmed(dev)==0 && ct--)
	{
		io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle,
								&byte, &rxbyte, 1, io_utils_spi_write);
	}

	if(ct)
	{
	 	ZF_LOGI("%d dummy clocks sent", (LATTICE_ICE40_TO_COUNT-ct)*8);
    }
	else
	{
		ZF_LOGI("timeout waiting for CDONE");
	}

	/* return status */
	if (latticeice40_check_if_programmed(dev)==0)
	{
		ZF_LOGE("config failed - CDONE not high");
		return -1;
	}

	ZF_LOGI("FPGA programming - Success!\n");

	return 0;
}

//---------------------------------------------------------------------------
int latticeice40_hard_reset(latticeice40_st *dev, int level)
{
	if (level == 0 || level == -1)
	{
		ZF_LOGD("Resetting FPGA reset pin to 0");
		io_utils_write_gpio_with_wait(dev->reset_pin, 0, 200);
		io_utils_usleep(1000);
	}
	if (level == 1 || level == -1)
	{
		ZF_LOGD("Setting FPGA reset pin to 1");
		io_utils_write_gpio_with_wait(dev->reset_pin, 1, 200);
		io_utils_usleep(1000);
	}
	return 0;
}