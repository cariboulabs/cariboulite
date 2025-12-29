#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __attribute__((__packed__)) { int16_t i; int16_t q; } iq16_t;
typedef struct nbfm4m_mod nbfm4m_mod_t;

typedef struct {
    double audio_fs;      // 48000.0
    double rf_fs;         // 4000000.0
    double f_dev_hz;      // e.g., 2500.0
    double preemph_tau_s; // 0 to disable
    float  out_scale;     // e.g., 12000
    int    linear_interp; // 1 = better quality
} nbfm4m_cfg_t;

nbfm4m_mod_t* nbfm4m_create(const nbfm4m_cfg_t* cfg);
void          nbfm4m_destroy(nbfm4m_mod_t* m);
size_t        nbfm4m_push_audio(nbfm4m_mod_t* m, const float* audio48k, size_t frames);
size_t        nbfm4m_pull_iq  (nbfm4m_mod_t* m, iq16_t* dst, size_t max_frames);

#ifdef __cplusplus
}
#endif