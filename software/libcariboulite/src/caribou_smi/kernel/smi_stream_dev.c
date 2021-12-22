/**
 * Character device driver for Broadcom Secondary Memory Interface
 *
 * Written by Luke Wren <luke@raspberrypi.org>
 * Copyright (c) 2015, Raspberry Pi (Trading) Ltd.
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

#define DEVICE_NAME "smi-stream-dev"
#define DRIVER_NAME "smi-stream-dev"
#define DEVICE_MINOR 0

static struct cdev smi_stream_cdev;
static dev_t smi_stream_devid;
static struct class *smi_stream_class;
static struct device *smi_stream_dev;

#define FIFO_SIZE_MULTIPLIER 6

struct bcm2835_smi_dev_instance 
{
	struct device *dev;

	bool non_blocking_reads;
	bool non_blocking_writes;
	
	struct task_struct *reader_thread;
	struct kfifo rx_fifo;
	char* rx_buffer;
	bool streaming;
	struct mutex read_lock;
	struct mutex write_lock;
	spinlock_t fifo_lock;
	wait_queue_head_t readable;
	wait_queue_head_t writeable;
};

static struct bcm2835_smi_instance *smi_inst;
static struct bcm2835_smi_dev_instance *inst;

static const char *const ioctl_names[] = 
{
	"READ_SETTINGS",
	"WRITE_SETTINGS",
	"ADDRESS",
	"GET_NATIVE_BUF_SIZE"
};

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
		settings = bcm2835_smi_get_settings_from_regs(smi_inst);
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
		settings = bcm2835_smi_get_settings_from_regs(smi_inst);
		if (copy_from_user(settings, (void *)arg, sizeof(struct smi_settings)))
		{
			dev_err(inst->dev, "settings copy failed.");
		}
		else
		{
			bcm2835_smi_set_regs_from_settings(smi_inst);
		}
		break;
	}
	//-------------------------------
	case BCM2835_SMI_IOC_ADDRESS:
	{
		dev_info(inst->dev, "SMI address set: 0x%02x", (int)arg);
		bcm2835_smi_set_address(smi_inst, arg);
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
	case SMI_STREAM_IOC_SET_NON_BLOCK_READ:
	{
		inst->non_blocking_reads = arg;
		dev_info(inst->dev, "Set NON_BLOCK_READ = %d", inst->non_blocking_reads);
		break;
	}
	//-------------------------------
	case SMI_STREAM_IOC_SET_NON_BLOCK_WRITE:
	{
		inst->non_blocking_writes = arg;
		dev_info(inst->dev, "Set NON_BLOCK_WRITE = %d", inst->non_blocking_writes);
		break;
	}
	//-------------------------------
	case SMI_STREAM_IOC_SET_STREAM_STATUS:
	{
		inst->streaming = (bool)(arg);
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

int reader_thread_stream_function(void *pv) 
{
	struct bcm2835_smi_bounce_info *bounce = NULL;
	ssize_t count = 0;
	ssize_t last_count = 0;

	while(!kthread_should_stop())
	{
		if (!inst->streaming)
		{
			msleep(10);
			continue;
		}

		count = bcm2835_smi_user_dma(smi_inst, DMA_DEV_TO_MEM, inst->rx_buffer, 
									DMA_BOUNCE_BUFFER_SIZE, &bounce);
		//printk("count1 = %d\n", count);
		if (count)
		{
			//printk("count3 = %d\n", count);
			if (last_count) 
			{
				kfifo_in(&inst->rx_fifo, bounce->buffer[0], last_count);
				wake_up_interruptible(&inst->readable);
			}

			//count = 0;//dma_bounce_user(DMA_DEV_TO_MEM, inst->rx_buffer, DMA_BOUNCE_BUFFER_SIZE, bounce);
			/* Wait for current chunk to complete: */
			if (down_timeout(&bounce->callback_sem, msecs_to_jiffies(1000))) 
			{
				printk("DMA bounce timed out");
				last_count = 0;
				continue;
			}
			last_count = count;
		}
		
	} 
	return 0; 
}

