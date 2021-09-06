#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Events"
#include "zf_log/zf_log.h"

#include "cariboulite_events.h"

//=================================================================
void caribou_smi_error_event( void *ctx, caribou_smi_channel_en ch, caribou_smi_error_en err)
{

}

//=================================================================
void caribou_smi_data_event( void *ctx, caribou_smi_stream_type_en type,
                                        caribou_smi_channel_en ch,
                                        uint32_t byte_count,
                                        uint8_t *buffer,
                                        uint32_t buffer_len_bytes)
{

}