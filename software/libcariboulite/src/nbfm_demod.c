// Compatibility facade: dispatch once per block; mode state is private.
#include "fm_demod_internal.h"
#include <stdlib.h>
nbfm_demod_t* nbfm_demod_create(const nbfm_demod_config_t* c) { return nb_demod_create(c); }
nbfm_demod_t* wbfm_demod_create(const nbfm_demod_config_t* c) { return wb_demod_create(c); }
void nbfm_demod_destroy(nbfm_demod_t* s) { free(s); }
void nbfm_demod_reset(nbfm_demod_t* s)
{
    if (!s) return;
    if (s->mode == FM_MODE_WBFM) wb_demod_reset(s); else nb_demod_reset(s);
}
int nbfm_demod_set_audio(nbfm_demod_t* s, float tau, float gain)
{
    return s && s->mode == FM_MODE_WBFM ? wb_demod_set_audio(s,tau,gain) : nb_demod_set_audio(s,tau,gain);
}
int nbfm_demod_set_deviation(nbfm_demod_t* s, float deviation_hz)
{
    return nb_demod_set_deviation(s, deviation_hz);
}
nbfm_demod_result_t nbfm_demod_process_with_raw(nbfm_demod_t* s,
    const iq16_t* input, size_t count, int16_t* output, float* raw,
    size_t capacity, double correction)
{
    return s && s->mode == FM_MODE_WBFM
        ? wb_demod_process_with_raw(s,input,count,output,raw,capacity,correction)
        : nb_demod_process_with_raw(s,input,count,output,raw,capacity,correction);
}
nbfm_demod_result_t nbfm_demod_process(nbfm_demod_t* s,
    const iq16_t* input, size_t count, int16_t* output, size_t capacity,
    double correction)
{
    return nbfm_demod_process_with_raw(s,input,count,output,NULL,capacity,correction);
}

// Neutral audio-mode API. Legacy entry points retain their existing behavior.
#include <errno.h>
audio_demod_t* audio_demod_create(audio_demod_mode_t mode, const audio_demod_config_t* config)
{
    switch(mode) {
        case AUDIO_DEMOD_NBFM: return nbfm_demod_create(config);
        case AUDIO_DEMOD_WBFM: return wbfm_demod_create(config);
        default: errno=EINVAL; return NULL;
    }
}
unsigned audio_demod_mode_capabilities(audio_demod_mode_t mode)
{
    return mode == AUDIO_DEMOD_NBFM ? AUDIO_DEMOD_CAP_NOISE_SQUELCH : 0u;
}
unsigned audio_demod_capabilities(const audio_demod_t* s)
{
    return s ? audio_demod_mode_capabilities(s->mode) : 0u;
}
void audio_demod_destroy(audio_demod_t* s) { nbfm_demod_destroy(s); }
void audio_demod_reset(audio_demod_t* s) { nbfm_demod_reset(s); }
int audio_demod_set_audio(audio_demod_t* s, float tau, float gain)
{ return nbfm_demod_set_audio(s,tau,gain); }
int audio_demod_set_nbfm_deviation(audio_demod_t* s, float deviation_hz)
{ return nbfm_demod_set_deviation(s, deviation_hz); }
audio_demod_result_t audio_demod_process_with_raw(audio_demod_t* s,
    const iq16_t* input, size_t count, int16_t* output, float* raw,
    size_t capacity, double correction)
{ return nbfm_demod_process_with_raw(s,input,count,output,raw,capacity,correction); }
audio_demod_result_t audio_demod_process(audio_demod_t* s,
    const iq16_t* input, size_t count, int16_t* output, size_t capacity, double correction)
{ return audio_demod_process_with_raw(s,input,count,output,NULL,capacity,correction); }
