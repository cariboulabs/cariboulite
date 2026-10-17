#pragma once
#include <stddef.h>
#include "iq16.h"
#include "audio_format.h"
#include "nbfm_defaults.h"

// Standalone DSP. No threads, device handles, queues or retained caller buffers.
// NBFM audio defaults to NBFM_DEFAULT_DEVIATION_HZ normalization.
typedef struct nbfm_demod audio_demod_t;
typedef enum { AUDIO_DEMOD_NBFM = 0, AUDIO_DEMOD_WBFM = 1 } audio_demod_mode_t;
typedef struct {
    unsigned rf_rate;       // exactly 1000000, 2000000 or 4000000 Hz
    unsigned audio_rate;    // exactly 48000 Hz
    float deemph_tau;       // seconds; zero bypasses de-emphasis
    float pcm_gain;         // scale filtered audio to signed-16-bit PCM
} audio_demod_config_t;
typedef struct {
    size_t consumed;        // IQ pairs consumed
    size_t produced;        // mono PCM samples written
    int error;             // 0 or negative errno-style code
} audio_demod_result_t;

// Unsupported modes fail with errno=EINVAL; the selected mode is immutable.
audio_demod_t* audio_demod_create(audio_demod_mode_t mode, const audio_demod_config_t* config);
// Zero for null/unsupported mode. Noise metric means the NBFM raw-tap contract.
#define AUDIO_DEMOD_CAP_NOISE_SQUELCH 1u
unsigned audio_demod_mode_capabilities(audio_demod_mode_t mode);
unsigned audio_demod_capabilities(const audio_demod_t* dsp);
// Both modes clear all signal history, decimator phases, filters and resampling.
void audio_demod_reset(audio_demod_t* dsp);
// Update audio controls without resetting filter history.
int audio_demod_set_audio(audio_demod_t* dsp, float deemph_tau, float pcm_gain);
// NBFM only: set expected peak deviation for audio normalization, in Hz
// (finite 1..24000). Channel filters and the fixed raw/noise-squelch scale are
// unchanged. Reset retains the setting. Single owner; invalid values or other
// modes return -EINVAL without changing the DSP.
int audio_demod_set_nbfm_deviation(audio_demod_t* dsp, float deviation_hz);
// correction is fractional output-rate adjustment in [-0.0005, 0.0005].
// No allocations. Stops at input end or output capacity; retry unused input.
// Zero capacity consumes nothing; no hidden pending output. Single owner.
audio_demod_result_t audio_demod_process(audio_demod_t* dsp,
    const iq16_t* input, size_t count, audio_s16_t* output, size_t capacity,
    double correction);
void audio_demod_destroy(audio_demod_t* dsp);

// Optional parallel 48 kHz discriminator tap, before DC/de-emphasis/low-pass
// and PCM gain in NBFM; WBFM taps follow mono/anti-alias filtering.
// NBFM raw values retain NBFM_DEFAULT_DEVIATION_HZ normalization regardless
// of the configured receive audio deviation.
// raw_audio has capacity floats and receives exactly produced
// samples; NULL skips the tap. Same progress/lifetime rules as process().
audio_demod_result_t audio_demod_process_with_raw(audio_demod_t* dsp,
    const iq16_t* input, size_t count, audio_s16_t* output, float* raw_audio,
    size_t capacity, double correction);
