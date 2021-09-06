#ifndef __CARIBOULITE_EVENTS_H__
#define __CARIBOULITE_EVENTS_H__

#include "caribou_smi/caribou_smi.h"

//=================================================================
void caribou_smi_error_event( void *ctx, caribou_smi_channel_en ch, caribou_smi_error_en err);

//=================================================================
void caribou_smi_data_event( void *ctx, caribou_smi_stream_type_en type,
                                        caribou_smi_channel_en ch,
                                        uint32_t byte_count,
                                        uint8_t *buffer,
                                        uint32_t buffer_len_bytes);

#endif // __CARIBOULITE_EVENTS_H__