// Hardware-free benchmark helper, launched by benchmark_nbfm_discriminator.py.
#include "fm_demod_internal.h"
#include "math_compat.h"
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DECLARE_BACKEND(prefix) \
    nbfm_demod_t* prefix##_nb_demod_create(const nbfm_demod_config_t*); \
    void prefix##_nb_demod_reset(nbfm_demod_t*); \
    nbfm_demod_result_t prefix##_nb_demod_process_with_raw(nbfm_demod_t*, \
        const iq16_t*, size_t, int16_t*, float*, size_t, double)
DECLARE_BACKEND(legacy);
DECLARE_BACKEND(atan2f);

typedef nbfm_demod_result_t (*process_fn)(nbfm_demod_t*, const iq16_t*,
    size_t, int16_t*, float*, size_t, double);
static volatile int64_t checksum;

static uint32_t random_u32(uint32_t* state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *state = x;
}

static double cpu_seconds(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time)) {
        perror("CLOCK_THREAD_CPUTIME_ID");
        exit(1);
    }
    return (double)time.tv_sec + (double)time.tv_nsec * 1e-9;
}

static float legacy_angle(float y, float x)
{
    float angle = y / (fabsf(x) + 0.28f * fabsf(y) + 1e-10f);
    if (x < 0.0f)
        angle = y >= 0.0f ? (float)M_PI + angle : -((float)M_PI - angle);
    return angle;
}

static float full_angle(float y, float x)
{
    return (x == 0.0f && y == 0.0f) ? 0.0f : atan2f(y, x);
}

static void angle_accuracy(void)
{
    double maximum[2] = {0}, typical[2] = {0};
    const unsigned points = 262144;
    for (unsigned n = 0; n <= points; ++n) {
        double theta = -M_PI + 2.0 * M_PI * n / points;
        float y = (float)sin(theta), x = (float)cos(theta);
        double reference = atan2((double)y, (double)x);
        double errors[2] = {
            fabs((double)legacy_angle(y, x) - reference),
            fabs((double)full_angle(y, x) - reference),
        };
        for (unsigned p = 0; p < 2; ++p) {
            if (errors[p] > maximum[p]) maximum[p] = errors[p];
            if (fabs(theta) <= 2.0 * M_PI * 2500.0 / 50000.0 && errors[p] > typical[p])
                typical[p] = errors[p];
        }
    }
    printf("\"angle_accuracy\": {\"sweep_points\": %u, "
        "\"legacy_max_absolute_error_rad\": %.12g, "
        "\"atan2f_max_absolute_error_rad\": %.12g, "
        "\"legacy_max_error_within_2500Hz_rad\": %.12g, "
        "\"atan2f_max_error_within_2500Hz_rad\": %.12g, "
        "\"zero_product_atan2f\": %.9g},\n",
        points + 1, maximum[0], maximum[1], typical[0], typical[1],
        full_angle(0.0f, 0.0f));
}

static iq16_t* make_input(unsigned rate, int noise, size_t samples)
{
    iq16_t* input = calloc(samples, sizeof(*input));
    if (!input) { perror("calloc input"); exit(1); }
    uint32_t seed = 0x4e42464du;
    double phase = 0.0;
    for (size_t n = 0; n < samples; ++n) {
        if (noise) {
            input[n].i = (int16_t)((int)(random_u32(&seed) & 8191) - 4096);
            input[n].q = (int16_t)((int)(random_u32(&seed) & 8191) - 4096);
        } else {
            double t = (double)n / rate;
            double audio = 0.6 * sin(2 * M_PI * 600 * t) +
                           0.25 * sin(2 * M_PI * 1700 * t) +
                           0.15 * sin(2 * M_PI * 2800 * t);
            phase += 2 * M_PI * (500 + 2500 * audio) / rate;
            if (phase > M_PI) phase -= 2 * M_PI;
            if (phase < -M_PI) phase += 2 * M_PI;
            input[n].i = (int16_t)lrint(3000 * cos(phase));
            input[n].q = (int16_t)lrint(3000 * sin(phase));
        }
    }
    return input;
}

