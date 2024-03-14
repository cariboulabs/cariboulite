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

/*
 *  Contribution by matteo serva
 *  https://github.com/matteoserva
 * 
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

#define SMI_TRANSFER_MULTIPLIER 64

module_param(fifo_mtu_multiplier, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
module_param(addr_dir_offset, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
module_param(addr_ch_offset, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

MODULE_PARM_DESC(fifo_mtu_multiplier, "the number of MTUs (N*MTU_SIZE) to allocate for kfifo's (default 6) valid: [3..33]");
MODULE_PARM_DESC(addr_dir_offset, "GPIO_SA[4:0] offset of the channel direction (default cariboulite 2), valid: [0..4] or (-1) if unused");
MODULE_PARM_DESC(addr_ch_offset, "GPIO_SA[4:0] offset of the channel select (default cariboulite 3), valid: [0..4] or (-1) if unused");

struct bcm2835_smi_dev_instance 
{
	struct device *dev;
	struct bcm2835_smi_instance *smi_inst;
	struct cdev smi_stream_cdev;
	dev_t smi_stream_devid;
	struct class *smi_stream_class;
	struct device *smi_stream_dev;

	// address related
	unsigned int cur_address;
    int address_changed;
    
    // flags
    int invalidate_rx_buffers;
    int invalidate_tx_buffers;
	
	unsigned int count_since_refresh;
	struct task_struct *reader_thread;
	struct task_struct *writer_thread;
	struct kfifo rx_fifo;
	struct kfifo tx_fifo;
	uint8_t* rx_fifo_buffer;
	uint8_t* tx_fifo_buffer;
	smi_stream_state_en state;
	struct mutex read_lock;
	struct mutex write_lock;
	spinlock_t state_lock;
	wait_queue_head_t poll_event;
	uint32_t current_read_chunk;
	uint32_t counter_missed;
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

int writer_thread_init(struct bcm2835_smi_dev_instance *inst);
int transfer_thread_init(struct bcm2835_smi_dev_instance *inst, enum dma_transfer_direction dir,dma_async_tx_callback callback);
static void stream_smi_read_dma_callback(void *param);
static void stream_smi_write_dma_callback(void *param);
void transfer_thread_stop(struct bcm2835_smi_dev_instance *inst);
void print_smil_registers(struct bcm2835_smi_dev_instance *inst);


//static struct bcm2835_smi_dev_instance *inst = NULL;

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

void print_smil_registers(struct bcm2835_smi_dev_instance *inst)
{
	struct bcm2835_smi_instance *smi_inst = inst->smi_inst;
	unsigned int smics = read_smi_reg(smi_inst, SMICS);
	unsigned int smil = read_smi_reg(smi_inst, SMIL);
	unsigned int smidc = read_smi_reg(smi_inst, SMIDC);
	unsigned int smidsw0 = read_smi_reg(smi_inst,SMIDSW0);
	
	dev_info(inst->dev, "regs: smics %08X smil %08X smids %08X smisw0 %08X",smics,smil,smidc,smidsw0);
}

void print_smil_registers_ext(struct bcm2835_smi_dev_instance *inst,const char* b)
{
	struct bcm2835_smi_instance *smi_inst = inst->smi_inst;
	unsigned int smics = read_smi_reg(smi_inst, SMICS);
	unsigned int smil = read_smi_reg(smi_inst, SMIL);
	unsigned int smidc = read_smi_reg(smi_inst, SMIDC);
	unsigned int smidsw0 = read_smi_reg(smi_inst,SMIDSW0);
	dev_info(inst->dev, "%s: regs: smics %08X smil %08X smids %08X smisw0 %08X",b,smics,smil,smidc,smidsw0);
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
		// put device in highZ to be safe
		return_val = smi_stream_dir_smi_to_device<<addr_dir_offset;
		
    }
    return return_val;
}

/***************************************************************************/
static inline int smi_is_active(struct bcm2835_smi_instance *inst)
{
	return read_smi_reg(inst, SMICS) & SMICS_ACTIVE;
}

