#ifndef __CARIBOU_SMI_H__
#define __CARIBOU_SMI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdint.h>

typedef enum
{
    caribou_smi_address_idle = 0<<1,
    caribou_smi_address_write_900 = 1<<1,
    caribou_smi_address_write_2400 = 2<<1,
    caribou_smi_address_write_res2 = 3<<1,
    caribou_smi_address_read_res1 = 4<<1,
    caribou_smi_address_read_900 = 5<<1,
    caribou_smi_address_read_2400 = 6<<1,
    caribou_smi_address_read_res = 7<<1,
} caribou_smi_address_en;

typedef enum
{
    caribou_smi_channel_900 = 0,
    caribou_smi_channel_2400 = 1,
} caribou_smi_channel_en;

typedef enum
{
    caribou_smi_stream_type_write = 0,
    caribou_smi_stream_type_read = 1,
    caribou_smi_stream_start = 0xFE,
    caribou_smi_stream_end = 0xFF,
} caribou_smi_stream_type_en;

#define CARIBOU_SMI_GET_STREAM_ID(type, ch) ( ((type)<<1) | (ch) )

typedef enum
{
    caribou_smi_error_read_failed = 0,
} caribou_smi_error_en;

#define CARIBOU_SMI_ERROR_STRS  {                                     \
                                    "reading from SMI source failed", \
                                }

typedef void (*caribou_smi_data_callback)( void *ctx,                                   // The context of the requesting application
                                                caribou_smi_stream_type_en type,        // which type of stream is it? read / write?
                                                caribou_smi_channel_en ch,              // which channel (900 / 2400)
                                                uint32_t byte_count,                    // for "read stream only" - number of read data bytes in buffer
                                                uint8_t *buffer,                        // for "read" - data buffer to be analyzed
                                                                                        // for "write" - the data buffer to be filled with information
                                                uint32_t buffer_len_bytes);             // the total size of the buffer

typedef void (*caribou_smi_error_callback)( void *ctx,
                                            caribou_smi_channel_en ch,
                                            caribou_smi_error_en err);

#define CARIBOU_SMI_READ_ADDR(a) (a>>3)
#define CARIBOU_SMI_STREAM_NUM(a) ( (a>>1) & 0x3 - 1 )

typedef struct
{
    caribou_smi_address_en addr;        // the SMI address that this stream is serving
    unsigned int batch_length;          // the size of a single read / write
    unsigned int num_of_buffers;        // number of buffers in the buffer train
    caribou_smi_data_callback data_cb;  // the application callback when read / write events happens

    uint8_t **buffers;                  // the buffer train to be allocated
    int current_smi_buffer_index;
    uint8_t *current_smi_buffer;        // the buffer that is currently in the SMI DMA
    uint8_t *current_app_buffer;        // the buffer that is currently analyzed / written by the application callback

    int active;                         // the thread is active
    int running;                        // the stream state - is it running and fetching / pushing information
    int stream_id;                      // the stream id for the application - may be deleted later
    pthread_t stream_thread;            // thread id
    void* parent_dev;                   // the pointer to the owning SMI device
} caribou_smi_stream_st;

#define CARIBOU_SMI_MAX_NUM_STREAMS 6

typedef struct
{
    int initialized;
    int filedesc;
    caribou_smi_error_callback error_cb;
    void* cb_context;

    caribou_smi_stream_st streams[CARIBOU_SMI_MAX_NUM_STREAMS];
    caribou_smi_address_en current_address;
} caribou_smi_st;

int caribou_smi_init(caribou_smi_st* dev, caribou_smi_error_callback error_cb, void* context);
int caribou_smi_close (caribou_smi_st* dev);
int caribou_smi_timeout_read(caribou_smi_st* dev, int source, char* buffer, int size_of_buf, int timeout_num_millisec);
int caribou_smi_setup_stream(caribou_smi_st* dev,
                                caribou_smi_stream_type_en type,
                                caribou_smi_channel_en channel,
                                int batch_length, int num_buffers,
                                caribou_smi_data_callback cb);
int caribou_smi_run_pause_stream (caribou_smi_st* dev, int id, int run);
int caribou_smi_destroy_stream(caribou_smi_st* dev, int id);
char* caribou_smi_get_error_string(caribou_smi_error_en err);
void dump_hex(const void* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif // __CARIBOU_SMI_H__