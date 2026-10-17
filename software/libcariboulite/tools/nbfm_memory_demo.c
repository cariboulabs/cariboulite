/* Hardware-free source -> modulator -> IQ -> demodulator -> sink example.
 * This bounded synchronous runner is not the realtime radio pipeline.
 * Build instructions and adapter integration: docs/audio-dsp-extension-guide.md.
 */
#include "memory_audio.h"
#include "tone_source.h"
#include "nbfm_mod.h"
#include "nbfm_demod.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

typedef struct {
    size_t audio_read, iq_produced, pcm_written;
} roundtrip_counts_t;

// Adapters remain caller-owned. Consume final EOF frames before stopping.
// AGAIN is returned as -EAGAIN rather than spinning: this example expects
// synchronous adapters. A resumable/real-time runner needs its own wait policy.
static int run_roundtrip(audio_source_t* source, audio_sink_t* sink,
                         unsigned rf_rate, size_t audio_limit,
                         roundtrip_counts_t* counts)
{
    if (!counts) return -EINVAL;
    *counts = (roundtrip_counts_t){0};
    if (!source || !sink || source->format.sample_rate != 48000 ||
        source->format.channels != 1 || sink->sample_rate != 48000 ||
        (rf_rate != 1000000 && rf_rate != 2000000 && rf_rate != 4000000) ||
        audio_limit > SIZE_MAX / 84)
        return -EINVAL;
    nbfm_cfg_t tx_cfg = {48000, rf_rate, NBFM_DEFAULT_DEVIATION_HZ, 0, 4000, 1};
    nbfm_demod_config_t rx_cfg = {rf_rate, 48000, 50e-6f, 8000};
    nbfm_mod_t* mod = nbfm_create(&tx_cfg);
    if (!mod) return -errno;
    nbfm_demod_t* demod = nbfm_demod_create(&rx_cfg);
    if (!demod) { int error = -errno; nbfm_destroy(mod); return error; }
    audio_f32_t audio[480];
    iq16_t iq[40000];
    audio_s16_t pcm[137]; // deliberately not a 10 ms output buffer
    uint64_t clock_excess = 0;
    int error = 0;
    while (counts->audio_read < audio_limit) {
        size_t request = audio_limit - counts->audio_read;
        if (request > 480) request = 480;
        audio_source_result_t read = audio_source_read(source, audio, request);
        if (read.frames > request) { error = -EIO; break; }
        if (read.frames) {
            // ceil(total_audio * RF / 48000), preserving fractional timing
            // across short reads without accumulating rounded-block errors.
            uint64_t ticks = (uint64_t)read.frames * rf_rate - clock_excess;
            size_t iq_count = (size_t)((ticks + 47999) / 48000);
            clock_excess = (uint64_t)iq_count * 48000 - ticks;
            counts->audio_read += read.frames;
            nbfm_result_t tx = nbfm_process(mod, audio, read.frames, iq, iq_count);
            if (tx.error || tx.consumed != read.frames || tx.produced != iq_count || tx.held_audio) {
                error = tx.error ? tx.error : -EIO;
                break;
            }
            counts->iq_produced += tx.produced;
            size_t offset = 0;
            while (offset < tx.produced) {
                nbfm_demod_result_t rx = nbfm_demod_process(demod, iq + offset,
                    tx.produced - offset, pcm, sizeof(pcm)/sizeof(*pcm), 0);
                if (rx.error || !rx.consumed) { error = rx.error ? rx.error : -EIO; break; }
                offset += rx.consumed;
                size_t written = 0;
                while (written < rx.produced) {
                    audio_sink_result_t w = audio_sink_write(sink, pcm + written, rx.produced - written);
                    if (w.frames > rx.produced - written) { error = -EIO; break; }
                    written += w.frames;
                    counts->pcm_written += w.frames;
                    if (w.status == AUDIO_SINK_ERROR) { error = w.error ? w.error : -EIO; break; }
                    if (w.status == AUDIO_SINK_AGAIN) { error = -EAGAIN; break; }
                    if (!w.frames) { error = -EIO; break; }
                }
                if (error) break;
            }
            if (error) break;
        }
        if (read.status == AUDIO_SOURCE_ERROR) { error = read.error ? read.error : -EIO; break; }
        if (read.status == AUDIO_SOURCE_EOF) break;
        if (read.status == AUDIO_SOURCE_AGAIN) { error = -EAGAIN; break; }
        if (!read.frames) { error = -EIO; break; }
    }
    nbfm_demod_destroy(demod);
    nbfm_destroy(mod);
    return error;
}

static double recovered_pitch(const audio_s16_t* samples, size_t frames)
{
    // Ignore startup filters; interpolate upward zero crossings.
    double first = 0, last = 0;
    size_t crossings = 0;
    for (size_t i = 2401; i < frames; ++i) {
        if (samples[i-1] < 0 && samples[i] >= 0) {
            double at = (double)(i-1) - (double)samples[i-1] / (samples[i] - samples[i-1]);
            if (!crossings++) first = at;
            last = at;
        }
    }
    return crossings > 1 ? 48000.0 * (crossings-1) / (last-first) : 0;
}

#ifndef NBFM_MEMORY_DEMO_NO_MAIN
int main(void)
{
    const size_t frames = 12137; // finite source, including a partial final block
    audio_f32_t* input = calloc(frames, sizeof(*input));
    audio_s16_t* output = calloc(frames, sizeof(*output));
    audio_source_t* tone = tone_source_open(600, 0.4f, (audio_format_t){48000, 1});
    if (!input || !output || !tone) {
        free(input); free(output); audio_source_destroy(tone);
        return 1;
    }
    audio_source_result_t generated = audio_source_read(tone, input, frames);
    audio_source_destroy(tone);
    int result = generated.status != AUDIO_SOURCE_OK || generated.frames != frames;
    for (unsigned rate = 1000000; !result && rate <= 4000000; rate *= 2) {
        audio_source_t* source = memory_source_open(input, frames, (audio_format_t){48000, 1});
        audio_sink_t* sink = memory_sink_open(output, frames, (audio_format_t){48000, 1});
        roundtrip_counts_t counts = {0};
        int error = source && sink ? run_roundtrip(source, sink, rate, frames + 1, &counts) : -ENOMEM;
        double pitch = recovered_pitch(output, counts.pcm_written);
        printf("%u MS/s: audio=%zu IQ=%zu PCM=%zu, recovered %.2f Hz, status=%d\n",
            rate/1000000, counts.audio_read, counts.iq_produced, counts.pcm_written, pitch, error);
        if (error || counts.audio_read != frames || counts.pcm_written < frames-2 || fabs(pitch-600) > 2)
            result = 1;
        audio_source_destroy(source);
        audio_sink_destroy(sink);
    }
    free(input); free(output);
    return result;
}
#endif