static long set_state(struct bcm2835_smi_dev_instance *inst,smi_stream_state_en new_state)
{
	unsigned int new_address = calc_address_from_state(new_state);
    //if (inst == NULL) return 0;
	
	spin_lock(&inst->state_lock);
	
	if(new_state == inst->state)
	{
		spin_unlock(&inst->state_lock);
		return 0;
	}
	 
	dev_info(inst->dev, "Set STREAMING_STATUS = %d, cur_addr = %d", new_state, new_address);
	 
	if(inst->state !=  smi_stream_idle)
	{
		//exiting from current state
		if (inst->state == smi_stream_tx_channel)
		{
			transfer_thread_stop(inst);
		}
		else
		{
			transfer_thread_stop(inst);
		}
		if(smi_is_active(inst->smi_inst))
		{
			spin_unlock(&inst->state_lock);
					return -EAGAIN;
		}
		inst->state = smi_stream_idle;
		bcm2835_smi_set_address(inst->smi_inst, calc_address_from_state(smi_stream_idle));
	}
	//now state is idle
	 
	if(new_state !=  smi_stream_idle)
	{
		int ret = -1;
		bcm2835_smi_set_address(inst->smi_inst, new_address);
		if (new_state == smi_stream_tx_channel)
		{
			ret = transfer_thread_init(inst,DMA_MEM_TO_DEV,stream_smi_write_dma_callback);
		}
		else
		{
			ret = transfer_thread_init(inst,DMA_DEV_TO_MEM,stream_smi_read_dma_callback);
		}
		
		if(!ret)
		{
			inst->state = new_state;
		}
		else
		{
			bcm2835_smi_set_address(inst->smi_inst, calc_address_from_state(smi_stream_idle));
		}
	}
	 

    
    
	spin_unlock(&inst->state_lock);
	return 0;
}

/***************************************************************************/
static void smi_setup_clock(struct bcm2835_smi_instance *inst)
{
	
}



/***************************************************************************/
static inline int smi_enabled(struct bcm2835_smi_instance *inst)
{
	return read_smi_reg(inst, SMICS) & SMICS_ENABLE;
}

/***************************************************************************/



static int smi_disable_sync(struct bcm2835_smi_dev_instance *inst )
{
	int smics_temp;
	int success = 0;
	int errors = 0;
	struct bcm2835_smi_instance *smi_inst = inst->smi_inst;
	dev_info(inst->dev, "smi disable sync enter");
	
	/* Disable the peripheral: */
	smics_temp = read_smi_reg(smi_inst, SMICS) & ~(SMICS_ENABLE | SMICS_WRITE);
	write_smi_reg(smi_inst, smics_temp, SMICS);
	// wait for the ENABLE to go low
	BUSY_WAIT_WHILE_TIMEOUT(smi_enabled(smi_inst), 1000000U, success);
	
	if (!success)
	{
		dev_info(inst->dev, "errore disable. %u %08X",smi_enabled(smi_inst),read_smi_reg(smi_inst, SMICS));
		errors = -1;
	}
	
	print_smil_registers(inst);
	dev_info(inst->dev, "smi disable sync exit");
	
	return errors;
	
}

static void smi_refresh_dma_command(struct bcm2835_smi_dev_instance *inst, int num_transfers)
{
	int smics_temp;
	struct bcm2835_smi_instance *smi_inst = inst->smi_inst;
	//print_smil_registers_ext("refresh 1");
	write_smi_reg(smi_inst, SMI_TRANSFER_MULTIPLIER*num_transfers, SMIL); //to avoid stopping and restarting
	//print_smil_registers_ext("refresh 2");
	// Start the transaction
	smics_temp = read_smi_reg(smi_inst, SMICS);
	smics_temp |= SMICS_START;
	//smics_temp &= ~(SMICS_PVMODE);
	write_smi_reg(smi_inst, smics_temp & 0xffff, SMICS);
	inst->count_since_refresh = 0;
	//print_smil_registers_ext("refresh 3");
}

/***************************************************************************/
static int smi_init_programmed_transfer(struct bcm2835_smi_dev_instance *inst, enum dma_transfer_direction dma_dir,int num_transfers)
{
	int smics_temp;
	int success = 0;
	struct bcm2835_smi_instance *smi_inst = inst->smi_inst;
	
	dev_info(inst->dev, "smi_init_programmed_read");
	print_smil_registers_ext(inst,"init 1");
	
	write_smi_reg(inst->smi_inst, 0, SMIL);
	print_smil_registers_ext(inst,"init 2");
	smics_temp = read_smi_reg(smi_inst, SMICS);
	/* Program the transfer count: */
	write_smi_reg(smi_inst, num_transfers, SMIL);
	print_smil_registers_ext(inst,"init 3");
	/* re-enable and start: */
	smics_temp |= SMICS_CLEAR;
	smics_temp |= SMICS_ENABLE;
	if(dma_dir == DMA_MEM_TO_DEV)
	{
			smics_temp |= SMICS_WRITE;
	}
	
	write_smi_reg(smi_inst, smics_temp, SMICS);
print_smil_registers_ext(inst,"init 4");
	

	/* IO barrier - to be sure that the last request have
	   been dispatched in the correct order
	*/
	mb();
	// busy wait as long as the transaction is active (taking place)
	BUSY_WAIT_WHILE_TIMEOUT(smi_is_active(smi_inst), 1000000U, success);
	if (!success)
	{
		dev_info(inst->dev, "bb errore disable. %u %08X",smi_enabled(smi_inst),read_smi_reg(smi_inst, SMICS));
		
		return -2;
	}

	// Clear the FIFO (reset it to zero contents)
	write_smi_reg(smi_inst, smics_temp, SMICS);
	print_smil_registers_ext(inst,"init 5");
	return 0;
}





