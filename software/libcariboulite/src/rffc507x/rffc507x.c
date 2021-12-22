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

#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "RFFC5072"

#include <stdint.h>
#include <string.h>
#include <math.h>
#include "zf_log/zf_log.h"
#include "rffc507x.h"
#include "rffc507x_regs.h" // private register def macros

#if (defined DEBUG)
#include <stdio.h>
#define LOG printf
#else
#define LOG(x,...)
#endif

#define LO_MAX 5400
#define LO_MAX_HZ (LO_MAX*1e6)
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
	0x0000,   /* 15h / 21d <== SDI_CTRL - SDI Control        */
	0x0000,   /* 16h / 22d <== GPO - General Purpose Outputs */
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
	//printf("writing reg %d, value: %04X\n", r, dev->rffc507x_regs[r]);
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
	return 0;
}

//===========================================================================
// Set up all registers according to defaults specified in docs.
int rffc507x_init(  rffc507x_st* dev,
					io_utils_spi_st* io_spi)
{
	if (dev == NULL)
	{
		ZF_LOGE("input dev is NULL");
		return -1;
	}
	ZF_LOGI("Initializing RFFC507x driver");
	memcpy(dev->rffc507x_regs, rffc507x_regs_default, sizeof(dev->rffc507x_regs));
	dev->rffc507x_regs_dirty = 0x7fffffff;

	ZF_LOGI("Setting up device GPIOs");

	dev->io_spi = io_spi;

	/* Configure GPIO pins. */
	io_utils_setup_gpio(dev->reset_pin, io_utils_dir_output, io_utils_pull_up);

	/* set to known state */
	io_utils_write_gpio(dev->reset_pin, 0);
	io_utils_usleep(10000);
	io_utils_write_gpio(dev->reset_pin, 1);

	dev->io_spi_handle = io_utils_spi_add_chip(dev->io_spi, dev->cs_pin, 5000000, 0, 0,
                        						io_utils_spi_chip_type_rffc, NULL);

	ZF_LOGI("Received spi handle %d", dev->io_spi_handle);

	// initial setup
	// SDI CTRL set of configurations
	set_RFFC507X_SIPIN(dev, 1);	// ENBL and MODE physical pins are ignores
								// controlling their functionality from the 3-wire spi
								// interface
	
	set_RFFC507X_ENBL(dev, 0);	// The device is disabled
	set_RFFC507X_MODE(dev, 1);

	// put zeros in freq contol registers
	set_RFFC507X_P2VCOSEL(dev, 0);
	set_RFFC507X_CTMAX(dev, 127);
	set_RFFC507X_CTMIN(dev, 0);
	set_RFFC507X_P2CTV(dev, 12);	
	set_RFFC507X_P1CTV(dev, 12);
	set_RFFC507X_RGBYP(dev, 1);
	set_RFFC507X_P2MIXIDD(dev, 6);
	set_RFFC507X_P1MIXIDD(dev, 6);
	
	// Others
	set_RFFC507X_LDEN(dev, 1);
	set_RFFC507X_LDLEV(dev, 1);

	set_RFFC507X_BYPAS(dev, 0);

	// Write default register values to chip.
	int ret = rffc507x_regs_commit(dev);
	if (ret < 0)
	{
		ZF_LOGE("Failed commiting varibles into rffc507X");
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
		ZF_LOGE("Device pointer NULL - cannot release");
		return -1;
	}

	if (!dev->initialized)
	{
		ZF_LOGE("Device not initialized - cannot release");
		return 0;
	}

	dev->initialized = 0;

	io_utils_setup_gpio(dev->reset_pin, io_utils_dir_input, io_utils_pull_up);

	// Release the SPI device
	io_utils_spi_remove_chip(dev->io_spi, dev->io_spi_handle);

	ZF_LOGI("Device release completed");

	return 0;
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
	//if ((dev->rffc507x_regs_dirty >> r) & 0x1)
	//{
		io_utils_spi_transmit(dev->io_spi, dev->io_spi_handle, &vout, (uint8_t*)&(dev->rffc507x_regs[r]), 
							2, io_utils_spi_read);
	//}
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
void rffc507x_disable(rffc507x_st* dev)
{
	ZF_LOGD("rfc5071_disable");
	set_RFFC507X_ENBL(dev, 0);
	rffc507x_regs_commit(dev);
}

//===========================================================================
void rffc507x_enable(rffc507x_st* dev)
{
	ZF_LOGD("rfc5071_enable");
	set_RFFC507X_ENBL(dev, 1);
	rffc507x_regs_commit(dev);
}

//===========================================================================
void rffc507x_calculate_freq_params(double ref_freq_hz, uint8_t lodiv, double fvco, uint8_t fbkdiv, 
									uint16_t *n, uint16_t *p1nmsb, uint8_t *p1nlsb, double* act_freq_hz)
{
	double n_div = fvco / fbkdiv / ref_freq_hz;
	*n = (uint16_t)(n_div);

	double temp_p1nmsb = ( (double)(1<<16) ) * ( n_div - (double)(*n) );
	*p1nmsb = (uint16_t)(temp_p1nmsb) & 0xFFFF;
	*p1nlsb = (uint8_t)( round(( temp_p1nmsb- *p1nmsb ) * ((double)(1<<8)))) & 0xFF;
	//*p1nlsb = (uint8_t)( (( temp_p1nmsb- *p1nmsb ) * ((double)(1<<8)))) & 0xFF;

	uint32_t n_div24_bit = (uint32_t)(round(n_div * (1<<24))) & 0xFFFFFFFF;
	//uint32_t n_div24_bit = (uint32_t)((n_div * (1<<24))) & 0xFFFFFFFF;

	if (act_freq_hz) *act_freq_hz = (ref_freq_hz * n_div24_bit * fbkdiv) / ((double)(lodiv) * (double)(1<<24));
}

//===========================================================================
int rffc507x_setup_reference_freq(rffc507x_st* dev, double ref_freq_hz)
{
	if (ref_freq_hz < 10e6 || ref_freq_hz > 104e6)
	{
		return -1;
	}
	dev->ref_freq_hz = ref_freq_hz;
	return 0;
}

//===========================================================================
double rffc507x_set_frequency(rffc507x_st* dev, double lo_hz)
{
	uint8_t lodiv;
	double fvco;
	uint8_t fbkdiv;
	uint16_t n;
	double tune_freq_hz;
	uint16_t p1nmsb;
	uint8_t p1nlsb;

	rffc507x_disable(dev);

	// Calculate n_lo
	uint8_t n_lo = (uint8_t)log2(LO_MAX_HZ / lo_hz);
	lodiv = 1 << n_lo;							// lodiv = 2^(n_lo)
	fvco = lodiv * lo_hz;						// in Hz!

	if (fvco > 3200000000.0f)
	{
		fbkdiv = 4;
		set_RFFC507X_PLLCPL(dev, 3);
	}
	else
	{
		fbkdiv = 2;
		set_RFFC507X_PLLCPL(dev, 2);
	}

	rffc507x_calculate_freq_params(dev->ref_freq_hz, lodiv, fvco, fbkdiv, &n, &p1nmsb, &p1nlsb, &tune_freq_hz);

	//ZF_LOGD("----------------------------------------------------------");
	//ZF_LOGD("LO_HZ=%.2f n_lo=%d lodiv=%d", lo_hz, n_lo, lodiv);
	//ZF_LOGD("fvco=%.2f fbkdiv=%d n=%d", fvco, fbkdiv, n);
	//ZF_LOGD("frac=%d, p1nmsb=%d, p1nlsb=%d, tune_freq_hz=%.2f", p1nmsb<<8 | p1nlsb, p1nmsb, p1nlsb, tune_freq_hz);

	// Path 2
	set_RFFC507X_P2LODIV(dev, n_lo);
	set_RFFC507X_P2N(dev, n);
	set_RFFC507X_P2PRESC(dev, fbkdiv >> 1);
	set_RFFC507X_P2VCOSEL(dev, 2);
	set_RFFC507X_P2NMSB(dev, p1nmsb);
	set_RFFC507X_P2NLSB(dev, p1nlsb);

	rffc507x_regs_commit(dev);
	rffc507x_enable(dev);

	if (fbkdiv == 4)
	{
		// For optimum VCO phase noise the prescaler divider should be set to divide by 2. If the VCO frequency is 
		// greater than 3.2GHz, it is necessary to set the ratio to 4 to allow the CT_cal algorithm to work. 
		// After the device is enabled, the divider values can be reprogrammed with the prescaler divider ratio of 2 
		// and the new n, nummsb, and numlsb values. Taking the previous example of an LO of 314.159265MHz:
		fbkdiv = 2;
		rffc507x_calculate_freq_params(dev->ref_freq_hz, lodiv, fvco, fbkdiv, &n, &p1nmsb, &p1nlsb, &tune_freq_hz);

		//ZF_LOGD("LO_HZ=%.2f n_lo=%d lodiv=%d", lo_hz, n_lo, lodiv);
		//ZF_LOGD("fvco=%.2f fbkdiv=%d n=%d", fvco, fbkdiv, n);
		//ZF_LOGD("frac=%d, p1nmsb=%d, p1nlsb=%d, tune_freq_hz=%.2f", p1nmsb<<8 | p1nlsb, p1nmsb, p1nlsb, tune_freq_hz);
		//ZF_LOGD("----------------------------------------------------------");

		// Path 2
		set_RFFC507X_P2LODIV(dev, n_lo);
		set_RFFC507X_P2N(dev, n);
		set_RFFC507X_P2PRESC(dev, fbkdiv >> 1);
		set_RFFC507X_P2NMSB(dev, p1nmsb);
		set_RFFC507X_P2NLSB(dev, p1nlsb);
		rffc507x_regs_commit(dev);
	}

	return tune_freq_hz;
}

//===========================================================================
void rffc507x_readback(rffc507x_st* dev, uint16_t *readback_buff, int buf_len)
{
	if (buf_len > 16) buf_len = 16;

	for (int i = 0; i < buf_len; i++)
	{
		set_RFFC507X_READSEL(dev, i);
		rffc507x_regs_commit(dev);
		readback_buff[i] = rffc507x_reg_read(dev, RFFC507X_READBACK_REG);

		//printf ("READBACK #%d: %04X\n", i, readback_buff[i]);
	}
}

//===========================================================================
void rffc507x_readback_status(rffc507x_st* dev, rffc507x_device_id_st* dev_id,
                                                rffc507x_device_status_st* stat)
{
	uint16_t *dev_id_int = (uint16_t *)dev_id;
	uint16_t *stat_int = (uint16_t *)stat;

	if (dev_id_int)
	{
		set_RFFC507X_READSEL(dev, 0);
		rffc507x_regs_commit(dev);
		*dev_id_int = rffc507x_reg_read(dev, RFFC507X_READBACK_REG);
		//printf("%04X\n", *dev_id_int);
	}

	if (stat_int)
	{
		set_RFFC507X_READSEL(dev, 1);
		rffc507x_regs_commit(dev);
		*stat_int = rffc507x_reg_read(dev, RFFC507X_READBACK_REG);
		//printf("%04X\n", *stat_int);
	} 
}

//===========================================================================
void rffc507x_print_dev_id(rffc507x_device_id_st* dev_id)
{
	if (!dev_id) return;
	uint16_t *temp = (uint16_t*)dev_id;
	ZF_LOGI("RFFC507X DEVID: 0x%04X ID: 0x%04X, Rev: %d (%s)", *temp, 
										dev_id->device_id, dev_id->device_rev, 
										dev_id->device_rev==1?"RFFC507x":"RFFC507xA");
}

//===========================================================================
void rffc507x_print_stat(rffc507x_device_status_st* stat)
{
	if (!stat) return;
	uint16_t *temp = (uint16_t*)stat;
	ZF_LOGI("RFFC507X STAT: 0x%04X PLL_LOCK: %d, CT_CAL: %d, KV_CAL: %d, CT_CAL_FAIL: %d",
			*temp,
			stat->pll_lock, stat->coarse_tune_cal_value, 
			stat->kv_cal_value, stat->coarse_tune_cal_fail);
}


//===========================================================================
void rffc507x_set_gpo(rffc507x_st* dev, uint8_t gpo)
{
	// We set GPO for both paths just in case.
	set_RFFC507X_P1GPO(dev, gpo);
	set_RFFC507X_P2GPO(dev, gpo);

	rffc507x_regs_commit(dev);
}

//===========================================================================
void rffc507x_calibrate(rffc507x_st* dev)
{
	// CAL_TIME
	set_RFFC507X_WAIT(dev, 1); 	// If high then the RF sections are not enabled until the PLL calibrations complete
	set_RFFC507X_TCT(dev, 31);	// Duration of CT acquisition
	set_RFFC507X_CTAVG(dev, 3);	// Number of samples averaged to compute final value during CT Calibration: 
								//  0 = average 16 samples; 
								// 	1 = average 32 samples; 
								// 	2 = average 64 samples; 
								// 	3 = average 128 samples
	set_RFFC507X_KVAVG(dev, 3); // Number of samples averaged to compute final value during KV compensation
	rffc507x_regs_commit(dev);

	/*
	set_RFFC507X_P2CT (dev, 1);	// VCO coarse tune enable, path2 mode
	rffc507x_regs_commit(dev);
	*/

	set_RFFC507X_P1KV(dev, 0);
	set_RFFC507X_P2KV(dev, 0);
	rffc507x_regs_commit(dev);
}

//===========================================================================
void rffc507x_relock(rffc507x_st* dev)
{
	set_RFFC507X_RELOK(dev, 1);
	rffc507x_regs_commit(dev);
}

//===========================================================================
void rffc507x_output_lo(rffc507x_st* dev, int state)
{
	set_RFFC507X_BYPAS(dev, state);
}