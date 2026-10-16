#include "fm_demod_internal.h"
#include "fm_audio.h"
#include "fm_discriminator.h"
#include "nbfm_channel_filter.h"
#include "math_compat.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct nb_state {
    nbfm_demod_t base;
    nbfm_demod_config_t config;
    int use_limiter;
    float K_norm;
    float audio_deviation_scale;
    nbfm_channel_filter_t channel;
    float pi50, pq50;
    int have_prev50;
    fm_audio_state_t audio;
    float y_prev_50k, y_curr_50k;
    double phase48;
} nb_state_t;

int nb_demod_set_audio(nbfm_demod_t* dsp, float tau, float gain)
{
    nb_state_t* s = (nb_state_t*)dsp;
    if (!s || !isfinite(tau) || tau < 0 || !isfinite(gain) || gain < 0)
        return -EINVAL;
    s->config.deemph_tau = tau;
    s->config.pcm_gain = gain;
    return 0;
}
int nb_demod_set_deviation(nbfm_demod_t* dsp, float deviation_hz)
{
    // isfinite() may be optimized away by the production -ffast-math flags.
    uint32_t bits;
    _Static_assert(sizeof(bits) == sizeof(deviation_hz), "32-bit float required");
    memcpy(&bits, &deviation_hz, sizeof(bits));
    if (!dsp || dsp->mode != FM_MODE_NBFM ||
        (bits & 0x7f800000u) == 0x7f800000u ||
        deviation_hz < NBFM_RX_MIN_DEVIATION_HZ ||
        deviation_hz > NBFM_RX_MAX_DEVIATION_HZ)
        return -EINVAL;
    nb_state_t* s = (nb_state_t*)dsp;
    s->audio_deviation_scale = NBFM_DEFAULT_DEVIATION_HZ / deviation_hz;
    return 0;
}
nbfm_demod_t* nb_demod_create(const nbfm_demod_config_t* config)
{
    if (!config || (config->rf_rate != 1000000 && config->rf_rate != 2000000 &&
        config->rf_rate != 4000000) ||
        config->audio_rate != 48000 || !isfinite(config->deemph_tau) ||
        config->deemph_tau < 0 || !isfinite(config->pcm_gain) || config->pcm_gain < 0) {
        errno = EINVAL;
        return NULL;
    }
    nb_state_t* s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->base.mode = FM_MODE_NBFM;
    s->config = *config;
    nbfm_channel_filter_init(&s->channel, config->rf_rate);
    s->use_limiter = 1;
    s->K_norm = 50000.0f / (2.0f * (float)M_PI * NBFM_DEFAULT_DEVIATION_HZ);
    s->audio_deviation_scale = 1.0f;
    fm_audio_init(&s->audio);
    return &s->base;
}
void nb_demod_reset(nbfm_demod_t* dsp)
{
    nb_state_t* s = (nb_state_t*)dsp;
    if (!s) return;
    nbfm_channel_filter_reset(&s->channel);
    s->pi50 = s->pq50 = 0.0f;
    s->have_prev50 = 0;
    fm_audio_reset(&s->audio);
    s->y_prev_50k = s->y_curr_50k = 0.0f;
    s->phase48 = 0.0;
}

nbfm_demod_result_t nb_demod_process_with_raw(nbfm_demod_t* dsp,
    const iq16_t* input, size_t count, int16_t* output, float* raw_audio, size_t capacity,
    double correction)
{
    nb_state_t* s = (nb_state_t*)dsp;
    nbfm_demod_result_t result = {0};
    if (!s || (!input && count) || (!output && capacity) ||
        !isfinite(correction) || fabs(correction) > 0.0005) {
        result.error = -EINVAL;
        return result;
    }
    for (size_t n = 0; n < count && result.produced < capacity; ++n) {
        ++result.consumed;
        float y50 = 0.0f;
        float i50, q50;
        if (!nbfm_channel_filter_push(&s->channel, input[n], &i50, &q50))
            continue;

        // --- limiter (unit vector) ---
        if (s->use_limiter) {
            float m2 = i50*i50 + q50*q50;
            if (m2 > 0.0f) {
                float invm = 1.0f / sqrtf(m2);
                i50 *= invm; q50 *= invm;
            }
        }

        // --- discriminator at 50 kS/s using previous 50k sample ---
        if (s->have_prev50) {
            float re, im;
            fm_conjugate_product(i50, q50, s->pi50, s->pq50, &re, &im);
            const float dphi = fm_discriminator_angle(im, re);
            y50 = dphi * s->K_norm;                   // normalize to ~±1 @ ±dev
        } else {
            s->have_prev50 = 1;
        }
        s->pi50 = i50; s->pq50 = q50;

        // Interpolation endpoints for this 50k interval
        s->y_prev_50k = s->y_curr_50k;
        s->y_curr_50k = y50;
        // Advance phase by "outputs per input" this step (r < 1)
        const double r = (48000.0 / 50000.0) * (1.0 + correction);
        s->phase48 += r;

        // If we crossed 1.0, emit exactly one 48k sample at that crossing
        if (s->phase48 >= 1.0) {
            // The phase overshoot measures how far the output crossing lies
            // before the current endpoint, in 50 kHz sample intervals.
            const double frac = (s->phase48 - 1.0) / r;    // ∈ [0..1)
            const float y_lin = s->y_curr_50k + (float)frac *
                (s->y_prev_50k - s->y_curr_50k);

            if (raw_audio) raw_audio[result.produced] = y_lin;

            output[result.produced++] = fm_audio_process(&s->audio,
                y_lin * s->audio_deviation_scale,
                s->config.deemph_tau, s->config.pcm_gain, 1);

            // Keep fractional remainder, including across process calls.
            s->phase48 -= 1.0;
        }
    }
    return result;
}

