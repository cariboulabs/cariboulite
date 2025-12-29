#pragma once
#include <stddef.h>

typedef struct alsa48k_source alsa48k_source_t;

// device examples: "default", "hw:1,0"
alsa48k_source_t* alsa48k_create(const char* device, float gain);
size_t alsa48k_read(alsa48k_source_t* s, float* dst, size_t max_frames);
void   alsa48k_destroy(alsa48k_source_t* s);