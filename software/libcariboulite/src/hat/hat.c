#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "HAT"
#include "zf_log/zf_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#include <endian.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "io_utils/io_utils_fs.h"
#include "hat.h"


//===========================================================
int serial_from_uuid(char* uuid, uint32_t *serial)
{
	uint32_t data0 = 0, data4 = 0;
	uint16_t data1 = 0, data2 = 0, data3 = 0, data5 = 0;
	uint32_t ser1, ser2, ser3, ser4;
	if (sscanf(uuid, "%08x-%04hx-%04hx-%04hx-%08x%04hx",
				&data0, &data1, &data2,
				&data3, &data4, &data5) != 6)
	{
		ZF_LOGE("the uuid '%s' is not valid", uuid);
		return -1;
	}
	ser1 = data5;
	ser2 = (data4 & 0xFFFF) | (data3 << 16);
	ser3 = (data2 & 0xFFFF) | (data1 << 16);
	ser4 = data0;
	if (serial) *serial = ser1 ^ ser2 ^ ser3 ^ ser4;
	return 0;
}

//===========================================================
static uint16_t getcrc(char* data, unsigned int size)
{
	uint16_t out = 0;
	int bits_read = 0, bit_flag;

	/* Sanity check: */
	if((data == NULL) || size == 0)
		return 0;

	while(size > 0)
	{
		bit_flag = out >> 15;

		/* Get next bit: */
		out <<= 1;
		// item a) work from the least significant bits
		out |= (*data >> bits_read) & 1;

		/* Increment bit counter: */
		bits_read++;
		if(bits_read > 7)
		{
			bits_read = 0;
			data++;
			size--;
		}

		/* Cycle check: */
		if(bit_flag)
			out ^= CRC16_POLY;
	}

	// item b) "push out" the last 16 bits
	int i;
	for (i = 0; i < 16; ++i) {
		bit_flag = out >> 15;
		out <<= 1;
		if(bit_flag)
			out ^= CRC16_POLY;
	}

	// item c) reverse the bits
	uint16_t crc = 0;
	i = 0x8000;
	int j = 0x0001;
	for (; i != 0; i >>=1, j <<= 1) {
		if (i & out) crc |= j;
	}

	return crc;
}

//===========================================================
static void hat_print_header(struct header_t *header)
{
	ZF_LOGI("# Header: signature=0x%08x", header->signature);
	ZF_LOGI("# Header: format version=0x%02x", header->ver);
	ZF_LOGI("# Header: reserved=%u", header->res);
	ZF_LOGI("# Header: numatoms=%u", header->numatoms);
	ZF_LOGI("# Header: eeplen=%u", header->eeplen);
}

//===========================================================
static void hat_print_vendor(struct vendor_info_t * vinf)
{
	ZF_LOGI("Vendor info: product_uuid %08x-%04x-%04x-%04x-%04x%08x",
							vinf->serial_4,
							vinf->serial_3>>16,
							vinf->serial_3 & 0xffff,
							vinf->serial_2>>16,
							vinf->serial_2 & 0xffff,
							vinf->serial_1);

	ZF_LOGI("Vendor info: raw serial numbers %08x %08x %08x %08x",
							vinf->serial_4,
							vinf->serial_3,
							vinf->serial_2,
							vinf->serial_1);
	ZF_LOGI("Vendor info: product_id 0x%04x", vinf->pid);
	ZF_LOGI("Vendor info: product_ver 0x%04x", vinf->pver);
	ZF_LOGI("Vendor info: vendor \"%s\"   # length=%u", vinf->vstr, vinf->vslen);
	ZF_LOGI("Vendor info: product \"%s\"   # length=%u", vinf->pstr, vinf->pslen);
}

