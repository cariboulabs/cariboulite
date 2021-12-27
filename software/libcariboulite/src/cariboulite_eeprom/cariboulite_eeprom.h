#ifndef __CARIBOU_EEPROM_H__
#define __CARIBOU_EEPROM_H__

#include <stdint.h>

/* Header type */
#define FORMAT_VERSION 		0x01
#define MAX_STRLEN 			256
#define MAX_EEPROM_BUF_SIZE 16384

// Signature is "R-Pi" in ASCII. It is required to reversed (little endian) on disk.
#define HEADER_SIGN be32toh((((char)'R' << 24) | ((char)'-' << 16) | ((char)'P' << 8) | ((char)'i')))

/* Atom type */
#define ATOM_INVALID_TYPE   0x0000
#define ATOM_VENDOR_TYPE    0x0001
#define ATOM_GPIO_TYPE      0x0002
#define ATOM_DT_TYPE        0x0003
#define ATOM_CUSTOM_TYPE    0x0004
#define ATOM_HINVALID_TYPE  0xffff
#define ATOM_VENDOR_NUM     0x0000
#define ATOM_GPIO_NUM       0x0001
#define ATOM_DT_NUM         0x0002
#define CRC16_POLY			0x8005

/* GPIO type */
#define GPIO_MIN     		2
#define GPIO_COUNT  		28

/* EEPROM yypes */
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
struct header_t {
	uint32_t signature;
	unsigned char ver;
	unsigned char res;
	uint16_t numatoms;
	uint32_t eeplen;
};

#define HEADER_SIZE 			( sizeof(struct header_t) )

/* Atom structure */
struct atom_t 
{
	uint16_t type;
	uint16_t count;
	uint32_t dlen;
	char* data;
	uint16_t crc16;
};

#define ATOM_HEADER_SIZE  		( 8 )
#define ATOM_CRC_SIZE  			( 2 )
#define ATOM_DATA_SIZE(a) 		( ATOM_HEADER_SIZE + ((a)->dlen) - 2 )		// at the dlen includes the CRC16 size
#define ATOM_CRC(a)  			( *(uint16_t*)( ((uint8_t*)(a)) + ATOM_DATA_SIZE(a) ) )
#define ATOM_TOTAL_SIZE(a) 		( ATOM_DATA_SIZE(a)  + ATOM_CRC_SIZE)

/* Vendor info atom data */
struct vendor_info_t 
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

#define VENDOR_STATIC_SIZE				( 4*sizeof(uint32_t) + 2*sizeof(uint16_t) + 2*sizeof(char) )
#define VENDOR_VSTR_POINT(v)			( (char*)(v) + VENDOR_STATIC_SIZE )
#define VENDOR_PSTR_POINT(v)			( (char*)(v) + VENDOR_STATIC_SIZE + (v)->vslen )
#define VENDOR_INFO_COMPACT_SIZE(v)		( VENDOR_STATIC_SIZE + (v)->vslen + (v)->pslen )

/* GPIO map atom data */
struct gpio_map_t 
{
	unsigned char flags;
	unsigned char power;
	unsigned char pins[GPIO_COUNT];
};
#define GPIO_MAP_SIZE 					(sizeof(struct gpio_map_t))

// [2:0] func_sel    GPIO function as per FSEL GPIO register field in BCM2835 datasheet
// [4:3] reserved    set to 0
// [6:5] pulltype    0=leave at default setting,  1=pullup, 2=pulldown, 3=no pull
// [  7] is_used     1=board uses this pin, 0=not connected and therefore not used

#define GPIO_MAP_BITS(func,pull,used) ( (uint8_t)( ((func)&0x7) | ((pull)&0x3)<<5 | ((used)&0x1)<<7 ) )

struct dt_data_t
{
	char* dt_data;
	uint32_t dt_data_size;
};

#define CARIBOULITE_CUSTOM_DATA_LEN 256
struct caribou_lite_data_t
{
	char custom_data[CARIBOULITE_CUSTOM_DATA_LEN];
};

typedef struct
{
	uint8_t i2c_address;
	eeprom_type_en eeprom_type;

	int initialized;
	int eeprom_initialized;
	int bus;
	char eeprom_type_name[32];
	int eeprom_size;
	char* eeprom_buffer;
	int eeprom_buffer_total_size;

	char* eeprom_buffer_to_write;
	int eeprom_buffer_to_write_total_size;
	int eeprom_buffer_to_write_used_size;

	struct header_t header;
	struct vendor_info_t vinf;
	struct gpio_map_t gpiomap;
	struct dt_data_t dt_data;
	struct caribou_lite_data_t custom_data;
}  cariboulite_eeprom_st;

int cariboulite_eeprom_init(cariboulite_eeprom_st *ee);
int cariboulite_eeprom_close(cariboulite_eeprom_st *ee);
int cariboulite_eeprom_fill_in(cariboulite_eeprom_st *ee, int prod_id, int prod_ver);
int cariboulite_eeprom_print(cariboulite_eeprom_st *ee);
int cariboulite_eeprom_generate_write_config(cariboulite_eeprom_st *ee, int prod_id, int prod_ver);

#endif // __CARIBOU_EEPROM_H__