#ifndef __HAT_H__
#define __HAT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "eeprom_utils.h"


/* Header type */
#define FORMAT_VERSION 		0x01
#define MAX_STRLEN 			256

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

#define INFO_MAX_LEN 64
typedef struct
{
    char category_name[INFO_MAX_LEN];
    char product_name[INFO_MAX_LEN];
    char product_id[INFO_MAX_LEN];
    char product_version[INFO_MAX_LEN];
    char product_uuid[INFO_MAX_LEN];
    char product_vendor[INFO_MAX_LEN];

    uint32_t numeric_serial_number;
    uint32_t numeric_version;
    uint32_t numeric_product_id;
} hat_board_info_st;

typedef struct
{
	char vendor_name[MAX_STRLEN];
	char product_name[MAX_STRLEN];
	int product_id;
	int product_version;
	unsigned char* device_tree_buffer;
	int device_tree_buffer_size;

	// eeprom device
	eeprom_utils_st dev;

	// buffers (read and write)
	char* read_buffer;
	int read_buffer_size;

	char* write_buffer;
	int write_buffer_size;
	int write_buffer_used_size;

	// hat initialized
	bool initialized;

	// eeprom contains valid information (not FFF)
	bool eeprom_initialized;

	// hat definitions
	struct header_t header;
	struct vendor_info_t vinf;
	struct gpio_map_t gpiomap;
	struct dt_data_t dt_data;
	unsigned char* custom_data;
	
	// temporary date
	char generated_uuid[128];
	uint32_t generated_serial;
}  hat_st;

int hat_init(hat_st *ee);
int hat_close(hat_st *ee);
int hat_fill_in(hat_st *ee);
int hat_print(hat_st *ee);
int hat_generate_write_config(hat_st *ee);

// HAT functions after configuration is written and system is 
// restarted. In this stage the sysfs shall contain the hat definitions
int hat_detect_board(hat_board_info_st *info);
int hat_detect_from_eeprom(hat_board_info_st *info);
void hat_print_board_info(hat_board_info_st *info, bool log);
int serial_from_uuid(char* uuid, uint32_t *serial);

#ifdef __cplusplus
}
#endif

#endif // __HAT_H__