static int smi_stream_open(struct inode *inode, struct file *file)
{
	int ret;
	int dev = iminor(inode);

	dev_dbg(inst->dev, "SMI device opened.");

	if (dev != DEVICE_MINOR) 
	{
		dev_err(inst->dev, "smi_stream_release: Unknown minor device: %d", dev);		// error here
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
		printk(KERN_ERR "error kfifo_alloc\n");
		kfree(inst->rx_buffer);
		return ret;
	}

	inst->streaming = 0;
	// Create the reader stream
	inst->reader_thread = kthread_create(reader_thread_stream_function, NULL, "Reader Thread"); 
	if(inst->reader_thread) 
	{
		wake_up_process(inst->reader_thread); 
	} 
	else 
	{
		printk(KERN_ERR "Cannot create kthread\n");
		kfifo_free(&inst->rx_fifo);
		kfree(inst->rx_buffer);
		return -ENOMEM;
	}

	return 0;
}

static int smi_stream_release(struct inode *inode, struct file *file)
{
	int dev = iminor(inode);
	
	kthread_stop(inst->reader_thread);
	kfifo_free(&inst->rx_fifo);
	kfree(inst->rx_buffer);
	inst->streaming = 0;

	if (dev != DEVICE_MINOR) 
	{
		dev_err(inst->dev, "smi_stream_release: Unknown minor device %d", dev);
		return -ENXIO;
	}

	return 0;
}


static ssize_t dma_bounce_user(
	enum dma_transfer_direction dma_dir,
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

static ssize_t smi_stream_read_file_fifo(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int ret = 0;
	unsigned int copied;

	if (kfifo_is_empty(&inst->rx_fifo)) 
	{
		if (file->f_flags & O_NONBLOCK)
		{
			return -EAGAIN;
		}
		else 
		{
			pr_debug("%s\n", "waiting");

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
	ret = kfifo_to_user(&inst->rx_fifo, buf, count, &copied);
	mutex_unlock(&inst->read_lock);

	return ret ? ret : copied;
}

static ssize_t smi_stream_read_file(struct file *f, char __user *user_ptr, size_t count, loff_t *offs)
{
	int odd_bytes;
	size_t count_check;

	dev_dbg(inst->dev, "User reading %zu bytes from SMI.", count);

	// We don't want to DMA a number of bytes % 4 != 0 (32 bit FIFO)
	// For reads of under 128 bytes => don't use DMA => odd_bytes = count (residue)
	// For reads with counts that are not 32 bit aligned (don't divide by 4), odd_bytes = count % 4

	if (count > DMA_THRESHOLD_BYTES)
	{
		odd_bytes = count & 0x3;
	}
	else
	{
		odd_bytes = count;
	}

	// Main read
	count -= odd_bytes;
	count_check = count;
	if (count) 
	{
		struct bcm2835_smi_bounce_info *bounce;

		count = bcm2835_smi_user_dma(smi_inst, DMA_DEV_TO_MEM, user_ptr, count, &bounce);
		if (count)
		{
			count = dma_bounce_user(DMA_DEV_TO_MEM, user_ptr, count, bounce);
		}
	}

	// Residue read
	if (odd_bytes && (count == count_check)) 
	{
		/* Read from FIFO directly if not using DMA */
		uint8_t buf[DMA_THRESHOLD_BYTES];
		unsigned long bytes_not_transferred;

		bcm2835_smi_read_buf(smi_inst, buf, odd_bytes);
		bytes_not_transferred = copy_to_user(user_ptr + count, buf, odd_bytes);
		if (bytes_not_transferred)
		{
			dev_err(inst->dev, "copy_to_user() failed.");
		}
		count += odd_bytes - bytes_not_transferred;
	}
	return count;
}

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

		count = bcm2835_smi_user_dma(smi_inst, DMA_MEM_TO_DEV, (char __user *)user_ptr, count, &bounce);
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
			bcm2835_smi_write_buf(smi_inst, buf, odd_bytes);
		}
		count += odd_bytes - bytes_not_transferred;
	}
	return count;
}

