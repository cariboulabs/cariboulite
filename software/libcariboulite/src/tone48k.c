#include "audio48k_source.h"
#include <math.h>
#include <stdlib.h>
#include "math_compat.h"

struct audio48k_source { double ph, dph; float amp; };

audio48k_source_t* tone48k_create(double f, float a) {
    audio48k_source_t* s = (audio48k_source_t*)calloc(1,sizeof(*s));
    s->ph = 0.0; s->dph = 2.0*M_PI*f/48000.0; s->amp = a; return s;
}

size_t audio48k_read(audio48k_source_t* s, float* dst, size_t n) {
    for (size_t i=0;i<n;i++) {
        dst[i] = (float)(s->amp * sin(s->ph));
        s->ph += s->dph;
        if (s->ph >  M_PI) s->ph -= 2.0*M_PI;
    }
    return n;
}

void audio48k_destroy(audio48k_source_t* s) { free(s); }