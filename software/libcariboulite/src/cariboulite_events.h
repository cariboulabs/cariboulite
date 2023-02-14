#ifndef __CARIBOULITE_EVENTS_H__
#define __CARIBOULITE_EVENTS_H__


#ifdef __cplusplus
extern "C" {
#endif

#include "caribou_smi/caribou_smi.h"

void caribou_smi_error_event(caribou_smi_channel_en channel, void* context);					
void caribou_smi_rx_data_event(caribou_smi_channel_en channel, caribou_smi_sample_complex_int16 *cplx_vec, size_t num_samples_in_vec, void* context);
size_t caribou_smi_tx_data_event(caribou_smi_channel_en channel, caribou_smi_sample_complex_int16 *cplx_vec, size_t *num_samples_in_vec, void* context);

#ifdef __cplusplus
}
#endif

#endif // __CARIBOULITE_EVENTS_H__