#ifndef __CARIBOU_SMI_H__
#define __CARIBOU_SMI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

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

	uint32_t native_batch_length_bytes;
    caribou_smi_stream_st streams[CARIBOU_SMI_MAX_NUM_STREAMS];
    caribou_smi_address_en current_address;
	bool smi_debug;
	bool fifo_push_debug;
	bool fifo_pull_debug;
	double rx_bitrate_mbps;
	double tx_bitrate_mbps;
} caribou_smi_st;

int caribou_smi_init(caribou_smi_st* dev, caribou_smi_error_callback error_cb, void* context);
int caribou_smi_close (caribou_smi_st* dev);
int caribou_smi_timeout_read(caribou_smi_st* dev, 
                            caribou_smi_address_en source, 
                            char* buffer, 
                            int size_of_buf, 
                            int timeout_num_millisec);

char* caribou_smi_get_error_string(caribou_smi_error_en err);
int caribou_smi_check_modules(bool reload);
void caribou_smi_set_debug_mode(caribou_smi_st* dev, bool smi_Debug, bool fifo_push_debug, bool fifo_pull_debug);


#ifdef __cplusplus
}
#endif

#endif // __CARIBOU_SMI_H__