#ifndef __IO_UTILS_I2C_H__
#define __IO_UTILS_I2C_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	int bus;
	uint8_t address;
	char i2c_device_file[32];
	
	int fd;
} io_utils_i2c_st;

int io_utils_i2c_open(io_utils_i2c_st* dev, int bus, uint8_t address);
int io_utils_i2c_close(io_utils_i2c_st* dev);
int io_utils_i2c_write(io_utils_i2c_st* dev, uint8_t *data, size_t len);
int io_utils_i2c_read(io_utils_i2c_st* dev, uint8_t *data, size_t len);
int io_utils_i2c_read_reg(io_utils_i2c_st* dev, uint8_t reg, uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // __IO_UTILS_I2C_H__
