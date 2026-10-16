#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#define RX_SQUELCH_NOISE 1u
#define RX_SQUELCH_CARRIER 2u
#include "audio_sink.h"
#include "audio_demod.h"
#include "pipeline_runtime.h"

typedef struct rf10_fifo_s rf10_fifo_t;
typedef struct aud10_fifo_s aud10_fifo_t;

// Thread control owned by the RX pipeline; audio output is mono at 48 kHz.
typedef struct {
    atomic_bool alive; // worker lifetime, including cancellation/error exit
    atomic_int last_error;
    pipeline_thread_diagnostics_t scheduling;
    atomic_uint squelch_flags; // control writes, worker reads; zero bypasses both
    atomic_uint squelch_open;  // worker publishes effective gate (0/1)
    atomic_uint noise_squelch_levels; // open RMS milli (upper 16), close (lower); zero defaults
    atomic_uint noise_squelch_rms_milli; // worker telemetry, raw detector RMS * 1000
    atomic_uint noise_squelch_detector_open; // detector decision even while bypassed
    atomic_uint noise_squelch_valid; // telemetry available for NBFM; zero before data/WBFM
    audio_demod_mode_t     mode;        // immutable while worker is alive
    audio_demod_t*       dsp;         // pipeline-owned; destroy after joining worker
    bool                active;
    rf10_fifo_t*        fifo_in;     // 10 ms IQ frames at fs_rf
    float               deemph_tau;  // e.g., 75e-6 (NA) or 50e-6 (EU)
    float               fs_rf;       // RF sample rate in Hz (2e6 or 4e6)
    float               fs_audio;    // 48000
    volatile bool       reset;       // set true to force state re-init
    int                 prime_blocks_10ms; // e.g., 20 blocks = 200 ms @ 48k
    bool                priming;     // internal flag
    
    // state
    float               deemph_y;
    int16_t             last_i, last_q; // FM discrim previous sample

    // Playback diagnostics (moved to pipeline in step 5)
    audio_sink_t*       sink;
    unsigned            pcm_rate;
    unsigned            pcm_channels;   
    float               pcm_gain;       
    uint64_t            pcm_total_frames; // diag counter
    aud10_fifo_t*  afifo_out;   // where the 10 ms audio frames go
} audio_demod_ctrl_t;

// pthread entry point for IQ-to-audio processing.
void* audio_demod_thread(void* arg);

// Compatibility aliases for existing internal/offline worker callers.
typedef audio_demod_ctrl_t nbfm_demod_ctrl_t;
void* nbfm_demod_thread(void* arg);
