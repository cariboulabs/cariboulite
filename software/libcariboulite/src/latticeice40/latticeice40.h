#ifndef __LATTICEICE40_H__
#define __LATTICEICE40_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <linux/types.h>
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"

typedef struct
{
	int cs_pin;
	int cdone_pin;
	int reset_pin;
	int verbose;

	io_utils_spi_st* io_spi;

	int io_spi_handle;
	int initialized;
} latticeice40_st;

int latticeice40_init(	latticeice40_st *dev,
						io_utils_spi_st* io_spi);
int latticeice40_release(latticeice40_st *dev);
int latticeice40_configure(latticeice40_st *dev, char *bitfilename);
int latticeice40_configure_from_buffer(	latticeice40_st *dev, 
										uint8_t *buffer, 
										uint32_t buffer_size);

/*
 * Hard reset pin toggling function
    Level: if -1 => a full reset (1=>0=>1) cycle is performed
		   if 0 / 1 => the pin is reset or set accordingly
 */
int latticeice40_hard_reset(latticeice40_st *dev, int level);

#ifdef __cplusplus
}
#endif

#endif // __LATTICEICE40_H__
