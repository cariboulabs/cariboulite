#ifndef __CARIBOU_SMI_H__
#define __CARIBOU_SMI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

#include "kernel/bcm2835_smi.h"
#include "kernel/smi_stream_dev.h"

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

#define CARIBOU_SMI_DEBUG_WORD 	        (0x01EFCDAB)
#define CARIBOU_SMI_BYTES_PER_SAMPLE    (4)
#define CARIBOU_SMI_SAMPLE_RATE         (4000000)

typedef enum
{
	caribou_smi_channel_900 = smi_stream_channel_0,
	caribou_smi_channel_2400 = smi_stream_channel_1,
} caribou_smi_channel_en;


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
#pragma pack()

typedef struct
{
    int initialized;
    int filedesc;
	size_t native_batch_len;
    
    uint8_t *read_temp_buffer;
    uint8_t *write_temp_buffer;

	// debugging
	caribou_smi_debug_mode_en debug_mode;
	caribou_smi_debug_data_st debug_data;
} caribou_smi_st;

int caribou_smi_init(caribou_smi_st* dev, 
					void* context);
int caribou_smi_close (caribou_smi_st* dev);
int caribou_smi_check_modules(bool reload);

void caribou_smi_set_debug_mode(caribou_smi_st* dev, caribou_smi_debug_mode_en mode);

int caribou_smi_read(caribou_smi_st* dev, caribou_smi_channel_en channel, 
                        caribou_smi_sample_complex_int16* buffer, caribou_smi_sample_meta* metadata, size_t length_samples);
                        
int caribou_smi_write(caribou_smi_st* dev, caribou_smi_channel_en channel, 
                        caribou_smi_sample_complex_int16* buffer, size_t length_samples);

size_t caribou_smi_get_native_batch_samples(caribou_smi_st* dev);

#ifdef __cplusplus
}
#endif

#endif // __CARIBOU_SMI_H__