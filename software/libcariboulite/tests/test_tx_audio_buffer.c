/* Hardware-free capture/ASRC tests, including deterministic clock mismatches.
 * Internal access keeps the fast virtual-clock simulation out of the public API. */
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
static bool virtual_clock;
static uint64_t virtual_ns;
static int test_clock_gettime(clockid_t clock, struct timespec *out);
#define clock_gettime test_clock_gettime
#include "tx_audio_buffer.c"
#undef clock_gettime
#include <assert.h>
#include <stdatomic.h>
#include <unistd.h>

static int test_clock_gettime(clockid_t clock, struct timespec *out)
{
    if (!virtual_clock || clock != CLOCK_MONOTONIC) return clock_gettime(clock, out);
    out->tv_sec = virtual_ns / 1000000000ULL;
    out->tv_nsec = virtual_ns % 1000000000ULL;
    return 0;
}

int alsa_source_reset(audio_source_t *source, bool prepare)
{ (void)source; (void)prepare; return -ENOTSUP; }

static void fixture_init(tx_audio_buffer_t *buffer)
{
    memset(buffer, 0, sizeof(*buffer));
    assert(!pthread_mutex_init(&buffer->mutex, NULL));
    pthread_condattr_t attributes;
    assert(!pthread_condattr_init(&attributes));
    assert(!pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC));
    assert(!pthread_cond_init(&buffer->changed, &attributes));
    assert(!pthread_condattr_destroy(&attributes));
    buffer->stats.minimum_frames = AUDIO_CAPACITY;
    clear_audio(buffer);
    buffer->enabled = buffer->primed = true;
}

static void fixture_destroy(tx_audio_buffer_t *buffer)
{
    pthread_cond_destroy(&buffer->changed);
    pthread_mutex_destroy(&buffer->mutex);
}

static void append_zeros(tx_audio_buffer_t *buffer, size_t frames)
{
    float samples[CAPTURE_BLOCK] = {0};
    while (frames) {
        size_t block = frames < CAPTURE_BLOCK ? frames : CAPTURE_BLOCK;
        append_locked(buffer, samples, block);
        frames -= block;
    }
}

static void clock_simulation(double input_per_output, bool jitter)
{
    tx_audio_buffer_t buffer;
    fixture_init(&buffer);
    append_zeros(&buffer, AUDIO_TARGET);
    double available = 0.0;
    uint64_t arrived = 0;
    uint64_t final_consumed = 0;
    virtual_clock = true;
    virtual_ns = 1000000000;
    float output[CAPTURE_BLOCK];
    for (int index = 0; index < 60000; ++index) {
        /* 600 virtual seconds. Burst capture stays in 480-frame periods; a
         * deterministic +/-2ms demand jitter has zero average clock error. */
        double jitter_frames = jitter ? ((index % 2) ? 96.0 : -96.0) : 0.0;
        available += CAPTURE_BLOCK * input_per_output + jitter_frames;
        virtual_ns = 1000000000 + (uint64_t)(available * 1e9 / AUDIO_RATE);
        while ((double)(arrived + CAPTURE_BLOCK) <= available) {
            append_zeros(&buffer, CAPTURE_BLOCK);
            arrived += CAPTURE_BLOCK;
            buffer.last_capture_ns = 1000000000 + arrived * 1000000000ULL / AUDIO_RATE;
            buffer.last_capture_frames = CAPTURE_BLOCK;
        }
        int result = tx_audio_buffer_read(&buffer, output, CAPTURE_BLOCK, 0);
        if (result) {
            fprintf(stderr, "simulation ratio=%g step=%d rc=%d queue=%zu ratio=%g\n",
                    input_per_output, index, result, buffer.count, buffer.ratio);
        }
        assert(!result);
        if (index == 53999) final_consumed = buffer.stats.consumed_frames;
    }
    virtual_clock = false;
    tx_audio_buffer_stats_t stats;
    tx_audio_buffer_get_stats(&buffer, &stats);
    printf("clock %.6f jitter=%d => correction %.1f ppm, queue %zu/%zu, range %zu..%zu\n",
           input_per_output, jitter, stats.correction_ppm, stats.queued_frames,
           stats.capacity_frames, stats.minimum_frames, stats.maximum_frames);
    assert(stats.clock_valid && !stats.underflows && !stats.overflows);
    assert(!stats.clock_limit_faults);
    double final_mean = (double)(stats.consumed_frames - final_consumed)
                      / (6000 * CAPTURE_BLOCK);
    assert(fabs(final_mean - input_per_output) < 0.000025);
    assert(fabs(stats.input_per_output - input_per_output) < 0.00025);
    assert(stats.queued_frames > AUDIO_TARGET - 1000
           && stats.queued_frames < AUDIO_TARGET + 1200);
    fixture_destroy(&buffer);
}

