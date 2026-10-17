#pragma once
#include "audio_sink.h"

// Blocking playback; NULL/empty device means default. Only 48000 Hz supported.
// Returns NULL and sets errno on failure. Mono first, stereo duplication fallback.
// audio_sink_get_stats() exposes atomic lifetime write/recovery counters.
audio_sink_t* alsa_sink_open(const char* device, unsigned sample_rate);
unsigned alsa_sink_channels(const audio_sink_t* sink);
