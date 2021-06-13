#ifndef __RFFC507X_H
#define __RFFC507X_H

#include <stdio.h>
#include <stdint.h>
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"


/* 31 registers, each containing 16 bits of data. */
#define RFFC507X_NUM_REGS 31
#define RFFC507X_REG_SET_CLEAN(d,r) (d)->rffc507x_regs_dirty &= ~(1UL<<r)
#define RFFC507X_REG_SET_DIRTY(d,r) (d)->rffc507x_regs_dirty |= (1UL<<r)

typedef struct
{
    int cs_pin;
    int reset_pin;
    int mode_pin;

    io_utils_spi_st* io_spi;
	int io_spi_handle;

    int initialized;
    uint16_t rffc507x_regs[RFFC507X_NUM_REGS];
    uint32_t rffc507x_regs_dirty;
} rffc507x_st;

// Initialize chip
int rffc507x_init(  rffc507x_st* dev,
					io_utils_spi_st* io_spi);
int rffc507x_release(rffc507x_st* dev);

// Read a register via SPI. Save a copy to memory and return
// value. Discard any uncommited changes and mark CLEAN
uint16_t rffc507x_reg_read(rffc507x_st* dev, uint8_t r);

// Write value to register via SPI and save a copy to memory. Mark
// CLEAN
void rffc507x_reg_write(rffc507x_st* dev, uint8_t r, uint16_t v);

// Write all dirty registers via SPI from memory. Mark all clean. Some
// operations require registers to be written in a certain order. Use
// provided routines for those operations
int rffc507x_regs_commit(rffc507x_st* dev);

// Set frequency (MHz)
uint64_t rffc507x_set_frequency(rffc507x_st* dev, uint16_t mhz);

// Set up rx only, tx only, or full duplex. Chip should be disabled
// before _tx, _rx, or _rxtx are called.
void rffc507x_tx(rffc507x_st* dev);
void rffc507x_rx(rffc507x_st* dev);
void rffc507x_rxtx(rffc507x_st* dev);
void rffc507x_enable(rffc507x_st* dev);
void rffc507x_disable(rffc507x_st* dev);
void rffc507x_set_gpo(rffc507x_st* dev, uint8_t gpo);

#endif // __RFFC507X_H
