// Exercise actual ALSA adapter buffering with scripted capture; no audio hardware.
#include <assert.h>
#include <errno.h>
#include "alsa_source.c"

static int reads;
static bool scripted;
static int script[8], script_count, script_position;
static int prepare_result, resume_result, drop_result;
static unsigned prepare_calls, resume_calls, drop_calls;
int __wrap_snd_pcm_prepare(snd_pcm_t* pcm)
{ (void)pcm; ++prepare_calls; return prepare_result; }
int __wrap_snd_pcm_resume(snd_pcm_t* pcm)
{ (void)pcm; ++resume_calls; return resume_result; }
int __wrap_snd_pcm_drop(snd_pcm_t* pcm)
{ (void)pcm; ++drop_calls; return drop_result; }
snd_pcm_sframes_t __wrap_snd_pcm_readi(snd_pcm_t* pcm, void* data, snd_pcm_uframes_t frames)
{
    (void)pcm;
    assert(frames == 2);
    if (scripted) {
        int result = script_position < script_count ? script[script_position++] : 0;
        if (result > 0) {
            int16_t* samples = data;
            samples[0] = 16384; samples[1] = -16384;
        }
        return result;
    }
    if (reads++ == 0) {
        int16_t* samples = data;
        samples[0] = 16384; samples[1] = -16384;
        return 2;
    }
    return -EIO;
}

int main(void)
{
    errno = 0;
    assert(!alsa_source_open("must-not-open", 1, (audio_format_t){44100, 1}));
    assert(errno == EINVAL);
    assert(!alsa_source_open("must-not-open", 1, (audio_format_t){48000, 2}));
    float output[4] = {0}, ring[16] = {0};
    int16_t capture[2];
    alsa_source_t fixture = { .base = {{48000, 1}, &alsa_audio_ops},
        .gain = 1, .period = 2, .rcap = 16, .ring = ring, .cap_i16 = capture };
    audio_source_result_t r = audio_source_read(&fixture.base, output, 4);
    assert(r.frames == 2 && r.status == AUDIO_SOURCE_ERROR && r.error == -EIO);
    assert(output[0] == 0.5f && output[1] == -0.5f);
    r = audio_source_read(&fixture.base, output, 4);
    assert(r.frames == 0 && r.status == AUDIO_SOURCE_ERROR);
    reads = 0;
    r = audio_source_read(&fixture.base, output, 1);
    assert(r.frames == 1 && r.status == AUDIO_SOURCE_OK && r.error == 0);
    r = audio_source_read(&fixture.base, output, 1);
    assert(r.frames == 1 && output[0] == -0.5f && reads == 1);
    assert(audio_source_read(NULL, output, 1).status == AUDIO_SOURCE_ERROR);
    assert(audio_source_read(&fixture.base, NULL, 1).status == AUDIO_SOURCE_ERROR);
    assert(audio_source_read(&fixture.base, NULL, 0).status == AUDIO_SOURCE_OK);
    audio_source_destroy(NULL);
    alsa_source_stats_t stats;
    assert(alsa_source_get_stats(&fixture.base, &stats));
    assert(stats.read_errors == 2 && stats.last_error == -EIO);
    audio_source_t other = {0};
    assert(!alsa_source_get_stats(&other, &stats));
    assert(!stats.read_calls && !stats.read_errors);
    assert(alsa_source_reset(&other, true) == -ENOTSUP);
    assert(alsa_source_reset(&fixture.base, false) == 0);
    assert(!fixture.rcount && drop_calls == 1);
    assert(alsa_source_reset(&fixture.base, true) == 0 && prepare_calls == 1);

    fixture = (alsa_source_t){ .base = {{48000, 1}, &alsa_audio_ops},
        .gain = 1, .period = 2, .rcap = 16, .ring = ring, .cap_i16 = capture };
    scripted = true;
    script[0] = -EPIPE; script[1] = 2; script_count = 2; script_position = 0;
    r = audio_source_read(&fixture.base, output, 2);
    assert(r.frames == 2 && r.status == AUDIO_SOURCE_OK);
    assert(alsa_source_get_stats(&fixture.base, &stats));
    assert(stats.read_calls == 2 && stats.frames_read == 2 && stats.overruns == 1);
    assert(stats.recovery_attempts == 1 && stats.recovery_successes == 1);
    assert(!stats.recovery_failures && !stats.read_errors);
    /* Resume returning AGAIN must take one attempt, then prepare, without an
     * unbounded sleep loop that could prevent TX worker parking. */
    resume_result = -EAGAIN;
    script[0] = -ESTRPIPE; script[1] = 2; script_position = 0;
    r = audio_source_read(&fixture.base, output, 1);
    assert(r.frames == 1 && fixture.rcount == 1 && resume_calls == 1);
    assert(alsa_source_reset(&fixture.base, false) == 0 && !fixture.rcount);
    prepare_result = -EIO;
    script[0] = -EPIPE; script_count = 1; script_position = 0;
    r = audio_source_read(&fixture.base, output, 2);
    assert(r.status == AUDIO_SOURCE_ERROR && r.error == -EIO);
    assert(alsa_source_get_stats(&fixture.base, &stats));
    assert(stats.overruns == 2 && stats.recovery_attempts == 3);
    assert(stats.recovery_successes == 2 && stats.recovery_failures == 1);
    assert(stats.read_errors == 1 && stats.last_error == -EIO);
    script[0] = -EAGAIN; script_position = 0;
    r = audio_source_read(&fixture.base, output, 2);
    assert(r.status == AUDIO_SOURCE_AGAIN && !r.frames);
    assert(alsa_source_get_stats(&fixture.base, &stats) && stats.read_errors == 1);
    drop_result = -EIO;
    assert(alsa_source_reset(&fixture.base, false) == -EIO);
    assert(alsa_source_get_stats(&fixture.base, &stats) && stats.read_errors == 2);
    puts("PASS: source format/buffering, nonblocking AGAIN, overrun/recovery "
         "diagnostics, bounded resume and parked-reader capture resets");
}
