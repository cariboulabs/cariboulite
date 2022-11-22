#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Events"
#include "zf_log/zf_log.h"

#include "cariboulite_config_default.h"
#include "cariboulite_events.h"

//=================================================================
void caribou_smi_error_event( void *ctx, caribou_smi_channel_en ch, caribou_smi_error_en err)
{
    sys_st* sys = (sys_st*)ctx;
}

//=================================================================
void caribou_smi_data_event(void *ctx, 
                            void *service_context,
                            caribou_smi_stream_type_en type,
							caribou_smi_event_type_en ev,
                            caribou_smi_channel_en ch,
                            size_t num_samples_in_vec,
							caribou_smi_sample_complex_int16 *cplx_vec,
							caribou_smi_sample_meta *metadat_vec,
                            size_t total_length_samples)
{
    sys_st* sys = (sys_st*)ctx;

	//-------------------------------------------------------
    if (ev==caribou_smi_event_type_start)
	{
		ZF_LOGD("start event: stream batch length: %lu samples\n", total_length_samples);
		return;
	}
	else if (ev==caribou_smi_event_type_end)
	{
		ZF_LOGD("end event: stream batch length: %lu samples\n", total_length_samples);
		return;
	}

	
    switch(type)
    {
        //-------------------------------------------------------
        case caribou_smi_stream_type_read:
            {
                if (ch == caribou_smi_channel_900)
                {

                }
                else if (ch == caribou_smi_channel_2400)
                {

                }
            }
            break;

        //-------------------------------------------------------
        case caribou_smi_stream_type_write:
            {
                if (ch == caribou_smi_channel_900)
                {

                }
                else if (ch == caribou_smi_channel_2400)
                {
                    
                }
            }
            break;

        //-------------------------------------------------------
        default:
            break;
    }
}