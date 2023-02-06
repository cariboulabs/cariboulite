/**
 * Declarations and definitions for Broadcom's Secondary Memory Interface
 *
 * Written by David Michaeli <cariboulabs.co@gmail.com>
 * Copyright (c) 2021, CaribouLabs.co
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2, as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SMI_STREAM_DEV_H_
#define _SMI_STREAM_DEV_H_

#include <linux/ioctl.h>

#ifndef __KERNEL__
	#include <stdint.h>
	#include <stdbool.h>
#else
	#define BCM2835_SMI_IMPLEMENTATION
	#include <linux/broadcom/bcm2835_smi.h>
#endif

#define DEVICE_NAME "smi-stream-dev"
#define DRIVER_NAME "smi-stream-dev"
#define DEVICE_MINOR 0

typedef enum
{
	smi_stream_dir_smi_to_device = 0,		// device data-bus is highZ
	smi_stream_dir_device_to_smi = 1,		// device data-bus is push-pull
} smi_stream_direction_en;

typedef enum
{
	smi_stream_channel_0 = 0,
	smi_stream_channel_1 = 1,
	smi_stream_channel_max,
} smi_stream_channel_en;

typedef enum
{
    smi_stream_idle = 0,
    smi_stream_rx_channel_0 = 1,
    smi_stream_rx_channel_1 = 2,
    smi_stream_tx = 3,
} smi_stream_state_en;

#ifdef __KERNEL__
struct bcm2835_smi_instance {
	struct device *dev;
	struct smi_settings settings;

	__iomem void *smi_regs_ptr;
	dma_addr_t smi_regs_busaddr;

	struct dma_chan *dma_chan;
	struct dma_slave_config dma_config;

	struct bcm2835_smi_bounce_info bounce;

	struct scatterlist buffer_sgl;

	struct clk *clk;

	/* Sometimes we are called into in an atomic context (e.g. by
	   JFFS2 + MTD) so we can't use a mutex */
	spinlock_t transaction_lock;
};
#endif // __KERNEL__

// Expansion of ioctls
#define SMI_STREAM_IOC_GET_NATIVE_BUF_SIZE	 	_IO(BCM2835_SMI_IOC_MAGIC,(BCM2835_SMI_IOC_MAX+1))
#define SMI_STREAM_IOC_SET_STREAM_STATUS	 	_IO(BCM2835_SMI_IOC_MAGIC,(BCM2835_SMI_IOC_MAX+2))
#define SMI_STREAM_IOC_SET_STREAM_IN_CHANNEL 	_IO(BCM2835_SMI_IOC_MAGIC,(BCM2835_SMI_IOC_MAX+3))

#endif /* _SMI_STREAM_DEV_H_ */
