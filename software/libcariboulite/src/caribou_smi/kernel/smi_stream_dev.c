/**
 * Character device driver for Broadcom Secondary Memory Interface
 * Streaming / Polling
 *
 * Based on char device by Luke Wren <luke@raspberrypi.org>
 * Copyright (c) 2015, Raspberry Pi (Trading) Ltd.
 * 
 * Written by David Michaeli (cariboulabs.co@gmail.com)
 * Copyright (c) 2022, CaribouLabs Ltd.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/aio.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/poll.h>

#include "smi_stream_dev.h"


#define FIFO_SIZE_MULTIPLIER 6

struct bcm2835_smi_dev_instance 
{
	struct device *dev;
	struct bcm2835_smi_instance *smi_inst;

	struct task_struct *reader_thread;
	struct kfifo rx_fifo;
	char* rx_buffer;
	bool streaming;
	struct mutex read_lock;
	struct mutex write_lock;
	wait_queue_head_t readable;
	wait_queue_head_t writeable;
};

static struct bcm2835_smi_dev_instance *inst;

static const char *const ioctl_names[] = 
{
	"READ_SETTINGS",
	"WRITE_SETTINGS",
	"ADDRESS",
	"GET_NATIVE_BUF_SIZE",
	"SET_NON_BLOCK_READ",
	"SET_NON_BLOCK_WRITE",
	"SET_STREAM_STATE"
};

/****************************************************************************
*
*   SMI LOW LEVEL
*
***************************************************************************/
/****************************************************************************
*
*   SMI clock manager setup
*
***************************************************************************/



/***************************************************************************/
static void write_smi_reg(struct bcm2835_smi_instance *inst, u32 val, unsigned reg)
{
	writel(val, inst->smi_regs_ptr + reg);
}

/***************************************************************************/
static u32 read_smi_reg(struct bcm2835_smi_instance *inst, unsigned reg)
{
	return readl(inst->smi_regs_ptr + reg);
}

#define BUSY_WAIT_WHILE_TIMEOUT(C,T,R) 			{int t = (T); while ((C) && t>0){t--;} (R)=t>0;}

/***************************************************************************/
static void smi_setup_clock(struct bcm2835_smi_instance *inst)
{
	/*uint32_t v = 0;
	dev_dbg(inst->dev, "Setting up clock...");
	// Disable SMI clock and wait for it to stop.
	write_smi_reg(inst, CM_PWD | 0, CM_SMI_CTL);
	while (read_smi_reg(inst, CM_SMI_CTL) & CM_SMI_CTL_BUSY) ;

	write_smi_reg(inst, CM_PWD | (1 << CM_SMI_DIV_DIVI_OFFS), CM_SMI_DIV);
	//write_smi_reg(inst, CM_PWD | (6 << CM_SMI_CTL_SRC_OFFS), CM_SMI_CTL);

	// Enable the clock
	v = read_smi_reg(inst, CM_SMI_CTL);
	write_smi_reg(inst, CM_PWD | v | CM_SMI_CTL_ENAB, CM_SMI_CTL);*/
}

/***************************************************************************/
static inline int smi_is_active(struct bcm2835_smi_instance *inst)
{
	return read_smi_reg(inst, SMICS) & SMICS_ACTIVE;
}

/***************************************************************************/
static inline int smi_enabled(struct bcm2835_smi_instance *inst)
{
	return read_smi_reg(inst, SMICS) & SMICS_ENABLE;
}

