#include <string.h>
#include "io_utils_spi.h"




int io_utils_spi_init(io_utils_spi_st* dev)
{
	if (dev == NULL)
	{
		printf("ERROR @ io_utils_spi_init: dev is NULL\n");
		return -1;
	}
	
	memset(dev->chips, 0, sizeof(io_utils_spi_chip_st * IO_UTILS_MAX_CHIPS));
	dev->num_of_chips = 0;
	
	if (pthread_mutex_init(&dev->mtx->, NULL) != 0) {
        printf("ERROR @ io_utils_spi_init: mutex creation failed\n");
        return -1;
    }
	
	
	dev->intialized = 1;
	return 0;
}

int io_utils_spi_close(io_utils_spi_st* dev)
{
	dev->intialized = 0;
	pthread_mutex_destroy(&op->om);
}

int io_utils_spi_add_chip(io_utils_spi_st* dev, int cs_pin, 
								int speed, 
								io_utils_spi_chip_type_en chip_type, 
								io_utils_spi_chip_transfer_type_en txrx_type)
{
}

int io_utils_spi_remove_chip(io_utils_spi_st* dev, int cs_pin)
{
}

int io_utils_spi_transmit(io_utils_spi_st* dev, int cs_pin, 
							const unsigned char* tx_buf, 
							unsigned char* rx_buf, 
							size_t length)
{
	
}