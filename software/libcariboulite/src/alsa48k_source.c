#include "alsa48k_source.h"
#include <alsa/asoundlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#ifndef CLAMPF
#define CLAMPF(x,lo,hi) ((x) < (lo) ? (lo) : ((x) > (hi) ? (hi) : (x)))
#endif

struct alsa48k_source {
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
        snd_pcm_prepare(pcm);
        return 0;
    } else if (err == -ESTRPIPE) {
        // suspended, try to resume
        while ((err = snd_pcm_resume(pcm)) == -EAGAIN) {
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
            nanosleep(&ts, NULL);
        }
        if (err < 0) snd_pcm_prepare(pcm);
        return 0;
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

alsa48k_source_t* alsa48k_create(const char* device, float gain)
{
    alsa48k_source_t* s = (alsa48k_source_t*)calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->fs  = 48000;
    s->ch  = 1;
    s->fmt = SND_PCM_FORMAT_S16_LE;
    s->gain = gain;

    const char* dev = device ? device : "default";
    int rc = snd_pcm_open(&s->pcm, dev, SND_PCM_STREAM_CAPTURE, 0 /* blocking */);
    if (rc < 0) { free(s); return NULL; }

    // recommend blocking capture; we keep our own small ring and do quick reads
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
        alsa48k_destroy(s);
        return NULL;
    }

    // Prime the device
    rc = snd_pcm_prepare(s->pcm);
    if (rc < 0) { alsa48k_destroy(s); return NULL; }

    s->rhead = s->rtail = s->rcount = 0;
    return s;
}

static void ring_push(alsa48k_source_t* s, const float* src, size_t n)
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

static size_t ring_pop(alsa48k_source_t* s, float* dst, size_t n)
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

static int pcm_capture_once(alsa48k_source_t* s)
{
    // read up to one period (blocking read is OK; period is small)
    snd_pcm_sframes_t got = snd_pcm_readi(s->pcm, s->cap_i16, s->period);
    if (got < 0) {
        if (alsa_recover_xrun(s->pcm, (int)got) < 0) return (int)got;
        got = snd_pcm_readi(s->pcm, s->cap_i16, s->period);
        if (got < 0) return (int)got;
    }
    if (got == 0) return 0;

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

size_t alsa48k_read(alsa48k_source_t* s, float* dst, size_t max_frames)
{
    if (!s || !dst || max_frames == 0) return 0;

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
            // On error, return what we do have (could be 0)
            break;
        }
        if (rc == 0) {
            // No new data; brief nap
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 2000000 }; // 2 ms
            nanosleep(&ts, NULL);
        }
        loops++;
    }

    // pop whatever is available up to max_frames
    return ring_pop(s, dst, max_frames);
}

void alsa48k_destroy(alsa48k_source_t* s)
{
    if (!s) return;
    if (s->pcm) snd_pcm_close(s->pcm);
    free(s->cap_i16);
    free(s->ring);
    free(s);
}