/***************************************************************************/
static int smi_disable(struct bcm2835_smi_instance *inst, enum dma_transfer_direction direction)
{
	// put smi in power save state while maintaining read/write capability from registers
	int smics_temp = read_smi_reg(inst, SMICS) & ~SMICS_ENABLE;
	int success = 0;

	if (direction == DMA_DEV_TO_MEM)
	{
		// RESET Write bit = setup a read sequence
		smics_temp &= ~SMICS_WRITE;
	}
	else
	{
		// SET Write bit = setup a write sequence
		smics_temp |= SMICS_WRITE;
	}
	write_smi_reg(inst, smics_temp, SMICS);

	//timeout = 100;
	//while ((read_smi_reg(inst, SMICS) & SMICS_ACTIVE) && timeout>0) {timeout --;}

	// wait till transfer state becomes '0' (not active)
	BUSY_WAIT_WHILE_TIMEOUT(smi_is_active(inst), 10000, success);
	if (!success) return -1;
	return 0;
}

/***************************************************************************/
static int smi_init_programmed_read(struct bcm2835_smi_instance *inst, int num_transfers)
{
	int smics_temp;
	int success = 0;

	/* Disable the peripheral: */
	smics_temp = read_smi_reg(inst, SMICS) & ~(SMICS_ENABLE | SMICS_WRITE);
	write_smi_reg(inst, smics_temp, SMICS);

	BUSY_WAIT_WHILE_TIMEOUT(smi_enabled(inst), 10000, success);
	if (!success)
	{
		return -1;
	}

	/* Program the transfer count: */
	write_smi_reg(inst, num_transfers, SMIL);

	/* re-enable and start: */
	smics_temp |= SMICS_ENABLE;
	write_smi_reg(inst, smics_temp, SMICS);
	smics_temp |= SMICS_CLEAR;

	/* Just to be certain: */
	mb();
	BUSY_WAIT_WHILE_TIMEOUT(smi_is_active(inst), 10000, success);
	if (!success)
	{
		return -1;
	}

	write_smi_reg(inst, smics_temp, SMICS);
	smics_temp |= SMICS_START;
	write_smi_reg(inst, smics_temp, SMICS);
	return 0;
}

/***************************************************************************/
static int smi_init_programmed_write(struct bcm2835_smi_instance *inst, int num_transfers)
{
	int smics_temp;
	int success = 0;

	/* Disable the peripheral: */
	smics_temp = read_smi_reg(inst, SMICS) & ~SMICS_ENABLE;
	write_smi_reg(inst, smics_temp, SMICS);

	BUSY_WAIT_WHILE_TIMEOUT(smi_enabled(inst), 10000, success);
	if (!success)
	{
		return -1;
	}

	/* Program the transfer count: */
	write_smi_reg(inst, num_transfers, SMIL);

	/* setup, re-enable and start: */
	smics_temp |= SMICS_WRITE | SMICS_ENABLE;
	write_smi_reg(inst, smics_temp, SMICS);
	smics_temp |= SMICS_START;
	write_smi_reg(inst, smics_temp, SMICS);
	return 0;
}


/****************************************************************************
*
*   SMI chardev file ops
*
***************************************************************************/
static long smi_stream_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;

	dev_info(inst->dev, "serving ioctl...");

	switch (cmd) 
	{
	//-------------------------------
	case BCM2835_SMI_IOC_GET_SETTINGS:
	{
		struct smi_settings *settings;

		dev_info(inst->dev, "Reading SMI settings to user.");
		settings = bcm2835_smi_get_settings_from_regs(inst->smi_inst);
		if (copy_to_user((void *)arg, settings, sizeof(struct smi_settings)))
		{
			dev_err(inst->dev, "settings copy failed.");
		}
		break;
	}
	//-------------------------------
	case BCM2835_SMI_IOC_WRITE_SETTINGS:
	{
		struct smi_settings *settings;

		dev_info(inst->dev, "Setting user's SMI settings.");
		settings = bcm2835_smi_get_settings_from_regs(inst->smi_inst);
		if (copy_from_user(settings, (void *)arg, sizeof(struct smi_settings)))
		{
			dev_err(inst->dev, "settings copy failed.");
		}
		else
		{
			bcm2835_smi_set_regs_from_settings(inst->smi_inst);
		}
		break;
	}
	//-------------------------------
	case BCM2835_SMI_IOC_ADDRESS:
	{
		dev_info(inst->dev, "SMI address set: 0x%02x", (int)arg);
		bcm2835_smi_set_address(inst->smi_inst, arg);
		break;
	}
	//-------------------------------
	case SMI_STREAM_IOC_GET_NATIVE_BUF_SIZE:
	{
		size_t size = (size_t)(DMA_BOUNCE_BUFFER_SIZE);
		dev_info(inst->dev, "Reading native buffer size information");
		if (copy_to_user((void *)arg, &size, sizeof(size_t)))
		{
			dev_err(inst->dev, "buffer sizes copy failed.");
		}
		break;
	}
	//-------------------------------
	case SMI_STREAM_IOC_SET_STREAM_STATUS:
	{
		inst->streaming = arg;
		dev_info(inst->dev, "Set STREAMING_STATUS = %d", inst->streaming);
		break;
	}
	default:
		dev_err(inst->dev, "invalid ioctl cmd: %d", cmd);
		ret = -ENOTTY;
		break;
	}

	return ret;
}

