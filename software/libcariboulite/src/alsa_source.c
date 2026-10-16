#include "alsa_source.h"
#include <alsa/asoundlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdatomic.h>

#ifndef CLAMPF
#define CLAMPF(x,lo,hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif

struct alsa_source {
    audio_source_t base;
    int last_error;
    atomic_uint_fast64_t read_calls, frames_read, overruns;
    atomic_uint_fast64_t recovery_attempts, recovery_successes, recovery_failures;
    atomic_uint_fast64_t read_errors;
    atomic_int retained_error;
    snd_pcm_t*      pcm;
    float           gain;

    // capture format
    unsigned int    fs;       // 48000
    unsigned int    ch;       // 1
    snd_pcm_format_t fmt;     // SND_PCM_FORMAT_S16_LE

    // ALSA sizes
    snd_pcm_uframes_t period; // typically 480 (10 ms)
    snd_pcm_uframes_t bufsize;// e.g. period * 8

    // temp capture buffer (int16 from ALSA) and float ring
    int16_t*        cap_i16;  // period frames
    float*          ring;
    size_t          rcap;     // ring capacity in frames
    size_t          rhead;    // read index
    size_t          rtail;    // write index
    size_t          rcount;   // frames stored
};

static int alsa_recover_xrun(snd_pcm_t* pcm, int err)
{
    if (err == -EPIPE) { // overrun/underrun
        return snd_pcm_prepare(pcm);
    } else if (err == -ESTRPIPE) {
        // suspended, try to resume
        err = snd_pcm_resume(pcm);
        return err < 0 ? snd_pcm_prepare(pcm) : 0;
    }
    return err;
}

static int alsa_set_hw_params(snd_pcm_t* pcm,
                              unsigned int* fs_io,
                              unsigned int* ch_io,
                              snd_pcm_format_t fmt,
                              snd_pcm_uframes_t* period_io,
                              snd_pcm_uframes_t* bufsize_io)
{
    int rc;
    snd_pcm_hw_params_t* hw = NULL;
    snd_pcm_hw_params_malloc(&hw);
    snd_pcm_hw_params_any(pcm, hw);

    // Interleaved capture
    rc = snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (rc < 0) goto done;

    // Format
    rc = snd_pcm_hw_params_set_format(pcm, hw, fmt);
    if (rc < 0) goto done;

    // Channels
    unsigned int ch = *ch_io;
    rc = snd_pcm_hw_params_set_channels(pcm, hw, ch);
    if (rc < 0) goto done;

    // Rate (set near 48000)
    unsigned int fs = *fs_io, fs_set = 0;
    int dir = 0;
    rc = snd_pcm_hw_params_set_rate_near(pcm, hw, &fs, &dir);
    if (rc < 0) goto done;
    fs_set = fs;
    if (fs_set != *fs_io) { rc = -EINVAL; goto done; } // insist on 48k exact

    // Period size (try 480 frames => 10 ms)
    snd_pcm_uframes_t period = *period_io;
    dir = 0;
    rc = snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, &dir);
    if (rc < 0) goto done;

    // Buffer size (use 8 periods)
    snd_pcm_uframes_t bufsize = period * 8;
    rc = snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &bufsize);
    if (rc < 0) goto done;

    // Apply
    rc = snd_pcm_hw_params(pcm, hw);
    if (rc < 0) goto done;

    // Read back what we actually got
    snd_pcm_hw_params_get_rate(hw, &fs_set, 0);
    snd_pcm_hw_params_get_channels(hw, &ch);
    snd_pcm_hw_params_get_period_size(hw, &period, 0);
    snd_pcm_hw_params_get_buffer_size(hw, &bufsize);

    *fs_io     = fs_set;
    *ch_io     = ch;
    *period_io = period;
    *bufsize_io= bufsize;
    rc = 0;

done:
    if (hw) snd_pcm_hw_params_free(hw);
    return rc;
}

static int alsa_set_sw_params(snd_pcm_t* pcm, snd_pcm_uframes_t period)
{
    int rc;
    snd_pcm_sw_params_t* sw = NULL;
    snd_pcm_sw_params_malloc(&sw);
    snd_pcm_sw_params_current(pcm, sw);

    // Start when at least one period is available in the buffer
    rc = snd_pcm_sw_params_set_start_threshold(pcm, sw, period);
    if (rc < 0) goto done;

    // Wake up when at least one period is ready
    rc = snd_pcm_sw_params_set_avail_min(pcm, sw, period);
    if (rc < 0) goto done;

    rc = snd_pcm_sw_params(pcm, sw);
done:
    if (sw) snd_pcm_sw_params_free(sw);
    return rc;
}