/****************************************************************************
*
*   SMI DMA functions
*
***************************************************************************/

static void stream_smi_read_dma_callback(void *param)
{
	/* Notify the bottom half that a chunk is ready for user copy */
	struct bcm2835_smi_dev_instance *inst = (struct bcm2835_smi_dev_instance *)param;
	struct bcm2835_smi_instance *smi_inst = inst->smi_inst;
	uint8_t* buffer_pos;
	
	
	smi_refresh_dma_command(inst, DMA_BOUNCE_BUFFER_SIZE/4);
	
	buffer_pos = (uint8_t*) smi_inst->bounce.buffer[0];
	buffer_pos = &buffer_pos[ (DMA_BOUNCE_BUFFER_SIZE/4) * (inst->current_read_chunk % 4)];
	if(kfifo_avail(&inst->rx_fifo) >=DMA_BOUNCE_BUFFER_SIZE/4)
	{
		kfifo_in(&inst->rx_fifo, buffer_pos, DMA_BOUNCE_BUFFER_SIZE/4);
	}
	else
	{
			inst->counter_missed++;
	}
	
	if(!(inst->current_read_chunk % 100 ))
		{
			dev_info(inst->dev,"init programmed read. missed: %u, sema %u",inst->counter_missed,smi_inst->bounce.callback_sem.count);
		}
	
	up(&smi_inst->bounce.callback_sem);
	
	inst->readable = true;
	wake_up_interruptible(&inst->poll_event);
	inst->current_read_chunk++;
}

static void stream_smi_check_and_restart(struct bcm2835_smi_dev_instance *inst)
{
	struct bcm2835_smi_instance *smi_inst = inst->smi_inst;
	inst->count_since_refresh++;
	if( (inst->count_since_refresh )>= SMI_TRANSFER_MULTIPLIER)
	{
		int i;
		for(i = 0; i < 1000; i++)
		{
				if(!smi_is_active(smi_inst))
					break;
				udelay(1);
		}
		if(i == 1000)
		{
			print_smil_registers_ext(inst,"write dma callback errore 1000");
		}
		
		smi_refresh_dma_command(inst, DMA_BOUNCE_BUFFER_SIZE/4);
	
	}
}

static void stream_smi_write_dma_callback(void *param)
{
	/* Notify the bottom half that a chunk is ready for user copy */
	struct bcm2835_smi_dev_instance *inst = (struct bcm2835_smi_dev_instance *)param;
	struct bcm2835_smi_instance *smi_inst = inst->smi_inst;
	uint8_t* buffer_pos;
	stream_smi_check_and_restart(inst);
	
	inst->current_read_chunk++;
	
	buffer_pos = (uint8_t*) smi_inst->bounce.buffer[0];
	buffer_pos = &buffer_pos[ (DMA_BOUNCE_BUFFER_SIZE/4) * (inst->current_read_chunk % 4)];
	
	if(kfifo_len (&inst->tx_fifo) >= DMA_BOUNCE_BUFFER_SIZE/4)
	{
		int num_copied = kfifo_out(&inst->tx_fifo, buffer_pos, DMA_BOUNCE_BUFFER_SIZE/4);
		(void)num_copied;
	}
	else
	{
			inst->counter_missed++;
	}
	
	if(!(inst->current_read_chunk % 111 ))
		{
			dev_info(inst->dev,"init programmed write. missed: %u, sema %u, val %08X",inst->counter_missed,smi_inst->bounce.callback_sem.count,*(uint32_t*) &buffer_pos[0]);
		}
	
	up(&smi_inst->bounce.callback_sem);
	
	inst->writeable = true;
	wake_up_interruptible(&inst->poll_event);
	
}


