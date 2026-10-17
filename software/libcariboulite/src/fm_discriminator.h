#pragma once
#include <math.h>
// Complex current * conjugate(previous), with legacy float operation order.
// Caller owns previous-IQ history and scaling. No normalization, state,
// allocation or reset here.
static inline void fm_conjugate_product(float i, float q, float pi, float pq,
                                        float* re, float* im)
{
    *re = i*pi + q*pq;
    *im = q*pi - i*pq;
}

// Four-quadrant phase difference in [-pi, pi]. A zero complex product has no
// defined phase: emit zero rather than a signed-zero-dependent +/-pi impulse.
// Evaluated at the discriminator's 50 kS/s rate in NBFM, not the RF input rate.
static inline float fm_discriminator_angle(float im, float re)
{
    return re == 0.0f && im == 0.0f ? 0.0f : atan2f(im, re);
}
