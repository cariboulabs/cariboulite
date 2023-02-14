#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOU_SMI_MODULES"

#define _GNU_SOURCE
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include "zf_log/zf_log.h"
#include "caribou_smi.h"
#include "kernel/smi_stream_dev_gen.h"


#define delete_module(name, flags) syscall(__NR_delete_module, name, flags)
#define init_module(module_image, len, param_values) syscall(__NR_init_module, module_image, len, param_values)
#define finit_module(fd, param_values, flags) syscall(__NR_finit_module, fd, param_values, flags)

//===========================================================
int caribou_smi_check_modules_loaded(char* mod_name)
{
	char line[256] = {0};
	int found = 0;
	FILE *fid = fopen ("/proc/modules", "r");
	if (fid == NULL)
	{
		return -1;
	}

	while (fgets(line, sizeof(line), fid))
	{
		char* srch = strstr(line, mod_name);
		if (srch != NULL)
		{
			if (!isspace(srch[strlen(mod_name)])) continue;
			found = 1;
		}
	}
	fclose(fid);
	return found;
}

//===========================================================
int caribou_smi_remove_module(char* module_name)
{
	if (delete_module(module_name, O_NONBLOCK) != 0) 
	{
        ZF_LOGE("Module removing '%s' failed", module_name);
        return -1;
    }
	return 0;
}

//===========================================================
int caribou_smi_insert_smi_modules(char* module_name, 
					uint8_t* buffer, 
					size_t len, 
					const char* params)
{
	if (init_module(buffer, len, params) != 0) 
	{
		ZF_LOGE("Module insertion '%s' failed", module_name);
		return -1;
	}
    return 0;
}

//===========================================================
int caribou_smi_check_modules(bool reload)
{
	int ret = 0;
	int bcm_smi_dev_loaded = caribou_smi_check_modules_loaded("bcm2835_smi_dev");
	int bcm_smi_loaded = caribou_smi_check_modules_loaded("bcm2835_smi");
	int smi_stream_dev_loaded = caribou_smi_check_modules_loaded("smi_stream_dev");

	if (bcm_smi_loaded != 1)
	{
		ZF_LOGE("SMI base driver not loaded - check device tree");
		return -1;
	}
	
	if (bcm_smi_dev_loaded == 1)
	{
		ret = caribou_smi_remove_module("bcm2835_smi_dev");
	}

	if (smi_stream_dev_loaded == 1 && reload)
	{
		ZF_LOGD("Unloading smi-stream module");
		ret = caribou_smi_remove_module("smi_stream_dev");
		smi_stream_dev_loaded = 0;
	}

	if (ret != 0)
	{
		ZF_LOGE("Error unloading module from system");
		return -1;
	}

	if (!smi_stream_dev_loaded || reload)
	{
		ZF_LOGD("Loading smi-stream module");
		return caribou_smi_insert_smi_modules("smi_stream_dev", smi_stream_dev, sizeof(smi_stream_dev), "");
	}
	return 0;
}


