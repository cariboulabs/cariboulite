#ifndef __SMI_STREAM_H__
#define __SMI_STREAM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum
{
	smi_stream_initialized = 0,
	smi_stream_event_type_start = 1,
	smi_stream_event_type_end = 2,
	smi_stream_released = 3,
	smi_stream_error = 4,
} smi_stream_event_type_en;

typedef enum
{
	smi_stream_dir_rx = 0,
	smi_stream_dir_tx = 1,
} smi_stream_direction_en;

typedef void (*smi_stream_data_callback)(int stream_addr,
										smi_stream_direction_en dir,
										uint8_t* data,
										size_t data_length,
										void* context);

typedef void (*smi_stream_event_callback)(	int stream_addr,
											smi_stream_event_type_en event,
											void* metadata,
											void* context);

typedef struct
{
    int stream_addr;     				// stream SMI address (5-bits) 0..31
	smi_stream_data_callback data_cb;  	// data callback for this stream (read and write)
	smi_stream_event_callback event_cb; // event callback for the stream
	void* context;                   	// the pointer to the owning
	int filedesc;						// smi device file descriptor
	bool initialized;					// is this stream initialized
	
	// Buffers
	size_t buffer_length;
	uint8_t *smi_read_buffers[2];
    uint8_t *smi_read_point;        	// the buffer that is currently in the SMI DMA
    uint8_t *app_read_point;        	// the buffer that is currently analyzed / written by the application callback
	
	uint8_t *smi_write_buffers[2];
	uint8_t *smi_write_point;        	// the buffer that is currently in the SMI DMA
    uint8_t *app_write_point;        	// the buffer that is currently analyzed / written by the application callback
	
	// Performance
	float rx_bitrate_mbps;
	float tx_bitrate_mbps;
	
	// Threads
	pthread_t reader_thread;
	pthread_mutex_t reader_lock;
	pthread_t writer_thread;
	pthread_mutex_t writer_lock;
    bool active;
} smi_stream_st;


int smi_stream_init(smi_stream_st* st,
					int filedesc,
					int stream_addr,
					smi_stream_data_callback data_cb,
					smi_stream_event_callback event_cb,
					void* context);
								
int smi_stream_release(smi_stream_st* st);

int smi_stream_read_buffer_info(smi_stream_st* st,
								size_t *batch_length_bytes, 
								int* num_buffers);
											
int smi_stream_set_state(smi_stream_st* dev, int run);


#ifdef __cplusplus
}
#endif

#endif // __SMI_STREAM_H__