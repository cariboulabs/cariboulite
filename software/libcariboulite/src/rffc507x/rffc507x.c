/*
 * This file was derived from code written by HackRF Team:
 * 	1. 2012 Michael Ossmann
 * 	2. 2014 Jared Boone <jared@sharebrained.com>
 * and was modified by David Michaeli (cariboulabs.co@gmail.com) to
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


#include <stdint.h>
#include <string.h>
#include "rffc507x.h"
#include "rffc507x_regs.h" // private register def macros

#if (defined DEBUG)
#include <stdio.h>
#define LOG printf
#else
#define LOG(x,...)
#endif


#define LO_MAX 5400
#define REF_FREQ 26
#define FREQ_ONE_MHZ (1000*1000)

//===========================================================================
// Default register values
static uint16_t rffc507x_regs_default[RFFC507X_NUM_REGS] =
{
	0xbefa,   /* 00 */
	0x4064,   /* 01 */
	0x9055,   /* 02 */
	0x2d02,   /* 03 */
	0xacbf,   /* 04 */
	0xacbf,   /* 05 */
	0x0028,   /* 06 */
	0x0028,   /* 07 */
	0xff00,   /* 08 */
	0x8220,   /* 09 */
	0x0202,   /* 0A */
	0x4800,   /* 0B */
	0x1a94,   /* 0C */
	0xd89d,   /* 0D */
	0x8900,   /* 0E */
	0x1e84,   /* 0F */
	0x89d8,   /* 10 */
	0x9d00,   /* 11 */
	0x2a20,   /* 12 */
	0x0000,   /* 13 */
	0x0000,   /* 14 */
	0x0000,   /* 15 */
	0x0000,   /* 16 */
	0x4900,   /* 17 */
	0x0281,   /* 18 */
	0xf00f,   /* 19 */
	0x0000,   /* 1A */
	0x0000,   /* 1B */
	0xc840,   /* 1C */
	0x1000,   /* 1D */
	0x0005,   /* 1E */
};

//===========================================================================
static inline void rffc507x_reg_commit(rffc507x_st* dev, uint8_t r)
{
	rffc507x_reg_write(dev, r, dev->rffc507x_regs[r]);
}

//===========================================================================
int rffc507x_regs_commit(rffc507x_st* dev)
{
	int r;
	for (r = 0; r < RFFC507X_NUM_REGS; r++) {
		if ((dev->rffc507x_regs_dirty >> r) & 0x1)
		{
			rffc507x_reg_commit(dev, r);
		}
	}
}

//===========================================================================
// Set up all registers according to defaults specified in docs.
int rffc507x_init(  rffc507x_st* dev,
					io_utils_spi_st* io_spi)
{
	if (dev == NULL)
	{
		printf("ERROR @ rffc507x_init: dev is NULL\n");
		return -1;
	}
	printf("INFO @ rffc507x_init: Initializing\n");
	memcpy(dev->rffc507x_regs, rffc507x_regs_default, sizeof(dev->rffc507x_regs));
	dev->rffc507x_regs_dirty = 0x7fffffff;

	printf("INFO @ rffc507x_init: setting up device pins\n");

	dev->io_spi = io_spi;

	/* Configure GPIO pins. */
	io_utils_setup_gpio(dev->mode_pin, io_utils_dir_output, io_utils_pull_up);
	io_utils_setup_gpio(dev->reset_pin, io_utils_dir_output, io_utils_pull_up);

	/* set to known state */
	io_utils_write_gpio(dev->mode_pin, 0);
	io_utils_write_gpio(dev->reset_pin, 1);

	dev->io_spi_handle = io_utils_spi_add_chip(dev->io_spi, dev->cs_pin, 5000000, 0, 0,
                        						io_utils_spi_chip_type_rffc, NULL);

	printf("INFO @ rffc507x_init: received spi handle %d\n", dev->io_spi_handle);

	// initial setup
	// put zeros in freq contol registers
	set_RFFC507X_P2N(dev, 0);
	set_RFFC507X_P2LODIV(dev, 0);
	set_RFFC507X_P2PRESC(dev, 0);
	set_RFFC507X_P2VCOSEL(dev, 0);

	set_RFFC507X_P2N(dev, 0);
	set_RFFC507X_P2LODIV(dev, 0);
	set_RFFC507X_P2PRESC(dev, 0);
	set_RFFC507X_P2VCOSEL(dev, 0);

	set_RFFC507X_P2N(dev, 0);
	set_RFFC507X_P2LODIV(dev, 0);
	set_RFFC507X_P2PRESC(dev, 0);
	set_RFFC507X_P2VCOSEL(dev, 0);

	// set ENBL and MODE to be configured via 3-wire interface, not control pins.
	set_RFFC507X_SIPIN(dev, 1);

	// GPOs are active at all times
	set_RFFC507X_GATE(dev, 1);

	// Write default register values to chip.
	int ret = rffc507x_regs_commit(dev);
	if (ret < 0)
	{
		printf("ERROR @ rffc507x_init: rffc507x_regs_commit failed\n");
		return -1;
	}

	dev->initialized = 1;

	return 0;
}

