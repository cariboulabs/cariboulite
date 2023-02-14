#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "EEPROM_UTILS"
#include "zf_log/zf_log.h"

#include "eeprom_utils.h"
#include "io_utils/io_utils_fs.h"
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

//===========================================================
int eeprom_init_device(eeprom_utils_st *ee)
{
	switch (ee->eeprom_type)
	{
		case eeprom_type_24c64: strcpy(ee->eeprom_type_name, "24c64"); ee->eeprom_size = 8192; break;
		case eeprom_type_24c128: strcpy(ee->eeprom_type_name, "24c128"); ee->eeprom_size = 16384; break;
		case eeprom_type_24c256: strcpy(ee->eeprom_type_name, "24c256"); ee->eeprom_size = 32768; break;
		case eeprom_type_24c512: strcpy(ee->eeprom_type_name, "24c512"); ee->eeprom_size = 65536; break;
		case eeprom_type_24c1024: strcpy(ee->eeprom_type_name, "24c1024"); ee->eeprom_size = 131072; break;
		case eeprom_type_24c32:
		default: strcpy(ee->eeprom_type_name, "24c32"); ee->eeprom_size = 4096; break;	// lowest denominator
	}

	ee->bus = io_utils_i2cbus_exists();
	if (ee->bus >= 0)
	{
		ZF_LOGI("i2c-%d has been found successfully", ee->bus);
	}

	// neither bus 0,9 were found in the dev dir -> we need to probe bus9
	if (ee->bus == -1)
	{
		if (io_utils_probe_gpio_i2c() == -1)
		{
			ZF_LOGE("Failed to probe i2c-9");
			return -1;
		}
		else
		{
			ee->bus = 9;
			ZF_LOGI("i2c-9 has been probed successfully");
		}
	}

	// probe the eeprom driver
	ZF_LOGI("trying to modprobe at24");
	char modprobe[] = "/usr/sbin/modprobe at24";
	char *argv[64];
	io_utils_parse_command(modprobe, argv);
	if (io_utils_execute_command(argv) != 0)
	{
		ZF_LOGE("MODPROBE of the eeprom 'at24' execution failed");
		return -1;
	}

	// the sys dir path
	char sys_dir_bus[128] = {0};
	char sys_dir_bus_addr[160] = {0};
	char sys_dir_bus_new_dev[160] = {0};
	sprintf(sys_dir_bus, "/sys/class/i2c-adapter/i2c-%d", ee->bus);
	sprintf(sys_dir_bus_addr, "%s/%d-00%x", sys_dir_bus, ee->bus, ee->i2c_address);
	sprintf(sys_dir_bus_new_dev, "%s/new_device", sys_dir_bus);

	int dir = 0;
	int ee_exists = io_utils_file_exists(sys_dir_bus_addr, NULL, &dir, NULL, NULL);
	if (!ee_exists || !dir)
	{
		// create the device
		char dev_type[64] = {0};
		sprintf(dev_type, "%s 0x%x", ee->eeprom_type_name, ee->i2c_address);
		if ( io_utils_write_to_file(sys_dir_bus_new_dev, dev_type, strlen(dev_type) + 1) != 0)
		{
			ZF_LOGE("EEPROM on addr 0x%x probing failed, retrying...", ee->i2c_address);

			if (io_utils_write_to_file(sys_dir_bus_new_dev, dev_type, strlen(dev_type) + 1) != 0)
			{
				ZF_LOGE("EEPROM on addr 0x%x probing failed", ee->i2c_address);
				return -1;
			}
		}
	}

	// recheck that the file exists now
	ee_exists = io_utils_file_exists(sys_dir_bus_addr, NULL, &dir, NULL, NULL);
	if (!ee_exists || !dir)
	{
		ZF_LOGE("EEPROM on addr 0x%x probing failed - file was not found", ee->i2c_address);
		return -1;
	}
	ZF_LOGI("EEPROM on addr 0x%x probing successful", ee->i2c_address);
	ee->initialized = true;

	return 0;
}

//===========================================================
int eeprom_close_device(eeprom_utils_st *ee)
{
	int dir = 0;
	char sys_dir_bus[128] = {0};
	char sys_dir_bus_addr[160] = {0};
	char sys_dir_bus_del_dev[160] = {0};

	if (ee->initialized == false)
	{
		ZF_LOGE("EEPROM device is not initialized");
		return -1;
	}

	sprintf(sys_dir_bus, "/sys/class/i2c-adapter/i2c-%d", ee->bus);
	sprintf(sys_dir_bus_addr, "%s/%d-00%x", sys_dir_bus, ee->bus, ee->i2c_address);
	sprintf(sys_dir_bus_del_dev, "%s/delete_device", sys_dir_bus);

	int ee_exists = io_utils_file_exists(sys_dir_bus_addr, NULL, &dir, NULL, NULL);
	if (ee_exists && dir)
	{
		char dev_type[64] = {0};
		sprintf(dev_type, "0x%x", ee->i2c_address);
		if (io_utils_write_to_file(sys_dir_bus_del_dev, dev_type, strlen(dev_type) + 1) != 0)
		{
			ZF_LOGE("EEPROM on addr 0x%x deletion failed on bus %d", ee->i2c_address, ee->bus);
			return -1;
		}
	}
	ZF_LOGI("EEPROM addr 0x%x on bus %d deletion was successful", ee->i2c_address, ee->bus);
	return 0;
}

//===========================================================
int eeprom_write(eeprom_utils_st *ee, char* buffer, int length)
{
	char eeprom_fname[200] = {0};
	sprintf(eeprom_fname, "/sys/class/i2c-adapter/i2c-%d/%d-00%x/eeprom",
			ee->bus, ee->bus, ee->i2c_address);
	int ee_exists = io_utils_file_exists(eeprom_fname, NULL, NULL, NULL, NULL);
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
	return io_utils_write_to_file(eeprom_fname, buffer, length);
}

//===========================================================
int eeprom_read(eeprom_utils_st *ee, char* buffer, int length)
{
	char eeprom_fname[200] = {0};
	sprintf(eeprom_fname, "/sys/class/i2c-adapter/i2c-%d/%d-00%x/eeprom",
			ee->bus, ee->bus, ee->i2c_address);
	int ee_exists = io_utils_file_exists(eeprom_fname, NULL, NULL, NULL, NULL);
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

	return io_utils_read_from_file(eeprom_fname, buffer, length);
}

