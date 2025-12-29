#include "nbfm4m_mod.h"
#include <stdlib.h>
#include <math.h>
#include "math_compat.h"

typedef struct { double a0,a1; float x1; } preemph_t;
static void preemph_init(preemph_t* p,double fs,double tau){
    if(tau<=0){p->a0=1;p->a1=0;p->x1=0;return;}
    double T=1.0/fs, alpha=tau/(tau+T);
    p->a0=1.0+alpha; p->a1=-alpha; p->x1=0;
}
static inline float preemph_run(preemph_t* p,float x){
    float y=(float)p->a0*x+(float)p->a1*p->x1; p->x1=x; return y;
}

struct nbfm4m_mod {
    double fs_a, fs_rf, f_dev, R, a_to_rf, k;
    float  out_scale; int lin;
    double phase, dphi_cur, dphi_next, interp_step, interp_acc;
    preemph_t pe;
    float* afifo; size_t cap, head, tail, cnt;
    
    //new
    uint32_t lm_phase;
    int use_lin;
};

nbfm4m_mod_t* nbfm4m_create(const nbfm4m_cfg_t* c){
    nbfm4m_mod_t* m=(nbfm4m_mod_t*)calloc(1,sizeof(*m));
    m->fs_a=c?c->audio_fs:48000.0; m->fs_rf=c?c->rf_fs:4000000.0;
    m->f_dev=c?c->f_dev_hz:2500.0; m->out_scale=c?c->out_scale:12000.0f;
    m->lin=c?c->linear_interp:1; m->R=m->fs_rf/m->fs_a; m->a_to_rf=1.0/m->R;
    m->k=2.0*M_PI*m->f_dev/m->fs_rf; preemph_init(&m->pe,m->fs_a,c?c->preemph_tau_s:0.0);
    m->interp_acc=1.0; m->cap=4096; m->afifo=(float*)calloc(m->cap,sizeof(float));
    
    //new
    m->lm_phase = 0; m->use_lin = m->lin;
    return m;
}
void nbfm4m_destroy(nbfm4m_mod_t* m) { 
    if(!m)return; 
    free(m->afifo); 
    free(m); 
}

size_t nbfm4m_push_audio(nbfm4m_mod_t* m,const float* a,size_t N) {
    size_t p=0; 
    for(size_t n=0;n<N;n++) {
        if(m->cnt == m->cap) break;
        m->afifo[m->tail] = a[n]; 
        m->tail=(m->tail+1)%m->cap; 
        m->cnt++; p++; 
    } 
    return p;
}
static int fetch_audio(nbfm4m_mod_t* m) {
    if (m->cnt == 0) return 0;

    float x = m->afifo[m->head];
    m->head = (m->head+1)%m->cap; 
    m->cnt--; 
    
    if (x>1) x=1; 
    else if (x<-1) x=-1;
    
    float xp=preemph_run(&m->pe,x); 
    m->dphi_next=m->k*(double)xp; 
    return 1;
}

size_t nbfm4m_pull_iq(nbfm4m_mod_t* m, iq16_t* dst, size_t N)
{
    const uint32_t L = 250;   // up by 250
    const uint32_t M = 3;     // down by 3
    size_t out = 0;

    while (out < N) {
        // Advance resampler phase (exact integer arithmetic)
        m->lm_phase += M;
        if (m->lm_phase >= L) {
            m->lm_phase -= L;

            // Move current -> next and fetch next audio-derived freq
            m->dphi_cur = m->dphi_next;
            if (!fetch_audio(m)) {
                // no new audio; hold frequency (rare if producer keeps up)
                m->dphi_next = m->dphi_cur;
            }
        }

        // Linear interpolation of frequency between audio ticks (optional)
        double frac = m->use_lin ? (double)m->lm_phase / (double)L : 0.0;
        double dphi = m->dphi_cur + (m->dphi_next - m->dphi_cur) * frac;

        // Integrate to phase with robust wrap
        m->phase += dphi;
        if (m->phase >  M_PI) m->phase -= 2.0 * M_PI;
        if (m->phase < -M_PI) m->phase += 2.0 * M_PI;

        float ci = (float)cos(m->phase);
        float sq = (float)sin(m->phase);
        int32_t I = (int32_t)lrintf(ci * m->out_scale);
        int32_t Q = (int32_t)lrintf(sq * m->out_scale);
        if (I >  32767) I =  32767; else if (I < -32768) I = -32768;
        if (Q >  32767) Q =  32767; else if (Q < -32768) Q = -32768;

        dst[out].i = (int16_t)I;
        dst[out].q = (int16_t)Q;
        out++;
    }
    return out;
}