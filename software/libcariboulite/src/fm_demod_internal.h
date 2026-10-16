#pragma once
#include "nbfm_demod.h"
// Private common handle; each mode embeds it first in its own allocation.
// Backend entry points are internal; callers use nbfm_demod.h.
struct nbfm_demod { fm_demod_mode_t mode; };
nbfm_demod_t* nb_demod_create(const nbfm_demod_config_t* config);
void nb_demod_reset(nbfm_demod_t* dsp);
int nb_demod_set_audio(nbfm_demod_t* dsp, float tau, float gain);
int nb_demod_set_deviation(nbfm_demod_t* dsp, float deviation_hz);
nbfm_demod_result_t nb_demod_process_with_raw(nbfm_demod_t* dsp,
    const iq16_t* input, size_t count, int16_t* output, float* raw,
    size_t capacity, double correction);
nbfm_demod_t* wb_demod_create(const nbfm_demod_config_t* config);
void wb_demod_reset(nbfm_demod_t* dsp);
int wb_demod_set_audio(nbfm_demod_t* dsp, float tau, float gain);
nbfm_demod_result_t wb_demod_process_with_raw(nbfm_demod_t* dsp,
    const iq16_t* input, size_t count, int16_t* output, float* raw,
    size_t capacity, double correction);