//===========================================================
static void hat_print_gpio(struct gpio_map_t *gpiomap)
{
	ZF_LOGI("GPIO map info: gpio_drive %d", gpiomap->flags & 15); //1111
	ZF_LOGI("GPIO map info: gpio_slew %d", (gpiomap->flags & 48)>>4); //110000
	ZF_LOGI("GPIO map info: gpio_hysteresis %d", (gpiomap->flags & 192)>>6); //11000000
	ZF_LOGI("GPIO map info: back_power %d", gpiomap->power);

	for (int j = 0; j<28; j++)
	{
		if (gpiomap->pins[j] & (1<<7))
		{
			//board uses this pin
			char *pull_str = "INVALID";
			switch ((gpiomap->pins[j] & 96)>>5) { //1100000
				case 0:	pull_str = "PULL DEFAULT";
						break;
				case 1: pull_str = "PULL UP";
						break;
				case 2: pull_str = "PULL DOWN";
						break;
				case 3: pull_str = "PULL NONE";
						break;
			}

			char *func_str = "INVALID";
			switch ((gpiomap->pins[j] & 7)) { //111
				case 0:	func_str = "INPUT";
						break;
				case 1: func_str = "OUTPUT";
						break;
				case 4: func_str = "ALT0";
						break;
				case 5: func_str = "ALT1";
						break;
				case 6: func_str = "ALT2";
						break;
				case 7: func_str = "ALT3";
						break;
				case 3: func_str = "ALT4";
						break;
				case 2: func_str = "ALT5";
						break;
			}

			ZF_LOGI("# GPIO map info: setgpio  %d  %s  %s", j, func_str, pull_str);
		}
	}
}

//===========================================================
static void hat_print_dt_data(struct dt_data_t *data)
{
	ZF_LOGI("# Device Tree info: length = %d", data->dt_data_size);
}


//===========================================================
static int hat_valid(hat_st *hat)
{
	if (!hat->initialized)
	{
		ZF_LOGE("eeprom driver is not initialized");
		return -1;
	}

	uint8_t *location = (uint8_t*)hat->read_buffer;
	uint32_t offset = 0;

	// check the header
	struct header_t* header = (struct header_t*)location;
	if (header->signature != HEADER_SIGN || header->ver != FORMAT_VERSION)
	{
		// signature: 0x52, 0x2D, 0x50, 0x69 ("R-Pi" in ASCII)
		// EEPROM data format version (0x00 reserved, 0x01 = first version)
		ZF_LOGD("Signature (0x%08X) / version (0x%02X) not valid", header->signature, header->ver);
		return 0;	// not valid
	}

	if (header->res != 0)
	{
		ZF_LOGD("Reserved field not zero (0x%08X)", header->res);
		return 0;	// not valid
	}

	if (header->numatoms < 2)
	{
		ZF_LOGD("Number of atoms smaller than 3 (%d)", header->numatoms);
		return 0;	// not valid
	}

	if (header->eeplen > (uint32_t)(hat->read_buffer_size))
	{
		ZF_LOGD("The declared data-size larger than eeprom size (%d > %d)",
							header->eeplen, hat->read_buffer_size);
		return 0;	// not valid
	}

	// Now check every atom and check its validity
	// we won't dive deeper in the atoms as the crc16 should be sufficiently
	// informative on the validity in addition to all the constants etc.
	int i;
	location += sizeof(struct header_t);
	offset += sizeof(struct header_t);
	for (i = 0; i<header->numatoms; i++)
	{
		struct atom_t *atom = (struct atom_t *)location;
		if (atom->type != ATOM_VENDOR_TYPE &&
			atom->type != ATOM_GPIO_TYPE &&
			atom->type != ATOM_DT_TYPE &&
			atom->type != ATOM_CUSTOM_TYPE)
		{
			ZF_LOGD("Found an invalid atom type (%d @ #%d)", atom->type, i);
			return 0;	// not valid
		}

		if (atom->count != i)
		{
			ZF_LOGD("Atom #%d count inconcistent (%d)", i, atom->count);
			return 0;	// not valid
		}

		if ((offset + ATOM_TOTAL_SIZE(atom)) > (uint32_t)(hat->read_buffer_size))
		{
			ZF_LOGD("Atom #%d data length + crc16 don't fit into eeprom", i);
			return 0;	// not valid
		}

		// calculate crc
		uint16_t calc_crc = getcrc((char*)atom, ATOM_DATA_SIZE(atom));
		uint16_t actual_crc = ATOM_CRC(atom);
		if (actual_crc != calc_crc)
		{
			ZF_LOGD("Atom #%d calc_crc (0x%04X) doesn't match the actual_crc (0x%04X)",
							i, calc_crc, actual_crc);
			return 0;	// not valid
		}

		location += ATOM_TOTAL_SIZE(atom);
		offset += ATOM_TOTAL_SIZE(atom);
	}

	if (header->eeplen != offset)
	{
		ZF_LOGD("The eeprom header total length doesn't match contents calculated size (%d <=> %d)",
							header->eeplen, offset);
			return 0;	// not valid
	}

	return 1; // valid
}

