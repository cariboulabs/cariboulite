#pragma once
#include "audio_source.h"

// 48 kHz mono; finite 0 <= frequency < Nyquist and 0 <= amplitude <= 1.
audio_source_t* tone_source_open(float frequency, float amplitude, audio_format_t format);
// Change frequency/amplitude without resetting phase. Zero Hz emits silence and
// freezes phase. Call only from the source's owning worker.
int tone_source_set(audio_source_t* source, float frequency, float amplitude);
// Self-test audio cues use the original indexed sine formula, starting at phase 0.
audio_source_t* tone_source_open_cue(float frequency);
