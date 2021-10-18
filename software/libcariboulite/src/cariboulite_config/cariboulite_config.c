#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE_CONFIG"
#include "zf_log/zf_log.h"


#include "cariboulite_config.h"
#include <sys/stat.h>

//===========================================================
static int config_file_exists(char* fname, int *size, int *dir, int *file, int *dev)
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
static int config_read_string_from_file(char* path, char* filename, char* data, int len)
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
int cariboulite_config_serial_from_uuid(char* uuid, uint32_t *serial)
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
// If the board is not detected, try detecting it outside:
// go directly to the eeprom configuration application
// prompt the user
// configure and tell the user he needs to reboot his system
int cariboulite_config_detect_board(cariboulite_board_info_st *info)
{
    int file_exists = 0;
    int size, dir, file, dev;

    // check if a hat is attached anyway..
	char hat_dir_path[] = "/proc/device-tree/hat";
    file_exists = config_file_exists(hat_dir_path, &size, &dir, &file, &dev);
    if (!file_exists || !dir)
	{
		ZF_LOGI("This board is not configured yet as a hat. Please follow the configuration steps.");
		return 0;
	}

	config_read_string_from_file(hat_dir_path, "name", info->category_name, sizeof(info->category_name));
	config_read_string_from_file(hat_dir_path, "product", info->product_name, sizeof(info->product_name));
	config_read_string_from_file(hat_dir_path, "product_id", info->product_id, sizeof(info->product_id));
	config_read_string_from_file(hat_dir_path, "product_ver", info->product_version, sizeof(info->product_version));
	config_read_string_from_file(hat_dir_path, "uuid", info->product_uuid, sizeof(info->product_uuid));
	config_read_string_from_file(hat_dir_path, "vendor", info->product_vendor, sizeof(info->product_vendor));

	// numeric version
	if (info->product_version[0] == '0' && (info->product_version[1] == 'x' ||
											info->product_version[1] == 'X'))
		sscanf(info->product_version, "0x%08x", &info->numeric_version);
	else
		sscanf(info->product_version, "%08x", &info->numeric_version);
	
	// numeric productid
	if (info->product_id[0] == '0' && (info->product_id[1] == 'x' ||
											info->product_id[1] == 'X'))
		sscanf(info->product_id, "0x%08x", &info->numeric_product_id);
	else
		sscanf(info->product_id, "%08x", &info->numeric_product_id);

	// seiral number
	if (cariboulite_config_serial_from_uuid(info->product_uuid, &info->numeric_serial_number) != 0)
	{
		// should never happen
		return 0;
	}

    return 1;
}

//===========================================================
void cariboulite_config_print_board_info(cariboulite_board_info_st *info)
{
	ZF_LOGI("# Board Info - Category name: %s", info->category_name);
	ZF_LOGI("# Board Info - Product name: %s", info->product_name);
	ZF_LOGI("# Board Info - Product ID: %s, Numeric: %d", info->product_id, info->numeric_product_id);
	ZF_LOGI("# Board Info - Product Version: %s, Numeric: %d", info->product_version, info->numeric_version);
	ZF_LOGI("# Board Info - Product UUID: %s, Numeric serial: 0x%08X", info->product_uuid, info->numeric_serial_number);
	ZF_LOGI("# Board Info - Vendor: %s", info->product_vendor);
}