static void fidelity(double ratio, double frequency)
{
    tx_audio_buffer_t buffer;
    fixture_init(&buffer);
    buffer.ratio = ratio;
    float samples[3000];
    for (size_t index = 0; index < sizeof(samples)/sizeof(samples[0]); ++index)
        samples[index] = 0.8f * sin(2.0 * M_PI * frequency * index / AUDIO_RATE);
    append_locked(&buffer, samples, sizeof(samples)/sizeof(samples[0]));
    float output[CAPTURE_BLOCK];
    assert(!tx_audio_buffer_read(&buffer, output, CAPTURE_BLOCK, 0));
    double error_energy = 0.0, signal_energy = 0.0;
    double in_phase = 0.0, quadrature = 0.0;
    for (size_t index = 0; index < CAPTURE_BLOCK; ++index) {
        double phase = 2.0 * M_PI * frequency * index * ratio / AUDIO_RATE;
        double expected = 0.8 * sin(phase);
        double error = output[index] - expected;
        signal_energy += expected * expected;
        error_energy += error * error;
        in_phase += output[index] * sin(phase);
        quadrature += output[index] * cos(phase);
        assert(fabs(output[index]) <= 0.8f + 1e-6f);
    }
    double relative_error = sqrt(error_energy / signal_energy);
    printf("fidelity ratio %.3f %.0fHz: RMS error %.5f, phase %.5frad\n",
           ratio, frequency, relative_error, atan2(quadrature, in_phase));
    /* At 3.4 kHz the interpolation's <=2.5% worst-case amplitude loss is
     * below 0.25dB; interpolation introduces no incorrect clock-frequency. */
    assert(relative_error < 0.025);
    fixture_destroy(&buffer);
}

