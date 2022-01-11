#ifndef __CARIBOULITE_EVENTS_H__
#define __CARIBOULITE_EVENTS_H__


#ifdef __cplusplus
extern "C" {
#endif

#include "caribou_smi/caribou_smi.h"


//=================================================================
void caribou_smi_error_event( void *ctx, caribou_smi_channel_en ch, caribou_smi_error_en err);

//=================================================================
void caribou_smi_data_event(void *ctx, 
                            void *service_context,
                            caribou_smi_stream_type_en type,
                            caribou_smi_channel_en ch,
                            size_t num_samples_in_vec,
							caribou_smi_sample_complex_int16 *cplx_vec,
							caribou_smi_sample_meta *metadat_vec,
                            size_t total_length_samples);


#ifdef __cplusplus
}
#endif

#endif // __CARIBOULITE_EVENTS_H__