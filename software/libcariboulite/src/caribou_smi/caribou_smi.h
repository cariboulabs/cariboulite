#ifndef __CARIBOU_SMI_H__
#define __CARIBOU_SMI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>


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
    uint32_t cur_err_cnt;
	uint8_t last_correct_byte;
	double error_rate;
	uint32_t cnt;
    double bitrate;
    struct timeval last_time;
} caribou_smi_debug_data_st;

#define CARIBOU_SMI_DEBUG_WORD 	        (0xABCDEF01)
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
	int16_t i;                      // MSB
	int16_t q;                      // LSB
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
    size_t write_partial_bytes; // prefix of the next unreported sample already sent
	size_t native_batch_len;
    uint32_t sample_rate;
    smi_stream_state_en state;
    
    uint8_t *read_temp_buffer;
    uint8_t *write_temp_buffer;
    
    bool invert_iq;

	// debugging
	caribou_smi_debug_mode_en debug_mode;
	caribou_smi_debug_data_st debug_data;
} caribou_smi_st;

int caribou_smi_init(caribou_smi_st* dev, 
					void* context);
/* owned_fd remains owned by the caller; initialization duplicates it. */
int caribou_smi_init_with_fd(caribou_smi_st* dev, void* context, int owned_fd);
int caribou_smi_close (caribou_smi_st* dev);
int caribou_smi_check_modules(bool reload);

void caribou_smi_invert_iq(caribou_smi_st* dev, bool invert);

void caribou_smi_set_debug_mode(caribou_smi_st* dev, caribou_smi_debug_mode_en mode);
int caribou_smi_set_driver_streaming_state(caribou_smi_st* dev, smi_stream_state_en state);
smi_stream_state_en caribou_smi_get_driver_streaming_state(caribou_smi_st* dev);

int caribou_smi_read(caribou_smi_st* dev, caribou_smi_channel_en channel, 
                        caribou_smi_sample_complex_int16* buffer, caribou_smi_sample_meta* metadata, size_t length_samples);

// Compatibility entry point: returns accepted samples, without padding.
// Short writes use the same retry contract as caribou_smi_write_samples.
int caribou_smi_write(caribou_smi_st* dev, caribou_smi_channel_en channel, 
                         caribou_smi_sample_complex_int16* buffer, size_t length_samples);

// Write N *samples* (each sizeof(caribou_smi_sample_complex_int16)).
// Returns number of *samples* actually accepted, 0 if back-pressured,
// or -errno on error. Retry from samples + returned_count after a short write.
// A partially sent sample is not counted until completed; its byte offset is
// retained across calls. Serialize writes and stream transitions for this device.
int caribou_smi_write_samples(caribou_smi_st *smi,
                      caribou_smi_channel_en ch,
                      const caribou_smi_sample_complex_int16 *samples,
                      int n_samples);

// Timed variants: at most one native batch; timeout_us <= 0 means try once.
// Return partial sample counts, zero on timeout, negative on failure.
// Writes retain the same partial-sample retry contract described above.
int caribou_smi_read_timed(caribou_smi_st *dev, caribou_smi_channel_en channel,
    caribou_smi_sample_complex_int16 *samples, caribou_smi_sample_meta *metadata,
    size_t count, long timeout_us);
// Interface-loopback diagnostics only: accepts echoed TX control bits.
// Returns complete decoded samples without interpolation; normal RX stays strict.
int caribou_smi_read_loopback_timed(caribou_smi_st *dev,
    caribou_smi_channel_en channel, caribou_smi_sample_complex_int16 *samples,
    size_t count, long timeout_us);
int caribou_smi_write_timed(caribou_smi_st *dev, caribou_smi_channel_en channel,
    const caribou_smi_sample_complex_int16 *samples, size_t count, long timeout_us);

size_t caribou_smi_get_native_batch_samples(caribou_smi_st* dev);

void caribou_smi_setup_ios(caribou_smi_st* dev);
void caribou_smi_set_sample_rate(caribou_smi_st* dev, uint32_t sample_rate);
int caribou_smi_flush_fifo(caribou_smi_st* dev);

int caribou_smi_get_current_settings(caribou_smi_st* dev, struct smi_settings* settings);

#ifdef __cplusplus
}
#endif

#endif // __CARIBOU_SMI_H__