//===========================================================================
int rffc507x_release(rffc507x_st* dev)
{
	if (dev == NULL)
	{
		printf("ERROR @ rffc507x_release: device pointer NULL\n");
		return -1;
	}

	if (!dev->initialized)
	{
		printf("Error @ rffc507x_release: device not initialized\n");
		return 0;
	}

	dev->initialized = 0;

	io_utils_setup_gpio(dev->reset_pin, io_utils_dir_input, io_utils_pull_up);
	io_utils_setup_gpio(dev->mode_pin, io_utils_dir_input, io_utils_pull_up);

	// Release the SPI device
	io_utils_spi_remove_chip(dev->io_spi, dev->io_spi_handle);

	printf("INFO @ rffc507x_release: device release completed\n");

	return 0;
}

//===========================================================================
static void rffc507x_serial_delay(void)
{
	uint32_t i;

	for (i = 0; i < 2; i++)
		__asm__("nop");
}

//===========================================================================
uint16_t rffc507x_reg_read(rffc507x_st* dev, uint8_t r)
{
	uint8_t vout = r;
	uint16_t vin = 0;

	// Readback register is not cached.
	if (r == RFFC507X_READBACK_REG)
	{
		io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle, &vout, (uint8_t*)&vin, 2, io_utils_spi_read);
		return vin;
	}

	// Discard uncommited write when reading. This shouldn't
	// happen, and probably has not been tested.
	if ((dev->rffc507x_regs_dirty >> r) & 0x1)
	{
		io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle, &vout, (uint8_t*)&vin, 2, io_utils_spi_read);
	}
	return dev->rffc507x_regs[r];
}

//===========================================================================
void rffc507x_reg_write(rffc507x_st* dev, uint8_t r, uint16_t v)
{
	dev->rffc507x_regs[r] = v;
	uint8_t vout[3] = {0};

	vout[0] = r;
	*((uint16_t*)&vout[1]) = v;

	io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle, vout, NULL, 3, io_utils_spi_write);
	RFFC507X_REG_SET_CLEAN(dev, r);
}

//===========================================================================
void rffc507x_tx(rffc507x_st* dev)
{
	printf("INFO @ rffc507x_tx: rffc507x_tx\n");
	set_RFFC507X_ENBL(dev, 0);
	set_RFFC507X_FULLD(dev, 0);
	set_RFFC507X_MODE(dev, 1); // mixer 2 used for both RX and TX
	rffc507x_regs_commit(dev);
}

//===========================================================================
void rffc507x_rx(rffc507x_st* dev)
{
	printf("INFO @ rffc507x_rx: rffc507x_rx\n");
	set_RFFC507X_ENBL(dev, 0);
	set_RFFC507X_FULLD(dev, 0);
	set_RFFC507X_MODE(dev, 1); // mixer 2 used for both RX and TX
	rffc507x_regs_commit(dev);
}

//===========================================================================
// This function turns on both mixer (full-duplex) on the RFFC507X, but our
// current hardware designs do not support full-duplex operation.
void rffc507x_rxtx(rffc507x_st* dev)
{
	printf("# rfc5071_rxtx\n");
	set_RFFC507X_ENBL(dev, 0);
	set_RFFC507X_FULLD(dev, 1); // mixer 1 and mixer 2 (RXTX)
	rffc507x_regs_commit(dev);

	rffc507x_enable(dev);
}

