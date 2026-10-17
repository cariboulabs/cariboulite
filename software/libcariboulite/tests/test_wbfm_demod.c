#include "nbfm_demod.h"
#include "math_compat.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void signal(iq16_t* iq, size_t n, unsigned fs, double tone,
                   double deviation, double offset)
{
    double phase = 0;
    for (size_t j=0; j<n; ++j) {
        phase += 2*M_PI*(offset + deviation*sin(2*M_PI*tone*j/fs))/fs;
        phase = remainder(phase, 2*M_PI);
        iq[j].i = (int16_t)lrint(12000*cos(phase));
        iq[j].q = (int16_t)lrint(12000*sin(phase));
    }
}
static size_t process(nbfm_demod_t* dsp, iq16_t* iq, size_t n,
                      int16_t* pcm, int chunked, double correction)
{
    size_t used=0, out=0;
    while (used<n) {
        size_t count = chunked ? 1 + used%1009 : n;
        if (count>n-used) count=n-used;
        size_t capacity = chunked ? 1 + used%37 : 12000-out;
        nbfm_demod_result_t r = nbfm_demod_process(dsp,iq+used,count,pcm+out,capacity,correction);
        assert(!r.error && r.consumed && r.produced<=capacity);
        used+=r.consumed; out+=r.produced;
        assert(out<=12000);
    }
    return out;
}
static double rms(const int16_t* p, size_t n)
{
    double sum=0;
    for(size_t j=2400;j<n;++j) sum+=(double)p[j]*p[j];
    return sqrt(sum/(n-2400));
}
static double tone_amplitude(const int16_t* p, size_t n, double hz)
{
    double re=0,im=0;
    for(size_t j=2400;j<n;++j) {
        re+=p[j]*cos(2*M_PI*hz*j/48000);
        im+=p[j]*sin(2*M_PI*hz*j/48000);
    }
    return 2*hypot(re,im)/(n-2400);
}
static double tone_residual(const int16_t* p, size_t n, double hz)
{
    double re=0,im=0,sum=0;
    for(size_t j=2400;j<n;++j) {
        re+=p[j]*cos(2*M_PI*hz*j/48000);
        im+=p[j]*sin(2*M_PI*hz*j/48000);
    }
    re*=2.0/(n-2400); im*=2.0/(n-2400);
    for(size_t j=2400;j<n;++j) {
        double e=p[j]-re*cos(2*M_PI*hz*j/48000)-im*sin(2*M_PI*hz*j/48000);
        sum+=e*e;
    }
    return sqrt(sum/(n-2400));
}
int main(void)
{
    setbuf(stdout, NULL);
    for(unsigned fs=1000000;fs<=4000000;fs*=2) {
        size_t n=fs/5;
        iq16_t* iq=malloc(n*sizeof(*iq));
        int16_t a[12000], b[12000];
        nbfm_demod_config_t config={fs,48000,0,10000};
        nbfm_demod_t* dsp=wbfm_demod_create(&config);
        assert(dsp);
        signal(iq,n,fs,1000,75000,0);
        nbfm_demod_result_t empty=nbfm_demod_process(dsp,iq,n,NULL,0,0);
        assert(!empty.error && !empty.consumed && !empty.produced);
        for(int c=-1;c<=1;++c) {
            double correction=c*0.0005;
            nbfm_demod_reset(dsp);
            size_t na=process(dsp,iq,n,a,0,correction);
            nbfm_demod_reset(dsp);
            size_t nb=process(dsp,iq,n,b,1,correction);
            assert(na==nb && !memcmp(a,b,na*sizeof(*a)));
            assert(fabs((double)na - n*48000.0/fs*(1+correction))<=1.01);
        }
        nbfm_demod_reset(dsp);
        clock_t start=clock();
        size_t count=process(dsp,iq,n,a,0,0);
        double elapsed=(double)(clock()-start)/CLOCKS_PER_SEC;
        double amplitude=tone_amplitude(a,count,1000);
        assert(amplitude>9700 && amplitude<10200);
        double residual=tone_residual(a,count,1000);
        assert(residual/amplitude<0.01);
        printf("%u Hz: full-deviation 1 kHz amplitude %.1f, residual %.2f%%; %.3fs CPU / 0.2s IQ\n",
               fs,amplitude,100*residual/amplitude,elapsed);
        // Mono passband, pilot rejection, and stereo/RDS rejection before resampling.
        const double tones[]={14000,19000,38000,57000};
        for(size_t k=0;k<sizeof(tones)/sizeof(tones[0]);++k) {
            signal(iq,n,fs,tones[k],7500,0);
            nbfm_demod_reset(dsp);
            count=process(dsp,iq,n,a,0,0);
            double level=rms(a,count);
            if(k==0) {
                assert(level>650 && level<750);
                assert(tone_residual(a,count,tones[k])/level<0.01);
            }
            else assert(level<5);
            printf("  %.0f Hz multiplex component: audio RMS %.2f\n",tones[k],level);
        }
        // A stronger station at an alias frequency must be rejected before
        // IQ decimation, even though it would fold onto the wanted carrier.
        signal(iq,n,fs,1000,10000,250000);
        for(size_t j=0;j<n;++j) {
            iq[j].i=6000+2*iq[j].i;
            iq[j].q=2*iq[j].q;
        }
        nbfm_demod_reset(dsp);
        count=process(dsp,iq,n,a,0,0);
        assert(rms(a,count)<5);
        // De-emphasis is shared and must remain effective in wide mode.
        signal(iq,n,fs,10000,7500,0);
        nbfm_demod_reset(dsp);
        count=process(dsp,iq,n,a,0,0);
        double flat=rms(a,count);
        assert(!nbfm_demod_set_audio(dsp,50e-6f,10000));
        nbfm_demod_reset(dsp);
        count=process(dsp,iq,n,a,0,0);
        assert(rms(a,count)/flat>0.28 && rms(a,count)/flat<0.36);
        // Reset clears all wideband history, including partial decimation stages.
        nbfm_demod_t* fresh=wbfm_demod_create(&config);
        assert(fresh);
        assert(!nbfm_demod_set_audio(dsp,0,10000));
        nbfm_demod_reset(dsp);
        count=process(dsp,iq,n,a,1,0);
        size_t nb=process(fresh,iq,n,b,0,0);
        assert(count==nb && !memcmp(a,b,count*sizeof(*a)));
        nbfm_demod_destroy(fresh);
        nbfm_demod_destroy(dsp);
        free(iq);
    }
    puts("PASS: mono WBFM at 1/2/4 MS/s, streaming, correction, reset, de-emphasis and multiplex rejection");
}