/***************************************************************************/
static struct dma_async_tx_descriptor *stream_smi_dma_submit_sgl(struct bcm2835_smi_instance *inst,
																struct scatterlist *sgl,
																size_t sg_len,
																enum dma_transfer_direction dir,
																dma_async_tx_callback callback)
{
	struct dma_async_tx_descriptor *desc = NULL;

	//printk(KERN_ERR DRIVER_NAME": SUBMIT_PREP %lu\n", (long unsigned int)(inst->dma_chan));
	desc = dmaengine_prep_slave_sg(inst->dma_chan,
				       sgl,
				       sg_len,
				       dir,
				       DMA_PREP_INTERRUPT | DMA_CTRL_ACK | DMA_PREP_FENCE);
	if (!desc) 
	{
		unsigned int timeout = 10000U;
		dev_err(inst->dev, "read_sgl: dma slave preparation failed!");
		write_smi_reg(inst, read_smi_reg(inst, SMICS) & ~SMICS_ACTIVE, 	SMICS);
		while ((read_smi_reg(inst, SMICS) & SMICS_ACTIVE) && (timeout--)>0)
		{
			cpu_relax();
		}
		dev_err(inst->dev, "read_sgl: SMICS_ACTIVE didn't fall");
		write_smi_reg(inst, read_smi_reg(inst, SMICS) | SMICS_ACTIVE, SMICS);
		return NULL;
	}
	//printk(KERN_ERR DRIVER_NAME": CB\n");
	desc->callback = callback;
	desc->callback_param = inst;

	//printk(KERN_ERR DRIVER_NAME": SUBMIT_DESC\n");
	if (dmaengine_submit(desc) < 0)
	{
		return NULL;
	}
	return desc;
}

//static int stream_smi_dma_callback_user_copy_count = 0;

/***************************************************************************/
static void stream_smi_dma_callback_user_copy(void *param)
{
	/* Notify the bottom half that a chunk is ready for user copy */
	struct bcm2835_smi_instance *inst = (struct bcm2835_smi_instance *)param;
	//stream_smi_dma_callback_user_copy_count ++;
	up(&inst->bounce.callback_sem);
}

/***************************************************************************/
ssize_t stream_smi_user_dma(	struct bcm2835_smi_instance *inst,
								enum dma_transfer_direction dma_dir,
								char __user *user_ptr,
								struct bcm2835_smi_bounce_info **bounce)
{
	struct scatterlist *sgl = NULL;

	spin_lock(&inst->transaction_lock);
	//printk(KERN_ERR DRIVER_NAME": SMI-DISABLE\n");
	/*if (smi_disable(inst, dma_dir) != 0)
	{
		dev_err(inst->dev, "smi_disable failed");
		return 0;
	}*/

	//printk(KERN_ERR DRIVER_NAME": SEMA-INIT\n");
	sema_init(&inst->bounce.callback_sem, 0);

