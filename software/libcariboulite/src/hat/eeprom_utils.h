#ifndef __EEPROM_UTILS_H__
#define __EEPROM_UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* EEPROM types */
typedef enum
{
	eeprom_type_24c32 = 4096,
	eeprom_type_24c64 = 8192,
	eeprom_type_24c128 = 16384,
	eeprom_type_24c256 = 32768,
	eeprom_type_24c512 = 65536,
	eeprom_type_24c1024 = 131072,
} eeprom_type_en;

typedef struct
{
	uint8_t i2c_address;
	eeprom_type_en eeprom_type;
	char eeprom_type_name[32];

	int bus;
	int eeprom_size;
	int initialized;	
} eeprom_utils_st;

int eeprom_init_device(eeprom_utils_st *ee);
int eeprom_close_device(eeprom_utils_st *ee);
int eeprom_write(eeprom_utils_st *ee, char* buffer, int length);
int eeprom_read(eeprom_utils_st *ee, char* buffer, int length);

int file_exists(char* fname, int *size, int *dir, int *file, int *dev);
int write_to_file(char* fname, char* data, int size_of_data);
int read_from_file(char* fname, char* data, int len_to_read);
int read_string_from_file(char* path, char* filename, char* data, int len);

#ifdef __cplusplus
}
#endif

#endif // __EEPROM_UTILS_H__