static struct dma_async_tx_descriptor *stream_smi_dma_init_cyclic(struct bcm2835_smi_instance *inst,
																enum dma_transfer_direction dir,
																dma_async_tx_callback callback, void*param)
{
	struct dma_async_tx_descriptor *desc = NULL;

	//printk(KERN_ERR DRIVER_NAME": SUBMIT_PREP %lu\n", (long unsigned int)(inst->dma_chan));
	desc = dmaengine_prep_dma_cyclic(inst->dma_chan,
				       inst->bounce.phys[0],
				       DMA_BOUNCE_BUFFER_SIZE,
				       DMA_BOUNCE_BUFFER_SIZE/4,
				       dir,DMA_PREP_INTERRUPT | DMA_CTRL_ACK | DMA_PREP_FENCE);
	if (!desc) 
	{
		dev_err(inst->dev, "read_sgl: dma slave preparation failed!");
		return NULL;
	}

	desc->callback = callback;
	desc->callback_param = param;

	if (dmaengine_submit(desc) < 0)
	{
		return NULL;
	}
	return desc;
}

/****************************************************************************
*
*   transfer thread functions
*
***************************************************************************/

int transfer_thread_init(struct bcm2835_smi_dev_instance *inst, enum dma_transfer_direction dir,dma_async_tx_callback callback)
{

    unsigned int errors = 0;
	int ret;
	int success;
	
	dev_info(inst->dev, "Enterred reader thread");
    inst->reader_thread_running = true;

	/* Disable the peripheral: */
	if(smi_disable_sync(inst))
	{
			return -1;
	}
	write_smi_reg(inst->smi_inst, 0, SMIL);
	
	sema_init(&inst->smi_inst->bounce.callback_sem, 0);
	
	spin_lock(&inst->smi_inst->transaction_lock);
	ret = smi_init_programmed_transfer(inst, dir, DMA_BOUNCE_BUFFER_SIZE/4);
	if (ret != 0)
	{
		spin_unlock(&inst->smi_inst->transaction_lock);
		dev_err(inst->smi_inst->dev, "smi_init_programmed_read returned %d", ret);
		smi_disable_sync(inst);
		return -2;
		
	}
	else
	{
	spin_unlock(&inst->smi_inst->transaction_lock);
	}
	inst->current_read_chunk = 0;
	inst->counter_missed = 0;
	if(!errors)
	{
		struct dma_async_tx_descriptor *desc = NULL;
		struct bcm2835_smi_instance *smi_inst = inst->smi_inst;
		spin_lock(&smi_inst->transaction_lock);
		desc = stream_smi_dma_init_cyclic(smi_inst, dir, callback,inst);

		if(desc)
		{
			dma_async_issue_pending(smi_inst->dma_chan);
		}
		else
		{
			errors = 1;
		}
		spin_unlock(&smi_inst->transaction_lock);
	}
	smi_refresh_dma_command(inst, DMA_BOUNCE_BUFFER_SIZE/4);
	BUSY_WAIT_WHILE_TIMEOUT(!smi_is_active(inst->smi_inst), 1000000U, success);
	print_smil_registers_ext(inst,"post init 0");
	return errors;
}

void transfer_thread_stop(struct bcm2835_smi_dev_instance *inst)
{
	int errors = 0;
	dev_info(inst->dev, "Reader state became idle, terminating dma %u %u", (inst->address_changed) ,errors);
	print_smil_registers_ext(inst,"thread stop 0");
	spin_lock(&inst->smi_inst->transaction_lock);
    dmaengine_terminate_sync(inst->smi_inst->dma_chan);
    spin_unlock(&inst->smi_inst->transaction_lock);
	
	dev_info(inst->dev, "Reader state became idle, terminating smi transaction");
	smi_disable_sync(inst);
	bcm2835_smi_set_regs_from_settings(inst->smi_inst);
	
	dev_info(inst->dev, "Left reader thread");
    inst->reader_thread_running = false;
    inst->reader_waiting_sema = false;
	return ;
}


/****************************************************************************
*
*   FILE ops
*
***************************************************************************/

static long smi_stream_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;

	struct bcm2835_smi_dev_instance *inst = file->private_data;
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
        ret = set_state(inst,(smi_stream_state_en)arg);
		
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

