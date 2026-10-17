#include "memory_audio.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    audio_source_t base;
    const audio_f32_t* samples;
    size_t frames, position;
} memory_source_t;
typedef struct {
    audio_sink_t base;
    audio_s16_t* samples;
    size_t capacity, position;
} memory_sink_t;

static audio_source_result_t memory_read(audio_source_t* base, audio_f32_t* dst, size_t count)
{
    memory_source_t* s = (memory_source_t*)base;
    size_t available = s->frames - s->position;
    if (count > available) count = available;
    if (count) memcpy(dst, s->samples + s->position, count * sizeof(*dst));
    s->position += count;
    return (audio_source_result_t){count,
        s->position == s->frames ? AUDIO_SOURCE_EOF : AUDIO_SOURCE_OK, 0};
}
static void memory_source_destroy(audio_source_t* base) { free(base); }
static const audio_source_ops_t source_ops = {memory_read, memory_source_destroy};

static audio_sink_result_t memory_write(audio_sink_t* base, const audio_s16_t* src, size_t count)
{
    memory_sink_t* s = (memory_sink_t*)base;
    size_t available = s->capacity - s->position;
    size_t written = count < available ? count : available;
    if (written) memcpy(s->samples + s->position, src, written * sizeof(*src));
    s->position += written;
    return (audio_sink_result_t){written,
        written == count ? AUDIO_SINK_OK : AUDIO_SINK_ERROR,
        written == count ? 0 : -ENOSPC};
}
static void memory_sink_destroy(audio_sink_t* base) { free(base); }
static const char* memory_state(audio_sink_t* base)
{
    memory_sink_t* s = (memory_sink_t*)base;
    return s->position == s->capacity ? "FULL" : "READY";
}
static const audio_sink_ops_t sink_ops = {
    .write=memory_write, .destroy=memory_sink_destroy, .state=memory_state
};

static int supported(audio_format_t format)
{
    return format.sample_rate == 48000 && format.channels == 1;
}
audio_source_t* memory_source_open(const audio_f32_t* samples, size_t frames,
                                    audio_format_t format)
{
    if (!supported(format) || (!samples && frames) || frames > SIZE_MAX / sizeof(*samples)) {
        errno = EINVAL;
        return NULL;
    }
    memory_source_t* s = calloc(1, sizeof(*s));
    if (!s) { errno = ENOMEM; return NULL; }
    s->base = (audio_source_t){format, &source_ops};
    s->samples = samples;
    s->frames = frames;
    return &s->base;
}
audio_sink_t* memory_sink_open(audio_s16_t* samples, size_t capacity, audio_format_t format)
{
    if (!supported(format) || (!samples && capacity) || capacity > SIZE_MAX / sizeof(*samples)) {
        errno = EINVAL;
        return NULL;
    }
    memory_sink_t* s = calloc(1, sizeof(*s));
    if (!s) { errno = ENOMEM; return NULL; }
    s->base = (audio_sink_t){format.sample_rate, &sink_ops};
    s->samples = samples;
    s->capacity = capacity;
    return &s->base;
}
size_t memory_sink_frames(const audio_sink_t* sink)
{
    return sink && sink->ops == &sink_ops ? ((const memory_sink_t*)sink)->position : 0;
}
