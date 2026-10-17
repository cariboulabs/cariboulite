#include "tone_source.h"
#include "math_compat.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    audio_format_t fmt = {48000, 1};
    assert(!tone_source_open(600, .4f, (audio_format_t){44100, 1}));
    assert(!tone_source_open(24000, .4f, fmt));
    assert(!tone_source_open(600, -1, fmt));
    audio_source_t* tone = tone_source_open(600, .4f, fmt);
    assert(tone);
    float phase = 0, out[480];
    const float frequencies[] = {600, 2525, 0, 2475, 600};
    const size_t sizes[] = {1, 17, 480, 23};
    for (size_t f = 0; f < 5; ++f) {
        float hz = frequencies[f];
        assert(!tone_source_set(tone, hz, .4f));
        for (int block = 0; block < 100; ++block) {
            size_t n = sizes[block % 4];
            audio_source_result_t r = audio_source_read(tone, out, n);
            assert(r.frames == n && r.status == AUDIO_SOURCE_OK);
            const float dphi = 2.f * (float)M_PI * (hz / 48000.f);
            for (size_t i = 0; i < n; ++i) {
                float expected = 0;
                if (hz) {
                    phase += dphi;
                    if (phase >= 2.f * (float)M_PI) phase -= 2.f * (float)M_PI;
                    expected = .4f * sinf(phase);
                }
                assert(fabsf(out[i] - expected) < 1e-6f);
            }
        }
    }
    audio_source_destroy(tone);
    for (int cue = 0; cue < 2; ++cue) {
        float hz = cue ? 2475.f : 2525.f;
        tone = tone_source_open_cue(hz);
        assert(tone);
        for (size_t offset = 0; offset < 12000; offset += 480) {
            audio_source_read(tone, out, 480);
            for (size_t j = 0; j < 480; ++j) {
                float expected = sinf(2.f * M_PI * hz * (float)(offset + j) / 48000.f);
                assert(lrintf(.6f * 32767.f * expected) == lrintf(.6f * 32767.f * out[j]));
            }
        }
        audio_source_destroy(tone);
    }
    puts("PASS: tone waveform, block continuity, injection switching, silence phase freeze and PCM cues");
}
