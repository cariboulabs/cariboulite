#pragma once
#include <stddef.h>
#include "audio_format.h"
#include "iq16.h"
#include "nbfm_defaults.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nbfm_mod nbfm_mod_t;
typedef struct {
    double audio_fs;      // exactly 48000 Hz, mono normalized float input
    double rf_fs;         // exactly 1000000, 2000000 or 4000000 IQ pairs/s
    double f_dev_hz;      // 0..24000 Hz deviation at unit audio before pre-emphasis
    double preemph_tau_s; // finite >=0 seconds; zero disables pre-emphasis
    float  out_scale;     // finite 0..32767 signed IQ amplitude (not RF power)
    int    linear_interp; // exactly 0 (hold) or 1 (linear frequency interpolation)
} nbfm_cfg_t;
typedef struct {
    size_t consumed;     // input frames copied into the internal audio queue
    size_t produced;     // IQ pairs written
    size_t held_audio;   // audio ticks without queued input: last frequency held
    int error;          // 0 or negative errno-style code; errors leave state intact
} nbfm_result_t;

// NULL config selects {48000, 4000000, NBFM_DEFAULT_DEVIATION_HZ, 0, 12000, 1}.
// Returns NULL with errno EINVAL/ENOMEM. Allocation occurs only at creation.
nbfm_mod_t* nbfm_create(const nbfm_cfg_t* cfg);
void nbfm_destroy(nbfm_mod_t* m); // NULL permitted
// Discard queued audio, reset phase, interpolation and pre-emphasis history.
// Configuration/storage are retained; equivalent signal state to a new instance.
void nbfm_reset(nbfm_mod_t* m); // NULL permitted
// Enqueue first, then generate up to capacity IQ pairs (including frequency hold
// on underrun). No pointers retained; input/output must not overlap. One owner.
// Short consumption means queue space ran out; retry unused input on a later call.
// Zero output capacity may still enqueue; zero input may still produce IQ.
nbfm_result_t nbfm_process(nbfm_mod_t* m, const audio_f32_t* audio, size_t frames,
                           iq16_t* output, size_t capacity);
size_t nbfm_buffered_audio(const nbfm_mod_t* m); // pending queue only; NULL => 0

// Compatibility APIs: same queue/hold behavior. Invalid args => 0/errno EINVAL.
// push rejects nonfinite samples; finite samples are clipped to [-1,1] on use.
size_t nbfm_push_audio(nbfm_mod_t* m, const audio_f32_t* audio, size_t frames);
size_t nbfm_pull_iq(nbfm_mod_t* m, iq16_t* dst, size_t max_frames);

#ifdef __cplusplus
}
#endif