//===========================================================
static int hat_contents_parse(hat_st *hat)
{
	uint8_t *location = NULL;
	if (!hat->initialized)
	{
		ZF_LOGE("eeprom driver is not initialized");
		return 0;
	}

	ZF_LOGI("Reading eeprom configuration (%d bytes)...", hat->read_buffer_size);
	if (eeprom_read(&hat->dev, hat->read_buffer, hat->read_buffer_size) < 0)
	{
		ZF_LOGE("Reading from eeprom failed");
		return -1;
	}

	// check the eeprom data's validity
	if ( !hat_valid(hat) )
	{
		ZF_LOGE("EEPROM data is not valid. Try reconfiguring it.");
		return -1;
	}

	location = (uint8_t*)hat->read_buffer;

	// Header
	memcpy(&hat->header, location, sizeof(hat->header));
	location += sizeof(hat->header);

	// Atoms
	for (int i = 0; i < hat->header.numatoms; i++)
	{
		struct atom_t *atom = (struct atom_t *)location;
		uint8_t *atom_data = location + ATOM_HEADER_SIZE;

		// Analyze he atom internal infomration
		switch (atom->type)
		{
			//-------------------------------------------------------------
			case ATOM_VENDOR_TYPE:
				{
					uint8_t *it = atom_data;
					memcpy(&hat->vinf, it, VENDOR_STATIC_SIZE); it += VENDOR_STATIC_SIZE;
					memcpy(&hat->vinf.vstr, it, hat->vinf.vslen); it += hat->vinf.vslen;
					memcpy(&hat->vinf.pstr, it, hat->vinf.pslen); it += hat->vinf.pslen;
					hat->vinf.vstr[hat->vinf.vslen] = 0;
					hat->vinf.pstr[hat->vinf.pslen] = 0;
				} break;

			//-------------------------------------------------------------
			case ATOM_GPIO_TYPE:
				{
					memcpy(&hat->gpiomap, atom_data, GPIO_MAP_SIZE);
				} break;

			//-------------------------------------------------------------
			case ATOM_DT_TYPE:
				{
					ZF_LOGD("Atom datalength = %d", atom->dlen - 2);		// substruct the crc16 size from the dlen
					hat->dt_data.dt_data = (char*)malloc(atom->dlen - 2);
					if (hat->dt_data.dt_data == NULL)
					{
						ZF_LOGE("Failed allocating dt data.");
						return -1;
					}
					hat->dt_data.dt_data_size = atom->dlen - 2;
					memcpy(hat->dt_data.dt_data, atom_data, hat->dt_data.dt_data_size);
				} break;

			//-------------------------------------------------------------
			default:
				ZF_LOGE("Error: unrecognised atom type");
				break;
		}

		location += ATOM_TOTAL_SIZE(atom);
	}

	return 0;
}

