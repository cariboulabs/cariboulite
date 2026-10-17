#pragma once
// Internal allocation-free audio primitives. Each modem owns its own history.
#include "math_compat.h"
#include <stdint.h>
#include <math.h>

typedef struct {
    float dc_a, lpf_a, lpf_b;
    float dc_y, x_prev_audio, deemph_state, lpf_y;
} fm_audio_state_t;

// Initializes coefficients only; history must be zeroed or reset by the owner.
static inline void fm_audio_init(fm_audio_state_t* s)
{
    s->dc_a = expf(-2.0f * (float)M_PI * 5.0f / 48000.0f);
    s->lpf_a = expf(-2.0f * (float)M_PI * 3200.0f / 48000.0f);
    s->lpf_b = 1.0f - s->lpf_a;
}
static inline void fm_audio_reset(fm_audio_state_t* s)
{
    s->dc_y = s->x_prev_audio = s->deemph_state = s->lpf_y = 0.0f;
}

// --- audio-rate deemphasis (48 kHz) ---
static inline float fm_deemph_48k(float x, float *z, float tau_s)
{
    if (tau_s <= 0.f) return x;          // bypass if tau==0
    const float fs = 48000.f;
    const float a  = expf(-1.0f/(fs * tau_s));
    const float b  = 1.0f - a;
    *z = a * (*z) + b * x;
    return *z;
}

// Input is normalized discriminator audio at 48 kHz. Preserve operation order:
// DC rejection, de-emphasis, legacy low-pass evaluation, mode selection, PCM.
// NBFM selects the 3.2 kHz low-pass; WBFM retains its upstream FIR bandwidth.
// Gain maps normalized audio to S16 units. Clipping and lrintf are unchanged.
static inline int16_t fm_audio_process(fm_audio_state_t* s, float sample,
                                      float tau, float gain, int narrow)
{
    // === 48k audio chain ===
    float x = sample;
    float y = (x - s->x_prev_audio) + s->dc_a * s->dc_y;
    s->x_prev_audio = x;
    s->dc_y = y; if (fabsf(s->dc_y) < 1e-20f) s->dc_y = 0.0f;

    float yd = fm_deemph_48k(y, &s->deemph_state, tau);
    if (fabsf(s->deemph_state) < 1e-20f) s->deemph_state = 0.0f;

    s->lpf_y = s->lpf_a * s->lpf_y + s->lpf_b * yd;
    float ya = narrow ? s->lpf_y : yd;
    if (fabsf(s->lpf_y) < 1e-20f) s->lpf_y = 0.0f;

    float pcm = ya * gain;
    if (pcm >  32767.f) pcm =  32767.f;
    if (pcm < -32768.f) pcm = -32768.f;
    return (int16_t)lrintf(pcm);
}
