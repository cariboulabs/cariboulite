#pragma once
#include <stdint.h>

// Counts are frames; a frame has one sample per channel. Representation is
// explicit in each API's sample type. Current modem paths are 48 kHz mono.
typedef struct { unsigned sample_rate; unsigned channels; } audio_format_t;
typedef float audio_f32_t;   // normalized audio; nominal range [-1, 1]
typedef int16_t audio_s16_t; // PCM amplitude [-32768, 32767], no implicit scaling
