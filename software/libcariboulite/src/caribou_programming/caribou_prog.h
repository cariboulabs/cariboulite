#ifndef __CARIBOU_PROG_H__
#define __CARIBOU_PROG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <linux/types.h>
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"

/**
 * @brief caribou-sdr programmer context
 */
typedef struct
{
	int cs_pin;
	int cdone_pin;
	int reset_pin;
	int verbose;

	io_utils_spi_st* io_spi;

	int io_spi_handle;
	int initialized;
} caribou_prog_st;

int caribou_prog_init(caribou_prog_st *dev, io_utils_spi_st* io_spi);
int caribou_prog_release(caribou_prog_st *dev);
int caribou_prog_configure(caribou_prog_st *dev, char *bitfilename);
int caribou_prog_configure_from_buffer(	caribou_prog_st *dev, 
										uint8_t *buffer, 
										uint32_t buffer_size);

/*
 * Hard reset pin toggling function
    Level: if -1 => a full reset (1=>0=>1) cycle is performed
		   if 0 / 1 => the pin is reset or set accordingly
 */
int caribou_prog_hard_reset(caribou_prog_st *dev, int level);

#ifdef __cplusplus
}
#endif

#endif // __CARIBOU_PROG_H__
