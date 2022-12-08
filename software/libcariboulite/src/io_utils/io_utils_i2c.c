#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "IO_UTILS_I2C"
#include "zf_log/zf_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/wait.h>
#include <ctype.h>
#include <endian.h>
#include <fcntl.h>
#include <sys/stat.h>
#include  <sys/types.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

#include "io_utils_i2c.h"

//===================================================================
int io_utils_i2c_open(io_utils_i2c_st* dev, int bus, uint8_t address)
{
	dev->bus = bus;
	dev->address = address;
	sprintf(dev->i2c_device_file, "/dev/i2c-%d", dev->bus);
			
	dev->fd = open(dev->i2c_device_file, O_RDWR);
	if (dev->fd == -1)
	{
		ZF_LOGE("opening device file '%s' failed", dev->i2c_device_file);
		return -1;
	}
	return 0;
}

//===================================================================
int io_utils_i2c_write(io_utils_i2c_st* dev, uint8_t *data, size_t len)
{
	struct i2c_msg message = {	.addr = dev->address,
								.flags = 0,
								.len = len,
								.buf = data};
								
	struct i2c_rdwr_ioctl_data ioctl_data = { &message, 1 };
	int result = ioctl(dev->fd, I2C_RDWR, &ioctl_data);
	if (result != 1)
	{
		ZF_LOGE("writing to i2c failed");
		return -1;
	}
	return 0;
}

//===================================================================
int io_utils_i2c_read(io_utils_i2c_st* dev, uint8_t *data, size_t len)
{
	struct i2c_msg messages = 	{ 	
									.addr = dev->address, 
									.flags = I2C_M_RD, 
									.len = len, 
									.buf = data 
								};
								
	struct i2c_rdwr_ioctl_data ioctl_data = { &messages, 1 };
	
	int result = ioctl(dev->fd, I2C_RDWR, &ioctl_data);
	if (result != 1)
	{
		ZF_LOGE("reading from i2c failed");
		return -1;
	}
	return 0;
}

//===================================================================
int io_utils_i2c_read_reg(io_utils_i2c_st* dev, uint8_t reg, uint8_t *data, size_t len)
{
	struct i2c_msg messages[] = { {	
									.addr = dev->address, 
									.flags = 0, 
									.len = 1, 
									.buf = &reg, 
								  },
								  {	
									.addr = dev->address, 
									.flags = I2C_M_RD, 
									.len = len, 
									.buf = data, 
								  }
								};
								
	struct i2c_rdwr_ioctl_data ioctl_data = { messages, 2 };							
	int result = ioctl(dev->fd, I2C_RDWR, &ioctl_data);
	if (result != 2)
	{
		ZF_LOGE("reading reg from i2c failed");
		return -1;
	}
	return 0;
}



