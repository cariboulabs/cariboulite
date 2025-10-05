#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audio48k_source audio48k_source_t;

audio48k_source_t* tone48k_create(double freq_hz, float amplitude);
size_t audio48k_read(audio48k_source_t* s, float* dst, size_t max_frames);
void   audio48k_destroy(audio48k_source_t* s);

#ifdef __cplusplus
}
#endif