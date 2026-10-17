#pragma once
#include <stddef.h>
#include <errno.h>
#include "audio_format.h"

// Interleaved normalized float samples. Counts are frames, not bytes.
typedef enum {
    AUDIO_SOURCE_OK, AUDIO_SOURCE_AGAIN, AUDIO_SOURCE_EOF, AUDIO_SOURCE_ERROR
} audio_source_status_t;
typedef struct {
    size_t frames;
    audio_source_status_t status;
    int error; // negative errno-style code for ERROR, otherwise zero
} audio_source_result_t;
typedef struct audio_source audio_source_t;
typedef struct {
    audio_source_result_t (*read)(audio_source_t*, audio_f32_t*, size_t);
    void (*destroy)(audio_source_t*);
} audio_source_ops_t;
struct audio_source {
    audio_format_t format; // immutable after creation
    const audio_source_ops_t* ops;
};

// One reader; caller owns dst. No pointer retention. May block, depending on adapter.
static inline audio_source_result_t audio_source_read(audio_source_t* s, audio_f32_t* dst, size_t frames)
{
    if (!s || !s->ops || !s->ops->read || (!dst && frames))
        return (audio_source_result_t){0, AUDIO_SOURCE_ERROR, -EINVAL};
    if (!frames) return (audio_source_result_t){0, AUDIO_SOURCE_OK, 0};
    return s->ops->read(s, dst, frames);
}
// Stop/join reader before destruction. NULL is permitted.
static inline void audio_source_destroy(audio_source_t* s)
{
    if (s) s->ops->destroy(s);
}