static void retirement_bursts(unsigned requested_rf_rate, double actual_rf_rate,
                              bool capture_poll_jitter)
{
    tx_audio_buffer_t buffer;
    fixture_init(&buffer);
    buffer.primed = false;
    /* The first producer read waits for its reserve plus real 10 ms output.
     * Driver/application cue reservoirs cover this startup prime. */
    append_zeros(&buffer, AUDIO_TARGET + CAPTURE_BLOCK);
    virtual_clock = true;
    virtual_ns = 1000000000ULL;
    uint64_t retired_iq = 0, produced_iq = 0, arrived_audio = 0;
    uint64_t mean_start_consumed = 0, mean_start_output = 0;
    float output[CAPTURE_BLOCK];
    unsigned maximum_burst = 0;
    for (unsigned callback = 1;; ++callback) {
        double elapsed = callback * 32768.0 / actual_rf_rate;
        virtual_ns = 1000000000ULL + (uint64_t)(elapsed * 1e9);
        for (;;) {
            uint64_t due = 1000000000ULL
                + (arrived_audio + CAPTURE_BLOCK) * 1000000000ULL / AUDIO_RATE;
            if (capture_poll_jitter)
                due += ((arrived_audio / CAPTURE_BLOCK) % 5) * 400000ULL;
            if (due > virtual_ns) break;
            append_zeros(&buffer, CAPTURE_BLOCK);
            arrived_audio += CAPTURE_BLOCK;
            buffer.last_capture_ns = due;
            buffer.last_capture_frames = CAPTURE_BLOCK;
        }
        retired_iq += 32768;
        unsigned burst = 0;
        while (produced_iq + requested_rf_rate / 100 <= retired_iq) {
            int result = tx_audio_buffer_read(&buffer, output, CAPTURE_BLOCK, 0);
            if (result)
                fprintf(stderr, "retirement %u actual%g t%.3f burst%u rc%d queue%zu\n",
                        requested_rf_rate, actual_rf_rate, elapsed, burst, result, buffer.count);
            assert(!result);
            produced_iq += requested_rf_rate / 100;
            ++burst;
        }
        if (burst > maximum_burst) maximum_burst = burst;
        if (elapsed < 540.0) {
            mean_start_consumed = buffer.stats.consumed_frames;
            mean_start_output = buffer.stats.output_frames;
        }
        if (elapsed >= 600.0) break;
    }
    virtual_clock = false;
    tx_audio_buffer_stats_t stats;
    tx_audio_buffer_get_stats(&buffer, &stats);
    double desired = requested_rf_rate / actual_rf_rate;
    double final_mean = (double)(stats.consumed_frames - mean_start_consumed)
        / (stats.output_frames - mean_start_output);
    printf("retirement bursts %u/%g IQ/s polljitter%d max%u: mean correction%.1fppm "
           "instant%.1fppm queue%zu max%zu\n", requested_rf_rate, actual_rf_rate,
           capture_poll_jitter, maximum_burst, (final_mean - 1.0) * 1e6, stats.correction_ppm,
           stats.queued_frames, stats.maximum_frames);
    assert(stats.clock_valid && !stats.underflows && !stats.overflows);
    assert(!stats.clock_limit_faults);
    assert(fabs(final_mean - desired) < 0.00005);
    assert(stats.queued_frames > AUDIO_TARGET - 1500
           && stats.queued_frames < AUDIO_TARGET + 1500);
    assert(stats.maximum_frames < AUDIO_CAPACITY);
    assert(maximum_burst == (requested_rf_rate == 1000000 ? 4u : 1u));
    fixture_destroy(&buffer);
}

