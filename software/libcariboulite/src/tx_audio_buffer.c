#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "tx_audio_buffer.h"
#include "alsa_source.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { AUDIO_RATE = 48000, CAPTURE_BLOCK = 480, AUDIO_CAPACITY = 8192,
       AUDIO_TARGET = 3360 };
/* 70 ms retains the 100 ppm/frame slew through the measured transport step.
 * 40 ms starves before a 3.3% startup mismatch can be corrected; the ideal
 * slew deficit alone is 54 ms, before capture quantization and DMA bursts. */
static const double CLOCK_LIMIT = 0.04;
static const uint64_t CLOCK_LIMIT_FAULT_FRAMES = 5 * AUDIO_RATE;

struct tx_audio_buffer {
    audio_source_t *source;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    bool created, alive, enabled, busy, terminate, aborted, cue, primed;
    float samples[AUDIO_CAPACITY];
    size_t head, count;
    double phase, ratio, filtered, integral;
    uint64_t last_capture_ns;
    size_t last_capture_frames;
    uint64_t consecutive_limit_frames;
    tx_audio_buffer_stats_t stats;
};

static struct timespec deadline_after_ms(int milliseconds)
{
    struct timespec result;
    clock_gettime(CLOCK_MONOTONIC, &result);
    result.tv_sec += milliseconds / 1000;
    result.tv_nsec += (long)(milliseconds % 1000) * 1000000L;
    if (result.tv_nsec >= 1000000000L) {
        ++result.tv_sec;
        result.tv_nsec -= 1000000000L;
    }
    return result;
}

static uint64_t current_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

static void audio_unlock(void *mutex)
{
    pthread_mutex_unlock(mutex);
}

static void observe_occupancy(tx_audio_buffer_t *buffer)
{
    if (buffer->count < buffer->stats.minimum_frames)
        buffer->stats.minimum_frames = buffer->count;
    if (buffer->count > buffer->stats.maximum_frames)
        buffer->stats.maximum_frames = buffer->count;
}

static void clear_audio(tx_audio_buffer_t *buffer)
{
    buffer->head = buffer->count = 0;
    buffer->phase = 0.0;
    buffer->ratio = 1.0;
    buffer->filtered = AUDIO_TARGET;
    buffer->integral = 0.0;
    buffer->consecutive_limit_frames = 0;
    buffer->last_capture_ns = 0;
    buffer->last_capture_frames = 0;
    buffer->primed = false;
    observe_occupancy(buffer);
}

static void fail_locked(tx_audio_buffer_t *buffer, int error)
{
    buffer->stats.last_error = error < 0 ? error : -EIO;
    buffer->enabled = false;
    pthread_cond_broadcast(&buffer->changed);
}

/* Normal operation never overwrites old samples. Cue discard is explicit and
 * separate from overflow, because cue audio intentionally replaces the mic. */
static void append_locked(tx_audio_buffer_t *buffer, const float *samples,
                          size_t frames)
{
    if (buffer->cue) {
        size_t excess = buffer->count + frames > AUDIO_TARGET
                      ? buffer->count + frames - AUDIO_TARGET : 0;
        size_t old = excess < buffer->count ? excess : buffer->count;
        buffer->head = (buffer->head + old) % AUDIO_CAPACITY;
        buffer->count -= old;
        buffer->stats.cue_discard_frames += old;
        excess -= old;
        samples += excess;
        frames -= excess;
        buffer->stats.cue_discard_frames += excess;
    }
    if (frames > AUDIO_CAPACITY - buffer->count) {
        ++buffer->stats.overflows;
        buffer->stats.overflow_frames += frames;
        fail_locked(buffer, -EOVERFLOW);
        return;
    }
    size_t tail = (buffer->head + buffer->count) % AUDIO_CAPACITY;
    size_t first = frames < AUDIO_CAPACITY - tail ? frames : AUDIO_CAPACITY - tail;
    memcpy(buffer->samples + tail, samples, first * sizeof(float));
    memcpy(buffer->samples, samples + first, (frames - first) * sizeof(float));
    buffer->count += frames;
    observe_occupancy(buffer);
    pthread_cond_broadcast(&buffer->changed);
}