static unsigned int smi_stream_poll(struct file *file, poll_table *pt)
{
	unsigned int mask = 0;
	poll_wait(file, &inst->readable, pt);
	//poll_wait(file, &inst->writeable, pt);

	if (!kfifo_is_empty(&inst->rx_fifo))
	{
		mask |= POLLIN | POLLRDNORM;
	}
	mask |= POLLOUT | POLLWRNORM;
	return mask;
}

static const struct file_operations smi_stream_fops = 
{
	.owner = THIS_MODULE,
	.unlocked_ioctl = smi_stream_ioctl,
	.open = smi_stream_open,
	.release = smi_stream_release,
	//.read = smi_stream_read_file,
	.read = smi_stream_read_file_fifo,
	.write = smi_stream_write_file,
	.poll = smi_stream_poll,
};

/****************************************************************************
*
*   smi_stream_probe - called when the driver is loaded.
*
***************************************************************************/

static int smi_stream_dev_probe(struct platform_device *pdev)
{
	int err;
	void *ptr_err;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node, *smi_node;
	printk(KERN_INFO DRIVER_NAME": smi_stream_dev_probe()\n");

	if (!node) 
	{
		dev_err(dev, "No device tree node supplied!");
		return -EINVAL;
	}

	smi_node = of_parse_phandle(node, "smi_handle", 0);

	if (!smi_node) {
		dev_err(dev, "No such property: smi_handle");
		return -ENXIO;
	}

	smi_inst = bcm2835_smi_get(smi_node);

	if (!smi_inst)
	{
		return -EPROBE_DEFER;
	}

	/* Allocate buffers and instance data */
	inst = devm_kzalloc(dev, sizeof(*inst), GFP_KERNEL);
	if (!inst)
	{
		return -ENOMEM;
	}

	inst->dev = dev;
	inst->non_blocking_reads = false;
	inst->non_blocking_writes = false;

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
		goto failed_cdev_add;
	}

	/* Create sysfs entries */
	smi_stream_class = class_create(THIS_MODULE, DEVICE_NAME);
	ptr_err = smi_stream_class;
	if (IS_ERR(ptr_err))
	{
		goto failed_class_create;
	}

	printk(KERN_INFO DRIVER_NAME": adding device to sysfs\n");
	smi_stream_dev = device_create(smi_stream_class, NULL,
					smi_stream_devid, NULL,
					"smi");
	ptr_err = smi_stream_dev;
	if (IS_ERR(ptr_err))
		goto failed_device_create;


	// various stuff
	init_waitqueue_head(&inst->readable);
	init_waitqueue_head(&inst->writeable);
	mutex_init(&inst->read_lock);
	mutex_init(&inst->write_lock);

	dev_info(inst->dev, "initialised");

	return 0;

failed_device_create:
	class_destroy(smi_stream_class);
failed_class_create:
	cdev_del(&smi_stream_cdev);
	err = PTR_ERR(ptr_err);
failed_cdev_add:
	unregister_chrdev_region(smi_stream_devid, 1);
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
	device_destroy(smi_stream_class, smi_stream_devid);
	class_destroy(smi_stream_class);
	cdev_del(&smi_stream_cdev);
	unregister_chrdev_region(smi_stream_devid, 1);

	dev_info(inst->dev, DRIVER_NAME": smi-stream dev removed - OK");
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
MODULE_DESCRIPTION(
	"Character device driver for BCM2835's secondary memory interface streaming mode");
MODULE_AUTHOR("David Michaeli <cariboulabs.co@gmail.com>");
