#ifndef __IO_UTILS_SPI_H__
#define __IO_UTILS_SPI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include "io_utils.h"

#define IO_UTILS_MAX_CHIPS	10

typedef enum
{
	io_utils_spi_chip_type_fpga_comm = 0,
	io_utils_spi_chip_type_rffc = 1,
	io_utils_spi_chip_type_modem = 2,
	io_utils_spi_chip_ice40_prog = 3,
	io_utils_spi_chip_type_modem_bitbang = 4,
} io_utils_spi_chip_type_en;

typedef enum
{
	io_utils_spi_read_write = 0,
	io_utils_spi_read = 1,
	io_utils_spi_write = 2,
} io_utils_spi_dir_en;

typedef struct
{
	int spi_dev_id;			// either spidev0 or spidev1
	int spi_dev_channel;	// the channel number (2 cs for spidev0, 3 cs of spidev1)
} io_utils_hard_spi_st;

typedef struct
{
	int cs_pin;
	int clock;
	int mode;
	int miso_mosi_swap;
	io_utils_hard_spi_st hard_dev;

	int initialized;
	io_utils_spi_chip_type_en chip_type;
	int hard_spi_handle;
} io_utils_spi_chip_st;

typedef struct
{
	// pins
	int miso;
	int mosi;
	int sck;

	io_utils_spi_chip_st chips[IO_UTILS_MAX_CHIPS];
	int num_of_chips;
	io_utils_spi_chip_st *current_chip;
	pthread_mutex_t mtx;
	int initialized;
} io_utils_spi_st;

int io_utils_spi_init(io_utils_spi_st* dev);
int io_utils_spi_close(io_utils_spi_st* dev);
int io_utils_spi_add_chip(io_utils_spi_st* dev, int cs_pin, int speed, int swap_mi_mo, int mode,
                            io_utils_spi_chip_type_en chip_type, io_utils_hard_spi_st *hard_dev);
int io_utils_spi_remove_chip(io_utils_spi_st* dev, int chip_handle);
int io_utils_spi_transmit(io_utils_spi_st* dev, int chip_handle,
							const unsigned char* tx_buf,
							unsigned char* rx_buf,
							size_t length,
                            io_utils_spi_dir_en dir);
void io_utils_spi_print_setup(io_utils_spi_st* dev);

#ifdef __cplusplus
}
#endif

#endif // __IO_UTILS_SPI_H__