#pragma once
#include "iq16.h"
#include "nbfm_channel_coeffs.h"
#include <stdint.h>
#include <string.h>

// Private complex anti-alias/channel filter: RF -> CIC3 -> 200 kS/s -> FIR
// decimate-by-4 -> 50 kS/s. Identical real coefficients filter I and Q before
// limiting. The FIR passes +/-6 kHz and reaches its stopband at +/-9 kHz.
typedef struct {
    unsigned decimation, cic_count, fir_count, pos;
    float cic_scale;
    uint32_t integrator_i[3], integrator_q[3];
    uint32_t comb_i[3], comb_q[3];
    float history_i[2 * NBFM_CHANNEL_TAPS];
    float history_q[2 * NBFM_CHANNEL_TAPS];
} nbfm_channel_filter_t;

static inline void nbfm_channel_filter_reset(nbfm_channel_filter_t* s)
{
    // Retain only the immutable rate configuration; clear all signal history
    // and decimator phases so reset is equivalent to a new filter instance.
    unsigned decimation = s->decimation;
    float scale = s->cic_scale;
    memset(s, 0, sizeof(*s));
    s->decimation = decimation;
    s->cic_scale = scale;
}

static inline void nbfm_channel_filter_init(nbfm_channel_filter_t* s,
                                            unsigned rf_rate)
{
    memset(s, 0, sizeof(*s));
    // The demodulator validates the supported 1/2/4 MS/s rates at creation.
    s->decimation = rf_rate / 200000;
    unsigned d = s->decimation;
    s->cic_scale = 1.0f / (float)(d * d * d);
}

static inline float nbfm_cic_signed(uint32_t value)
{
    // CIC integrators wrap modulo 2^32 using defined unsigned arithmetic.
    // The final CIC3 result is bounded by 32768 * 20^3 = 262144000, so its
    // signed value fits int32_t even for full-range input at the largest rate.
    // Recover it without signed overflow or implementation-defined conversion.
    int32_t signed_value = value <= INT32_MAX ? (int32_t)value :
        -(int32_t)(UINT32_MAX - value) - 1;
    return (float)signed_value;
}

static inline int nbfm_channel_filter_push(nbfm_channel_filter_t* s,
    iq16_t sample, float* i, float* q)
{
    s->integrator_i[0] += (uint32_t)(int32_t)sample.i;
    s->integrator_q[0] += (uint32_t)(int32_t)sample.q;
    s->integrator_i[1] += s->integrator_i[0];
    s->integrator_q[1] += s->integrator_q[0];
    s->integrator_i[2] += s->integrator_i[1];
    s->integrator_q[2] += s->integrator_q[1];
    if (++s->cic_count != s->decimation) return 0;
    s->cic_count = 0;

    uint32_t ci = s->integrator_i[2], cq = s->integrator_q[2];
    for (unsigned stage = 0; stage < 3; ++stage) {
        uint32_t di = ci - s->comb_i[stage];
        uint32_t dq = cq - s->comb_q[stage];
        s->comb_i[stage] = ci;
        s->comb_q[stage] = cq;
        ci = di; cq = dq;
    }
    float fi = nbfm_cic_signed(ci) * s->cic_scale;
    float fq = nbfm_cic_signed(cq) * s->cic_scale;
    unsigned p = s->pos;
    s->history_i[p] = s->history_i[p + NBFM_CHANNEL_TAPS] = fi;
    s->history_q[p] = s->history_q[p + NBFM_CHANNEL_TAPS] = fq;
    s->pos = p + 1 == NBFM_CHANNEL_TAPS ? 0 : p + 1;
    if (++s->fir_count != 4) return 0;
    s->fir_count = 0;

    // Mirrored ring histories give contiguous windows. Exploit coefficient
    // symmetry and evaluate only at decimation output instants (50 kS/s).
    const float* hi = s->history_i + s->pos;
    const float* hq = s->history_q + s->pos;
    const unsigned center = NBFM_CHANNEL_TAPS / 2;
    float yi = nbfm_channel_coeffs[center] * hi[center];
    float yq = nbfm_channel_coeffs[center] * hq[center];
    for (unsigned j = 0; j < center; ++j) {
        yi += nbfm_channel_coeffs[j] * (hi[j] + hi[NBFM_CHANNEL_TAPS - 1 - j]);
        yq += nbfm_channel_coeffs[j] * (hq[j] + hq[NBFM_CHANNEL_TAPS - 1 - j]);
    }
    *i = yi; *q = yq;
    return 1;
}
