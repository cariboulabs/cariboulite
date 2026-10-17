#pragma once
#include "audio_source.h"
#include "pipeline_runtime.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct tx_audio_buffer tx_audio_buffer_t;

typedef struct {
    bool created, alive, running, cue, primed, clock_valid;
    int last_error;
    size_t queued_frames, capacity_frames, target_frames;
    size_t minimum_frames, maximum_frames;
    double input_per_output, correction_ppm, occupancy_filtered_frames;
    uint64_t captured_frames, consumed_frames, output_frames;
    uint64_t capture_calls, again_calls, read_calls;
    uint64_t underflows, overflows, overflow_frames, cue_discard_frames;
    uint64_t resets, clock_limit_frames, clock_limit_faults;
    pipeline_thread_status_t scheduling;
} tx_audio_buffer_stats_t;

/* Borrowed 48 kHz mono source with nonblocking read (the ALSA adapter). One
 * retained capture thread starts parked. The owner serializes lifecycle calls;
 * only the producer calls read(). Destroy the helper before its source. */
tx_audio_buffer_t *tx_audio_buffer_create(audio_source_t *source);
/* Resume clears stale samples and resets a parked ALSA source before capture.
 * park() acknowledges quiescence before it returns; no reader then touches the
 * source. Lifecycle functions are bounded by the source's nonblocking read. */
int tx_audio_buffer_resume(tx_audio_buffer_t *buffer);
void tx_audio_buffer_park(tx_audio_buffer_t *buffer);
void tx_audio_buffer_request_abort(tx_audio_buffer_t *buffer);
void tx_audio_buffer_destroy(tx_audio_buffer_t *buffer);
/* Keep capturing through opening/closing cues. While cue=true, explicitly
 * discard the oldest excess beyond 70 ms, retaining recent microphone audio.
 * The producer must mark cue mode before it stops consuming microphone data. */
void tx_audio_buffer_set_cue(tx_audio_buffer_t *buffer, bool cue);
/* Output is normalized 48 kHz audio at the hardware demand cadence. The ASRC
 * follows occupancy with a slowly varying input/output ratio, capped at +/-4%.
 * Waits are interruptible by park/abort. timeout_ms<0 waits without a deadline;
 * a positive timeout reports starvation as -ETIMEDOUT instead of inserting
 * silence. Read calls must request at most 480 frames. The initial read primes
 * 70 ms of remaining real audio to cover DMA bursts and measured clock steps
 * while correction slews at at most 100 ppm per 10 ms frame. */
int tx_audio_buffer_read(tx_audio_buffer_t *buffer, float *dst,
                         size_t output_frames, int timeout_ms);
void tx_audio_buffer_get_stats(tx_audio_buffer_t *buffer,
                              tx_audio_buffer_stats_t *stats);
/* Counters only, serialized by the lifecycle owner while parked. */
void tx_audio_buffer_reset_stats(tx_audio_buffer_t *buffer);
