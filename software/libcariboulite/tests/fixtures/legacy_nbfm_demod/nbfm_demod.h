#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "audio_sink.h"

typedef struct rf10_fifo_s rf10_fifo_t;
typedef struct aud10_fifo_s aud10_fifo_t;

// Thread control owned by the RX pipeline; audio output is mono at 48 kHz.
typedef struct {
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
} nbfm_demod_ctrl_t;

// pthread entry point for IQ-to-audio processing.
void* nbfm_demod_thread(void* arg);