//===========================================================
int hat_fill_in(hat_st *hat)
{
	struct atom_t *atom = NULL;
	uint8_t *location = (uint8_t *)hat->write_buffer;
	struct header_t* header = (struct header_t*)hat->write_buffer;

	// Header generation
	// -------------------------------------------------------
	header->signature = HEADER_SIGN;
	header->ver = FORMAT_VERSION;
	header->res = 0;
	header->numatoms = 0;
	header->eeplen = sizeof(struct header_t);

	// Vendor information generation
	// -------------------------------------------------------
	location += header->eeplen;
	atom = (struct atom_t*)location;
	struct vendor_info_t* vinf = (struct vendor_info_t*)(location + ATOM_HEADER_SIZE);

	vinf->pid = hat->product_id;
	vinf->pver = hat->product_version;
	vinf->vslen = strlen(hat->vendor_name);
	vinf->pslen = strlen(hat->product_name);
	strcpy(VENDOR_VSTR_POINT(vinf), hat->vendor_name);
	strcpy(VENDOR_PSTR_POINT(vinf), hat->product_name);

	// read 128 random bits from /dev/urandom
	int random_file = open("/dev/urandom", O_RDONLY);
	ssize_t result = read(random_file, &vinf->serial_1, 16);
	close(random_file);

	if (result <= 0)
	{
		printf("Unable to read from /dev/urandom to set up UUID");
		return -1;
	}
	else
	{
		//put in the version
		vinf->serial_3 = (vinf->serial_3 & 0xffff0fff) | 0x00004000;

		//put in the variant
		vinf->serial_2 = (vinf->serial_2 & 0x3fffffff) | 0x80000000;

		printf("Gen UUID=%08x-%04x-%04x-%04x-%04x%08x\n", 	vinf->serial_4,
															vinf->serial_3>>16,
															vinf->serial_3 & 0xffff,
															vinf->serial_2>>16,
															vinf->serial_2 & 0xffff,
															vinf->serial_1);
		sprintf(hat->generated_uuid, "%08x-%04x-%04x-%04x-%04x%08x", vinf->serial_4,
															vinf->serial_3>>16,
															vinf->serial_3 & 0xffff,
															vinf->serial_2>>16,
															vinf->serial_2 & 0xffff,
															vinf->serial_1);
		serial_from_uuid(hat->generated_uuid, &hat->generated_serial);
	}

	atom->type = ATOM_VENDOR_TYPE;
	atom->count = header->numatoms;
	atom->dlen = VENDOR_INFO_COMPACT_SIZE(vinf) + 2;
	ATOM_CRC(atom) = getcrc((char*)atom, ATOM_DATA_SIZE(atom));
	header->eeplen += ATOM_TOTAL_SIZE(atom);
	header->numatoms += 1;

	// GPIO map information
	// -------------------------------------------------------
	location += ATOM_TOTAL_SIZE(atom);
	atom = (struct atom_t*)location;
	atom->type = ATOM_GPIO_TYPE;
	atom->count = header->numatoms;
	atom->dlen = GPIO_MAP_SIZE + 2;
	struct gpio_map_t* gpio = (struct gpio_map_t*)(location+ATOM_HEADER_SIZE);
	gpio->flags = 0;  	// drive, slew, hysteresis  =>  0=leave at default
	gpio->power = 0;	// 0 = no back power

	// MAPPING: (func,pull,used)
	// [2:0] func_sel    GPIO function as per FSEL GPIO register field in BCM2835 datasheet
	// [4:3] reserved    set to 0
	// [6:5] pulltype    0=leave at default setting,  1=pullup, 2=pulldown, 3=no pull
	// [  7] is_used     1=board uses this pin, 0=not connected and therefore not used

	gpio->pins[2] = GPIO_MAP_BITS(5,2,0);	// SMI SA3
	gpio->pins[3] = GPIO_MAP_BITS(5,2,0);	// SMI SA2
	gpio->pins[4] = GPIO_MAP_BITS(1,0,1);	// FPGA SOFT RESET
	gpio->pins[5] = GPIO_MAP_BITS(1,0,1);	// MXR_RESET
	gpio->pins[6] = GPIO_MAP_BITS(5,2,1);	// SMI SOE_SE
	gpio->pins[7] = GPIO_MAP_BITS(5,2,1);	// SMI SWE_SRW
	gpio->pins[8] = GPIO_MAP_BITS(5,0,1);	// SMI SD0
	gpio->pins[9] = GPIO_MAP_BITS(5,0,1);	// SMI SD1
	gpio->pins[10] = GPIO_MAP_BITS(5,0,1);	// SMI SD2
	gpio->pins[11] = GPIO_MAP_BITS(5,0,1);	// SMI SD3
	gpio->pins[12] = GPIO_MAP_BITS(5,0,1);	// SMI SD4
	gpio->pins[13] = GPIO_MAP_BITS(5,0,1);	// SMI SD5
	gpio->pins[14] = GPIO_MAP_BITS(5,0,1);	// SMI SD6
	gpio->pins[15] = GPIO_MAP_BITS(5,0,1);	// SMI SD7
	gpio->pins[16] = GPIO_MAP_BITS(0,0,1);	// SPI1 CS #2 - MIXER
	gpio->pins[17] = GPIO_MAP_BITS(0,0,1);	// SPI1 CS #1 - MODEM
	gpio->pins[18] = GPIO_MAP_BITS(0,0,1); 	// SPI1 CS #0 - FPGA
	gpio->pins[19] = GPIO_MAP_BITS(0,0,1);	// SPI1 MISO
	gpio->pins[20] = GPIO_MAP_BITS(0,0,1); 	// SPI1 MOSI
	gpio->pins[21] = GPIO_MAP_BITS(0,0,1);	// SPI1 SCK
	gpio->pins[22] = GPIO_MAP_BITS(0,1,1);	// MODEM IRQ
	gpio->pins[23] = GPIO_MAP_BITS(1,0,1);	// MODEM RESET
	gpio->pins[24] = GPIO_MAP_BITS(5,0,1);	// SMI READ_REQ
	gpio->pins[25] = GPIO_MAP_BITS(5,0,1);	// SMI WRITE_REQ
	gpio->pins[26] = GPIO_MAP_BITS(1,0,1);	// FPGA RESET
	gpio->pins[27] = GPIO_MAP_BITS(0,0,1);	// FPGA CDONE
	ATOM_CRC(atom) = getcrc((char*)atom, ATOM_DATA_SIZE(atom));

	header->eeplen += ATOM_TOTAL_SIZE(atom);
	header->numatoms += 1;

	// Device Tree information
	// -------------------------------------------------------
	location += ATOM_TOTAL_SIZE(atom);
	atom = (struct atom_t*)location;
	atom->type = ATOM_DT_TYPE;
	atom->count = header->numatoms;
	atom->dlen = hat->device_tree_buffer_size + 2;
	uint8_t *dt_data = (uint8_t *)(location+ATOM_HEADER_SIZE);
	memcpy(dt_data, hat->device_tree_buffer, hat->device_tree_buffer_size);
	ATOM_CRC(atom) = getcrc((char*)atom, ATOM_DATA_SIZE(atom));

	header->eeplen += ATOM_TOTAL_SIZE(atom);
	header->numatoms += 1;

	hat->write_buffer_used_size = header->eeplen;
	return 0;
}

