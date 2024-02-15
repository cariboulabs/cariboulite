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
#include <linux/init.h>

#include "smi_stream_dev.h"


// MODULE SPECIFIC PARAMETERS
// the modules.d line is as follows: "options smi_stream_dev fifo_mtu_multiplier=6 addr_dir_offset=2 addr_ch_offset=3"
static int              fifo_mtu_multiplier = 6;// How many MTUs to allocate for kfifo's
static int              addr_dir_offset = 2;    // GPIO_SA[4:0] offset of the channel direction
static int              addr_ch_offset = 3;     // GPIO_SA[4:0] offset of the channel select

module_param(fifo_mtu_multiplier, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
module_param(addr_dir_offset, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
module_param(addr_ch_offset, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

MODULE_PARM_DESC(fifo_mtu_multiplier, "the number of MTUs (N*MTU_SIZE) to allocate for kfifo's (default 6) valid: [3..19]");
MODULE_PARM_DESC(addr_dir_offset, "GPIO_SA[4:0] offset of the channel direction (default cariboulite 2), valid: [0..4] or (-1) if unused");
MODULE_PARM_DESC(addr_ch_offset, "GPIO_SA[4:0] offset of the channel select (default cariboulite 3), valid: [0..4] or (-1) if unused");

struct bcm2835_smi_dev_instance 
{
	struct device *dev;
	struct bcm2835_smi_instance *smi_inst;

	// address related
	unsigned int cur_address;
    int address_changed;
    
    // flags
    int invalidate_rx_buffers;
    int invalidate_tx_buffers;
	
	struct task_struct *reader_thread;
	struct task_struct *writer_thread;
	struct kfifo rx_fifo;
	struct kfifo tx_fifo;
	smi_stream_state_en state;
	struct mutex read_lock;
	struct mutex write_lock;
	wait_queue_head_t poll_event;
	bool readable;
	bool writeable;
    bool reader_thread_running;
    bool writer_thread_running;
    bool reader_waiting_sema;
    bool writer_waiting_sema;
};


// Prototypes
ssize_t stream_smi_user_dma(	struct bcm2835_smi_instance *inst,
								enum dma_transfer_direction dma_dir,
								struct bcm2835_smi_bounce_info **bounce, 
                                int buff_num);
int reader_thread_stream_function(void *pv);
int writer_thread_stream_function(void *pv);


static struct bcm2835_smi_dev_instance *inst = NULL;

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


#define BUSY_WAIT_WHILE_TIMEOUT(C,T,R) 			{int t = (T); while ((C) && t>0){t--;} (R)=t>0;}

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


static unsigned int calc_address_from_state(smi_stream_state_en state)
{
	unsigned int return_val = (smi_stream_dir_device_to_smi<<addr_dir_offset) | (smi_stream_channel_0<<addr_ch_offset);
	if (state == smi_stream_rx_channel_0)
    {
        return_val = (smi_stream_dir_device_to_smi<<addr_dir_offset) | (smi_stream_channel_0<<addr_ch_offset);
    }
    else if (state == smi_stream_rx_channel_1)
    {
        return_val = (smi_stream_dir_device_to_smi<<addr_dir_offset) | (smi_stream_channel_1<<addr_ch_offset);
    }
    else if (state == smi_stream_tx_channel)
    { 
        return_val = smi_stream_dir_smi_to_device<<addr_dir_offset;
    }
    else
    {
    }
    return return_val;
}

static void set_state(smi_stream_state_en state)
{
    if (inst == NULL) return;
    
    if (state == smi_stream_rx_channel_0)
    {
        inst->cur_address = (smi_stream_dir_device_to_smi<<addr_dir_offset) | (smi_stream_channel_0<<addr_ch_offset);
    }
    else if (state == smi_stream_rx_channel_1)
    {
        inst->cur_address = (smi_stream_dir_device_to_smi<<addr_dir_offset) | (smi_stream_channel_1<<addr_ch_offset);
    }
    else if (state == smi_stream_tx_channel)
    { 
        inst->cur_address = smi_stream_dir_smi_to_device<<addr_dir_offset;
    }
    else
    {
    }
    
    if (inst->state != state)
    {
        dev_info(inst->dev, "Set STREAMING_STATUS = %d, cur_addr = %d", state, inst->cur_address);
        inst->address_changed = 1;
        

    }
    
    inst->state = state;
}

/***************************************************************************/
static void smi_setup_clock(struct bcm2835_smi_instance *inst)
{
	
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


static int smi_disable_sync(struct bcm2835_smi_instance *smi_inst)
{
	int smics_temp;
	int success = 0;
	/* Disable the peripheral: */
	smics_temp = read_smi_reg(smi_inst, SMICS) & ~(SMICS_ENABLE | SMICS_WRITE);
	write_smi_reg(smi_inst, smics_temp, SMICS);
	// wait for the ENABLE to go low
	BUSY_WAIT_WHILE_TIMEOUT(smi_enabled(smi_inst), 1000000U, success);
	if (!success)
	{
		return -1;
	}
	return 0;
	
}

/***************************************************************************/
static int smi_init_programmed_read(struct bcm2835_smi_instance *smi_inst, int num_transfers)
{
	int smics_temp;
	int success = 0;

	if(smi_is_active(smi_inst))
	{
		
		write_smi_reg(smi_inst, 4*num_transfers, SMIL);
		return 0;
	}
	
	
	write_smi_reg(inst->smi_inst, 0, SMIL);
	
	
	smics_temp = read_smi_reg(smi_inst, SMICS);
	/* Program the transfer count: */
	write_smi_reg(smi_inst, 4*num_transfers, SMIL);

	/* re-enable and start: */
	smics_temp |= SMICS_ENABLE;
	write_smi_reg(smi_inst, smics_temp, SMICS);

	smics_temp |= SMICS_CLEAR;

	/* IO barrier - to be sure that the last request have
	   been dispatched in the correct order
	*/
	mb();
	// busy wait as long as the transaction is active (taking place)
	BUSY_WAIT_WHILE_TIMEOUT(smi_is_active(smi_inst), 1000000U, success);
	if (!success)
	{
		return -2;
	}
	// Clear the FIFO (reset it to zero contents)
	write_smi_reg(smi_inst, smics_temp, SMICS);

	// Start the transaction
	smics_temp |= SMICS_START;
	write_smi_reg(smi_inst, smics_temp, SMICS);
	return 0;
}

/***************************************************************************/
static int smi_init_programmed_write(struct bcm2835_smi_instance *smi_inst, int num_transfers)
{
	int smics_temp;
	int success = 0; 
   
	/* Disable the peripheral: */
	smics_temp = read_smi_reg(smi_inst, SMICS) & ~SMICS_ENABLE;
	write_smi_reg(smi_inst, smics_temp, SMICS);

	// Wait as long as the SMI is still enabled
	BUSY_WAIT_WHILE_TIMEOUT(smi_enabled(smi_inst), 100000U, success);
	if (!success)
	{
		return -1;
	}

	/* Program the transfer count: */
	write_smi_reg(smi_inst, num_transfers, SMIL);

	/* setup, re-enable and start: */
	smics_temp |= SMICS_WRITE | SMICS_ENABLE;
	write_smi_reg(smi_inst, smics_temp, SMICS);

	smics_temp |= SMICS_START;
	write_smi_reg(smi_inst, smics_temp, SMICS);
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

	//dev_info(inst->dev, "serving ioctl...");

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
		//bcm2835_smi_set_address(inst->smi_inst, arg);
		break;
	}
	//-------------------------------
	case SMI_STREAM_IOC_SET_STREAM_IN_CHANNEL:
	{
		//dev_info(inst->dev, "SMI channel: 0x%02x", (int)arg);
		//set_address_channel((smi_stream_channel_en)arg);
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
        set_state((smi_stream_state_en)arg);
		
		break;
	}
        //-------------------------------
	case SMI_STREAM_IOC_SET_FIFO_MULT:
	{
        int temp = (int)arg;
        if (temp > 20 || temp < 2)
        {
            dev_err(inst->dev, "Parameter error: 2<fifo_mtu_multiplier<20, got %d", temp);
            return -EINVAL;
        }
        dev_info(inst->dev, "Setting FIFO size multiplier to %d", temp);
        fifo_mtu_multiplier = temp;
		break;
	}
    //-------------------------------
	case SMI_STREAM_IOC_SET_ADDR_DIR_OFFSET:
	{
        int temp = (int)arg;
        if (temp > 4 || temp < -1)
        {
            dev_err(inst->dev, "Parameter error: 0<=addr_dir_offset<=4 or (-1 - unused), got %d", temp);
            return -EINVAL;
        }
        dev_info(inst->dev, "Setting address direction indication offset to %d", temp);
        addr_dir_offset = temp;
		break;
	}
    //-------------------------------
	case SMI_STREAM_IOC_SET_ADDR_CH_OFFSET:
	{
        int temp = (int)arg;
        if (temp > 4 || temp < -1)
        {
            dev_err(inst->dev, "Parameter error: 0<=addr_ch_offset<=4 or (-1 - unused), got %d", temp);
            return -EINVAL;
        }
        dev_info(inst->dev, "Setting address channel indication offset to %d", temp);
        addr_ch_offset = temp;
		break;
	}
    
    //-------------------------------
	case SMI_STREAM_IOC_GET_FIFO_MULT:
	{
		dev_dbg(inst->dev, "Reading FIFO size multiplier of %d", fifo_mtu_multiplier);
		if (copy_to_user((void *)arg, &fifo_mtu_multiplier, sizeof(fifo_mtu_multiplier)))
		{
			dev_err(inst->dev, "fifo_mtu_multiplier copy failed.");
		}
		break;
	}
    //-------------------------------
	case SMI_STREAM_IOC_GET_ADDR_DIR_OFFSET:
	{
        dev_dbg(inst->dev, "Reading address direction indication offset of %d", addr_dir_offset);
		if (copy_to_user((void *)arg, &addr_dir_offset, sizeof(addr_dir_offset)))
		{
			dev_err(inst->dev, "addr_dir_offset copy failed.");
		}
		break;
	}
    //-------------------------------
	case SMI_STREAM_IOC_GET_ADDR_CH_OFFSET:
	{
        dev_dbg(inst->dev, "Reading address channel indication offset of %d", addr_ch_offset);
		if (copy_to_user((void *)arg, &addr_ch_offset, sizeof(addr_ch_offset)))
		{
			dev_err(inst->dev, "addr_ch_offset copy failed.");
		}
		break;
	}
    //-------------------------------
    case SMI_STREAM_IOC_FLUSH_FIFO:
    {
        // moved to read file operation
        break;
    }
    //-------------------------------
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

	desc->callback = callback;
	desc->callback_param = inst;

	if (dmaengine_submit(desc) < 0)
	{
		return NULL;
	}
	return desc;
}

/***************************************************************************/
static void stream_smi_dma_callback_user_copy(void *param)
{
	/* Notify the bottom half that a chunk is ready for user copy */
	struct bcm2835_smi_instance *inst = (struct bcm2835_smi_instance *)param;
	up(&inst->bounce.callback_sem);
}

/***************************************************************************/
ssize_t stream_smi_user_dma(	struct bcm2835_smi_instance *inst,
								enum dma_transfer_direction dma_dir,
								struct bcm2835_smi_bounce_info **bounce, 
                                int buff_num)
{
	struct scatterlist *sgl = NULL;

	spin_lock(&inst->transaction_lock);

	sema_init(&inst->bounce.callback_sem, 0);

	if (bounce)
	{
		*bounce = &(inst->bounce);
	}

	sgl = &(inst->bounce.sgl[buff_num]);
	if (sgl == NULL)
	{
		dev_err(inst->dev, "sgl is NULL");
		spin_unlock(&inst->transaction_lock);
		return 0;
	}

	if (!stream_smi_dma_submit_sgl(inst, sgl, 1, dma_dir, stream_smi_dma_callback_user_copy)) 
	{
		dev_err(inst->dev, "sgl submit failed");
		spin_unlock(&inst->transaction_lock);
		return 0;
	}

	dma_async_issue_pending(inst->dma_chan);

	if (dma_dir == DMA_DEV_TO_MEM)
	{
        int ret = smi_init_programmed_read(inst, DMA_BOUNCE_BUFFER_SIZE);
		if (ret != 0)
		{
			spin_unlock(&inst->transaction_lock);
            dev_err(inst->dev, "smi_init_programmed_read returned %d", ret);
			return 0;
		}
	}
	else 
	{
        int ret = smi_init_programmed_write(inst, DMA_BOUNCE_BUFFER_SIZE);
		if (ret != 0)
		{
			spin_unlock(&inst->transaction_lock);
			dev_err(inst->dev, "smi_init_programmed_write returned %d", ret);
			return 0;
		}
	}
	
	//printk(KERN_ERR DRIVER_NAME": SPIN-UNLOCK\n");
	spin_unlock(&inst->transaction_lock);
	return DMA_BOUNCE_BUFFER_SIZE;
}

/***************************************************************************/



/***************************************************************************/
int reader_thread_stream_function(void *pv) 
{
    int current_dma_buffer = 0;
	int current_dma_user = 0;
    int buffer_ready[2] = {0, 0};       // does a certain buffer contain actual data
    uint32_t dma_buffer_idx_wr = 0;
	uint32_t dma_buffer_idx_rd = 0;
    unsigned int errors = 0;
	int ret;
	
	

    ktime_t start;
    s64 t1, t2, t3;

	dev_info(inst->dev, "Enterred reader thread");
    inst->reader_thread_running = true;

	/* Disable the peripheral: */
	if(smi_disable_sync(inst->smi_inst))
	{
			return -1;
	}
	write_smi_reg(inst->smi_inst, 0, SMIL);
	
	sema_init(&inst->smi_inst->bounce.callback_sem, 0);
	
	while(!kthread_should_stop() && !(inst->address_changed) && !errors)
	{       
		struct bcm2835_smi_instance *smi_inst = inst->smi_inst;
		uint32_t used_buffers = dma_buffer_idx_wr - dma_buffer_idx_rd;
		while(used_buffers < DMA_BOUNCE_BUFFER_COUNT)
		{
				
				struct scatterlist *sgl = NULL;

				spin_lock(&smi_inst->transaction_lock);


				sgl = &(smi_inst->bounce.sgl[current_dma_buffer]);
				if (sgl == NULL)
				{
					dev_err(smi_inst->dev, "sgl is NULL");
					spin_unlock(&smi_inst->transaction_lock);
					errors = 1;
					break;

				}

				if (!stream_smi_dma_submit_sgl(smi_inst, sgl, 1, DMA_DEV_TO_MEM, stream_smi_dma_callback_user_copy)) 
				{
					dev_err(smi_inst->dev, "sgl submit failed");
					spin_unlock(&smi_inst->transaction_lock);
					errors = 1;
					break;
				}

				dma_async_issue_pending(smi_inst->dma_chan);
				spin_unlock(&smi_inst->transaction_lock);
			

			
			current_dma_buffer = (current_dma_buffer + 1) % DMA_BOUNCE_BUFFER_COUNT;
			dma_buffer_idx_wr++;
			used_buffers = dma_buffer_idx_wr - dma_buffer_idx_rd;
		}
        
        t1 = ktime_to_ns(ktime_sub(ktime_get(), start));
        
		spin_lock(&smi_inst->transaction_lock);
		if(!(dma_buffer_idx_rd % 100 ))
		printk("init programmed read %u %u\n",dma_buffer_idx_wr ,dma_buffer_idx_rd);
		ret = smi_init_programmed_read(smi_inst, DMA_BOUNCE_BUFFER_SIZE);
		if (ret != 0)
		{
			spin_unlock(&smi_inst->transaction_lock);
            dev_err(smi_inst->dev, "smi_init_programmed_read returned %d", ret);
			errors = 1;
			break;
		}
		spin_unlock(&smi_inst->transaction_lock);
		
		// wait for completion
        inst->reader_waiting_sema = true;
        if (down_timeout(&smi_inst->bounce.callback_sem, msecs_to_jiffies(200))) 
        {
            dev_info(inst->dev, "Reader DMA bounce timed out");
            errors = 1;
			break;
        }
        inst->reader_waiting_sema = false;
		
		current_dma_user = (current_dma_user + 1) % DMA_BOUNCE_BUFFER_COUNT;
		dma_buffer_idx_rd++;
        
		
		if (!mutex_lock_interruptible(&inst->read_lock))
		{
			kfifo_in(&inst->rx_fifo, smi_inst->bounce.buffer[current_dma_user], DMA_BOUNCE_BUFFER_SIZE);
			mutex_unlock(&inst->read_lock);
            
			// for the polling mechanism
			inst->readable = true;
            wake_up_interruptible(&inst->poll_event);
		}
            
		
        
        

        t3 = ktime_to_ns(ktime_sub(ktime_get(), start));
        
        //dev_info(inst->dev, "TIMING (1,2,3): %lld %lld %lld %d", (long long)t1, (long long)t2, (long long)t3, current_dma_buffer);
	}

	dev_info(inst->dev, "Reader state became idle, terminating dma");
	spin_lock(&inst->smi_inst->transaction_lock);
    dmaengine_terminate_sync(inst->smi_inst->dma_chan);
    spin_unlock(&inst->smi_inst->transaction_lock);
	
	dev_info(inst->dev, "Reader state became idle, terminating smi transaction");
	smi_disable_sync(inst->smi_inst);
	
	dev_info(inst->dev, "Left reader thread");
    inst->reader_thread_running = false;
    inst->reader_waiting_sema = false;
	return 0; 
}

/***************************************************************************/
int writer_thread_stream_function(void *pv)
{
	struct bcm2835_smi_bounce_info *bounce = &(inst->smi_inst->bounce);
	int count = 0;
    int current_dma_buffer = 0;
	int num_bytes = 0;
	int num_copied = 0;
	dev_info(inst->dev, "Enterred writer thread. Matteo");
    inst->writer_thread_running = true;

	while(!kthread_should_stop() && !(inst->address_changed))
	{        
		// check if the streaming state is on, if not, sleep and check again
		if (inst->state != smi_stream_tx_channel)
		{
			msleep(5);
			continue;
		}
        
        //msleep(5);
        // sync smi address
        if (inst->address_changed) 
        {
            bcm2835_smi_set_address(inst->smi_inst, inst->cur_address);
            inst->address_changed = 0;
        }
		
		// check if the tx fifo contains enough data
		if (mutex_lock_interruptible(&inst->write_lock))
		{
			return -EINTR;
		}
		num_bytes = kfifo_len (&inst->tx_fifo);
		mutex_unlock(&inst->write_lock);
		
		// if contains enough for a single DMA trnasaction
		if (num_bytes >= DMA_BOUNCE_BUFFER_SIZE)
		{
			// pull data from the fifo into the DMA buffer
			if (mutex_lock_interruptible(&inst->write_lock))
			{
				return -EINTR;
			}
			num_copied = kfifo_out(&inst->tx_fifo, bounce->buffer[current_dma_buffer], DMA_BOUNCE_BUFFER_SIZE);
			mutex_unlock(&inst->write_lock);
			
			// for the polling mechanism
			inst->writeable = true;
			wake_up_interruptible(&inst->poll_event);
			
			if (num_copied != DMA_BOUNCE_BUFFER_SIZE)
			{
				// error
				dev_warn(inst->dev, "kfifo_out didn't copy all elements (writer)");
			}
			
			count = stream_smi_user_dma(inst->smi_inst, DMA_MEM_TO_DEV, NULL, current_dma_buffer);
			if (count != DMA_BOUNCE_BUFFER_SIZE)
			{
				// error
                dev_err(inst->dev, "stream_smi_user_dma error");
				continue;
			}
				
			// Wait for current chunk to complete
            inst->writer_waiting_sema = true;
			if (down_timeout(&bounce->callback_sem, msecs_to_jiffies(1000))) 
			{
				dev_err(inst->dev, "Writer DMA bounce timed out");
                spin_lock(&inst->smi_inst->transaction_lock);
                dmaengine_terminate_sync(inst->smi_inst->dma_chan);
                spin_unlock(&inst->smi_inst->transaction_lock);
			}
            inst->writer_waiting_sema = false;
		}
        else
        {
            // if doen't have enough data, invoke poll
			inst->writeable = true;
			wake_up_interruptible(&inst->poll_event);
		}
	}
    
    dev_info(inst->dev, "Left writer thread");
    inst->writer_thread_running = false;
    inst->writer_waiting_sema = false;
	
	return 0;
}

int main_thread_stream_function(void *pv)
{
	while(!kthread_should_stop())
	{        
		// check if the streaming state is on, if not, sleep and check again
		smi_stream_state_en state = inst->state;
		if (inst->state == smi_stream_idle)
		{
			msleep(5);
			continue;
		}
		
		inst->address_changed = 0;
		
		state = inst->state;
		
		if(state == smi_stream_rx_channel_0 || state == smi_stream_rx_channel_1)
		{
			bcm2835_smi_set_address(inst->smi_inst, calc_address_from_state(state));
			reader_thread_stream_function(pv);
		}
		else if(state == smi_stream_tx_channel)
		{
			bcm2835_smi_set_address(inst->smi_inst, calc_address_from_state(state));
			writer_thread_stream_function(pv);
		}

		while(!kthread_should_stop() && inst->state != smi_stream_idle)
		{
			msleep(5);
		}
		bcm2835_smi_set_address(inst->smi_inst, calc_address_from_state(smi_stream_idle));
		
		
	}
	
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
	
	// preinit the thread handlers to NULL
	inst->reader_thread = NULL;
	inst->writer_thread = NULL;

	// create the data fifo ( N x dma_bounce size )
	// we want this fifo to be deep enough to allow the application react without
	// loosing stream elements
	ret = kfifo_alloc(&inst->rx_fifo, fifo_mtu_multiplier * DMA_BOUNCE_BUFFER_SIZE, GFP_KERNEL);
	if (ret)
	{
		printk(KERN_ERR DRIVER_NAME": error rx kfifo_alloc\n");
		return ret;
	}
	
	// and the writer
	ret = kfifo_alloc(&inst->tx_fifo, fifo_mtu_multiplier * DMA_BOUNCE_BUFFER_SIZE, GFP_KERNEL);
	if (ret)
	{
		printk(KERN_ERR DRIVER_NAME": error tx kfifo_alloc\n");
		return ret;
	}

	// when file is being openned, stream state is still idle
    set_state(smi_stream_idle);
	
	// Create the reader thread
	// this thread is in charge of continuedly interogating the smi for new rx data and
	// activating dma transfers
	inst->reader_thread = kthread_create(main_thread_stream_function, NULL, "smi-reader-thread"); 
	if(IS_ERR(inst->reader_thread))
	{
		printk(KERN_ERR DRIVER_NAME": reader_thread creation failed - kthread\n");
		ret = PTR_ERR(inst->reader_thread);
		inst->reader_thread = NULL;
		kfifo_free(&inst->rx_fifo);
		kfifo_free(&inst->tx_fifo);
		return ret;
	} 

	
	
	// wake up both threads
	wake_up_process(inst->reader_thread); 
	
    inst->address_changed = 0;
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

	// make sure stream is idle
	set_state(smi_stream_idle);
    
	if (inst->reader_thread != NULL) kthread_stop(inst->reader_thread);
	if (inst->writer_thread != NULL) kthread_stop(inst->writer_thread);
	
	if (kfifo_initialized(&inst->rx_fifo)) kfifo_free(&inst->rx_fifo);
	if (kfifo_initialized(&inst->tx_fifo)) kfifo_free(&inst->tx_fifo);
    
    inst->reader_thread = NULL;
    inst->writer_thread = NULL;
    inst->address_changed = 0;

	return 0;
}

/***************************************************************************/
static ssize_t smi_stream_read_file_fifo(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	unsigned int copied = 0;
	
    if (buf == NULL)
	{
        //dev_info(inst->dev, "Flushing internal rx_kfifo");
        if (mutex_lock_interruptible(&inst->read_lock))
        {
            return -EINTR;
        }
        kfifo_reset_out(&inst->rx_fifo);
        mutex_unlock(&inst->read_lock);
        inst->invalidate_rx_buffers = 1;
    }
    else
    {
        if (mutex_lock_interruptible(&inst->read_lock))
        {
            return -EINTR;
        }
        ret = kfifo_to_user(&inst->rx_fifo, buf, count, &copied);
        mutex_unlock(&inst->read_lock);
    }
	return ret < 0 ? ret : (ssize_t)copied;
}

/***************************************************************************/
static ssize_t smi_stream_write_file(struct file *f, const char __user *user_ptr, size_t count, loff_t *offs)
{
	int ret = 0;
	unsigned int num_bytes_available = 0;
	unsigned int num_to_push = 0;
	unsigned int actual_copied = 0;
	
	if (mutex_lock_interruptible(&inst->write_lock))
	{
		return -EINTR;
	}
	
	if (kfifo_is_full(&inst->tx_fifo))
	{
		mutex_unlock(&inst->write_lock);
		return -EAGAIN;
	}
	
	// check how many bytes are available in the tx fifo
	num_bytes_available = kfifo_avail(&inst->tx_fifo);
	num_to_push = num_bytes_available > count ? count : num_bytes_available;
	ret = kfifo_from_user(&inst->tx_fifo, user_ptr, num_to_push, &actual_copied);

	mutex_unlock(&inst->write_lock);

	return ret ? ret : (ssize_t)actual_copied;
}

/***************************************************************************/
static unsigned int smi_stream_poll(struct file *filp, struct poll_table_struct *wait)
{
	__poll_t mask = 0;
        
    //dev_info(inst->dev, "poll_waiting");
    poll_wait(filp, &inst->poll_event, wait);
    
    if (inst->readable)
    {
        //dev_info(inst->dev, "poll_wait result => readable=%d", inst->readable);
		inst->readable = false;
		mask |= ( POLLIN | POLLRDNORM );
	}
	
	if (inst->writeable)
	{
        //dev_info(inst->dev, "poll_wait result => writeable=%d", inst->writeable);
		inst->writeable = false;
		mask |= ( POLLOUT | POLLWRNORM );
	}

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

	printk(KERN_INFO DRIVER_NAME": smi_stream_dev_probe (fifo_mtu_multiplier=%d, addr_dir_offset=%d, addr_ch_offset=%d)\n",
                                    fifo_mtu_multiplier,
                                    addr_dir_offset,
                                    addr_ch_offset);
    
    // Check parameters
    if (fifo_mtu_multiplier > 20 || fifo_mtu_multiplier < 2)
    {
        dev_err(dev, "Parameter error: 2<fifo_mtu_multiplier<20");
		return -EINVAL;
    }
    
    if (addr_dir_offset > 4 || addr_dir_offset < -1)
    {
        dev_err(dev, "Parameter error: 0<=addr_dir_offset<=4 or (-1 - unused)");
		return -EINVAL;
    }
    
    if (addr_ch_offset > 4 || addr_ch_offset < -1)
    {
        dev_err(dev, "Parameter error: 0<=addr_ch_offset<=4 or (-1 - unused)");
		return -EINVAL;
    }
    
    if (addr_dir_offset == addr_ch_offset && addr_dir_offset != -1)
    {
        dev_err(dev, "Parameter error: addr_ch_offset should be different than addr_dir_offset");
		return -EINVAL;
    }

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

	//smi_stream_print_smi_inst(inst->smi_inst);

	inst->dev = dev;

	/* Create character device entries */
	err = alloc_chrdev_region(&smi_stream_devid, DEVICE_MINOR, 1, DEVICE_NAME);
	if (err != 0) 
	{
		dev_err(inst->dev, "unable to allocate device number");
		return -ENOMEM;
	}

	// init the char device with file operations
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

	// Create sysfs entries with "smi-stream-dev"
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
	inst->writer_thread = NULL;
    inst->invalidate_rx_buffers = 0;
    inst->invalidate_tx_buffers = 0;
	init_waitqueue_head(&inst->poll_event);
	inst->readable = false;
	inst->writeable = false;
    inst->reader_thread_running = false;
    inst->writer_thread_running = false;
    inst->reader_waiting_sema = false;
    inst->writer_waiting_sema = false;
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
