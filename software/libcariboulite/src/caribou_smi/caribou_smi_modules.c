#include "caribou_smi.h"

#include "kernel/bcm2835_smi_gen.h"
#include "kernel/smi_stream_dev_gen.h"

#include <string.h>
#include <ctype.h>

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

int caribou_smi_check_modules()
{
	int bcm_smi_dev = caribou_smi_check_smi_modules_loaded("bcm2835_smi_dev");
	int bcm_smi = caribou_smi_check_smi_modules_loaded("bcm2835_smi");

	if (bcm_smi_dev == -1 || bcm_smi == -1)
	{
		return -1;
	}

	if (bcm_smi_dev || bcm_smi)
	{
		caribou_smi_remove_module("bcm2835_smi_dev");
		caribou_smi_remove_module("bcm2835_smi");
	}

	return 0;
}

int caribou_smi_insert_smi_modules()
{

}

int caribou_smi_remove_module(char* module_name)
{

}