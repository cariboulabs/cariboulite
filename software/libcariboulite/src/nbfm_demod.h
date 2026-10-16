#pragma once
#include "audio_demod.h"

// Legacy FM names retained for existing callers.
typedef audio_demod_t nbfm_demod_t;
typedef audio_demod_mode_t fm_demod_mode_t;
typedef audio_demod_config_t nbfm_demod_config_t;
typedef audio_demod_result_t nbfm_demod_result_t;
#define FM_MODE_NBFM AUDIO_DEMOD_NBFM
#define FM_MODE_WBFM AUDIO_DEMOD_WBFM

nbfm_demod_t* nbfm_demod_create(const nbfm_demod_config_t* config);
// Mono broadcast FM, +/-75 kHz deviation. Shares the streaming/audio API.
nbfm_demod_t* wbfm_demod_create(const nbfm_demod_config_t* config);
// Both modes clear all signal history, decimator phases, filters and resampling.
void nbfm_demod_reset(nbfm_demod_t* dsp);
// Update audio controls without resetting filter history.
int nbfm_demod_set_audio(nbfm_demod_t* dsp, float deemph_tau, float pcm_gain);
// NBFM only; same normalization and validation contract as the neutral API.
int nbfm_demod_set_deviation(nbfm_demod_t* dsp, float deviation_hz);
// correction is fractional output-rate adjustment in [-0.0005, 0.0005].
// No allocations. Stops at input end or output capacity; retry unused input.
// Zero capacity consumes nothing; no hidden pending output. Single owner.
nbfm_demod_result_t nbfm_demod_process(nbfm_demod_t* dsp,
    const iq16_t* input, size_t count, audio_s16_t* output, size_t capacity,
    double correction);
void nbfm_demod_destroy(nbfm_demod_t* dsp);

// Optional parallel 48 kHz discriminator tap, before DC/de-emphasis/low-pass
// and PCM gain in NBFM; WBFM taps follow mono/anti-alias filtering.
// NBFM raw values retain the default deviation scale independently of audio.
// raw_audio has capacity floats and receives exactly produced
// samples; NULL skips the tap. Same progress/lifetime rules as process().
nbfm_demod_result_t nbfm_demod_process_with_raw(nbfm_demod_t* dsp,
    const iq16_t* input, size_t count, audio_s16_t* output, float* raw_audio,
    size_t capacity, double correction);
