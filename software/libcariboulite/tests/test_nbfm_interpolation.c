#include "nbfm_demod.h"
#include "nbfm_channel_filter.h"
#include "math_compat.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void make_fm(iq16_t* input, size_t count, unsigned rate)
{
    double phase = 0;
    for (size_t n = 0; n < count; ++n) {
        double time = (double)n / rate;
        double hz = 500 + 1400*sin(2*M_PI*600*time) +
            1000*sin(2*M_PI*3000*time);
        phase = remainder(phase + 2*M_PI*hz/rate, 2*M_PI);
        input[n] = (iq16_t){(int16_t)lrint(1800*cos(phase)),
                           (int16_t)lrint(1800*sin(phase))};
    }
}

// Collect the actual filter endpoints, then independently derive their phase
// differences in double precision. Endpoint 0 is the receiver's initial zero.
// The oracle below uses absolute output times; it shares neither the production
// phase accumulator nor its interpolation expression.
static size_t endpoints(unsigned rate, const iq16_t* input, size_t count,
                        double* audio)
{
    nbfm_channel_filter_t filter;
    nbfm_channel_filter_init(&filter, rate);
    size_t n = 0;
    double previous_i = 0, previous_q = 0;
    audio[0] = 0;
    for (size_t k = 0; k < count; ++k) {
        float i, q;
        if (!nbfm_channel_filter_push(&filter, input[k], &i, &q)) continue;
        double re = (double)i*previous_i + (double)q*previous_q;
        double im = (double)q*previous_i - (double)i*previous_q;
        audio[++n] = re == 0 && im == 0 ? 0 :
            atan2(im, re) * 50000/(2*M_PI*2500);
        previous_i = i; previous_q = q;
    }
    return n;
}

static size_t process(nbfm_demod_t* state, const iq16_t* input, size_t count,
                      int16_t* pcm, float* raw, size_t capacity,
                      double correction, int segmented)
{
    static const size_t chunks[] = {1, 3, 41, 4093, 11, 997, 17003};
    static const size_t caps[] = {0, 1, 7, 2, 480, 3, 71};
    size_t used = 0, produced = 0, calls = 0;
    while (used < count) {
        size_t chunk = segmented ? chunks[calls%7] : count-used;
        size_t cap = segmented ? caps[calls%7] : capacity-produced;
        if (chunk > count-used) chunk = count-used;
        if (cap > capacity-produced) cap = capacity-produced;
        nbfm_demod_result_t result = nbfm_demod_process_with_raw(state,
            input+used, chunk, pcm+produced, raw+produced, cap, correction);
        assert(!result.error && result.consumed <= chunk && result.produced <= cap);
        if (!cap) assert(!result.consumed && !result.produced);
        else assert(result.consumed);
        used += result.consumed; produced += result.produced; ++calls;
        assert(produced < capacity || used == count);
    }
    return produced;
}

static void check(unsigned rate, double correction)
{
    size_t count = rate/5 + 7*(rate/50000) + 13, capacity = 9700;
    iq16_t* input = malloc(count*sizeof(*input));
    int16_t* a_pcm = malloc(capacity*sizeof(*a_pcm));
    int16_t* b_pcm = malloc(capacity*sizeof(*b_pcm));
    float* a_raw = malloc(capacity*sizeof(*a_raw));
    float* b_raw = malloc(capacity*sizeof(*b_raw));
    double* y50 = malloc((count/(rate/50000)+1)*sizeof(*y50));
    assert(input && a_pcm && b_pcm && a_raw && b_raw && y50);
    make_fm(input, count, rate);
    size_t n50 = endpoints(rate, input, count, y50);
    nbfm_demod_config_t config = {rate, 48000, 50e-6f, 8000};
    nbfm_demod_t* a = nbfm_demod_create(&config);
    nbfm_demod_t* b = nbfm_demod_create(&config);
    assert(a && b);
    size_t n = process(a, input, count, a_pcm, a_raw, capacity, correction, 0);
    size_t m = process(b, input, count, b_pcm, b_raw, capacity, correction, 1);
    assert(n == m && !memcmp(a_pcm, b_pcm, n*sizeof(*a_pcm)) &&
           !memcmp(a_raw, b_raw, n*sizeof(*a_raw)));

    const double r = 48000.0/50000 * (1+correction);
    assert(n == (size_t)floor(n50*r));
    double worst = 0;
    for (size_t k = 0; k < n; ++k) {
        double time50 = (k+1)/r;
        size_t left = (size_t)floor(time50);
        double position = time50-left;
        // Some output times coincide exactly with an input endpoint.
        assert(left < n50 || (left == n50 && position < 1e-9));
        double expected = y50[left];
        if (left < n50) expected += position*(y50[left+1]-y50[left]);
        double error = fabs(a_raw[k]-expected);
        if (error > worst) worst = error;
        assert(error < 5e-6);
    }

    // The previous run stopped within the RF decimation interval. Reset must
    // clear endpoints and fractional output time as well as the channel FIR.
    nbfm_demod_reset(a);
    m = process(a, input, count, b_pcm, b_raw, capacity, correction, 1);
    assert(n == m && !memcmp(a_pcm, b_pcm, n*sizeof(*a_pcm)) &&
           !memcmp(a_raw, b_raw, n*sizeof(*a_raw)));
    printf("PASS %u Hz, correction %+.4f: %zu outputs at independent absolute "
           "sample times, max error %.3g, chunks/capacities/reset identical\n",
           rate, correction, n, worst);
    nbfm_demod_destroy(a); nbfm_demod_destroy(b);
    free(input); free(a_pcm); free(b_pcm); free(a_raw); free(b_raw); free(y50);
}

int main(void)
{
    for (unsigned rate = 1000000; rate <= 4000000; rate *= 2) {
        check(rate, 0);
        check(rate, 0.0005);
        check(rate, -0.0005);
    }
    puts("NBFM interpolation timing checks pass at 1/2/4 MS/s");
    return 0;
}