static void retirement_step(double final_rf_rate)
{
    tx_audio_buffer_t buffer;
    fixture_init(&buffer);
    buffer.primed = false;
    append_zeros(&buffer, AUDIO_TARGET + CAPTURE_BLOCK);
    virtual_clock = true;
    virtual_ns = 1000000000ULL;
    uint64_t retired_iq = 0, produced_iq = 0, arrived_audio = 0;
    uint64_t mean_start_consumed = 0, mean_start_output = 0;
    size_t minimum_audio = AUDIO_CAPACITY;
    unsigned maximum_burst = 0;
    double elapsed = 0.0;
    float output[CAPTURE_BLOCK];
    while (elapsed < 600.0) {
        double actual_rf_rate = elapsed < 60.0 ? 2006000.0 : final_rf_rate;
        elapsed += 32768.0 / actual_rf_rate;
        virtual_ns = 1000000000ULL + (uint64_t)(elapsed * 1e9);
        for (;;) {
            uint64_t due = 1000000000ULL
                + (arrived_audio + CAPTURE_BLOCK) * 1000000000ULL / AUDIO_RATE
                + ((arrived_audio / CAPTURE_BLOCK) % 5) * 400000ULL;
            if (due > virtual_ns) break;
            append_zeros(&buffer, CAPTURE_BLOCK);
            arrived_audio += CAPTURE_BLOCK;
            buffer.last_capture_ns = due;
            buffer.last_capture_frames = CAPTURE_BLOCK;
        }
        retired_iq += 32768;
        unsigned burst = 0;
        while (produced_iq + 20000 <= retired_iq) {
            int result = tx_audio_buffer_read(&buffer, output, CAPTURE_BLOCK, 0);
            if (result)
                fprintf(stderr, "2Mstep to%g t%.3f burst%u rc%d queue%zu "
                        "ratio%g filtered%g limitframes%llu\n",
                        final_rf_rate, elapsed, burst, result, buffer.count,
                        buffer.ratio, buffer.filtered,
                        (unsigned long long)buffer.stats.clock_limit_frames);
            assert(!result);
            if (buffer.count < minimum_audio) minimum_audio = buffer.count;
            produced_iq += 20000;
            ++burst;
        }
        if (burst > maximum_burst) maximum_burst = burst;
        if (elapsed < 540.0) {
            mean_start_consumed = buffer.stats.consumed_frames;
            mean_start_output = buffer.stats.output_frames;
        }
    }
    virtual_clock = false;
    tx_audio_buffer_stats_t stats;
    tx_audio_buffer_get_stats(&buffer, &stats);
    double desired = 2000000.0 / final_rf_rate;
    double mean = (double)(stats.consumed_frames - mean_start_consumed)
        / (stats.output_frames - mean_start_output);
    printf("2M retirementstep at60s 2006000->%g IQ/s: minqueue%zu max%zu "
           "maxburst%u guardframes%llu finalmean%.1fppm queue%zu\n",
           final_rf_rate, minimum_audio, stats.maximum_frames, maximum_burst,
           (unsigned long long)stats.clock_limit_frames,
           (mean - 1.0) * 1e6, stats.queued_frames);
    assert(stats.clock_valid && !stats.underflows && !stats.overflows);
    assert(!stats.clock_limit_faults && maximum_burst == 2);
    assert(fabs(mean - desired) < 0.00005);
    assert(minimum_audio > 0 && stats.maximum_frames < AUDIO_CAPACITY);
    fixture_destroy(&buffer);
}

static void explicit_faults_and_cues(void)
{
    tx_audio_buffer_t buffer;
    float output[CAPTURE_BLOCK];
    fixture_init(&buffer);
    append_zeros(&buffer, AUDIO_CAPACITY);
    append_zeros(&buffer, CAPTURE_BLOCK);
    assert(buffer.stats.last_error == -EOVERFLOW && buffer.stats.overflows == 1);
    assert(buffer.stats.overflow_frames == CAPTURE_BLOCK && buffer.count == AUDIO_CAPACITY);
    assert(tx_audio_buffer_read(&buffer, output, CAPTURE_BLOCK, 0) == -EOVERFLOW);
    fixture_destroy(&buffer);

    fixture_init(&buffer);
    assert(tx_audio_buffer_read(&buffer, output, CAPTURE_BLOCK, 10) == -ETIMEDOUT);
    assert(buffer.stats.underflows == 1 && buffer.stats.last_error == -ETIMEDOUT);
    fixture_destroy(&buffer);

    fixture_init(&buffer);
    append_zeros(&buffer, 3000);
    tx_audio_buffer_set_cue(&buffer, true);
    append_zeros(&buffer, AUDIO_RATE); /* one second of deliberately bypassed mic */
    assert(buffer.count == AUDIO_TARGET);
    assert(buffer.stats.cue_discard_frames == 3000 + AUDIO_RATE - AUDIO_TARGET);
    assert(!buffer.stats.overflows && !buffer.stats.last_error);
    tx_audio_buffer_set_cue(&buffer, false);
    append_zeros(&buffer, CAPTURE_BLOCK * 2);
    assert(!tx_audio_buffer_read(&buffer, output, CAPTURE_BLOCK, 0));
    fixture_destroy(&buffer);

    fixture_init(&buffer);
    append_zeros(&buffer, AUDIO_TARGET);
    /* Force a controller whose observed clock remains beyond its guard. Keep
     * the test queue stable so the persistent guard, not overflow, fires. */
    buffer.integral = 0.05;
    for (int index = 0; index < 500; ++index) {
        update_clock_locked(&buffer, CAPTURE_BLOCK);
    }
    assert(buffer.stats.last_error == -ERANGE);
    assert(buffer.stats.clock_limit_faults == 1);
    assert(buffer.stats.clock_limit_frames == CLOCK_LIMIT_FAULT_FRAMES);
    fixture_destroy(&buffer);
}

