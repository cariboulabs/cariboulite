#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Events"
#include "zf_log/zf_log.h"

#include "cariboulite.h"
#include "cariboulite_events.h"

//=================================================================
void caribou_smi_error_event(caribou_smi_channel_en channel, void* context)
{
    sys_st* sys = (sys_st*)context;
}

//=================================================================
void caribou_smi_rx_data_event(	caribou_smi_channel_en channel, 
								caribou_smi_sample_complex_int16 *cplx_vec, 
								size_t num_samples_in_vec, 
								void* context)
{
    sys_st* sys = (sys_st*)context;
	
    switch(channel)
    {
        //-------------------------------------------------------
        case caribou_smi_channel_900:
            {
            }
            break;

        //-------------------------------------------------------
        case caribou_smi_channel_2400:
            {
            }
            break;

        //-------------------------------------------------------
        default:
            break;
    }
}

//=================================================================
size_t caribou_smi_tx_data_event(	caribou_smi_channel_en channel, 
									caribou_smi_sample_complex_int16 *cplx_vec, 
									size_t *num_samples_in_vec, 
									void* context)
{
	return 0;
}