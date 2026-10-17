#include "tone_source.h"
#include "math_compat.h"
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

struct tone_source {
    audio_source_t base;
    float phase, frequency, amplitude;
    bool indexed;
    size_t index;
};

static audio_source_result_t read_tone(audio_source_t* base, float* dst, size_t n)
{
    struct tone_source* s = (struct tone_source*)base;
    const float step = 2.0f * (float)M_PI * (s->frequency / 48000.0f);
    for (size_t i = 0; i < n; ++i) {
        if (s->frequency == 0) { dst[i] = 0; continue; }
        if (s->indexed) {
            dst[i] = s->amplitude * sinf(2.f * M_PI * s->frequency * (float)s->index++ / 48000.f);
        } else {
            s->phase += step;
            if (s->phase >= 2.0f * (float)M_PI) s->phase -= 2.0f * (float)M_PI;
            dst[i] = s->amplitude * sinf(s->phase);
        }
    }
    return (audio_source_result_t){n, AUDIO_SOURCE_OK, 0};
}
static void destroy_tone(audio_source_t* source) { free(source); }
static const audio_source_ops_t ops = {read_tone, destroy_tone};

int tone_source_set(audio_source_t* source, float frequency, float amplitude)
{
    if (!source || source->ops != &ops || !isfinite(frequency) ||
        frequency < 0 || frequency >= 24000 || !isfinite(amplitude) ||
        amplitude < 0 || amplitude > 1) return -EINVAL;
    struct tone_source* s = (struct tone_source*)source;
    s->frequency = frequency;
    s->amplitude = amplitude;
    return 0;
}

audio_source_t* tone_source_open(float frequency, float amplitude, audio_format_t format)
{
    if (format.sample_rate != 48000 || format.channels != 1) { errno = EINVAL; return NULL; }
    struct tone_source* s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->base = (audio_source_t){format, &ops};
    if (tone_source_set(&s->base, frequency, amplitude)) {
        free(s); errno = EINVAL; return NULL;
    }
    return &s->base;
}

audio_source_t* tone_source_open_cue(float frequency)
{
    audio_source_t* base = tone_source_open(frequency, 1, (audio_format_t){48000, 1});
    if (base) ((struct tone_source*)base)->indexed = true;
    return base;
}