static void capture_scheduling(tx_audio_buffer_t *buffer)
{
    pipeline_thread_status_t status = { .valid = true, .requested_priority = 0,
        .requested_cpu = 0, .applied_cpu = -1 };
    struct sched_param parameter = { .sched_priority = 0 };
    status.fallback_error = pthread_setschedparam(pthread_self(), SCHED_OTHER,
                                                 &parameter);
    cpu_set_t allowed, chosen;
    CPU_ZERO(&allowed);
    int error = pthread_getaffinity_np(pthread_self(), sizeof(allowed), &allowed);
    if (error) {
        status.affinity_error = error;
    } else {
        int cpu = CPU_ISSET(0, &allowed) ? 0 : -1;
        if (cpu < 0) {
            for (int index = 0; index < CPU_SETSIZE; ++index) {
                if (CPU_ISSET(index, &allowed)) { cpu = index; break; }
            }
        }
        CPU_ZERO(&chosen);
        if (cpu >= 0) {
            CPU_SET(cpu, &chosen);
            status.affinity_error = pthread_setaffinity_np(pthread_self(),
                                                           sizeof(chosen), &chosen);
            if (!status.affinity_error) status.applied_cpu = cpu;
        } else {
            status.affinity_error = EINVAL;
        }
    }
    error = pthread_getschedparam(pthread_self(), &status.policy, &parameter);
    if (error) status.query_error = error;
    else status.priority = parameter.sched_priority;
    if (status.applied_cpu >= 0)
        snprintf(status.affinity, sizeof(status.affinity), "%d", status.applied_cpu);
    buffer->stats.scheduling = status;
}

static void *capture_worker(void *argument)
{
    tx_audio_buffer_t *buffer = argument;
    pthread_setname_np(pthread_self(), "tx_capture");
    pthread_mutex_lock(&buffer->mutex);
    capture_scheduling(buffer);
    buffer->alive = true;
    pthread_cond_broadcast(&buffer->changed);
    while (!buffer->terminate) {
        while (!buffer->enabled && !buffer->terminate)
            pthread_cond_wait(&buffer->changed, &buffer->mutex);
        if (buffer->terminate) break;
        buffer->busy = true;
        pthread_mutex_unlock(&buffer->mutex);
        float samples[CAPTURE_BLOCK];
        audio_source_result_t result = audio_source_read(buffer->source, samples,
                                                        CAPTURE_BLOCK);
        pthread_mutex_lock(&buffer->mutex);
        buffer->busy = false;
        ++buffer->stats.capture_calls;
        buffer->stats.captured_frames += result.frames;
        pthread_cond_broadcast(&buffer->changed);
        if (!buffer->enabled) continue;
        if (result.frames > CAPTURE_BLOCK) {
            fail_locked(buffer, -EOVERFLOW);
            continue;
        }
        if (result.status == AUDIO_SOURCE_ERROR || result.status == AUDIO_SOURCE_EOF) {
            fail_locked(buffer, result.error ? result.error : -EIO);
            continue;
        }
        if (result.frames) {
            buffer->last_capture_ns = current_ns();
            buffer->last_capture_frames = result.frames;
            append_locked(buffer, samples, result.frames);
            continue;
        }
        ++buffer->stats.again_calls;
        /* ALSA's common source API currently has no poll-fd interface. A timed
         * condition wait provides a bounded 2 ms fallback and immediate abort.
         * Producer signals may wake this early; the source remains nonblocking. */
        struct timespec wake = deadline_after_ms(2);
        pthread_cond_timedwait(&buffer->changed, &buffer->mutex, &wake);
    }
    buffer->alive = false;
    pthread_cond_broadcast(&buffer->changed);
    pthread_mutex_unlock(&buffer->mutex);
    return NULL;
}