	//printk(KERN_ERR DRIVER_NAME": BOUNCE\n");

	if (bounce)
	{
		*bounce = &(inst->bounce);
	}

	//printk(KERN_ERR DRIVER_NAME": SGL\n");
	sgl = &(inst->bounce.sgl[0]);
	if (sgl == NULL)
	{
		dev_err(inst->dev, "sgl is NULL");
		spin_unlock(&inst->transaction_lock);
		return 0;
	}
	
	//printk(KERN_ERR DRIVER_NAME": SUBMIT SGL\n");
	if (!stream_smi_dma_submit_sgl(inst, sgl, 1, dma_dir, stream_smi_dma_callback_user_copy)) 
	{
		dev_err(inst->dev, "sgl submit failed");
		spin_unlock(&inst->transaction_lock);
		return 0;
	}
	//printk(KERN_ERR DRIVER_NAME": ASYNC PENDING\n");
	dma_async_issue_pending(inst->dma_chan);

	//printk(KERN_ERR DRIVER_NAME": PROGRAMMED READ\n");

	// we have only 8 bit width - << here we got a stall >>
	if (dma_dir == DMA_DEV_TO_MEM)
	{
		if (smi_init_programmed_read(inst, DMA_BOUNCE_BUFFER_SIZE) != 0)
		{
			//dev_err(inst->dev, "smi_init_programmed_read failed");
			spin_unlock(&inst->transaction_lock);
			return 0;
		}
	}
	else 
	{
		if (smi_init_programmed_write(inst, DMA_BOUNCE_BUFFER_SIZE) != 0)
		{
			//dev_err(inst->dev, "smi_init_programmed_write failed");
			spin_unlock(&inst->transaction_lock);
			return 0;
		}
	}
	
	//printk(KERN_ERR DRIVER_NAME": SPIN-UNLOCK\n");
	spin_unlock(&inst->transaction_lock);
	return DMA_BOUNCE_BUFFER_SIZE;
}

/***************************************************************************/
int reader_thread_stream_function(void *pv) 
{
	int count = 0;
	struct bcm2835_smi_bounce_info *bounce = NULL;

	dev_info(inst->dev, "Enterred reader thread");

	while(!kthread_should_stop())
	{
		if (!inst->streaming)
		{
			msleep(20);
			continue;
		}

		//dev_info(inst->dev, "STREAM_SMI_USER_DMA count = %d", stream_smi_dma_callback_user_copy_count);

		count = stream_smi_user_dma(inst->smi_inst, DMA_DEV_TO_MEM, inst->rx_buffer, &bounce);
		if (count != DMA_BOUNCE_BUFFER_SIZE || bounce == NULL)
		{
			//dev_err(inst->dev, "reader_thread return illegal count = %d", count);
			continue;
		}

		// Wait for current chunk to complete:
		if (down_timeout(&bounce->callback_sem, msecs_to_jiffies(1000))) 
		{
			dev_info(inst->dev, "DMA bounce timed out");
			spin_lock(&inst->smi_inst->transaction_lock);
			dmaengine_terminate_sync(inst->smi_inst->dma_chan);
			//dev_info(inst->dev, "dmaengine_terminate_sync");
			spin_unlock(&inst->smi_inst->transaction_lock);
			continue;
		}

		if (mutex_lock_interruptible(&inst->read_lock))
		{
			return -EINTR;
		}
		kfifo_in(&inst->rx_fifo, bounce->buffer[0], DMA_BOUNCE_BUFFER_SIZE);
		mutex_unlock(&inst->read_lock);
		
		wake_up_interruptible(&inst->readable);
	}

	dev_info(inst->dev, "Left reader thread");
	return 0; 
}

