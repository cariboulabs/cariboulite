#include "alsa_sink.h"
#include <alsa/asoundlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdatomic.h>

typedef struct {
    audio_sink_t base;
    snd_pcm_t* pcm;
    unsigned channels;
    atomic_uint_fast64_t write_calls, frames_requested, frames_written;
    atomic_uint_fast64_t partial_writes, would_block, zero_writes, underruns;
    atomic_uint_fast64_t recovery_attempts, recovery_successes, recovery_failures;
    atomic_uint_fast64_t write_errors;
    atomic_int last_error;
} alsa_sink_t;

static void init_stats(alsa_sink_t* s)
{
#define INIT(field) atomic_init(&s->field, 0)
    INIT(write_calls); INIT(frames_requested); INIT(frames_written);
    INIT(partial_writes); INIT(would_block); INIT(zero_writes); INIT(underruns);
    INIT(recovery_attempts); INIT(recovery_successes); INIT(recovery_failures);
    INIT(write_errors); INIT(last_error);
#undef INIT
}

static void count_error(alsa_sink_t* s, int error)
{
    atomic_fetch_add_explicit(&s->write_errors, 1, memory_order_relaxed);
    atomic_store_explicit(&s->last_error, error, memory_order_relaxed);
}

static audio_sink_result_t sink_write(audio_sink_t* base, const int16_t* mono, size_t frames)
{
    alsa_sink_t* s = (alsa_sink_t*)base;
    int16_t stereo[480 * 2];
    const int16_t* samples = mono;
    // Bound stack conversion; caller retries the remaining frames.
    if (s->channels == 2) {
        if (frames > 480) frames = 480;
        for (size_t i = 0; i < frames; ++i) stereo[2*i] = stereo[2*i+1] = mono[i];
        samples = stereo;
    }
    atomic_fetch_add_explicit(&s->write_calls, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&s->frames_requested, frames, memory_order_relaxed);
    snd_pcm_sframes_t n = snd_pcm_writei(s->pcm, samples, frames);
    if (n == -EPIPE) {
        atomic_fetch_add_explicit(&s->underruns, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&s->recovery_attempts, 1, memory_order_relaxed);
        int rc = snd_pcm_prepare(s->pcm);
        if (rc < 0) {
            atomic_fetch_add_explicit(&s->recovery_failures, 1, memory_order_relaxed);
            count_error(s, rc);
        } else {
            atomic_fetch_add_explicit(&s->recovery_successes, 1, memory_order_relaxed);
        }
        return (audio_sink_result_t){0, rc < 0 ? AUDIO_SINK_ERROR : AUDIO_SINK_AGAIN, rc < 0 ? rc : 0};
    }
    if (n == -EAGAIN || n == 0) {
        atomic_fetch_add_explicit(n == -EAGAIN ? &s->would_block : &s->zero_writes,
                                  1, memory_order_relaxed);
        return (audio_sink_result_t){0, AUDIO_SINK_AGAIN, 0};
    }
    if (n < 0) {
        count_error(s, (int)n);
        return (audio_sink_result_t){0, AUDIO_SINK_ERROR, (int)n};
    }
    atomic_fetch_add_explicit(&s->frames_written, (uint64_t)n, memory_order_relaxed);
    if ((size_t)n < frames)
        atomic_fetch_add_explicit(&s->partial_writes, 1, memory_order_relaxed);
    return (audio_sink_result_t){(size_t)n, AUDIO_SINK_OK, 0};
}
static void sink_destroy(audio_sink_t* base)
{
    alsa_sink_t* s = (alsa_sink_t*)base;
    snd_pcm_close(s->pcm);
    free(s);
}
static const char* sink_state(audio_sink_t* base)
{
    return snd_pcm_state_name(snd_pcm_state(((alsa_sink_t*)base)->pcm));
}
static bool sink_get_stats(audio_sink_t* base, audio_sink_stats_t* out)
{
    alsa_sink_t* s = (alsa_sink_t*)base;
#define SNAPSHOT(field) out->field = atomic_load_explicit(&s->field, memory_order_relaxed)
    SNAPSHOT(write_calls); SNAPSHOT(frames_requested); SNAPSHOT(frames_written);
    SNAPSHOT(partial_writes); SNAPSHOT(would_block); SNAPSHOT(zero_writes);
    SNAPSHOT(underruns); SNAPSHOT(recovery_attempts); SNAPSHOT(recovery_successes);
    SNAPSHOT(recovery_failures); SNAPSHOT(write_errors); SNAPSHOT(last_error);
#undef SNAPSHOT
    return true;
}
static const audio_sink_ops_t ops = {
    .write=sink_write, .destroy=sink_destroy, .state=sink_state,
    .get_stats=sink_get_stats
};
unsigned alsa_sink_channels(const audio_sink_t* sink)
{
    return sink ? ((const alsa_sink_t*)sink)->channels : 0;
}