tx_audio_buffer_t *tx_audio_buffer_create(audio_source_t *source)
{
    if (!source || source->format.sample_rate != AUDIO_RATE || source->format.channels != 1) {
        errno = EINVAL;
        return NULL;
    }
    tx_audio_buffer_t *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) return NULL;
    buffer->source = source;
    buffer->stats.minimum_frames = AUDIO_CAPACITY;
    clear_audio(buffer);
    int error = pthread_mutex_init(&buffer->mutex, NULL);
    if (error) { free(buffer); errno = error; return NULL; }
    pthread_condattr_t attributes;
    error = pthread_condattr_init(&attributes);
    if (error) goto mutex_failure;
    error = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    if (!error) error = pthread_cond_init(&buffer->changed, &attributes);
    pthread_condattr_destroy(&attributes);
    if (error) goto mutex_failure;
    error = pthread_create(&buffer->thread, NULL, capture_worker, buffer);
    if (error) {
        pthread_cond_destroy(&buffer->changed);
        goto mutex_failure;
    }
    buffer->created = true;
    pthread_mutex_lock(&buffer->mutex);
    while (!buffer->alive) pthread_cond_wait(&buffer->changed, &buffer->mutex);
    pthread_mutex_unlock(&buffer->mutex);
    return buffer;
mutex_failure:
    pthread_mutex_destroy(&buffer->mutex);
    free(buffer);
    errno = error;
    return NULL;
}

void tx_audio_buffer_park(tx_audio_buffer_t *buffer)
{
    if (!buffer) return;
    pthread_mutex_lock(&buffer->mutex);
    buffer->enabled = false;
    pthread_cond_broadcast(&buffer->changed);
    while (buffer->busy) pthread_cond_wait(&buffer->changed, &buffer->mutex);
    pthread_mutex_unlock(&buffer->mutex);
}

int tx_audio_buffer_resume(tx_audio_buffer_t *buffer)
{
    if (!buffer) return -EINVAL;
    tx_audio_buffer_park(buffer);
    /* Capture's one-reader acknowledgement above makes this handle operation
     * safe. Non-ALSA fixtures may provide the same nonblocking read contract. */
    int error = alsa_source_reset(buffer->source, true);
    if (error == -ENOTSUP) error = 0;
    pthread_mutex_lock(&buffer->mutex);
    clear_audio(buffer);
    ++buffer->stats.resets;
    buffer->stats.last_error = error;
    buffer->aborted = false;
    buffer->enabled = !error;
    pthread_cond_broadcast(&buffer->changed);
    pthread_mutex_unlock(&buffer->mutex);
    return error;
}

void tx_audio_buffer_request_abort(tx_audio_buffer_t *buffer)
{
    if (!buffer) return;
    pthread_mutex_lock(&buffer->mutex);
    buffer->aborted = true;
    buffer->enabled = false;
    pthread_cond_broadcast(&buffer->changed);
    pthread_mutex_unlock(&buffer->mutex);
}

void tx_audio_buffer_destroy(tx_audio_buffer_t *buffer)
{
    if (!buffer) return;
    pthread_mutex_lock(&buffer->mutex);
    buffer->terminate = true;
    buffer->enabled = false;
    pthread_cond_broadcast(&buffer->changed);
    pthread_mutex_unlock(&buffer->mutex);
    if (buffer->created) pthread_join(buffer->thread, NULL);
    pthread_cond_destroy(&buffer->changed);
    pthread_mutex_destroy(&buffer->mutex);
    free(buffer);
}