/***************************************************************************/
static int smi_stream_open(struct inode *inode, struct file *file)
{
	int ret;
	int dev = iminor(inode);

	dev_dbg(inst->dev, "SMI device opened.");

	if (dev != DEVICE_MINOR) 
	{
		dev_err(inst->dev, "smi_stream_open: Unknown minor device: %d", dev);		// error here
		return -ENXIO;
	}

	inst->rx_buffer = kmalloc(DMA_BOUNCE_BUFFER_SIZE, GFP_KERNEL);
	if (!inst->rx_buffer)
	{
		return -ENOMEM;
	}

	// create the dataqueue
	ret = kfifo_alloc(&inst->rx_fifo, FIFO_SIZE_MULTIPLIER * DMA_BOUNCE_BUFFER_SIZE, GFP_KERNEL);
	if (ret)
	{
		printk(KERN_ERR DRIVER_NAME": error kfifo_alloc\n");
		kfree(inst->rx_buffer);
		return ret;
	}

	inst->streaming = 0;
	// Create the reader stream
	inst->reader_thread = kthread_create(reader_thread_stream_function, NULL, "smi-reader-thread"); 
	if(IS_ERR(inst->reader_thread))
	{
		printk(KERN_ERR DRIVER_NAME": reader_thread creation failed - kthread\n");
		ret = PTR_ERR(inst->reader_thread);
		inst->reader_thread = NULL;
		kfifo_free(&inst->rx_fifo);
		kfree(inst->rx_buffer);
		inst->rx_buffer = NULL;
		return ret;
	} 
	wake_up_process(inst->reader_thread); 
	
	return 0;
}

/***************************************************************************/
static int smi_stream_release(struct inode *inode, struct file *file)
{
	int dev = iminor(inode);

	dev_info(inst->dev, "smi_stream_release: closing device: %d", dev);

	if (dev != DEVICE_MINOR) 
	{
		dev_err(inst->dev, "smi_stream_release: Unknown minor device %d", dev);
		return -ENXIO;
	}

	inst->streaming = 0;
	if (inst->reader_thread != NULL) kthread_stop(inst->reader_thread);
	kfifo_free(&inst->rx_fifo);
	if (inst->rx_buffer != NULL) kfree(inst->rx_buffer);

	return 0;
}

/***************************************************************************/
static ssize_t dma_bounce_user(	enum dma_transfer_direction dma_dir,
								char __user *user_ptr,
								size_t count,
								struct bcm2835_smi_bounce_info *bounce)
{
	int chunk_size;
	int chunk_no = 0;
	int count_left = count;

	while (count_left) 
	{
		int rv;
		void *buf;

		/* Wait for current chunk to complete: */
		if (down_timeout(&bounce->callback_sem, msecs_to_jiffies(1000))) 
		{
			dev_err(inst->dev, "DMA bounce timed out");
			count -= (count_left);
			break;
		}

		if ( bounce->callback_sem.count >= (DMA_BOUNCE_BUFFER_COUNT - 1) )
		{
			dev_err(inst->dev, "WARNING: Ring buffer overflow");
		}

		chunk_size = count_left > DMA_BOUNCE_BUFFER_SIZE ? DMA_BOUNCE_BUFFER_SIZE : count_left;
		buf = bounce->buffer[chunk_no % DMA_BOUNCE_BUFFER_COUNT];

		if (dma_dir == DMA_DEV_TO_MEM)
		{
			rv = copy_to_user(user_ptr, buf, chunk_size);
		}
		else
		{
			rv = copy_from_user(buf, user_ptr, chunk_size);
		}
		
		if (rv)
		{
			dev_err(inst->dev, "copy_*_user() failed!: %d", rv);
		}

		user_ptr += chunk_size;
		count_left -= chunk_size;
		chunk_no++;
	}
	return count;
}

