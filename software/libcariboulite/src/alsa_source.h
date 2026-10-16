#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct alsa_source alsa_source_t;

// device examples: "default", "hw:1,0"
alsa_source_t* alsa_source_create(const char* device, float gain);
size_t alsa_source_read(alsa_source_t* s, float* dst, size_t max_frames);
void   alsa_source_destroy(alsa_source_t* s);
#include "audio_source.h"
// Currently supports only 48000 Hz mono. Returns NULL and sets errno on failure.
audio_source_t* alsa_source_open(const char* device, float gain, audio_format_t format);

typedef struct {
    uint64_t read_calls, frames_read;
    uint64_t overruns, recovery_attempts, recovery_successes, recovery_failures;
    uint64_t read_errors;
    int last_error;
} alsa_source_stats_t;

/* Lifetime counters, safe while the one reader runs. Non-ALSA sources return
 * false; tone generation has no microphone capture statistics. */
bool alsa_source_get_stats(audio_source_t*, alsa_source_stats_t*);
/* Only call after the reader has parked/joined. Discard capture/ring backlog;
 * prepare=true leaves fresh capture prepared, false leaves capture dropped. */
int alsa_source_reset(audio_source_t*, bool prepare);
