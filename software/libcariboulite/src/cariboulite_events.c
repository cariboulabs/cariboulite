#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Events"
#include "zf_log/zf_log.h"

#include "cariboulite_config/cariboulite_config_default.h"
#include "cariboulite_events.h"

//=================================================================
void caribou_smi_error_event( void *ctx, caribou_smi_channel_en ch, caribou_smi_error_en err)
{
    cariboulite_st* sys = (cariboulite_st*)ctx;
}

//=================================================================
void caribou_smi_data_event(void *ctx, 
                            void *service_context,
                            caribou_smi_stream_type_en type,
                            caribou_smi_channel_en ch,
                            uint32_t byte_count,
                            uint8_t *buffer,
                            uint32_t buffer_len_bytes)
{
    cariboulite_st* sys = (cariboulite_st*)ctx;
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
        case caribou_smi_stream_start:
            {
                ZF_LOGD("start event: stream channel %d, batch length: %d bytes\n", ch, buffer_len_bytes);
            }
            break;

        //-------------------------------------------------------
        case caribou_smi_stream_end:
            {
                ZF_LOGD("end event: stream channel %d, batch length: %d bytes\n", ch, buffer_len_bytes);
            }
            break;

        //-------------------------------------------------------
        default:
            break;
    }
}