/***************************************************************************/
static ssize_t smi_stream_read_file_fifo(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	unsigned int copied;

	int num_bytes = 0;
	size_t count_actual = count;
	
	if (kfifo_is_empty(&inst->rx_fifo))
	{
		return -EAGAIN;
		if (file->f_flags & O_NONBLOCK)
		{
			return -EAGAIN;
		}
		else 
		{
			return 0;
			//pr_debug("%s\n", "waiting");

			ret = wait_event_interruptible(inst->readable, !kfifo_is_empty(&inst->rx_fifo));
			if (ret == -ERESTARTSYS) 
			{
				pr_err("interrupted\n");
				return -EINTR;
			}
		}
	}

	if (mutex_lock_interruptible(&inst->read_lock))
	{
		return -EINTR;
	}
	num_bytes = kfifo_len (&inst->rx_fifo);
	count_actual = num_bytes > count ? count : num_bytes;
	ret = kfifo_to_user(&inst->rx_fifo, buf, count_actual, &copied);
	mutex_unlock(&inst->read_lock);

	return ret ? ret : copied;
}

/***************************************************************************/
static ssize_t smi_stream_write_file(struct file *f, const char __user *user_ptr, size_t count, loff_t *offs)
{
	int odd_bytes;
	size_t count_check;

	dev_dbg(inst->dev, "User writing %zu bytes to SMI.", count);
	if (count > DMA_THRESHOLD_BYTES)
	{
		odd_bytes = count & 0x3;
	}
	else
	{
		odd_bytes = count;
	}

	count -= odd_bytes;
	count_check = count;
	if (count) 
	{
		struct bcm2835_smi_bounce_info *bounce;

		count = bcm2835_smi_user_dma(inst->smi_inst, DMA_MEM_TO_DEV, (char __user *)user_ptr, count, &bounce);
		if (count)
		{
			count = dma_bounce_user(DMA_MEM_TO_DEV, (char __user *)user_ptr, count, bounce);
		}
	}
	if (odd_bytes && (count == count_check)) 
	{
		uint8_t buf[DMA_THRESHOLD_BYTES];
		unsigned long bytes_not_transferred;

		bytes_not_transferred = copy_from_user(buf, user_ptr + count, odd_bytes);
	
		if (bytes_not_transferred)
		{
			dev_err(inst->dev, "copy_from_user() failed.");
		}
		else
		{
			bcm2835_smi_write_buf(inst->smi_inst, buf, odd_bytes);
		}
		count += odd_bytes - bytes_not_transferred;
	}
	return count;
}

/***************************************************************************/
static unsigned int smi_stream_poll(struct file *file, poll_table *pt)
{
	unsigned int mask = 0;
	poll_wait(file, &inst->readable, pt);
	//poll_wait(file, &inst->writeable, pt);

	if (!kfifo_is_empty(&inst->rx_fifo))
	{
		mask |= POLLIN | POLLRDNORM;
	}
	//mask |= POLLOUT | POLLWRNORM;
	return mask;
}

/***************************************************************************/
static const struct file_operations smi_stream_fops = 
{
	.owner = THIS_MODULE,
	.unlocked_ioctl = smi_stream_ioctl,
	.open = smi_stream_open,
	.release = smi_stream_release,
	.read = smi_stream_read_file_fifo,
	.write = smi_stream_write_file,
	.poll = smi_stream_poll,
};