void tx_audio_buffer_set_cue(tx_audio_buffer_t *buffer, bool cue)
{
    if (!buffer) return;
    pthread_mutex_lock(&buffer->mutex);
    if (buffer->cue != cue) {
        buffer->cue = cue;
        buffer->primed = false;
        buffer->phase = 0.0;
        buffer->filtered = AUDIO_TARGET;
        buffer->integral = 0.0;
        buffer->ratio = 1.0;
        buffer->consecutive_limit_frames = 0;
        if (cue && buffer->count > AUDIO_TARGET) {
            size_t excess = buffer->count - AUDIO_TARGET;
            buffer->head = (buffer->head + excess) % AUDIO_CAPACITY;
            buffer->count -= excess;
            buffer->stats.cue_discard_frames += excess;
            observe_occupancy(buffer);
        }
        pthread_cond_broadcast(&buffer->changed);
    }
    pthread_mutex_unlock(&buffer->mutex);
}

static void update_clock_locked(tx_audio_buffer_t *buffer, size_t frames)
{
    double interval = (double)frames / AUDIO_RATE;
    /* ALSA delivers whole capture periods. Include the fraction of a period
     * already accumulated upstream, so the servo does not track the resulting
     * staircase as an audio-clock change. This bounded estimate is never used
     * to satisfy a read: only captured real samples enter the output. */
    uint64_t now = current_ns();
    double pending = buffer->last_capture_ns && now >= buffer->last_capture_ns
                   ? (double)(now - buffer->last_capture_ns) * AUDIO_RATE / 1e9 : 0.0;
    if (pending > buffer->last_capture_frames) pending = buffer->last_capture_frames;
    buffer->filtered += interval / (0.5 + interval)
                      * ((double)buffer->count + pending - buffer->filtered);
    double error_seconds = (buffer->filtered - AUDIO_TARGET) / AUDIO_RATE;
    double proposed_integral = buffer->integral + 0.2 * error_seconds * interval;
    /* The measured clock step needs reserve while slew catches up. Moderate
     * proportional gain avoids demanding the guard limit for >5 seconds just
     * to rebuild that reserve after an otherwise admissible 3.3% mismatch. */
    double proposed = 0.75 * error_seconds + proposed_integral;
    /* Anti-windup: do not integrate away a real sustained rate fault. */
    if ((proposed < CLOCK_LIMIT && proposed > -CLOCK_LIMIT) ||
        (proposed >= CLOCK_LIMIT && error_seconds < 0.0) ||
        (proposed <= -CLOCK_LIMIT && error_seconds > 0.0))
        buffer->integral = proposed_integral;
    double correction = 0.75 * error_seconds + buffer->integral;
    bool limited = correction >= CLOCK_LIMIT || correction <= -CLOCK_LIMIT;
    if (correction > CLOCK_LIMIT) correction = CLOCK_LIMIT;
    if (correction < -CLOCK_LIMIT) correction = -CLOCK_LIMIT;
    double desired = 1.0 + correction;
    double maximum_change = 0.01 * interval; /* at most 100 ppm / 10 ms */
    double difference = desired - buffer->ratio;
    if (difference > maximum_change) difference = maximum_change;
    if (difference < -maximum_change) difference = -maximum_change;
    buffer->ratio += difference;
    if (limited) {
        buffer->stats.clock_limit_frames += frames;
        buffer->consecutive_limit_frames += frames;
        if (buffer->consecutive_limit_frames >= CLOCK_LIMIT_FAULT_FRAMES) {
            ++buffer->stats.clock_limit_faults;
            fail_locked(buffer, -ERANGE);
        }
    } else {
        buffer->consecutive_limit_frames = 0;
    }
}

