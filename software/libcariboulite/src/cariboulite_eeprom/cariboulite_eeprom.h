#ifndef __CARIBOU_EEPROM_H__
#define __CARIBOU_EEPROM_H__

#include <stdint.h>

/* Atom types */
#define ATOM_INVALID_TYPE   0x0000
#define ATOM_VENDOR_TYPE    0x0001
#define ATOM_GPIO_TYPE      0x0002
#define ATOM_DT_TYPE        0x0003
#define ATOM_CUSTOM_TYPE    0x0004
#define ATOM_HINVALID_TYPE  0xffff

#define ATOM_VENDOR_NUM     0x0000
#define ATOM_GPIO_NUM       0x0001
#define ATOM_DT_NUM         0x0002

//minimal sizes of data structures
#define HEADER_SIZE 12
#define ATOM_SIZE   10
#define VENDOR_SIZE 22
#define GPIO_SIZE   30
#define CRC_SIZE     2

#define GPIO_MIN     2
#define GPIO_COUNT  28

#define FORMAT_VERSION 0x01

#define CRC16 0x8005

#define MAX_STRLEN 256
#define MAX_EEPROM_BUF_SIZE 16384

typedef enum
{
	eeprom_type_24c32 = 4096,
	eeprom_type_24c64 = 8192,
	eeprom_type_24c128 = 16384,
	eeprom_type_24c256 = 32768,
	eeprom_type_24c512 = 65536,
	eeprom_type_24c1024 = 131072,
} eeprom_type_en;

/* EEPROM header structure */
// Signature is "R-Pi" in ASCII. It is required to reversed (little endian) on disk.
#define HEADER_SIGN be32toh((((char)'R' << 24) | ((char)'-' << 16) | ((char)'P' << 8) | ((char)'i')))

struct header_t {
	uint32_t signature;
	unsigned char ver;
	unsigned char res;
	uint16_t numatoms;
	uint32_t eeplen;
};

/* Atom structure */
struct atom_t 
{
	uint16_t type;
	uint16_t count;
	uint32_t dlen;
	char* data;
	uint16_t crc16;
};

/* Vendor info atom data */
struct vendor_info_d 
{
	uint32_t serial_1; //least significant
	uint32_t serial_2;
	uint32_t serial_3;
	uint32_t serial_4; //most significant
	uint16_t pid;
	uint16_t pver;
	unsigned char vslen;
	unsigned char pslen;
	char vstr[MAX_STRLEN + 1];
	char pstr[MAX_STRLEN + 1];
};

/* GPIO map atom data */
struct gpio_map_d 
{
	unsigned char flags;
	unsigned char power;
	unsigned char pins[GPIO_COUNT];
};

#define CARIBOULITE_CUSTOM_DATA_LEN 256
struct caribou_lite_data_d
{
	char custom_data[CARIBOULITE_CUSTOM_DATA_LEN];
};

typedef struct
{
	uint8_t i2c_address;
	eeprom_type_en eeprom_type;

	int initialized;
	int bus;
	char eeprom_type_name[32];
	int eeprom_size;
	char* eeprom_buffer;
	int eeprom_buffer_size;

	struct header_t header;
	struct vendor_info_d vinf;
	struct gpio_map_d gpiomap;
	struct caribou_lite_data_d custom_data;
}  cariboulite_eeprom_st;

int cariboulite_eeprom_init(cariboulite_eeprom_st *ee);
int cariboulite_eeprom_close(cariboulite_eeprom_st *ee);
int cariboulite_eeprom_print(cariboulite_eeprom_st *ee);

#endif // __CARIBOU_EEPROM_H__