/****************************************************************************
*
*   smi_stream_probe - called when the driver is loaded.
*
***************************************************************************/
static void smi_stream_print_smi_inst(struct bcm2835_smi_instance* inst)
{
	uint8_t * buff_temp = NULL;
	int kk = 0;

	// print out the SMI instance data
	printk("sizeof bool %d, smi_settings %d, void* %d, dma_addr_t %d, int %d, device* %d", 
			sizeof(bool), sizeof(struct smi_settings), sizeof(void*), sizeof(dma_addr_t), sizeof(int), sizeof(struct device*));
	buff_temp = (void*)inst;
	for (kk = 0; kk < sizeof(struct bcm2835_smi_instance); kk++)
	{
		if (kk%32 == 0) printk(" ");
		printk(KERN_CONT"%02X ", buff_temp[kk]);
	}

	printk(">> struct device *dev = %016llx", *((uint64_t*)&inst->dev));
    printk(">> SMI SETTINGS:\n");
    printk(">>     width: %d\n", inst->settings.data_width);
    printk(">>     pack: %c\n", inst->settings.pack_data ? 'Y' : 'N');
    printk(">>     read setup: %d, strobe: %d, hold: %d, pace: %d\n", inst->settings.read_setup_time, inst->settings.read_strobe_time, inst->settings.read_hold_time, inst->settings.read_pace_time);
    printk(">>     write setup: %d, strobe: %d, hold: %d, pace: %d\n", inst->settings.write_setup_time, inst->settings.write_strobe_time, inst->settings.write_hold_time, inst->settings.write_pace_time);
    printk(">>     dma enable: %c, passthru enable: %c\n", inst->settings.dma_enable ? 'Y':'N', inst->settings.dma_passthrough_enable ? 'Y':'N');
    printk(">>     dma threshold read: %d, write: %d\n", inst->settings.dma_read_thresh, inst->settings.dma_write_thresh);
    printk(">>     dma panic threshold read: %d, write: %d\n", inst->settings.dma_panic_read_thresh, inst->settings.dma_panic_write_thresh);
	printk(">> iomem* smi_regs_ptr = %016llx", *((uint64_t*)&inst->smi_regs_ptr));
	printk(">> dma_addr_t smi_regs_busaddr = %016llx", *((uint64_t*)&inst->smi_regs_busaddr));
	printk(">> dma_chan *dma_chan = %016llx", *((uint64_t*)&inst->dma_chan));
	printk(">> dma_config.direction = %d", inst->dma_config.direction);
	printk(">> dma_config.src_addr = %016llx", *((uint64_t*)&inst->dma_config.src_addr));
	printk(">> dma_config.dst_addr = %016llx", *((uint64_t*)&inst->dma_config.dst_addr));
	printk(">> dma_config.src_addr_width = %d", inst->dma_config.src_addr_width);
	printk(">> dma_config.dst_addr_width = %d", inst->dma_config.dst_addr_width);
	printk(">> dma_config.src_maxburst = %d", inst->dma_config.src_maxburst);
	printk(">> dma_config.dst_maxburst = %d", inst->dma_config.dst_maxburst);
	printk(">> dma_config.src_port_window_size = %d", inst->dma_config.src_port_window_size);
	printk(">> dma_config.dst_port_window_size = %d", inst->dma_config.dst_port_window_size);
	printk(">> dma_config.device_fc = %d", inst->dma_config.device_fc);
	printk(">> dma_config.slave_id = %d", inst->dma_config.slave_id);
	printk(">> dma_config.clk = %016llx", *((uint64_t*)&inst->clk));

	//struct bcm2835_smi_bounce_info bounce;

	//struct scatterlist buffer_sgl;

}

static struct cdev smi_stream_cdev;
static dev_t smi_stream_devid;
static struct class *smi_stream_class;
static struct device *smi_stream_dev;