static int smi_stream_open(struct inode *inode, struct file *file)
{
	int dev = iminor(inode);
	
	struct bcm2835_smi_dev_instance *inst = container_of(inode->i_cdev, struct bcm2835_smi_dev_instance, smi_stream_cdev);
	file->private_data = inst;
	
	dev_dbg(inst->dev, "SMI device opened.");

	if (dev != DEVICE_MINOR) 
	{
		dev_err(inst->dev, "smi_stream_open: Unknown minor device: %d", dev);		// error here
		return -ENXIO;
	}
	
	// when file is being openned, stream state is still idle
    set_state(inst,smi_stream_idle);
	

	
    inst->address_changed = 0;
	return 0;
}

/***************************************************************************/
static int smi_stream_release(struct inode *inode, struct file *file)
{
	int dev = iminor(inode);
	struct bcm2835_smi_dev_instance *inst = file->private_data;
	
	dev_info(inst->dev, "smi_stream_release: closing device: %d", dev);

	if (dev != DEVICE_MINOR) 
	{
		dev_err(inst->dev, "smi_stream_release: Unknown minor device %d", dev);
		return -ENXIO;
	}

	// make sure stream is idle
	set_state(inst,smi_stream_idle);

	
    	inst->address_changed = 0;

	return 0;
}

