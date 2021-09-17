#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE_EEPROM"
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
#include  <sys/types.h>
#include "cariboulite_eeprom.h"
#include "cariboulite_dtbo.h"

//===========================================================
static int file_exists(char* fname, int *size, int *dir, int *file, int *dev)
{
	struct stat st;
	if(stat(fname,&st) != 0)
	{
    	return 0;
	}

	if (dir) *dir = S_ISDIR(st.st_mode);
	if (file) *file = S_ISREG(st.st_mode);
	if (dev) *dev =  S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode);
	if (size) *size = st.st_size;

	return 1;
}

//===========================================================
static int write_to_file(char* fname, char* data, int size_of_data)
{
	FILE* fid = NULL;

	fid = fopen(fname, "wb");
	if (fid == NULL) return -1;
	int wrote = fwrite(data, 1, size_of_data, fid);
	if (wrote != size_of_data)
	{
		ZF_LOGE("Writing to file failed (wrote %d instead of %d)", wrote, size_of_data);
		fclose(fid);
		return -1;
	}
	fclose(fid);
	return 0;
}

//===========================================================
static int read_from_file(char* fname, char* data, int len_to_read)
{
	FILE* fid = NULL;

	fid = fopen(fname, "rb");
	if (fid == NULL) return -1;
	int bytes_read = fread(data, 1, len_to_read, fid);
	if (bytes_read != len_to_read)
	{
		ZF_LOGE("Reading from file failed (read %d instead of %d)", bytes_read, len_to_read);
		fclose(fid);
		return -1;
	}
	fclose(fid);
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
static int i2cbus_exists(void)
{
	int dev = 0;
	// first check 'i2c-9'
	if ( file_exists("/dev/i2c-9", NULL, NULL, NULL, &dev) )
	{
		if (dev) return 9;
		ZF_LOGE("i2c-9 was found but not a valid device file");
	}

	// then check 'i2c-0'
	if ( file_exists("/dev/i2c-0", NULL, NULL, NULL, &dev) )
	{
		if (dev) return 0;
		ZF_LOGE("i2c-0 was found but not a valid device file");
	}
	return -1;
}

//===========================================================
static void parse_command(char *line, char **argv)
{
     while (*line != '\0') {       /* if not the end of line ....... */ 
          while (*line == ' ' || *line == '\t' || *line == '\n')
               *line++ = '\0';     /* replace white spaces with 0    */
          *argv++ = line;          /* save the argument position     */
          while (*line != '\0' && *line != ' ' && 
                 *line != '\t' && *line != '\n') 
               line++;             /* skip the argument until ...    */
     }
     *argv = '\0';                 /* mark the end of argument list  */
}

//===========================================================
static int execute_command(char **argv)
{
     pid_t  pid;
     int    status;

     if ((pid = fork()) < 0) {     // fork a child process
          printf("*** ERROR: forking child process failed\n");
          exit(1);
     }
     else if (pid == 0) {          // for the child process:
          if (execvp(*argv, argv) < 0) {     // execute the command
               printf("*** ERROR: exec failed\n");
               exit(1);
          }
     }
     else {                                  /* for the parent:      */
          while (wait(&status) != pid)       /* wait for completion  */
               ;
     }
	 return status;
}

//===========================================================
static int probe_gpio_i2c(void)
{
	ZF_LOGI("trying to modprobe i2c_dev");
	char modprobe[] = "/usr/sbin/modprobe i2c_dev";
	char *argv[64];
	parse_command(modprobe, argv);
	if (execute_command(argv) != 0)
	{
		ZF_LOGE("MODPROBE of the eeprom 'i2c_dev' execution failed");
		return -1;
	}

	char dtoverlay[] = "/usr/bin/dtoverlay i2c-gpio i2c_gpio_sda=0 i2c_gpio_scl=1 bus=9";
	parse_command(dtoverlay, argv);
	if (execute_command(argv) != 0)
	{
		ZF_LOGE("DTOVERLAY execution failed");
		return -1;
	}

	int dev = 0;
	if (file_exists("/dev/i2c-9", NULL, NULL, NULL, &dev))
	{
		if (dev) return 0;
		ZF_LOGE("i2c-9 was found but it is not a valid device file");
	}
	else
	{
		ZF_LOGE("i2c-9 was not found");
	}

	return -1;
}

//===========================================================
static int init_eeprom_device(char* eeprom_type, uint8_t i2c_addr)
{
	int bus = i2cbus_exists();
	if (bus >= 0)
	{
		ZF_LOGI("i2c-%d has been found successfully", bus);
	}

	// neither bus 0,9 were found in the dev dir -> we need to probe bus9
	if (bus == -1)
	{
		if (probe_gpio_i2c() == -1)
		{
			ZF_LOGE("Failed to probe i2c-9");
			return -1;
		}
		else 
		{
			ZF_LOGI("i2c-9 has been probed successfully");
		}
	}

	// probe the eeprom driver
	ZF_LOGI("trying to modprobe at24");
	char modprobe[] = "/usr/sbin/modprobe at24";
	char *argv[64];
	parse_command(modprobe, argv);
	if (execute_command(argv) != 0)
	{
		ZF_LOGE("MODPROBE of the eeprom 'at24' execution failed");
		return -1;
	}

	// the sys dir path
	char sys_dir_bus[128] = {0};
	char sys_dir_bus_addr[128] = {0};
	char sys_dir_bus_new_dev[128] = {0};
	sprintf(sys_dir_bus, "/sys/class/i2c-adapter/i2c-%d", bus);
	sprintf(sys_dir_bus_addr, "%s/%d-00%x", sys_dir_bus, bus, i2c_addr);
	sprintf(sys_dir_bus_new_dev, "%s/new_device", sys_dir_bus);

	int dir = 0;
	int ee_exists = file_exists(sys_dir_bus_addr, NULL, &dir, NULL, NULL);
	if (!ee_exists || !dir)
	{
		// create the device
		char dev_type[64] = {0};
		sprintf(dev_type, "%s 0x%x", eeprom_type, i2c_addr);
		if (write_to_file(sys_dir_bus_new_dev, dev_type, strlen(dev_type) + 1) != 0)
		{
			ZF_LOGE("EEPROM on addr 0x%x probing failed", i2c_addr);
			return -1;
		}
	}

	// recheck that the file exists now
	ee_exists = file_exists(sys_dir_bus_addr, NULL, &dir, NULL, NULL);
	if (!ee_exists || !dir)
	{
		ZF_LOGE("EEPROM on addr 0x%x probing failed - file was not found", i2c_addr);
		return -1;
	}
	ZF_LOGI("EEPROM on addr 0x%x probing successful", i2c_addr);

	return bus;
}

//===========================================================
static int close_eeprom_device(int bus, uint8_t i2c_addr)
{
	int dir = 0;
	char sys_dir_bus[128] = {0};
	char sys_dir_bus_addr[128] = {0};
	char sys_dir_bus_del_dev[128] = {0};
	sprintf(sys_dir_bus, "/sys/class/i2c-adapter/i2c-%d", bus);
	sprintf(sys_dir_bus_addr, "%s/%d-00%x", sys_dir_bus, bus, i2c_addr);
	sprintf(sys_dir_bus_del_dev, "%s/delete_device", sys_dir_bus);

	int ee_exists = file_exists(sys_dir_bus_addr, NULL, &dir, NULL, NULL);
	if (ee_exists && dir)
	{
		char dev_type[64] = {0};
		sprintf(dev_type, "0x%x", i2c_addr);
		if (write_to_file(sys_dir_bus_del_dev, dev_type, strlen(dev_type) + 1) != 0)
		{
			ZF_LOGE("EEPROM on addr 0x%x deletion failed on bus %d", i2c_addr, bus);
			return -1;
		}
	}
	ZF_LOGI("EEPROM addr 0x%x on bus %d deletion was successful", i2c_addr, bus);
	return 0;
}

//===========================================================
static int write_eeprom(cariboulite_eeprom_st *ee, char* buffer, int length)
{
	char eeprom_fname[200] = {0};
	sprintf(eeprom_fname, "/sys/class/i2c-adapter/i2c-%d/%d-00%x/eeprom", 
			ee->bus, ee->bus, ee->i2c_address);
	int ee_exists = file_exists(eeprom_fname, NULL, NULL, NULL, NULL);
	if (!ee_exists)
	{
		ZF_LOGE("The eeprom driver for bus %d, adde 0x%x is not initialized", ee->bus, ee->i2c_address);
		return -1;
	}

	if (length > ee->eeprom_size)
	{
		ZF_LOGW("EEPROM write size (length=%d) exceeds %d bytes, truncating", length, ee->eeprom_size);
		length = ee->eeprom_size;
	}
	return write_to_file(eeprom_fname, buffer, length);
}

//===========================================================
static int read_eeprom(cariboulite_eeprom_st *ee, char* buffer, int length)
{
	char eeprom_fname[200] = {0};
	sprintf(eeprom_fname, "/sys/class/i2c-adapter/i2c-%d/%d-00%x/eeprom", 
			ee->bus, ee->bus, ee->i2c_address);
	int ee_exists = file_exists(eeprom_fname, NULL, NULL, NULL, NULL);
	if (!ee_exists)
	{
		ZF_LOGE("The eeprom driver for bus %d, adde 0x%x is not initialized", ee->bus, ee->i2c_address);
		return -1;
	}

	if (length > ee->eeprom_size)
	{
		ZF_LOGW("EEPROM read size (length=%d) exceeds %d bytes, truncating", length, ee->eeprom_size);
		length = ee->eeprom_size;
	}

	return read_from_file(eeprom_fname, buffer, length);
}

//===========================================================
static void eeprom_print_header(struct header_t *header)
{
	ZF_LOGI("# HEADER: signature=0x%08x", header->signature);
	ZF_LOGI("# HEADER: format version=0x%02x", header->ver);
	ZF_LOGI("# HEADER: reserved=%u", header->res);
	ZF_LOGI("# HEADER: numatoms=%u", header->numatoms);
	ZF_LOGI("# HEADER: eeplen=%u", header->eeplen);
}

//===========================================================
static void eeprom_print_vendor(struct vendor_info_t * vinf)
{
	ZF_LOGI("Vendor info: product_uuid %08x-%04x-%04x-%04x-%04x%08x", 
							vinf->serial_4, vinf->serial_3>>16, 
							vinf->serial_3 & 0xffff, vinf->serial_2>>16, 
							vinf->serial_2 & 0xffff, vinf->serial_1);
	ZF_LOGI("Vendor info: product_id 0x%04x", vinf->pid);
	ZF_LOGI("Vendor info: product_ver 0x%04x", vinf->pver);
	ZF_LOGI("Vendor info: vendor \"%s\"   # length=%u", vinf->vstr, vinf->vslen);
	ZF_LOGI("Vendor info: product \"%s\"   # length=%u\n", vinf->pstr, vinf->pslen);
}

//===========================================================
static void eeprom_print_gpio(struct gpio_map_t *gpiomap)
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
static void eeprom_print_dt_data(struct dt_data_t *data)
{
	ZF_LOGI("# Device Tree info: length = %d", data->dt_data_size);
}

//===========================================================
static int cariboulite_eeprom_valid(cariboulite_eeprom_st *ee)
{
	if (!ee->initialized)
	{
		ZF_LOGE("eeprom driver is not initialized");
		return -1;
	}

	uint8_t *location = ee->eeprom_buffer;
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

	if (header->eeplen > ee->eeprom_buffer_total_size)
	{
		ZF_LOGD("The declared data-size larger than eeprom size (%d > %d)", 
							header->eeplen, ee->eeprom_buffer_total_size);
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
			ZF_LOGD("Atom #%d count inconcistent", i, atom->count);
			return 0;	// not valid
		}

		if ((offset + 10 + atom->dlen) > ee->eeprom_buffer_total_size)
		{
			ZF_LOGD("Atom #%d data length + crc16 don't fit into eeprom");
			return 0;	// not valid
		}

		// calculate crc
		uint16_t calc_crc = getcrc((char*)atom, 8 + atom->dlen);
		uint16_t actual_crc = ATOM_CRC(atom);
		if (actual_crc != calc_crc)
		{
			ZF_LOGD("Atom #%d calc_crc (0x%04X) doesn't match the actual_crc (0x%04X)", 
							i, calc_crc, actual_crc);
			return 0;	// not valid
		}

		location += 10 + atom->dlen;
		offset += 10 + atom->dlen;
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
static int cariboulite_eeprom_contents_parse(cariboulite_eeprom_st *ee)
{
	uint8_t *location = NULL;
	if (!ee->initialized)
	{
		ZF_LOGE("eeprom driver is not initialized");
		return 0;
	}

	ZF_LOGI("Reading eeprom configuration (%d bytes)...", ee->eeprom_buffer_total_size);
	if (read_eeprom(ee, ee->eeprom_buffer, ee->eeprom_buffer_total_size) < 0)
	{
		ZF_LOGE("Reading from eeprom failed");
		return -1;
	}

	// check the eeprom data's validity
	if ( !cariboulite_eeprom_valid(ee) )
	{
		ZF_LOGE("EEPROM data is not valid. Try reconfiguring it.");
		return -1;
	}

	location = ee->eeprom_buffer;
	
	// Header
	memcpy(&ee->header, location, sizeof(ee->header));
	location += sizeof(ee->header);

	// Atoms
	for (int i = 0; i < ee->header.numatoms; i++)
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
					memcpy(&ee->vinf, it, VENDOR_STATIC_SIZE); it += VENDOR_STATIC_SIZE;
					memcpy(&ee->vinf.vstr, it, ee->vinf.vslen); it += ee->vinf.vslen;
					memcpy(&ee->vinf.pstr, it, ee->vinf.pslen); it += ee->vinf.pslen;
					ee->vinf.vstr[ee->vinf.vslen] = 0;
					ee->vinf.pstr[ee->vinf.pslen] = 0;
				} break;

			//-------------------------------------------------------------
			case ATOM_GPIO_TYPE:
				{
					memcpy(&ee->gpiomap, atom_data, GPIO_MAP_SIZE);
				} break;

			//-------------------------------------------------------------
			case ATOM_DT_TYPE:
				{
					ee->dt_data.dt_data = (uint8_t*)malloc(atom->dlen);
					if (ee->dt_data.dt_data == NULL)
					{
						ZF_LOGE("Failed allocating dt data.");
						return -1;
					}
					ee->dt_data.dt_data_size = atom->dlen;
					memcpy(&ee->dt_data.dt_data, atom_data, ee->dt_data.dt_data_size);
				}

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
static int cariboulite_eeprom_fill_in(cariboulite_eeprom_st *ee)
{
	struct atom_t *atom = NULL;
	uint8_t *location = ee->eeprom_buffer_to_write;
	struct header_t* header = (struct header_t*)ee->eeprom_buffer_to_write;

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

	vinf->pid = 1;
	vinf->pver = 1;
	vinf->vslen = strlen("CaribouLabs.co");
	vinf->pslen = strlen("CaribouLite RPI Hat");
	strcpy(VENDOR_VSTR_POINT(vinf), "CaribouLabs.co");
	strcpy(VENDOR_PSTR_POINT(vinf), "CaribouLite RPI Hat");

	//read 128 random bits from /dev/urandom
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
		vinf->serial_3 = (ee->vinf.serial_3 & 0xffff0fff) | 0x00004000;
		//put in the variant
		vinf->serial_2 = (ee->vinf.serial_2 & 0x3fffffff) | 0x80000000;
		printf("Gen UUID=%08x-%04x-%04x-%04x-%04x%08x\n", vinf->serial_4, 
														vinf->serial_3>>16, 
														vinf->serial_3 & 0xffff, 
														vinf->serial_2>>16, 
														vinf->serial_2 & 0xffff, 
														vinf->serial_1);
	}

	atom->type = ATOM_VENDOR_TYPE;
	atom->count = 0;
	atom->dlen = VENDOR_INFO_COMPACT_SIZE(vinf);
	ATOM_CRC(atom) = getcrc((uint8_t*)atom, ATOM_DATA_SIZE(atom));
	header->eeplen += ATOM_TOTAL_SIZE(atom);
	header->numatoms += 1;

	// GPIO map information
	// -------------------------------------------------------
	location += ATOM_TOTAL_SIZE(atom);
	atom = (struct atom_t*)location;
	atom->type = ATOM_GPIO_TYPE;
	atom->count = 1;
	atom->dlen = GPIO_MAP_SIZE;
	struct gpio_map_t* gpio = (struct gpio_map_t*)(location+ATOM_HEADER_SIZE);
	gpio->flags = 0;  	// drive, slew, hysteresis  =>  0=leave at default
	gpio->power = 0;	// 0 = no back power
	gpio->pins[2] = GPIO_MAP_BITS(5,2,1);	// SMI SA3
	gpio->pins[3] = GPIO_MAP_BITS(5,2,1);	// SMI SA2
	gpio->pins[4] = GPIO_MAP_BITS(5,2,1);	// SMI SA1
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
	ATOM_CRC(atom) = getcrc((uint8_t*)atom, ATOM_DATA_SIZE(atom));

	header->eeplen += ATOM_TOTAL_SIZE(atom);
	header->numatoms += 1;

	// Device Tree information
	// -------------------------------------------------------
	/*location += ATOM_TOTAL_SIZE(atom);
	atom = (struct atom_t*)location;
	atom->type = ATOM_DT_TYPE;
	atom->count = 2;
	atom->dlen = sizeof(cariboulite_dtbo);
	uint8_t *dt_data = (uint8_t *)(location+ATOM_HEADER_SIZE);
	memcpy(dt_data, cariboulite_dtbo, atom->dlen);
	header->eeplen += ATOM_TOTAL_SIZE(atom);
	header->numatoms += 1;
*/

	ee->eeprom_buffer_to_write_used_size = header->eeplen;
	return 0;
}

//===========================================================
int cariboulite_eeprom_init(cariboulite_eeprom_st *ee)
{
	ZF_LOGI("Initializing caribou eeprom driver");
	switch (ee->eeprom_type)
	{
		case eeprom_type_24c32: strcpy(ee->eeprom_type_name, "24c32"); ee->eeprom_size = 4096; break;
		case eeprom_type_24c64: strcpy(ee->eeprom_type_name, "24c64"); ee->eeprom_size = 8192; break;
		case eeprom_type_24c128: strcpy(ee->eeprom_type_name, "24c128"); ee->eeprom_size = 16384; break;
		case eeprom_type_24c256: strcpy(ee->eeprom_type_name, "24c256"); ee->eeprom_size = 32768; break;
		case eeprom_type_24c512: strcpy(ee->eeprom_type_name, "24c512"); ee->eeprom_size = 65536; break;
		case eeprom_type_24c1024: strcpy(ee->eeprom_type_name, "24c1024"); ee->eeprom_size = 131072; break;
		default: strcpy(ee->eeprom_type_name, "24c32"); ee->eeprom_size = 4096; break;	// lowest denominator
	}

	ee->bus = init_eeprom_device(ee->eeprom_type_name, ee->i2c_address);
	if (ee->bus < 0)
	{
		ZF_LOGE("Initializing caribou eeprom driver failed");
		return -1;
	}

	ee->eeprom_buffer = NULL;
	ee->eeprom_buffer_to_write = NULL;

	ee->eeprom_buffer_total_size = ee->eeprom_size > MAX_EEPROM_BUF_SIZE ? MAX_EEPROM_BUF_SIZE : ee->eeprom_size;
	ee->eeprom_buffer = (uint8_t *)malloc(ee->eeprom_buffer_total_size);
	if (ee->eeprom_buffer == NULL)
	{
		ZF_LOGE("eeprom buffer allocation failed");
		close_eeprom_device(ee->bus, ee->i2c_address);
		return -1;
	}

	ee->eeprom_buffer_to_write_total_size = ee->eeprom_size > MAX_EEPROM_BUF_SIZE ? MAX_EEPROM_BUF_SIZE : ee->eeprom_size;
	ee->eeprom_buffer_to_write = (uint8_t *)malloc(ee->eeprom_buffer_to_write_total_size);
	if (ee->eeprom_buffer_to_write == NULL)
	{
		ZF_LOGE("eeprom buffer to write allocation failed");
		close_eeprom_device(ee->bus, ee->i2c_address);
		return -1;
	}
	ee->eeprom_buffer_to_write_used_size = 0;;

	ee->initialized = 1;

	// check if the eeprom is initialized (of contains FFFF garbage)
	ee->eeprom_initialized = 0;
	if (read_eeprom(ee, ee->eeprom_buffer, ee->eeprom_buffer_total_size) < 0)
	{
		ZF_LOGE("Reading from eeprom failed");
		return -1;
	}
	ee->eeprom_initialized = cariboulite_eeprom_valid(ee);
	cariboulite_eeprom_contents_parse(ee);

	if (!ee->eeprom_initialized)
	{
		ZF_LOGI("=======================================================");
		ZF_LOGI("The EEPROM is not initialized or corrupted");
		cariboulite_eeprom_fill_in(ee);
		ZF_LOGI("Push the button on the board and hold, then press ENTER to continue...");
		getchar();
		write_eeprom(ee, ee->eeprom_buffer_to_write, ee->eeprom_buffer_to_write_used_size);
		ZF_LOGI("EEPROM configuration Done");
		ZF_LOGI("=======================================================");
	}

	return 0;
}

//===========================================================
int cariboulite_eeprom_close(cariboulite_eeprom_st *ee)
{
	ZF_LOGI("closing caribou eeprom driver");
	if (!ee->initialized)
	{
		ZF_LOGE("eeprom is not initialized");
		return -1;
	}

	if (ee->eeprom_buffer != NULL) free(ee->eeprom_buffer);
	if (ee->eeprom_buffer_to_write != NULL) free(ee->eeprom_buffer_to_write);
	ee->eeprom_buffer_total_size = 0;
	ee->eeprom_buffer_to_write_total_size = 0;

	return 0;
	//return close_eeprom_device(ee->bus, ee->i2c_address);
}

//===========================================================
int cariboulite_eeprom_print(cariboulite_eeprom_st *ee)
{
	if (!ee->eeprom_initialized)
	{
		if (cariboulite_eeprom_contents_parse(ee) != 0)
		{
			ZF_LOGE("Parsing EEPROM data failed - try reconfiguring");
			return -1;
		}
	}
	
	eeprom_print_header(&ee->header);
	eeprom_print_vendor(&ee->vinf);
	eeprom_print_gpio(&ee->gpiomap);
	eeprom_print_dt_data(&ee->dt_data);
	
	return 0;
}

