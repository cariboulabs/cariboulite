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
} caribou_smi_error_en;

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
    caribou_smi_error_callback error_cb;
    void* cb_context;

	// streams 900/2400 MHz
	smi_stream_st streams[2];

	// debugging
	bool smi_debug;
	bool fifo_push_debug;
	bool fifo_pull_debug;
} caribou_smi_st;

int caribou_smi_init(caribou_smi_st* dev, caribou_smi_error_callback error_cb, void* context);
int caribou_smi_close (caribou_smi_st* dev);
int caribou_smi_check_modules(bool reload);
void caribou_smi_set_debug_mode(caribou_smi_st* dev, bool smi_debug, bool fifo_push_debug, bool fifo_pull_debug);

#ifdef __cplusplus
}
#endif

#endif // __CARIBOU_SMI_H__