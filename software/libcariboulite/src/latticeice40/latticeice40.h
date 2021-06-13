#ifndef __LATTICEICE40_H__
#define __LATTICEICE40_H__

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

#endif // __LATTICEICE40_H__
