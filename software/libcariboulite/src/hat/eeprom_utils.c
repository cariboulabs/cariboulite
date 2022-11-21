#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "EEPROM_UTILS"
#include "zf_log/zf_log.h"

#include "eeprom_utils.h"
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
int file_exists(char* fname, int *size, int *dir, int *file, int *dev)
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
int write_to_file(char* fname, char* data, int size_of_data)
{
	FILE* fid = NULL;

	fid = fopen(fname, "wb");
	if (fid == NULL)
	{
		ZF_LOGE("opening file '%s' for writing failed", fname);
		return -1;
	}
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
int read_from_file(char* fname, char* data, int len_to_read)
{
	FILE* fid = NULL;

	fid = fopen(fname, "rb");
	if (fid == NULL)
	{
		ZF_LOGE("opening file '%s' for reading failed", fname);
		return -1;
	}
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
int read_string_from_file(char* path, char* filename, char* data, int len)
{
	FILE* fid = NULL;
    int retval =  0;

	char full_path[128] = {0};
	sprintf(full_path, "%s/%s", path, filename);

	fid = fopen(full_path, "r");
	if (fid == NULL) 
	{
		ZF_LOGE("opening file '%s' for reading failed", full_path);
		return -1;
	}

	if (fgets(data, len, fid) == NULL)
	{
		ZF_LOGE("reading from '%s' failed", full_path);
        retval = -1;
	}
	fclose(fid);
    return retval;
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

	ee->bus = i2cbus_exists();
	if (ee->bus >= 0)
	{
		ZF_LOGI("i2c-%d has been found successfully", ee->bus);
	}

	// neither bus 0,9 were found in the dev dir -> we need to probe bus9
	if (ee->bus == -1)
	{
		if (probe_gpio_i2c() == -1)
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
	parse_command(modprobe, argv);
	if (execute_command(argv) != 0)
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
	int ee_exists = file_exists(sys_dir_bus_addr, NULL, &dir, NULL, NULL);
	if (!ee_exists || !dir)
	{
		// create the device
		char dev_type[64] = {0};
		sprintf(dev_type, "%s 0x%x", ee->eeprom_type_name, ee->i2c_address);
		if (write_to_file(sys_dir_bus_new_dev, dev_type, strlen(dev_type) + 1) != 0)
		{
			ZF_LOGE("EEPROM on addr 0x%x probing failed, retrying...", ee->i2c_address);

			if (write_to_file(sys_dir_bus_new_dev, dev_type, strlen(dev_type) + 1) != 0)
			{
				ZF_LOGE("EEPROM on addr 0x%x probing failed", ee->i2c_address);
				return -1;
			}
		}
	}

	// recheck that the file exists now
	ee_exists = file_exists(sys_dir_bus_addr, NULL, &dir, NULL, NULL);
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

	int ee_exists = file_exists(sys_dir_bus_addr, NULL, &dir, NULL, NULL);
	if (ee_exists && dir)
	{
		char dev_type[64] = {0};
		sprintf(dev_type, "0x%x", ee->i2c_address);
		if (write_to_file(sys_dir_bus_del_dev, dev_type, strlen(dev_type) + 1) != 0)
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
int eeprom_read(eeprom_utils_st *ee, char* buffer, int length)
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