//===========================================================
int hat_init(hat_st *hat)
{
	ZF_LOGI("Initializing eeprom driver");
	if (eeprom_init_device(&hat->dev) != 0)
	{
		ZF_LOGE("Initializing hat driver failed");
		return -1;
	}

	hat->read_buffer = NULL;
	hat->write_buffer = NULL;
	hat->read_buffer_size = hat->dev.eeprom_size;
	hat->read_buffer = (char *)malloc(hat->read_buffer_size);
	if (hat->read_buffer == NULL)
	{
		ZF_LOGE("hat read buffer allocation failed");
		eeprom_close_device(&hat->dev);
		return -1;
	}

	hat->write_buffer_size = hat->dev.eeprom_size;
	hat->write_buffer = (char *)malloc(hat->write_buffer_size);
	if (hat->write_buffer == NULL)
	{
		ZF_LOGE("hat write buffer allocation failed");
		eeprom_close_device(&hat->dev);
		return -1;
	}
	hat->write_buffer_used_size = 0;
	hat->initialized = true;

	// check if the eeprom is initialized (of contains FFFF garbage)
	hat->eeprom_initialized = false;
	if (eeprom_read(&hat->dev, hat->read_buffer, hat->read_buffer_size) < 0)
	{
		ZF_LOGE("Reading from eeprom failed");
		return -1;
	}
	hat->eeprom_initialized = hat_valid(hat);
	hat_contents_parse(hat);

	return 0;
}