/***************************************************************************/
static ssize_t smi_stream_read_file_fifo(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	unsigned int copied = 0;
	
	struct bcm2835_smi_dev_instance *inst = file->private_data;
	
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
		return 0;
    }
	
	if (kfifo_is_empty(&inst->rx_fifo))
	{
			int ret = wait_event_interruptible(inst->poll_event,  !kfifo_is_empty(&inst->rx_fifo) );
			if (ret)
				return ret;
	}
	
    
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
static ssize_t smi_stream_write_file(struct file *filp, const char __user *user_ptr, size_t count, loff_t *offs)
{
	int ret = 0;
	unsigned int num_bytes_available = 0;
	unsigned int num_to_push = 0;
	unsigned int actual_copied = 0;
	
	struct bcm2835_smi_dev_instance *inst = filp->private_data;
	
	if (mutex_lock_interruptible(&inst->write_lock))
	{
		return -EAGAIN;
	}
	
	if (kfifo_is_full(&inst->tx_fifo))
	{
		if(wait_event_interruptible(inst->poll_event,  !kfifo_is_full(&inst->tx_fifo)))
		{
			mutex_unlock(&inst->write_lock);
			return -EAGAIN;
		}
		
		
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
    struct bcm2835_smi_dev_instance *inst = filp->private_data;    
    
    poll_wait(filp, &inst->poll_event, wait);

    if (!kfifo_is_empty(&inst->rx_fifo))
    {
        //dev_info(inst->dev, "poll_wait result => readable=%d", inst->readable);
		inst->readable = false;
		mask |= ( POLLIN | POLLRDNORM );
	}
	
	if (!kfifo_is_full(&inst->rx_fifo))
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






static int smi_stream_init_buffers(struct bcm2835_smi_dev_instance *inst)
{
	
	// preinit the thread handlers to NULL
	inst->reader_thread = NULL;
	inst->writer_thread = NULL;

	// create the data fifo ( N x dma_bounce size )
	// we want this fifo to be deep enough to allow the application react without
	// loosing stream elements
	
	inst->rx_fifo_buffer = vmalloc(fifo_mtu_multiplier * DMA_BOUNCE_BUFFER_SIZE);
	if (!inst->rx_fifo_buffer)
	{
		printk(KERN_ERR DRIVER_NAME": error rx_fifo_buffer vmallok failed\n");
		return -ENOMEM;
	}
	
	inst->tx_fifo_buffer = vmalloc(fifo_mtu_multiplier * DMA_BOUNCE_BUFFER_SIZE);
    if (!inst->tx_fifo_buffer)
	{
		printk(KERN_ERR DRIVER_NAME": error tx_fifo_buffer vmallok failed\n");
        vfree(inst->rx_fifo_buffer);
		return -ENOMEM;
	}

	kfifo_init(&inst->rx_fifo, inst->rx_fifo_buffer, fifo_mtu_multiplier * DMA_BOUNCE_BUFFER_SIZE);
	kfifo_init(&inst->tx_fifo, inst->tx_fifo_buffer, fifo_mtu_multiplier * DMA_BOUNCE_BUFFER_SIZE);

	return 0;
}

static void smi_stream_clear_buffers(struct bcm2835_smi_dev_instance *inst)
{
	if (inst->reader_thread != NULL) kthread_stop(inst->reader_thread);
	if (inst->writer_thread != NULL) kthread_stop(inst->writer_thread);
	if (inst->rx_fifo_buffer) vfree(inst->rx_fifo_buffer);
	if (inst->tx_fifo_buffer) vfree(inst->tx_fifo_buffer);

	inst->rx_fifo_buffer = NULL;
    	inst->tx_fifo_buffer = NULL;
	inst->reader_thread = NULL;
    	inst->writer_thread = NULL;

}

static int smi_stream_dev_probe(struct platform_device *pdev)
{
	int err;
	void *ptr_err;
	struct bcm2835_smi_dev_instance *inst;
	struct device *dev = &pdev->dev;
	struct device_node *smi_node;

	printk(KERN_INFO DRIVER_NAME": smi_stream_dev_probe (fifo_mtu_multiplier=%d, addr_dir_offset=%d, addr_ch_offset=%d)\n",
                                    fifo_mtu_multiplier,
                                    addr_dir_offset,
                                    addr_ch_offset);
    
    // Check parameters
    if (fifo_mtu_multiplier > 32 || fifo_mtu_multiplier < 2)
    {
        dev_err(dev, "Parameter error: 2<fifo_mtu_multiplier<33");
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
	err = alloc_chrdev_region(&inst->smi_stream_devid, DEVICE_MINOR, 1, DEVICE_NAME);
	if (err != 0) 
	{
		dev_err(inst->dev, "unable to allocate device number");
		return -ENOMEM;
	}

	// init the char device with file operations
	
	cdev_init(&inst->smi_stream_cdev, &smi_stream_fops);
	inst->smi_stream_cdev.owner = THIS_MODULE;
	err = cdev_add(&inst->smi_stream_cdev, inst->smi_stream_devid, 1);
	if (err != 0) 
	{
		dev_err(inst->dev, "unable to register device");
		err = -ENOMEM;
		unregister_chrdev_region(inst->smi_stream_devid, 1);
		dev_err(dev, "could not load smi_stream_dev");
		return err;
	}

	// Create sysfs entries with "smi-stream-dev"
	inst->smi_stream_class = class_create(THIS_MODULE, DEVICE_NAME);
	ptr_err = inst->smi_stream_class;
	if (IS_ERR(ptr_err))
	{
		cdev_del(&inst->smi_stream_cdev);
		unregister_chrdev_region(inst->smi_stream_devid, 1);
		dev_err(dev, "could not load smi_stream_dev");
		return PTR_ERR(ptr_err);
	}

	printk(KERN_INFO DRIVER_NAME": creating a device and registering it with sysfs\n");
	inst->smi_stream_dev = device_create(inst->smi_stream_class, 	// pointer to the struct class that this device should be registered to
					NULL,								// pointer to the parent struct device of this new device, if any
					inst->smi_stream_devid, 					// the dev_t for the char device to be added
					NULL,								// the data to be added to the device for callbacks
					"smi");								// string for the device's name

	ptr_err = inst->smi_stream_dev;
	if (IS_ERR(ptr_err)) 
	{
		err = PTR_ERR(ptr_err);
		goto err_clear_all;
	}

	err = smi_stream_init_buffers(inst);
	if(err)
		goto err_clear_all;
	
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
	spin_lock_init(&inst->state_lock);
	
	//inst->reader_thread = kthread_create(test_thread_stream_function, NULL, "smi-test-thread"); 
	platform_set_drvdata(pdev, inst);
	dev_info(inst->dev, "initialised");
	//wake_up_process(inst->reader_thread); 
	return 0;
	
	err_clear_all:
	class_destroy(inst->smi_stream_class);
	cdev_del(&inst->smi_stream_cdev);
	unregister_chrdev_region(inst->smi_stream_devid, 1);
	dev_err(dev, "could not load smi_stream_dev");
	return err;
	
	
}

/****************************************************************************
*
*   smi_stream_remove - called when the driver is unloaded.
*
***************************************************************************/

static int smi_stream_dev_remove(struct platform_device *pdev)
{
	struct bcm2835_smi_dev_instance *inst=platform_get_drvdata(pdev);
		
	if (inst->reader_thread != NULL) kthread_stop(inst->reader_thread);
    inst->reader_thread = NULL;	
	
	smi_stream_clear_buffers(inst);
	
	device_destroy(inst->smi_stream_class, inst->smi_stream_devid);
	class_destroy(inst->smi_stream_class);
	cdev_del(&inst->smi_stream_cdev);
	unregister_chrdev_region(inst->smi_stream_devid, 1);

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