//===========================================================================
void rffc507x_disable(rffc507x_st* dev)
{
	printf("# rfc5071_disable\n");
	set_RFFC507X_ENBL(dev, 0);
	rffc507x_regs_commit(dev);
}

//===========================================================================
void rffc507x_enable(rffc507x_st* dev)
{
	printf("# rfc5071_enable\n");
	set_RFFC507X_ENBL(dev, 1);
	rffc507x_regs_commit(dev);
}

//===========================================================================
// configure frequency synthesizer in integer mode (lo in MHz)
uint64_t rffc507x_config_synth_int(rffc507x_st* dev, uint16_t lo)
{
	uint8_t lodiv;
	uint16_t fvco;
	uint8_t fbkdiv;
	uint16_t n;
	uint64_t tune_freq_hz;
	uint16_t p1nmsb;
	uint8_t p1nlsb;

	LOG("# config_synth_int\n");

	// Calculate n_lo
	uint8_t n_lo = 0;
	uint16_t x = LO_MAX / lo;
	while ((x > 1) && (n_lo < 5))
	{
		n_lo++;
		x >>= 1;
	}

	lodiv = 1 << n_lo;
	fvco = lodiv * lo;

	/* higher divider and charge pump current required above
	 * 3.2GHz. Programming guide says these values (fbkdiv, n,
	 * maybe pump?) can be changed back after enable in order to
	 * improve phase noise, since the VCO will already be stable
	 * and will be unaffected. */
	if (fvco > 3200)
	{
		fbkdiv = 4;
		set_RFFC507X_PLLCPL(dev, 3);
	}
	else
	{
		fbkdiv = 2;
		set_RFFC507X_PLLCPL(dev, 2);
	}

	uint64_t tmp_n = ((uint64_t)fvco << 29ULL) / (fbkdiv*REF_FREQ) ;
	n = tmp_n >> 29ULL;

	p1nmsb = (tmp_n >> 13ULL) & 0xffff;
	p1nlsb = (tmp_n >> 5ULL) & 0xff;

	tune_freq_hz = (REF_FREQ * (tmp_n >> 5ULL) * fbkdiv * FREQ_ONE_MHZ)
			/ (lodiv * (1 << 24ULL));
	printf("# lo=%d n_lo=%d lodiv=%d fvco=%d fbkdiv=%d n=%d tune_freq_hz=%d\n",
				lo, n_lo, lodiv, fvco, fbkdiv, n, tune_freq_hz);

	// Path 1
	set_RFFC507X_P1LODIV(dev, n_lo);
	set_RFFC507X_P1N(dev, n);
	set_RFFC507X_P1PRESC(dev, fbkdiv >> 1);
	set_RFFC507X_P1NMSB(dev, p1nmsb);
	set_RFFC507X_P1NLSB(dev, p1nlsb);

	// Path 2
	set_RFFC507X_P2LODIV(dev, n_lo);
	set_RFFC507X_P2N(dev, n);
	set_RFFC507X_P2PRESC(dev, fbkdiv >> 1);
	set_RFFC507X_P2NMSB(dev, p1nmsb);
	set_RFFC507X_P2NLSB(dev, p1nlsb);

	rffc507x_regs_commit(dev);

	return tune_freq_hz;
}

//===========================================================================
// !!!!!!!!!!! hz is currently ignored !!!!!!!!!!!
uint64_t rffc507x_set_frequency(rffc507x_st* dev, uint16_t mhz)
{
	uint32_t tune_freq;

	rffc507x_disable(dev);
	tune_freq = rffc507x_config_synth_int(dev, mhz);
	rffc507x_enable(dev);

	return tune_freq;
}

//===========================================================================
void rffc507x_set_gpo(rffc507x_st* dev, uint8_t gpo)
{
	// We set GPO for both paths just in case.
	set_RFFC507X_P1GPO(dev, gpo);
	set_RFFC507X_P2GPO(dev, gpo);

	rffc507x_regs_commit(dev);
}