static void process_checked(process_fn process, nbfm_demod_t* state,
    const iq16_t* input, size_t count, int16_t* pcm, float* raw)
{
    nbfm_demod_result_t result = process(state, input, count, pcm, raw, 512, 0);
    if (result.error || result.consumed != count || result.produced < 479 || result.produced > 481) {
        fprintf(stderr, "Unexpected DSP progress: %d %zu/%zu %zu\n",
            result.error, result.consumed, count, result.produced);
        exit(1);
    }
    checksum += pcm[result.produced / 2];
}

static void one_case(unsigned rate, int noise, unsigned rounds, unsigned blocks)
{
    const size_t count = rate / 100;
    const unsigned input_blocks = 16;
    iq16_t* input = make_input(rate, noise, count * input_blocks);
    nbfm_demod_config_t config = {rate, 48000, 50e-6f, 8000};
    nbfm_demod_t* states[2] = {
        legacy_nb_demod_create(&config), atan2f_nb_demod_create(&config),
    };
    if (!states[0] || !states[1]) { perror("nb_demod_create"); exit(1); }
    process_fn process[2] = {
        legacy_nb_demod_process_with_raw, atan2f_nb_demod_process_with_raw,
    };
    double* totals[2] = {calloc(rounds, sizeof(double)), calloc(rounds, sizeof(double))};
    if (!totals[0] || !totals[1]) { perror("calloc totals"); exit(1); }
    double peak[2] = {0};
    int16_t pcm[512];
    float raw[512];
    uint32_t order_seed = 0x50494137u;
    for (unsigned round = 0; round < rounds; ++round) {
        legacy_nb_demod_reset(states[0]);
        atan2f_nb_demod_reset(states[1]);
        for (unsigned block = 0; block < 20; ++block)
            for (unsigned p = 0; p < 2; ++p)
                process_checked(process[p], states[p], input + count * (block % input_blocks), count, pcm, raw);
        for (unsigned block = 0; block < blocks; ++block) {
            unsigned first = random_u32(&order_seed) & 1;
            for (unsigned pass = 0; pass < 2; ++pass) {
                unsigned p = first ^ pass;
                double start = cpu_seconds();
                nbfm_demod_result_t result = process[p](states[p],
                    input + count * (block % input_blocks), count, pcm, raw, 512, 0);
                double elapsed = cpu_seconds() - start;
                totals[p][round] += elapsed;
                if (elapsed > peak[p]) peak[p] = elapsed;
                if (result.error || result.consumed != count || result.produced < 479 || result.produced > 481) {
                    fprintf(stderr, "Unexpected timed DSP progress\n");
                    exit(1);
                }
                checksum += pcm[result.produced / 2];
            }
        }
    }
    printf("{\"rf_rate\": %u, \"input\": \"%s\", ", rate,
        noise ? "deterministic full-band RF noise" : "three-tone FM, +/-2.5 kHz, +500 Hz offset");
    for (unsigned p = 0; p < 2; ++p) {
        printf("\"%s\": {\"round_seconds_per_block\": [", p ? "atan2f" : "legacy");
        for (unsigned round = 0; round < rounds; ++round)
            printf("%s%.12g", round ? ", " : "", totals[p][round] / blocks);
        printf("], \"peak_ms_per_block\": %.9g}%s", peak[p] * 1000, p ? "" : ", ");
    }
    printf("}");
    free(totals[0]); free(totals[1]);
    free(states[0]); free(states[1]); free(input);
}

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    unsigned rounds = (unsigned)strtoul(argv[1], NULL, 10);
    unsigned blocks = (unsigned)strtoul(argv[2], NULL, 10);
    printf("{\n");
    angle_accuracy();
    printf("\"cases\": [\n");
    unsigned rates[] = {1000000, 2000000, 4000000};
    for (unsigned rate = 0; rate < 3; ++rate)
        for (int noise = 0; noise < 2; ++noise) {
            one_case(rates[rate], noise, rounds, blocks);
            printf("%s\n", rate == 2 && noise == 1 ? "" : ",");
        }
    printf("], \"checksum\": %" PRId64 "\n}\n", checksum);
    return 0;
}