//===========================================================
int hat_close(hat_st *hat)
{
	ZF_LOGI("closing hat driver");
	if (!hat->initialized)
	{
		ZF_LOGE("hat is not initialized");
		return -1;
	}

	if (hat->read_buffer != NULL) free(hat->read_buffer);
	if (hat->write_buffer != NULL) free(hat->write_buffer);
	hat->read_buffer_size = 0;
	hat->write_buffer_size = 0;

	return 0;
}

//===========================================================
int hat_generate_write_config(hat_st *hat)
{
	if (!hat->eeprom_initialized)
	{
		ZF_LOGI("Filling in HAT information");
		hat_fill_in(hat);
		ZF_LOGI("Writing into HAT");
		eeprom_write(&hat->dev, hat->write_buffer, hat->write_buffer_used_size);
		ZF_LOGI("Writing into HAT - Done");
	}
	else
	{
		
		sprintf(hat->generated_uuid, "%08x-%04x-%04x-%04x-%04x%08x", hat->vinf.serial_4,
															hat->vinf.serial_3>>16,
															hat->vinf.serial_3 & 0xffff,
															hat->vinf.serial_2>>16,
															hat->vinf.serial_2 & 0xffff,
															hat->vinf.serial_1);
		serial_from_uuid(hat->generated_uuid, &hat->generated_serial);
	}
	return 0;
}

//===========================================================
int hat_print(hat_st *hat)
{
	if (!hat->eeprom_initialized)
	{
		if (hat_contents_parse(hat) != 0)
		{
			ZF_LOGE("Parsing EEPROM data failed - try reconfiguring");
			return -1;
		}
	}

	hat_print_header(&hat->header);
	hat_print_vendor(&hat->vinf);
	hat_print_gpio(&hat->gpiomap);
	hat_print_dt_data(&hat->dt_data);

	return 0;
}

