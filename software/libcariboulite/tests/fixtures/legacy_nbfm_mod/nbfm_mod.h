#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "iq16.h"
typedef struct nbfm_mod nbfm_mod_t;

typedef struct {
    double audio_fs;      // 48000.0
    double rf_fs;         // RF sample rate in Hz (e.g., 2000000.0 or 4000000.0)
    double f_dev_hz;      // e.g., 2500.0
    double preemph_tau_s; // 0 to disable
    float  out_scale;     // e.g., 12000
    int    linear_interp; // 1 = better quality
} nbfm_cfg_t;

nbfm_mod_t* nbfm_create(const nbfm_cfg_t* cfg);
void          nbfm_destroy(nbfm_mod_t* m);
size_t        nbfm_push_audio(nbfm_mod_t* m, const float* audio48k, size_t frames);
size_t        nbfm_pull_iq  (nbfm_mod_t* m, iq16_t* dst, size_t max_frames);

#ifdef __cplusplus
}
#endif