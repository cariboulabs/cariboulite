#include "fm_discriminator.h"
#include "nbfm_demod.h"
#include "math_compat.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t random_state = 0xcafec001;
static uint32_t random_word(void)
{
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

static int finite_float(float value)
{
    // This remains a real finite-value check under the production fast-math
    // policy, which otherwise permits the compiler to assume all values finite.
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}

static double check_angle(float im, float re)
{
    // Volatile inputs prevent the compiler from replacing the signed-axis
    // cases with constant-folded results under either floating-point policy.
    volatile float input_im = im, input_re = re;
    float actual = fm_discriminator_angle(input_im, input_re);
    double expected = re == 0 && im == 0 ? 0 : atan2((double)im, (double)re);
    double error = fabs((double)actual - expected);
    assert(finite_float(actual) && fabsf(actual) <= (float)M_PI);
    assert(error < 1e-6);
    return error;
}

static void angle_accuracy(void)
{
    double worst = 0;
    const float scales[] = {1e-18f, 1e-9f, 1, 1e9f, 1e18f};
    for (unsigned n = 0; n <= 100000; ++n) {
        double angle = -M_PI + 2*M_PI*n/100000;
        for (unsigned scale = 0; scale < sizeof(scales)/sizeof(*scales); ++scale) {
            double error = check_angle((float)(scales[scale]*sin(angle)),
                                       (float)(scales[scale]*cos(angle)));
            if (error > worst) worst = error;
        }
    }
    for (unsigned n = 0; n < 100000; ++n) {
        double angle = -M_PI + 2*M_PI*(double)random_word()/UINT32_MAX;
        double error = check_angle((float)sin(angle), (float)cos(angle));
        if (error > worst) worst = error;
    }
    // The former approximation returned about +/-4.50 radians for +/-2
    // radians in the negative-real half-plane, outside the principal range.
    check_angle(sinf(2), cosf(2));
    check_angle(sinf(-2), cosf(-2));
    const float axes[][2] = {
        {0, 1}, {-0.0f, 1}, {0, -1}, {-0.0f, -1},
        {1, 0}, {-1, 0}, {1, -0.0f}, {-1, -0.0f},
        {0, 0}, {-0.0f, 0}, {0, -0.0f}, {-0.0f, -0.0f}
    };
    for (unsigned n = 0; n < sizeof(axes)/sizeof(*axes); ++n)
        check_angle(axes[n][0], axes[n][1]);
    printf("PASS full-circle, signed axes, zero vectors and magnitude scaling: maximum error %.3g radians\n", worst);
}

static void iq_rotation(void)
{
    double worst = 0;
    // Production enables flush-to-zero for subnormal products. Stay within
    // the normal-product range here; the angle itself is tested over the much
    // wider 1e-18-to-1e18 complex-product magnitudes above.
    const float scales[] = {1e-9f, 1, 1e9f};
    for (unsigned n = 0; n < 100000; ++n) {
        double previous = -M_PI + 2*M_PI*(double)random_word()/UINT32_MAX;
        double rotation = -M_PI + 2*M_PI*(double)random_word()/UINT32_MAX;
        double current = previous + rotation;
        float amplitude = scales[n % 3];
        float pi = (float)(amplitude*cos(previous));
        float pq = (float)(amplitude*sin(previous));
        float i = (float)(amplitude*cos(current));
        float q = (float)(amplitude*sin(current));
        float re, im;
        fm_conjugate_product(i, q, pi, pq, &re, &im);
        float actual = fm_discriminator_angle(im, re);
        double expected = atan2((double)q*pi - (double)i*pq,
                               (double)i*pi + (double)q*pq);
        // Compare modulo a full turn, since roundoff at the negative-real
        // branch cut may legitimately choose the opposite representation.
        double error = fabs(remainder((double)actual-expected, 2*M_PI));
        assert(finite_float(actual) && fabsf(actual) <= (float)M_PI);
        if (error >= 1e-6)
            fprintf(stderr, "IQ rotation %u amplitude %g: (%g,%g), actual %.9g expected %.9g error %.9g\n",
                    n, amplitude, re, im, actual, expected, error);
        assert(error < 1e-6);
        if (error > worst) worst = error;
    }
    const float samples[][4] = {
        {0, 0, 1, 1}, {1, 1, 0, 0}, {0, 0, 0, 0},
        {-0.0f, 0, -1, 1}, {1, -1, 0, -0.0f}
    };
    for (unsigned n = 0; n < sizeof(samples)/sizeof(*samples); ++n) {
        float re, im;
        fm_conjugate_product(samples[n][0], samples[n][1],
                             samples[n][2], samples[n][3], &re, &im);
        assert(fm_discriminator_angle(im, re) == 0);
    }
    printf("PASS actual current * conjugate(previous) rotations: maximum error %.3g radians\n", worst);
}

static void receiver_recovery(unsigned rate)
{
    size_t count = rate/10, capacity = 4801;
    iq16_t* iq = calloc(count, sizeof(*iq));
    int16_t* pcm = malloc(capacity*sizeof(*pcm));
    float* raw = malloc(capacity*sizeof(*raw));
    assert(iq && pcm && raw);
    double phase = 0;
    for (size_t n = 0; n < count; ++n) {
        double time = (double)n/rate;
        if (time >= 0.02 && time < 0.04) {
            double envelope = fmin(1, fmin((time-0.02)/0.005, (0.04-time)/0.005));
            phase = remainder(phase + 2*M_PI*(500+2500*sin(2*M_PI*600*time))/rate, 2*M_PI);
            iq[n] = (iq16_t){(int16_t)lrint(1800*envelope*cos(phase)),
                             (int16_t)lrint(1800*envelope*sin(phase))};
        } else if (time >= 0.04 && time < 0.06) {
            // Band-limited phase near weak random IQ can cross every quadrant;
            // intermittent exact zeros also exercise lost-signal transitions.
            if (random_word() & 3)
                iq[n] = (iq16_t){(int16_t)((int)(random_word()%3001)-1500),
                                 (int16_t)((int)(random_word()%3001)-1500)};
        } else if (time >= 0.08) {
            phase = remainder(phase + 2*M_PI*500/rate, 2*M_PI);
            iq[n] = (iq16_t){(int16_t)lrint(1800*cos(phase)),
                             (int16_t)lrint(1800*sin(phase))};
        }
    }
    nbfm_demod_config_t config = {rate, 48000, 50e-6f, 8000};
    nbfm_demod_t* dsp = nbfm_demod_create(&config);
    assert(dsp);
    size_t used = 0, produced = 0;
    while (used < count) {
        nbfm_demod_result_t result = nbfm_demod_process_with_raw(dsp,
            iq+used, count-used, pcm+produced, raw+produced, capacity-produced, 0);
        assert(!result.error && result.consumed > 0);
        used += result.consumed;
        produced += result.produced;
    }
    assert(produced >= 4799 && produced <= 4800);
    const float bound = 50000.0f/(2*NBFM_DEFAULT_DEVIATION_HZ);
    double maximum = 0, recovered_sum = 0;
    for (size_t n = 0; n < produced; ++n) {
        assert(finite_float(raw[n]) && fabsf(raw[n]) <= bound+1e-5f);
        if (fabsf(raw[n]) > maximum) maximum = fabsf(raw[n]);
        if (n < 480 || (n >= 3120 && n < 3600)) assert(raw[n] == 0);
        if (n >= 4320 && n < 4704) {
            float expected = 500.0f/NBFM_DEFAULT_DEVIATION_HZ;
            assert(fabsf(raw[n]-expected) < 0.01f);
            recovered_sum += raw[n];
        }
    }
    assert(fabs(recovered_sum/384-500.0/NBFM_DEFAULT_DEVIATION_HZ) < 0.001);
    printf("PASS %u Hz public receiver: zero input, fading FM, random IQ, silence and carrier recovery; peak raw %.4f within +/-%.1f\n",
           rate, maximum, bound);
    nbfm_demod_destroy(dsp);
    free(iq); free(pcm); free(raw);
}

int main(void)
{
    setbuf(stdout, NULL);
    angle_accuracy();
    iq_rotation();
    for (unsigned rate = 1000000; rate <= 4000000; rate *= 2)
        receiver_recovery(rate);
    return 0;
}
