/*
 * This file was derived from code written by HackRF Team:
 * 	1. 2012 Michael Ossmann
 * 	2. 2014 Jared Boone <jared@sharebrained.com>
 * and was modified / rewritten by David Michaeli (cariboulabs.co@gmail.com) to
 * adapt if for the CaribouLite project running on Linux OS (RPI).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __RFFC507X_H
#define __RFFC507X_H

#ifdef __cplusplus
extern "C" {
#endif

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
void rffc507x_setup_pin_functions(rffc507x_st* dev);
void rffc507x_readback(rffc507x_st* dev, uint16_t *readback_buff, int buf_len);

#ifdef __cplusplus
}
#endif

#endif // __RFFC507X_H