static int smi_stream_dev_probe(struct platform_device *pdev)
{
	int err;
	void *ptr_err;
	struct device *dev = &pdev->dev;
	struct device_node *smi_node;

	

	printk(KERN_INFO DRIVER_NAME": smi_stream_dev_probe()\n");

	if (!dev->of_node) 
	{
		dev_err(dev, "No device tree node supplied!");
		return -EINVAL;
	}

	smi_node = of_parse_phandle(dev->of_node, "smi_handle", 0);
	if (!smi_node) 
	{
		dev_err(dev, "No such property: smi_handle");
		return -ENXIO;
	}

	// Allocate buffers and instance data (of type struct bcm2835_smi_dev_instance)
	inst = devm_kzalloc(dev, sizeof(*inst), GFP_KERNEL);
	if (!inst)
	{
		return -ENOMEM;
	}

	inst->smi_inst = bcm2835_smi_get(smi_node);
	if (!inst->smi_inst)
	{
		return -EPROBE_DEFER;
	}

	smi_stream_print_smi_inst(inst->smi_inst);

	inst->dev = dev;

	/* Create character device entries */
	err = alloc_chrdev_region(&smi_stream_devid, DEVICE_MINOR, 1, DEVICE_NAME);
	if (err != 0) 
	{
		dev_err(inst->dev, "unable to allocate device number");
		return -ENOMEM;
	}

	cdev_init(&smi_stream_cdev, &smi_stream_fops);
	smi_stream_cdev.owner = THIS_MODULE;
	err = cdev_add(&smi_stream_cdev, smi_stream_devid, 1);
	if (err != 0) 
	{
		dev_err(inst->dev, "unable to register device");
		err = -ENOMEM;
		unregister_chrdev_region(smi_stream_devid, 1);
		dev_err(dev, "could not load smi_stream_dev");
		return err;
	}

	/* Create sysfs entries */
	smi_stream_class = class_create(THIS_MODULE, DEVICE_NAME);
	ptr_err = smi_stream_class;
	if (IS_ERR(ptr_err))
	{
		cdev_del(&smi_stream_cdev);
		unregister_chrdev_region(smi_stream_devid, 1);
		dev_err(dev, "could not load smi_stream_dev");
		return PTR_ERR(ptr_err);
	}

	printk(KERN_INFO DRIVER_NAME": creating a device and registering it with sysfs\n");
	smi_stream_dev = device_create(smi_stream_class, 	// pointer to the struct class that this device should be registered to
					NULL,								// pointer to the parent struct device of this new device, if any
					smi_stream_devid, 					// the dev_t for the char device to be added
					NULL,								// the data to be added to the device for callbacks
					"smi");								// string for the device's name

	ptr_err = smi_stream_dev;
	if (IS_ERR(ptr_err)) 
	{
		class_destroy(smi_stream_class);
		cdev_del(&smi_stream_cdev);
		unregister_chrdev_region(smi_stream_devid, 1);
		dev_err(dev, "could not load smi_stream_dev");
		return PTR_ERR(ptr_err);
	}

	smi_setup_clock(inst->smi_inst);

	// Streaming instance initializations
	inst->reader_thread = NULL;
	inst->rx_buffer = NULL;
	init_waitqueue_head(&inst->readable);
	init_waitqueue_head(&inst->writeable);
	mutex_init(&inst->read_lock);
	mutex_init(&inst->write_lock);

	dev_info(inst->dev, "initialised");

	return 0;
}

/****************************************************************************
*
*   smi_stream_remove - called when the driver is unloaded.
*
***************************************************************************/

static int smi_stream_dev_remove(struct platform_device *pdev)
{
	device_destroy(smi_stream_class, smi_stream_devid);
	class_destroy(smi_stream_class);
	cdev_del(&smi_stream_cdev);
	unregister_chrdev_region(smi_stream_devid, 1);

	dev_info(inst->dev, DRIVER_NAME": smi-stream dev removed");
	return 0;
}

/****************************************************************************
*
*   Register the driver with device tree
*
***************************************************************************/

static const struct of_device_id smi_stream_dev_of_match[] = {
	{.compatible = "brcm,bcm2835-smi-dev",},
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, smi_stream_dev_of_match);

static struct platform_driver smi_stream_dev_driver = {
	.probe = smi_stream_dev_probe,
	.remove = smi_stream_dev_remove,
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = smi_stream_dev_of_match,
		   },
};

module_platform_driver(smi_stream_dev_driver);

//MODULE_INFO(intree, "Y");
MODULE_ALIAS("platform:smi-stream-dev");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Character device driver for BCM2835's secondary memory interface streaming mode");
MODULE_AUTHOR("David Michaeli <cariboulabs.co@gmail.com>");
