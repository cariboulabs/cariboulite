#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE_CONFIG"
#include "zf_log/zf_log.h"

#include "config.h"


//===========================================================
int config_detect_board(sys_st *sys)
{
	if (hat_detect_board(&sys->board_info) == 0)
	{
		return 0;
	}

	sys->sys_type = (system_type_en)sys->board_info.numeric_product_id;
    return 1;
}

//===========================================================
void config_print_board_info(sys_st *sys)
{
	hat_print_board_info(&sys->board_info);

	switch (sys->sys_type)
	{
		case system_type_cariboulite_full: ZF_LOGI("# Board Info - Product Type: CaribouLite FULL"); break;
		case system_type_cariboulite_ism: ZF_LOGI("# Board Info - Product Type: CaribouLite ISM"); break;
		case system_type_unknown: 
		default: ZF_LOGI("# Board Info - Product Type: Unknown"); break;
	}
}