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
	#include "bcm2835_smi.h"
#else
	#include <linux/broadcom/bcm2835_smi.h>
#endif

// Expansion of ioctls
#define SMI_STREAM_IOC_GET_NATIVE_BUF_SIZE	 _IO(BCM2835_SMI_IOC_MAGIC, (BCM2835_SMI_IOC_MAX+1))
#define SMI_STREAM_IOC_SET_NON_BLOCK_READ	 _IO(BCM2835_SMI_IOC_MAGIC, (BCM2835_SMI_IOC_MAX+2))
#define SMI_STREAM_IOC_SET_NON_BLOCK_WRITE	 _IO(BCM2835_SMI_IOC_MAGIC, (BCM2835_SMI_IOC_MAX+3))
#define SMI_STREAM_IOC_SET_STREAM_STATUS	 _IO(BCM2835_SMI_IOC_MAGIC, (BCM2835_SMI_IOC_MAX+4))

#endif /* _SMI_STREAM_DEV_H_ */
