/*
 * Simple Linux wrapper for access to /dev/spidev
 * File: "spi.h"
 * Credit: Alex Zorg (@azorg) @ https://github.com/azorg/spi/blob/master/spi.h
 */

#ifndef SPI_H
#define SPI_H
//-----------------------------------------------------------------------------
#include <linux/spi/spidev.h>
#include <stdint.h>
//-----------------------------------------------------------------------------
// common error codes (return values)
#define SPI_ERR_NONE        0 // no error, success
#define SPI_ERR_OPEN       -1 // failed to open the bus
#define SPI_ERR_SET_MODE   -2 // can't set bus mode
#define SPI_ERR_GET_MODE   -3 // can't get bus mode
#define SPI_ERR_GET_LSB    -4 // can't get 'LSB first'
#define SPI_ERR_SET_BITS   -5 // can't set bits per word
#define SPI_ERR_GET_BITS   -6 // can't get bits per word
#define SPI_ERR_SET_SPEED  -7 // can't set max speed [Hz]
#define SPI_ERR_GET_SPEED  -8 // can't get max speed [Hz]
#define SPI_ERR_READ       -9 // can't read
#define SPI_ERR_WRITE     -10 // can't write
#define SPI_ERR_EXCHANGE  -11 // can't read/write

//----------------------------------------------------------------------------
#ifdef SPI_DEBUG
#  include <stdio.h>  // fprintf()
#    define SPI_DBG(fmt, arg...) fprintf(stderr, "SPI: " fmt "\n", ## arg)
#else
#  define SPI_DBG(fmt, ...) // debug output off
#endif // SPI_DEBUG
//----------------------------------------------------------------------------
// `spi_t` type structure
typedef struct spi_ {
    int   fd;    // file descriptor: fd = open(filename, O_RDWR);
    __u32 speed; // speed [Hz]
    __u8  mode;  // SPI mode
    __u8  lsb;   // LSB first
    __u8  bits;  // bits per word
} spi_t;
//----------------------------------------------------------------------------
#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus
//----------------------------------------------------------------------------
// open and init SPIdev
// * spi_mode may have next mask: SPI_LOOP | SPI_CPHA | SPI_CPOL |
//   SPI_LSB_FIRST | SPI_CS_HIGH SPI_3WIRE | SPI_NO_CS | SPI_READY
//   or 0 by zefault
int spi_init(spi_t *self,
             const char *device, // filename like "/dev/spidev0.0"
             int mode,           // SPI_* (look "linux/spi/spidev.h")
             int bits,           // bits per word (usually 8)
             int speed);         // max speed [Hz]
//----------------------------------------------------------------------------
// sets mode on existing spi
int spi_set_mode(spi_t* self, int mode);
//----------------------------------------------------------------------------
// sets speed on existing spi
int spi_set_speed(spi_t* self, int speed);
//----------------------------------------------------------------------------
// close SPIdev file and free memory
void spi_free(spi_t *self);
//----------------------------------------------------------------------------
// read data from SPIdev
int spi_read(spi_t *self, void* rx_buf, int len);
//----------------------------------------------------------------------------
// write data to SPIdev
int spi_write(spi_t *self, const void* tx_buf, int len);
//----------------------------------------------------------------------------
// read and write `len` bytes from/to SPIdev
int spi_exchange(spi_t *self, void* rx_buf, const void* tx_buf, int len);
//----------------------------------------------------------------------------
// read data from SPIdev from specific register address
int spi_read_reg8(spi_t *self, uint8_t reg_addr, void* rx_buf, int len);
//----------------------------------------------------------------------------
// write data to SPIdev to specific register address
int spi_write_reg8(spi_t *self, uint8_t reg_addr, const void* tx_buf, int len);
//----------------------------------------------------------------------------
// get error code destriprion
char* spi_get_code_desc(int err_code);
//----------------------------------------------------------------------------
#ifdef __cplusplus
}
#endif // __cplusplus
//----------------------------------------------------------------------------
#endif // SPI_H

/*** end of "spi.h" file ***/