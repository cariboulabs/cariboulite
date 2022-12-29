#ifndef __CARIBOU_SMI_H__
#define __CARIBOU_SMI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "smi_stream.h"

typedef enum
{
    caribou_smi_error_read_failed = 0,
	caribou_smi_error_write_failed = 1,
} caribou_smi_error_en;

// DEBUG Information
typedef enum
{
	caribou_smi_none = 0,
	caribou_smi_lfsr = 1,
	caribou_smi_push = 2,
	caribou_smi_pull = 3,
} caribou_smi_debug_mode_en;

typedef struct
{
	uint32_t error_accum_counter;
	uint8_t last_correct_byte;
	double error_rate;
	uint32_t cnt;
} caribou_smi_debug_data_st;

#define CARIBOU_SMI_DEBUG_WORD 	(0x01EFCDAB)

// Data container
#pragma pack(1)

// associated with CS16 - total 4 bytes / element
typedef struct
{
	int16_t i;                      // LSB
	int16_t q;                      // MSB
} caribou_smi_sample_complex_int16;

typedef struct
{
	uint8_t sync;
} caribou_smi_sample_meta;

typedef enum
{
	caribou_smi_channel_900 = smi_stream_channel_0,
	caribou_smi_channel_2400 = smi_stream_channel_1,
} caribou_smi_channel_en;

#pragma pack()


typedef void (*caribou_smi_rx_data_callback)(caribou_smi_channel_en channel,
											caribou_smi_sample_complex_int16 *samples,
											size_t num_of_samples,
											void* context);

typedef size_t (*caribou_smi_tx_data_callback)(	caribou_smi_channel_en channel,
												caribou_smi_sample_complex_int16* data,
												size_t* num_of_samples,
												void* context);

typedef void (*caribou_smi_error_callback)(	caribou_smi_channel_en channel,
											void* context);


typedef struct
{
    int initialized;
    int filedesc;
    caribou_smi_error_callback error_cb;
	caribou_smi_rx_data_callback rx_cb;
	caribou_smi_tx_data_callback tx_cb;
    void* context;

	// streams 900/2400 MHz
	smi_stream_st streams[smi_stream_channel_max];
	caribou_smi_sample_complex_int16 *rx_cplx_buffer[smi_stream_channel_max];
	caribou_smi_sample_complex_int16 *tx_cplx_buffer[smi_stream_channel_max];
	size_t native_batch_len;

	// debugging
	caribou_smi_debug_mode_en debug_mode;
	caribou_smi_debug_data_st debug_data;
} caribou_smi_st;

int caribou_smi_init(caribou_smi_st* dev, 
					caribou_smi_error_callback error_cb,
					void* context);
void caribou_smi_setup_data_callbacks (caribou_smi_st* dev, 
									caribou_smi_rx_data_callback rx_cb, 
									caribou_smi_tx_data_callback tx_cb, 
									void *data_context);
int caribou_smi_close (caribou_smi_st* dev);
int caribou_smi_check_modules(bool reload);
void caribou_smi_set_debug_mode(caribou_smi_st* dev, caribou_smi_debug_mode_en mode);

#ifdef __cplusplus
}
#endif

#endif // __CARIBOU_SMI_H__