static void excessive_clock_fault(void)
{
    tx_audio_buffer_t buffer;
    fixture_init(&buffer);
    append_zeros(&buffer, AUDIO_TARGET);
    buffer.ratio = 1.04;
    buffer.integral = 0.04;
    virtual_clock = true;
    virtual_ns = 1000000000ULL;
    double available = 0;
    uint64_t arrived = 0;
    float output[CAPTURE_BLOCK];
    int result = 0;
    for (unsigned index = 0; index < 550 && !result; ++index) {
        available += CAPTURE_BLOCK * 1.05; /* real +5% source/output mismatch */
        virtual_ns = 1000000000ULL + (uint64_t)(available * 1e9 / AUDIO_RATE);
        while ((double)(arrived + CAPTURE_BLOCK) <= available) {
            append_zeros(&buffer, CAPTURE_BLOCK);
            arrived += CAPTURE_BLOCK;
            buffer.last_capture_ns = 1000000000ULL
                + arrived * 1000000000ULL / AUDIO_RATE;
            buffer.last_capture_frames = CAPTURE_BLOCK;
        }
        result = tx_audio_buffer_read(&buffer, output, CAPTURE_BLOCK, 0);
    }
    virtual_clock = false;
    assert(result == -ERANGE && buffer.stats.clock_limit_faults == 1);
    assert(buffer.stats.clock_limit_frames == CLOCK_LIMIT_FAULT_FRAMES);
    assert(!buffer.stats.underflows && !buffer.stats.overflows);
    assert(buffer.count < AUDIO_CAPACITY && fabs(buffer.ratio - 1.04) < 1e-10);
    printf("real +5%% clock mismatch => explicit4%% guard fault after%lluoutputframes "
           "withqueue%zu, no capture loss\n",
           (unsigned long long)buffer.stats.clock_limit_frames, buffer.count);
    fixture_destroy(&buffer);
}

typedef struct {
    audio_source_t base;
    atomic_bool stalled, fail;
    atomic_uint reads;
    uint64_t next_ns;
} scripted_source_t;

static uint64_t test_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static audio_source_result_t scripted_read(audio_source_t *source, float *dst, size_t n)
{
    scripted_source_t *script = (scripted_source_t *)source;
    atomic_fetch_add(&script->reads, 1);
    if (atomic_load(&script->fail))
        return (audio_source_result_t){0, AUDIO_SOURCE_ERROR, -EIO};
    uint64_t now = test_ns();
    if (atomic_load(&script->stalled) || now < script->next_ns)
        return (audio_source_result_t){0, AUDIO_SOURCE_AGAIN, 0};
    script->next_ns = now + 2000000; /* nonblocking 96 frames per 2 ms */
    if (n > 96) n = 96;
    for (size_t index = 0; index < n; ++index) dst[index] = 0.25f;
    return (audio_source_result_t){n, AUDIO_SOURCE_OK, 0};
}

static void no_destroy(audio_source_t *source) { (void)source; }
static const audio_source_ops_t scripted_ops = {scripted_read, no_destroy};

typedef struct { tx_audio_buffer_t *buffer; int result; } reader_t;
static void *waiting_reader(void *argument)
{
    reader_t *reader = argument;
    float output[CAPTURE_BLOCK];
    reader->result = tx_audio_buffer_read(reader->buffer, output, CAPTURE_BLOCK, -1);
    return NULL;
}

