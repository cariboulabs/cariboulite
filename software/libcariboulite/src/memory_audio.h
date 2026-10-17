#pragma once
#include "audio_source.h"
#include "audio_sink.h"

// Finite, nonblocking 48 kHz mono adapters. Only adapter state is allocated.
// Backing arrays are borrowed until destroy: keep input alive/unchanged and
// output alive/exclusively writable. Read/write call buffers must not overlap
// their backing array. Destroy never frees the caller's array.
// NULL array is permitted only for zero frames/capacity. Invalid config =>
// NULL/errno EINVAL; allocation failure => NULL/errno ENOMEM.
audio_source_t* memory_source_open(const audio_f32_t* samples, size_t frames,
                                    audio_format_t format);
audio_sink_t* memory_sink_open(audio_s16_t* samples, size_t capacity,
                                audio_format_t format);
// Source returns EOF with the final frames (possibly zero); copies unchanged.
// Sink returns ERROR/-ENOSPC with any frames that fit on a short write. It
// never returns AGAIN; caller must handle progress before error. No overwrite.
// Same single-owner/no allocation during I/O rules as the common interfaces.
size_t memory_sink_frames(const audio_sink_t* sink); // NULL/wrong adapter => 0