audio_sink_t* alsa_sink_open(const char* device, unsigned sample_rate)
{
    if (sample_rate != 48000) { errno = EINVAL; return NULL; }
    alsa_sink_t* s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    init_stats(s);
    snd_pcm_hw_params_t* hw = NULL;
    snd_pcm_sw_params_t* sw = NULL;
    const char* dev = device && *device ? device : "default";
    int rc = snd_pcm_open(&s->pcm, dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) goto fail;
#define CHECK(call) do { rc = (call); if (rc < 0) goto fail; } while (0)
    CHECK(snd_pcm_hw_params_malloc(&hw));
    CHECK(snd_pcm_hw_params_any(s->pcm, hw));
    CHECK(snd_pcm_hw_params_set_access(s->pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED));
    CHECK(snd_pcm_hw_params_set_format(s->pcm, hw, SND_PCM_FORMAT_S16_LE));
    unsigned rate = sample_rate;
    int dir = 0;
    CHECK(snd_pcm_hw_params_set_rate_near(s->pcm, hw, &rate, &dir));
    if (rate != sample_rate) { rc = -EINVAL; goto fail; }
    s->channels = 1;
    if (snd_pcm_hw_params_set_channels(s->pcm, hw, 1) < 0) {
        s->channels = 2;
        CHECK(snd_pcm_hw_params_set_channels(s->pcm, hw, 2));
    }
    snd_pcm_uframes_t period = 480, buffer = 2400;
    CHECK(snd_pcm_hw_params_set_period_size_near(s->pcm, hw, &period, NULL));
    CHECK(snd_pcm_hw_params_set_buffer_size_near(s->pcm, hw, &buffer));
    CHECK(snd_pcm_hw_params(s->pcm, hw));
    CHECK(snd_pcm_prepare(s->pcm));
    CHECK(snd_pcm_sw_params_malloc(&sw));
    CHECK(snd_pcm_sw_params_current(s->pcm, sw));
    CHECK(snd_pcm_get_params(s->pcm, &buffer, &period));
    CHECK(snd_pcm_sw_params_set_start_threshold(s->pcm, sw, buffer - period));
    CHECK(snd_pcm_sw_params_set_avail_min(s->pcm, sw, period));
    CHECK(snd_pcm_sw_params(s->pcm, sw));
    snd_pcm_sw_params_free(sw);
    snd_pcm_hw_params_free(hw);
    s->base = (audio_sink_t){rate, &ops};
    fprintf(stderr, "ALSA: opened %s, %uch, S16_LE, %u Hz\n", dev, s->channels, rate);
    return &s->base;
fail:
    fprintf(stderr, "ALSA: playback(%s): %s\n", dev, snd_strerror(rc));
    if (sw) snd_pcm_sw_params_free(sw);
    if (hw) snd_pcm_hw_params_free(hw);
    if (s->pcm) snd_pcm_close(s->pcm);
    free(s);
    errno = -rc;
    return NULL;
#undef CHECK
}