alsa_source_t* alsa_source_create(const char* device, float gain)
{
    alsa_source_t* s = (alsa_source_t*)calloc(1, sizeof(*s));
    if (!s) return NULL;
    atomic_init(&s->read_calls, 0);
    atomic_init(&s->frames_read, 0);
    atomic_init(&s->overruns, 0);
    atomic_init(&s->recovery_attempts, 0);
    atomic_init(&s->recovery_successes, 0);
    atomic_init(&s->recovery_failures, 0);
    atomic_init(&s->read_errors, 0);
    atomic_init(&s->retained_error, 0);

    s->fs  = 48000;
    s->ch  = 1;
    s->fmt = SND_PCM_FORMAT_S16_LE;
    s->gain = gain;

    const char* dev = device ? device : "default";
    /* The TX producer assembles exact frames, waiting briefly between AGAIN
     * results. Nonblocking capture lets an abort interrupt a missing input. */
    int rc = snd_pcm_open(&s->pcm, dev, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
    if (rc < 0) { free(s); return NULL; }

    // Keep a small ring and return AGAIN while the next period is unavailable.
    s->period  = 480; // target 10 ms
    s->bufsize = s->period * 8;

    rc = alsa_set_hw_params(s->pcm, &s->fs, &s->ch, s->fmt, &s->period, &s->bufsize);
    if (rc < 0) { snd_pcm_close(s->pcm); free(s); return NULL; }

    rc = alsa_set_sw_params(s->pcm, s->period);
    if (rc < 0) { snd_pcm_close(s->pcm); free(s); return NULL; }

    // allocate temp capture buffer (period frames) and a modest ring (~170 ms)
    s->cap_i16 = (int16_t*)malloc(sizeof(int16_t) * s->period * s->ch);
    s->rcap    = (size_t)(s->period * 17); // ~170 ms ring at 10 ms period
    s->ring    = (float*)malloc(sizeof(float) * s->rcap);
    if (!s->cap_i16 || !s->ring) {
        alsa_source_destroy(s);
        return NULL;
    }

    // Prime the device
    rc = snd_pcm_prepare(s->pcm);
    if (rc < 0) { alsa_source_destroy(s); return NULL; }

    s->rhead = s->rtail = s->rcount = 0;
    return s;
}

static void ring_push(alsa_source_t* s, const float* src, size_t n)
{
    // Drop oldest if overflow (keep newest)
    if (n > s->rcap) {
        src += (n - s->rcap);
        n = s->rcap;
    }
    while (s->rcount + n > s->rcap) {
        // pop one frame (overwrite oldest)
        s->rhead = (s->rhead + 1) % s->rcap;
        s->rcount--;
    }
    // write n frames
    size_t tail_to_end = s->rcap - s->rtail;
    size_t first = (n < tail_to_end) ? n : tail_to_end;
    memcpy(&s->ring[s->rtail], src, first * sizeof(float));
    s->rtail = (s->rtail + first) % s->rcap;
    size_t remain = n - first;
    if (remain) {
        memcpy(&s->ring[s->rtail], src + first, remain * sizeof(float));
        s->rtail = (s->rtail + remain) % s->rcap;
    }
    s->rcount += n;
}

static size_t ring_pop(alsa_source_t* s, float* dst, size_t n)
{
    size_t take = (n < s->rcount) ? n : s->rcount;
    size_t head_to_end = s->rcap - s->rhead;
    size_t first = (take < head_to_end) ? take : head_to_end;
    memcpy(dst, &s->ring[s->rhead], first * sizeof(float));
    s->rhead = (s->rhead + first) % s->rcap;
    size_t remain = take - first;
    if (remain) {
        memcpy(dst + first, &s->ring[s->rhead], remain * sizeof(float));
        s->rhead = (s->rhead + remain) % s->rcap;
    }
    s->rcount -= take;
    return take;
}

static int pcm_capture_once(alsa_source_t* s)
{
    // Read up to one period; this handle is nonblocking in production.
    atomic_fetch_add_explicit(&s->read_calls, 1, memory_order_relaxed);
    snd_pcm_sframes_t got = snd_pcm_readi(s->pcm, s->cap_i16, s->period);
    if (got == -EAGAIN || got == -EINTR) return 0;
    if (got < 0) {
        if (got == -EPIPE || got == -ESTRPIPE) {
            if (got == -EPIPE)
                atomic_fetch_add_explicit(&s->overruns, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(&s->recovery_attempts, 1, memory_order_relaxed);
            int rc = alsa_recover_xrun(s->pcm, (int)got);
            if (rc < 0) {
                atomic_fetch_add_explicit(&s->recovery_failures, 1, memory_order_relaxed);
                atomic_fetch_add_explicit(&s->read_errors, 1, memory_order_relaxed);
                atomic_store_explicit(&s->retained_error, rc, memory_order_relaxed);
                return rc;
            }
            atomic_fetch_add_explicit(&s->recovery_successes, 1, memory_order_relaxed);
        } else {
            atomic_fetch_add_explicit(&s->read_errors, 1, memory_order_relaxed);
            atomic_store_explicit(&s->retained_error, (int)got, memory_order_relaxed);
            return (int)got;
        }
        atomic_fetch_add_explicit(&s->read_calls, 1, memory_order_relaxed);
        got = snd_pcm_readi(s->pcm, s->cap_i16, s->period);
        if (got == -EAGAIN || got == -EINTR) return 0;
        if (got < 0) {
            atomic_fetch_add_explicit(&s->read_errors, 1, memory_order_relaxed);
            atomic_store_explicit(&s->retained_error, (int)got, memory_order_relaxed);
            return (int)got;
        }
    }
    if (got == 0) return 0;
    atomic_fetch_add_explicit(&s->frames_read, (uint64_t)got, memory_order_relaxed);

    // convert to float and push
    const float g = s->gain;
    static float tmpf[8192]; // enough for typical period sizes
    if ((size_t)got > sizeof(tmpf)/sizeof(tmpf[0])) {
        // fallback allocate for unusually large period
        float* dyn = (float*)malloc(sizeof(float) * (size_t)got);
        if (!dyn) return -ENOMEM;
        for (snd_pcm_sframes_t i = 0; i < got; i++) {
            float x = (float)s->cap_i16[i] / 32768.0f;
            dyn[i] = CLAMPF(x * g, -1.0f, 1.0f);
        }
        ring_push(s, dyn, (size_t)got);
        free(dyn);
    } else {
        for (snd_pcm_sframes_t i = 0; i < got; i++) {
            float x = (float)s->cap_i16[i] / 32768.0f;
            tmpf[i] = CLAMPF(x * g, -1.0f, 1.0f);
        }
        ring_push(s, tmpf, (size_t)got);
    }
    return (int)got;
}

size_t alsa_source_read(alsa_source_t* s, float* dst, size_t max_frames)
{
    if (!s || !dst || max_frames == 0) return 0;
    s->last_error = 0;

    // If we already have enough in the ring, just pop and return.
    if (s->rcount >= max_frames) {
        return ring_pop(s, dst, max_frames);
    }

    // Otherwise, capture until we can satisfy the request (bounded wait).
    const int max_loops = 8; // ~80 ms worst case (period ~10 ms)
    int loops = 0;

    while (s->rcount < max_frames && loops < max_loops) {
        int rc = pcm_capture_once(s);
        if (rc < 0) {
            s->last_error = rc;
            // Return buffered frames along with the error through the common API.
            break;
        }
        if (rc == 0) {
            // The producer owns pacing and can observe an abort between reads.
            break;
        }
        loops++;
    }

    // pop whatever is available up to max_frames
    return ring_pop(s, dst, max_frames);
}

void alsa_source_destroy(alsa_source_t* s)
{
    if (!s) return;
    if (s->pcm) snd_pcm_close(s->pcm);
    free(s->cap_i16);
    free(s->ring);
    free(s);
}

static audio_source_result_t alsa_audio_read(audio_source_t* source, float* dst, size_t frames)
{
    alsa_source_t* s = (alsa_source_t*)source;
    size_t got = alsa_source_read(s, dst, frames);
    return (audio_source_result_t){got,
        s->last_error ? AUDIO_SOURCE_ERROR : got ? AUDIO_SOURCE_OK : AUDIO_SOURCE_AGAIN,
        s->last_error};
}

static void alsa_audio_destroy(audio_source_t* source)
{
    alsa_source_destroy((alsa_source_t*)source);
}

static const audio_source_ops_t alsa_audio_ops = {alsa_audio_read, alsa_audio_destroy};

bool alsa_source_get_stats(audio_source_t* source, alsa_source_stats_t* out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!source || source->ops != &alsa_audio_ops) return false;
    alsa_source_t* s = (alsa_source_t*)source;
#define READ_STAT(field) out->field = atomic_load_explicit(&s->field, memory_order_relaxed)
    READ_STAT(read_calls); READ_STAT(frames_read); READ_STAT(overruns);
    READ_STAT(recovery_attempts); READ_STAT(recovery_successes);
    READ_STAT(recovery_failures); READ_STAT(read_errors);
#undef READ_STAT
    out->last_error = atomic_load_explicit(&s->retained_error, memory_order_relaxed);
    return true;
}

int alsa_source_reset(audio_source_t* source, bool prepare)
{
    if (!source || source->ops != &alsa_audio_ops) return -ENOTSUP;
    alsa_source_t* s = (alsa_source_t*)source;
    int rc = snd_pcm_drop(s->pcm);
    /* A newly prepared stream may already be stopped. */
    if (rc == -EBADFD && snd_pcm_state(s->pcm) != SND_PCM_STATE_RUNNING) rc = 0;
    s->rhead = s->rtail = s->rcount = 0;
    s->last_error = 0;
    if (rc >= 0 && prepare) rc = snd_pcm_prepare(s->pcm);
    if (rc < 0) {
        atomic_fetch_add_explicit(&s->read_errors, 1, memory_order_relaxed);
        atomic_store_explicit(&s->retained_error, rc, memory_order_relaxed);
    }
    return rc;
}

audio_source_t* alsa_source_open(const char* device, float gain, audio_format_t format)
{
    if (format.sample_rate != 48000 || format.channels != 1) {
        errno = EINVAL;
        return NULL;
    }
    alsa_source_t* s = alsa_source_create(device, gain);
    if (!s) { errno = EIO; return NULL; }
    s->base.format = format;
    s->base.ops = &alsa_audio_ops;
    return &s->base;
}