int tx_audio_buffer_read(tx_audio_buffer_t *buffer, float *dst,
                         size_t frames, int timeout_ms)
{
    if (!buffer || (!dst && frames) || frames > CAPTURE_BLOCK) return -EINVAL;
    if (!frames) return 0;
    struct timespec deadline = {0};
    if (timeout_ms >= 0) deadline = deadline_after_ms(timeout_ms);
    volatile int result = 0;
    pthread_mutex_lock(&buffer->mutex);
    pthread_cleanup_push(audio_unlock, &buffer->mutex);
    ++buffer->stats.read_calls;
    for (;;) {
        if (!buffer->enabled || buffer->aborted || buffer->terminate) {
            result = buffer->stats.last_error ? buffer->stats.last_error : -ECANCELED;
            break;
        }
        if (buffer->cue) { result = -ECANCELED; break; }
        size_t needed = (size_t)ceil(buffer->phase + frames * buffer->ratio) + 1;
        size_t ready = needed + (buffer->primed ? 0 : AUDIO_TARGET);
        if (buffer->count >= ready) {
            buffer->primed = true;
            double position = buffer->phase;
            /* Linear interpolation at 48 kHz keeps the voice passband error
             * below 0.25 dB through 3.4 kHz, needs no large fractional-delay
             * filter, and never increases the capture's normalized amplitude. */
            for (size_t index = 0; index < frames; ++index) {
                size_t whole = (size_t)position;
                float fraction = (float)(position - whole);
                float left = buffer->samples[(buffer->head + whole) % AUDIO_CAPACITY];
                float right = buffer->samples[(buffer->head + whole + 1) % AUDIO_CAPACITY];
                dst[index] = left + fraction * (right - left);
                position += buffer->ratio;
            }
            size_t consumed = (size_t)position;
            buffer->phase = position - consumed;
            buffer->head = (buffer->head + consumed) % AUDIO_CAPACITY;
            buffer->count -= consumed;
            buffer->stats.consumed_frames += consumed;
            buffer->stats.output_frames += frames;
            observe_occupancy(buffer);
            update_clock_locked(buffer, frames);
            if (buffer->stats.last_error) result = buffer->stats.last_error;
            break;
        }
        if (timeout_ms == 0) { result = -EAGAIN; break; }
        int wait_result = timeout_ms < 0
            ? pthread_cond_wait(&buffer->changed, &buffer->mutex)
            : pthread_cond_timedwait(&buffer->changed, &buffer->mutex, &deadline);
        if (wait_result == ETIMEDOUT) {
            ++buffer->stats.underflows;
            fail_locked(buffer, -ETIMEDOUT);
            result = -ETIMEDOUT;
            break;
        }
        if (wait_result) { result = -wait_result; break; }
    }
    pthread_cleanup_pop(1);
    return result;
}

void tx_audio_buffer_get_stats(tx_audio_buffer_t *buffer,
                              tx_audio_buffer_stats_t *stats)
{
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    if (!buffer) return;
    pthread_mutex_lock(&buffer->mutex);
    *stats = buffer->stats;
    stats->created = buffer->created;
    stats->alive = buffer->alive;
    stats->running = buffer->enabled;
    stats->cue = buffer->cue;
    stats->primed = buffer->primed;
    stats->clock_valid = !buffer->stats.last_error;
    stats->queued_frames = buffer->count;
    stats->capacity_frames = AUDIO_CAPACITY;
    stats->target_frames = AUDIO_TARGET;
    stats->input_per_output = buffer->ratio;
    stats->correction_ppm = (buffer->ratio - 1.0) * 1e6;
    stats->occupancy_filtered_frames = buffer->filtered;
    pthread_mutex_unlock(&buffer->mutex);
}

void tx_audio_buffer_reset_stats(tx_audio_buffer_t *buffer)
{
    if (!buffer) return;
    pthread_mutex_lock(&buffer->mutex);
    pipeline_thread_status_t scheduling = buffer->stats.scheduling;
    int error = buffer->stats.last_error;
    memset(&buffer->stats, 0, sizeof(buffer->stats));
    buffer->stats.scheduling = scheduling;
    buffer->stats.last_error = error;
    buffer->stats.minimum_frames = buffer->stats.maximum_frames = buffer->count;
    pthread_mutex_unlock(&buffer->mutex);
}