static void threaded_lifecycle(void)
{
    scripted_source_t source = { .base = {{AUDIO_RATE, 1}, &scripted_ops} };
    tx_audio_buffer_t *buffer = tx_audio_buffer_create(&source.base);
    assert(buffer);
    assert(!tx_audio_buffer_resume(buffer));
    tx_audio_buffer_set_cue(buffer, true);
    usleep(150000);
    tx_audio_buffer_stats_t stats;
    tx_audio_buffer_get_stats(buffer, &stats);
    assert(stats.alive && stats.running && stats.cue);
    assert(stats.queued_frames == AUDIO_TARGET && stats.cue_discard_frames > 0);
    assert(stats.scheduling.valid && stats.scheduling.policy == SCHED_OTHER);
    tx_audio_buffer_set_cue(buffer, false);
    float output[CAPTURE_BLOCK];
    assert(!tx_audio_buffer_read(buffer, output, CAPTURE_BLOCK, 200));
    for (size_t index = 0; index < CAPTURE_BLOCK; ++index) assert(output[index] == 0.25f);
    tx_audio_buffer_park(buffer);
    unsigned parked_reads = atomic_load(&source.reads);
    usleep(10000);
    assert(atomic_load(&source.reads) == parked_reads);

    atomic_store(&source.stalled, true);
    assert(!tx_audio_buffer_resume(buffer));
    reader_t reader = { .buffer = buffer };
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, waiting_reader, &reader));
    usleep(10000);
    uint64_t start = test_ns();
    tx_audio_buffer_request_abort(buffer);
    assert(!pthread_join(thread, NULL));
    assert(reader.result == -ECANCELED && test_ns() - start < 100000000);
    tx_audio_buffer_park(buffer);
    /* Lifecycle timeout fallback may cancel the producer in cond_wait. Its
     * POSIX cancellation cleanup must release the reacquired audio mutex. */
    assert(!tx_audio_buffer_resume(buffer));
    assert(!pthread_create(&thread, NULL, waiting_reader, &reader));
    usleep(10000);
    assert(!pthread_cancel(thread));
    void *cancelled;
    assert(!pthread_join(thread, &cancelled) && cancelled == PTHREAD_CANCELED);
    tx_audio_buffer_park(buffer);
    atomic_store(&source.stalled, false);
    assert(!tx_audio_buffer_resume(buffer));
    assert(!tx_audio_buffer_read(buffer, output, CAPTURE_BLOCK, 200));
    tx_audio_buffer_park(buffer);
    assert(!tx_audio_buffer_resume(buffer));
    atomic_store(&source.fail, true);
    assert(tx_audio_buffer_read(buffer, output, CAPTURE_BLOCK, 200) == -EIO);
    tx_audio_buffer_get_stats(buffer, &stats);
    assert(!stats.clock_valid && stats.last_error == -EIO);
    tx_audio_buffer_destroy(buffer);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    clock_simulation(1.004, false);
    clock_simulation(0.987, false);
    clock_simulation(1.004, true);
    clock_simulation(0.987, true);
    clock_simulation(1.0, true);
    clock_simulation(0.967, true);
    clock_simulation(1.033, true);
    retirement_bursts(1000000, 995900.0, false);
    retirement_bursts(4000000, 4050000.0, false);
    retirement_bursts(1000000, 995900.0, true);
    retirement_bursts(4000000, 4050000.0, true);
    retirement_step(2060000.0);
    retirement_step(2066000.0);
    fidelity(0.987, 600.0);
    fidelity(1.004, 600.0);
    fidelity(0.987, 3400.0);
    fidelity(1.004, 3400.0);
    fidelity(0.967, 3400.0);
    fidelity(1.033, 3400.0);
    explicit_faults_and_cues();
    excessive_clock_fault();
    threaded_lifecycle();
    puts("PASS: bounded ASRC clock tracking/jitter, audio fidelity, explicit faults, "
         "cue capture, one-reader parking, interruptible abort and restart");
}
