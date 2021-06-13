#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "latticeice40.h"


#define LATTICE_ICE40_BUFSIZE 512
#define LATTICE_ICE40_TO_COUNT 200

//---------------------------------------------------------------------------
static int latticeice40_check_if_programmed(latticeice40_st* dev)
{
	if (dev == NULL)
	{
		printf("ERROR @ latticeice40_check_if_programmed: device pointer NULL\n");
		return -1;
	}

	if (!dev->initialized)
	{
		printf("Error @ latticeice40_check_if_programmed: device not initialized\n");
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
		printf("ERROR @ latticeice40_init: device pointer NULL\n");
		return -1;
	}

	if (dev->initialized)
	{
		printf("ERROR @ latticeice40_init: device already initialized\n");
		return 0;
	}

	dev->io_spi = io_spi;

	// Init pin directions lgpio
	io_utils_setup_gpio(dev->cdone_pin, io_utils_dir_input, io_utils_pull_up);
	io_utils_setup_gpio(dev->cs_pin, io_utils_dir_output, io_utils_pull_up);
	io_utils_setup_gpio(dev->reset_pin, io_utils_dir_output, io_utils_pull_up);

	dev->io_spi_handle = io_utils_spi_add_chip(dev->io_spi, dev->cs_pin, 5000000, 1, 0,
                                        io_utils_spi_chip_ice40_prog, NULL);

	dev->initialized = 1;

	// check if the FPGA is already configures
	if (latticeice40_check_if_programmed(dev) == 1)
	{
		printf("INFO @ latticeice40_init: FPGA is already configured and running.\n");
	}

	printf("INFO @ latticeice40_init: device init completed\n");

	return 0;
}

//---------------------------------------------------------------------------
int latticeice40_release(latticeice40_st *dev)
{
	if (dev == NULL)
	{
		printf("ERROR @ latticeice40_release: device pointer NULL\n");
		return -1;
	}

	if (!dev->initialized)
	{
		printf("Error @ latticeice40_release: device not initialized\n");
		return 0;
	}

	// Release the SPI device
	io_utils_spi_remove_chip(dev->io_spi, dev->io_spi_handle);

	// Release the GPIO functions
	io_utils_setup_gpio(dev->cdone_pin, io_utils_dir_input, io_utils_pull_up);
	io_utils_setup_gpio(dev->cs_pin, io_utils_dir_input, io_utils_pull_up);
	io_utils_setup_gpio(dev->reset_pin, io_utils_dir_input, io_utils_pull_up);

	dev->initialized = 0;
	printf("INFO @ latticeice40_release: device release completed\n");

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
		printf("ERROR @ latticeice40_configure: device pointer NULL\n");
		return -1;
	}

	if (!dev->initialized)
	{
		printf("Error @ latticeice40_configure: device not initialized\n");
		return -1;
	}

	// open file or return error
	if(!(fd = fopen(bitfilename, "r")))
	{
		printf("INFO @ latticeice40_configure: open file %s failed\n", bitfilename);
		return 0;
	}
	else
	{
		fseek(fd, 0L, SEEK_END);
		file_length = ftell(fd);
		printf("INFO @ latticeice40_configure: opened bitstream file %s\n", bitfilename);
		fseek(fd, 0L, SEEK_SET);
	}

	// set SS low for spi config
	io_utils_write_gpio_with_wait(dev->cs_pin, 0, 200);

	// pulse RST low min 200 us ns
	io_utils_write_gpio_with_wait(dev->reset_pin, 0, 200);
	io_utils_usleep(200);

	// Wait for DONE low
	printf("INFO @ latticeice40_configure: RESET low, Waiting for CDONE low\n");
	ct = LATTICE_ICE40_TO_COUNT;

	while(io_utils_read_gpio(dev->cdone_pin)==1 && ct--)
	{
		asm volatile ("nop");
	}
	if (!ct)
	{
		printf("ERROR @ latticeice40_configure: CDONE didn't fall during RESET='0'\n");
		return -1;
	}
	io_utils_write_gpio_with_wait(dev->reset_pin, 1, 200);
	io_utils_usleep(1200);

	// Send 8 dummy clocks with SS high
	io_utils_write_gpio_with_wait(dev->cs_pin, 1, 200);
	io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle, &byte, &rxbyte, 1, io_utils_spi_write);

	// Read file & send bitstream to FPGA via SPI with CS LOW
	printf("INFO @ latticeice40_configure: Sending bitstream\n");
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
	printf("\nINFO @ latticeice40_configure: sent %ld bytes\n", ct);
	printf("INFO @ latticeice40_configure: bitstream sent, closing file\n");
	fclose(fd);

	/* send dummy data while waiting for DONE */
 	printf("INFO @ latticeice40_configure: sending dummy clocks, waiting for CDONE to rise (or fail)\n");

	ct = LATTICE_ICE40_TO_COUNT;
	while(latticeice40_check_if_programmed(dev)==0 && ct--)
	{
		io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle,
								&byte, &rxbyte, 1, io_utils_spi_write);
	}

	if(ct)
	{
	 	printf("INFO @ latticeice40_configure: %d dummy clocks sent\n", (LATTICE_ICE40_TO_COUNT-ct)*8);
    }
	else
	{
		printf("INFO @ latticeice40_configure: timeout waiting for CDONE\n");
	}

	/* return status */
	if (latticeice40_check_if_programmed(dev)==0)
	{
		printf("ERROR @ latticeice40_configure: config failed - CDONE not high\n");
		return -1;
	}

	printf("INFO @ latticeice40_configure: Success!\n");

	return 0;
}