//===========================================================
// If the board is not detected, try detecting it outside:
// go directly to the eeprom configuration application
// prompt the user
// configure and tell the user he needs to reboot his system
int hat_detect_board(hat_board_info_st *info)
{
    int exists = 0;
    int size, dir, file, dev;

    // check if a hat is attached anyway..
	char hat_dir_path[] = "/proc/device-tree/hat";
    exists = io_utils_file_exists(hat_dir_path, &size, &dir, &file, &dev);
    if (!exists || !dir)
	{
		ZF_LOGI("This board is not configured yet as a hat.");
		return 0;
	}

	io_utils_read_string_from_file(hat_dir_path, "name", info->category_name, sizeof(info->category_name));
	io_utils_read_string_from_file(hat_dir_path, "product", info->product_name, sizeof(info->product_name));
	io_utils_read_string_from_file(hat_dir_path, "product_id", info->product_id, sizeof(info->product_id));
	io_utils_read_string_from_file(hat_dir_path, "product_ver", info->product_version, sizeof(info->product_version));
	io_utils_read_string_from_file(hat_dir_path, "uuid", info->product_uuid, sizeof(info->product_uuid));
	io_utils_read_string_from_file(hat_dir_path, "vendor", info->product_vendor, sizeof(info->product_vendor));

	// numeric version
	if (info->product_version[0] == '0' && (info->product_version[1] == 'x' ||
											info->product_version[1] == 'X'))
		sscanf(info->product_version, "0x%08x", &info->numeric_version);
	else
		sscanf(info->product_version, "%08x", &info->numeric_version);

	// numeric productid
	if (info->product_id[0] == '0' && (info->product_id[1] == 'x' || info->product_id[1] == 'X'))
		sscanf(info->product_id, "0x%08x", &info->numeric_product_id);
	else
		sscanf(info->product_id, "%08x", &info->numeric_product_id);
	
	// serial number
	if (serial_from_uuid(info->product_uuid, &info->numeric_serial_number) != 0)
	{
		// should never happen
		return 0;
	}

    return 1;
}

//===========================================================
int hat_detect_from_eeprom(hat_board_info_st *info)
{
	hat_st hat = 
	{
		.dev = 
		{
			.i2c_address =  0x50,    // the i2c address of the eeprom chip
			.eeprom_type = eeprom_type_24c32,
		},
	};
	
	if (hat_init(&hat) != 0 || (info == NULL))
	{
		return -1;
	}
	
	if (!hat.eeprom_initialized)
	{
		return 0;
	}
	
	sprintf(info->category_name, "hat");
	memcpy(info->product_name, VENDOR_PSTR_POINT(&hat.vinf), hat.vinf.pslen);
	info->product_name[hat.vinf.pslen] = 0;
	sprintf(info->product_id, "%d", hat.vinf.pid);
	sprintf(info->product_version, "%d", hat.vinf.pver);
	memcpy(info->product_vendor, VENDOR_VSTR_POINT(&hat.vinf), hat.vinf.vslen);
	info->product_vendor[hat.vinf.vslen] = 0;

	sprintf(info->product_uuid, "%08x-%04x-%04x-%04x-%04x%08x", hat.vinf.serial_4,
															hat.vinf.serial_3>>16,
															hat.vinf.serial_3 & 0xffff,
															hat.vinf.serial_2>>16,
															hat.vinf.serial_2 & 0xffff,
															hat.vinf.serial_1);
	
	info->numeric_version = hat.vinf.pver;
	info->numeric_product_id = hat.vinf.pid;
	
	serial_from_uuid(info->product_uuid, &info->numeric_serial_number);
	
	return 1;
}

//===========================================================
void hat_print_board_info(hat_board_info_st *info, bool log)
{
	if (log)
	{
		ZF_LOGI("# Board Info - Category name: %s", info->category_name);
		ZF_LOGI("# Board Info - Product name: %s", info->product_name);
		ZF_LOGI("# Board Info - Product ID: %s, Numeric: %d", info->product_id, info->numeric_product_id);
		ZF_LOGI("# Board Info - Product Version: %s, Numeric: %d", info->product_version, info->numeric_version);
		ZF_LOGI("# Board Info - Product UUID: %s, Numeric serial: 0x%08X", info->product_uuid, info->numeric_serial_number);
		ZF_LOGI("# Board Info - Vendor: %s", info->product_vendor);
	}
	else
	{
		printf("	Category name: %s\n", info->category_name);
		printf("	Product name: %s\n", info->product_name);
		printf("	Product ID: %s, Numeric: %d\n", info->product_id, info->numeric_product_id);
		printf("	Product Version: %s, Numeric: %d\n", info->product_version, info->numeric_version);
		printf("	Product UUID: %s, Numeric serial: 0x%08X\n", info->product_uuid, info->numeric_serial_number);
		printf("	Vendor: %s\n", info->product_vendor);
	}
}
