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
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include  <sys/types.h>
#include "cariboulite_eeprom.h"

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
	return fclose(fid);
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
	return fclose(fid);
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
			out ^= CRC16;
	}

	// item b) "push out" the last 16 bits
	int i;
	for (i = 0; i < 16; ++i) {
		bit_flag = out >> 15;
		out <<= 1;
		if(bit_flag)
			out ^= CRC16;
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

	ee->eeprom_buffer_size = ee->eeprom_size > MAX_EEPROM_BUF_SIZE ? MAX_EEPROM_BUF_SIZE : ee->eeprom_size;
	ee->eeprom_buffer = (uint8_t *)malloc(ee->eeprom_buffer_size);
	if (ee->eeprom_buffer == NULL)
	{
		ZF_LOGE("eeprom buffer allocation failed");
		close_eeprom_device(ee->bus, ee->i2c_address);
		return -1;
	}

	ee->initialized = 1;
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

	return close_eeprom_device(ee->bus, ee->i2c_address);
}

//===========================================================
static void eeprom_print_header(struct header_t *header)
{
	ZF_LOGI("# HEADER: signature=0x%08x", header->signature);
	ZF_LOGI("# HEADER: version=0x%02x", header->ver);
	ZF_LOGI("# HEADER: reserved=%u", header->res);
	ZF_LOGI("# HEADER: numatoms=%u", header->numatoms);
	ZF_LOGI("# HEADER: eeplen=%u", header->eeplen);
}

//===========================================================
static void eeprom_print_vendor(struct vendor_info_d * vinf)
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
static void eeprom_print_gpio(struct gpio_map_d *gpiomap)
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
int cariboulite_eeprom_print(cariboulite_eeprom_st *ee)
{
	uint8_t *location = NULL;
	if (!ee->initialized)
	{
		ZF_LOGE("eeprom driver is not initialized");
		return 0;
	}

	ZF_LOGI("Reading eeprom configuration (%d bytes)...", ee->eeprom_buffer_size);
	if (read_eeprom(ee, ee->eeprom_buffer, ee->eeprom_buffer_size) != ee->eeprom_buffer_size)
	{
		ZF_LOGE("Reading from eeprom failed");
		return -1;
	}

	location = ee->eeprom_buffer;
	memcpy(&ee->header, location, sizeof(ee->header));
	location += sizeof(ee->header);
	
	ZF_LOGI("# ---------- Dump generated by eepdump handling format version 0x%02x ---------- #", FORMAT_VERSION);
	
	if (FORMAT_VERSION!=ee->header.ver) ZF_LOGI("# WARNING: format version mismatch!!!");
	if (HEADER_SIGN!=ee->header.signature) ZF_LOGI("# WARNING: format signature mismatch!!!");
	if (FORMAT_VERSION!=ee->header.ver && HEADER_SIGN!=ee->header.signature) 
	{
		ZF_LOGE("header version and signature mismatch, maybe wrong file?");
		return -1;
	}
	eeprom_print_header(&ee->header);

	for (int i = 0; i < ee->header.numatoms; i++)
	{
		struct atom_t *atom = (struct atom_t *)location;
		ZF_LOGI("# Start of atom #%u of type 0x%04x and length %u", atom->count, atom->type, atom->dlen);
		
		if (atom->count != i) ZF_LOGE("Error: atom count mismatch"); 
		if ((uint32_t)ee->eeprom_buffer_size < atom->dlen)
		{
			ZF_LOGE("size of atom[%i] = %i longer than rest of data (%i)", i, atom->dlen, ee->eeprom_buffer_size);
			return -1;
		}

		uint8_t *location_of_data = location + ATOM_SIZE - CRC_SIZE;
		uint8_t *location_of_crc = location + ATOM_SIZE + atom->dlen - CRC_SIZE;
		uint16_t *crc = (uint16_t*)location_of_crc;
		uint16_t calc_crc = getcrc(location, atom->dlen + ATOM_SIZE - CRC_SIZE);

		if (calc_crc != (*crc))
		{
			ZF_LOGE("Error: atom %d CRC16 mismatch. Calculated CRC16=0x%02x", i, calc_crc);
			return -1;
		}

		// Analyze he atom internal infomration
		location = location_of_data;
		switch (atom->type)
		{
			case ATOM_VENDOR_TYPE:
				{
					memcpy(&ee->vinf, location, VENDOR_SIZE);
					location += VENDOR_SIZE;
					memcpy(&ee->vinf.vstr, location, ee->vinf.vslen); location += ee->vinf.vslen;
					memcpy(&ee->vinf.pstr, location, ee->vinf.pslen);location += ee->vinf.pslen;
					ee->vinf.vstr[ee->vinf.vslen] = 0;
					ee->vinf.pstr[ee->vinf.pslen] = 0;
					eeprom_print_vendor(&ee->vinf);
				} break;
			case ATOM_GPIO_TYPE:
				{
					memcpy(&ee->gpiomap, location, GPIO_SIZE);
					eeprom_print_gpio(&ee->gpiomap);
				} break;
			default:
				ZF_LOGE("Error: unrecognised atom type");
				break;
		}
	}

	return 0;
}





