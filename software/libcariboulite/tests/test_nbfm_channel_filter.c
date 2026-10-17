#include "nbfm_demod.h"
#include "nbfm_channel_filter.h"
#include "nbfm_channel_coeffs.h"
#include "math_compat.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double channel_response(unsigned rate, double frequency)
{
    nbfm_channel_filter_t filter;
    nbfm_channel_filter_init(&filter,rate);
    double re=0, im=0;
    size_t measured=0;
    for (size_t n=0; n<rate/50; ++n) {
        double phase=2*M_PI*frequency*n/rate;
        iq16_t sample={(int16_t)lrint(12000*cos(phase)),
                       (int16_t)lrint(12000*sin(phase))};
        float i, q;
        if (nbfm_channel_filter_push(&filter,sample,&i,&q) && n>=rate/100) {
            re+=i*cos(phase)+q*sin(phase);
            im+=q*cos(phase)-i*sin(phase);
            ++measured;
        }
    }
    assert(measured==500);
    return hypot(re,im)/(12000*measured);
}

static void coefficient_response(void)
{
    // Evaluate the committed float coefficients independently, including the
    // whole 9-to-100 kHz stopband rather than a few selected test frequencies.
    double pass_min=1, pass_max=1, stop_max=0;
    for (unsigned frequency=0; frequency<=100000; frequency+=25) {
        double response=nbfm_channel_coeffs[NBFM_CHANNEL_TAPS/2];
        for (unsigned n=0; n<NBFM_CHANNEL_TAPS/2; ++n)
            response+=2*nbfm_channel_coeffs[n]*cos(2*M_PI*frequency*
                ((int)n-(int)(NBFM_CHANNEL_TAPS/2))/200000);
        response=fabs(response);
        if (frequency<=6000) {
            if (response<pass_min) pass_min=response;
            if (response>pass_max) pass_max=response;
        }
        if (frequency>=9000 && response>stop_max) stop_max=response;
    }
    double ripple=20*log10(pass_max/pass_min);
    double rejection=-20*log10(stop_max);
    printf("FIR coefficients: 0-to-6 kHz ripple %.4f dB, 9-to-100 kHz rejection %.2f dB\n",ripple,rejection);
    assert(ripple<0.05 && rejection>70);
}

static void measured_response(unsigned rate)
{
    const double pass[]={0,3000,-3000,5000,-5000,5500,-5500,6000,-6000};
    const double stop[]={9000,-9000,12500,-12500,18000,25000,44000,
        56000,100000,194000,206000};
    double dc=channel_response(rate,0);
    double worst_pass=0, worst_stop=0;
    for (size_t n=0; n<sizeof(pass)/sizeof(*pass); ++n) {
        double response=channel_response(rate,pass[n])/dc;
        double deviation=fabs(20*log10(response));
        if (deviation>worst_pass) worst_pass=deviation;
        assert(deviation<0.10);
    }
    for (size_t n=0; n<sizeof(stop)/sizeof(*stop); ++n) {
        double response=channel_response(rate,stop[n])/dc;
        if (response>worst_stop) worst_stop=response;
        assert(response<0.000316228); // At least 70 dB, including RF aliases.
    }
    printf("%u Hz channel: pass loss <=%.4f dB through +/-6 kHz; measured stop rejection >=%.2f dB\n",
        rate,worst_pass,-20*log10(worst_stop));
}

static void full_range_dc(unsigned rate)
{
    nbfm_channel_filter_t filter;
    nbfm_channel_filter_init(&filter,rate);
    double worst=0;
    for (size_t n=0; n<rate/10; ++n) {
        int reverse=n>=rate/20;
        iq16_t sample=reverse ? (iq16_t){-32768,32767} : (iq16_t){32767,-32768};
        float i, q;
        if (nbfm_channel_filter_push(&filter,sample,&i,&q) &&
            n%(rate/20)>rate/500) {
            double error=fmax(fabs(i-sample.i),fabs(q-sample.q));
            if (error>worst) worst=error;
            assert(error<0.1);
        }
    }
    printf("%u Hz CIC3: full-range positive/negative DC survives repeated integrator wraps (error %.4f S16 units)\n",rate,worst);
}

// Measurements use the public production demodulator, including the actual
// channel filter, limiter, discriminator and 50-to-48 kHz interpolation.
static void make_fm(iq16_t* input, size_t count, unsigned rate,
                    double voice_hz, double offset_hz, double blocker)
{
    double desired_phase = 0;
    double blocker_phase = 0;
    const double amplitude = 1800;
    for (size_t n=0; n<count; ++n) {
        double audio = sin(2*M_PI*voice_hz*n/rate);
        desired_phase = remainder(desired_phase +
            2*M_PI*(offset_hz+2500*audio)/rate, 2*M_PI);
        blocker_phase = remainder(blocker_phase + 2*M_PI*12500/rate, 2*M_PI);
        input[n] = (iq16_t){
            (int16_t)lrint(amplitude*(cos(desired_phase)+blocker*cos(blocker_phase))),
            (int16_t)lrint(amplitude*(sin(desired_phase)+blocker*sin(blocker_phase)))};
    }
}

static size_t demodulate(nbfm_demod_t* state, const iq16_t* input, size_t count,
                         int16_t* pcm, float* raw, size_t capacity, int chunks)
{
    static const size_t sizes[] = {1, 17, 83, 4093, 3, 211, 10007};
    static const size_t caps[] = {0, 1, 2, 7, 480, 3, 127};
    size_t used=0, produced=0, calls=0;
    while (used<count) {
        size_t n = chunks ? sizes[calls%7] : count-used;
        size_t cap = chunks ? caps[calls%7] : capacity-produced;
        if (n>count-used) n=count-used;
        if (cap>capacity-produced) cap=capacity-produced;
        nbfm_demod_result_t result = nbfm_demod_process_with_raw(state, input+used,
            n, pcm+produced, raw ? raw+produced : NULL, cap, 0);
        assert(!result.error && result.consumed<=n && result.produced<=cap);
        if (cap) assert(result.consumed>0); else assert(!result.consumed && !result.produced);
        used += result.consumed;
        produced += result.produced;
        ++calls;
        assert(produced<capacity || used==count);
    }
    return produced;
}

static double tone_measure(const float* audio, size_t count, double voice_hz,
                           double* distortion)
{
    // Ignore FIR/audio startup. Fit sin, cos and DC so channel delay and tuning
    // offset do not count as distortion. The retained window spans full cycles.
    size_t start=960;
    assert(count>start+4800);
    count = ((count-start)/4800)*4800;
    double sine=0, cosine=0, dc=0, energy=0;
    for (size_t n=0; n<count; ++n) {
        double y=audio[start+n];
        double phase=2*M_PI*voice_hz*n/48000;
        sine+=y*sin(phase); cosine+=y*cos(phase); dc+=y; energy+=y*y;
    }
    sine*=2.0/count; cosine*=2.0/count; dc/=count;
    double amplitude=hypot(sine,cosine);
    double remaining=fmax(0,energy/count-dc*dc-0.5*amplitude*amplitude);
    *distortion=sqrt(2*remaining)/amplitude;
    return amplitude;
}

static void streaming(unsigned rate)
{
    size_t count=rate/10+13, capacity=4804;
    iq16_t* input=malloc(count*sizeof(*input));
    int16_t* a_pcm=malloc(capacity*sizeof(*a_pcm));
    int16_t* b_pcm=malloc(capacity*sizeof(*b_pcm));
    float* a_raw=malloc(capacity*sizeof(*a_raw));
    float* b_raw=malloc(capacity*sizeof(*b_raw));
    assert(input && a_pcm && b_pcm && a_raw && b_raw);
    make_fm(input,count,rate,600,500,0);
    nbfm_demod_config_t config={rate,48000,50e-6f,8000};
    nbfm_demod_t *a=nbfm_demod_create(&config), *b=nbfm_demod_create(&config);
    assert(a && b);
    size_t n=demodulate(a,input,count,a_pcm,a_raw,capacity,0);
    size_t m=demodulate(b,input,count,b_pcm,b_raw,capacity,1);
    assert(n==m && !memcmp(a_pcm,b_pcm,n*sizeof(*a_pcm)) &&
        !memcmp(a_raw,b_raw,n*sizeof(*a_raw)));
    // A restart must clear both RF filter histories and partial decimation,
    // even after an arbitrary stopping point within a CIC/FIR interval.
    nbfm_demod_reset(a);
    nbfm_demod_destroy(b); b=nbfm_demod_create(&config); assert(b);
    n=demodulate(a,input,count,a_pcm,a_raw,capacity,1);
    m=demodulate(b,input,count,b_pcm,b_raw,capacity,0);
    assert(n==m && !memcmp(a_pcm,b_pcm,n*sizeof(*a_pcm)) &&
        !memcmp(a_raw,b_raw,n*sizeof(*a_raw)));
    nbfm_demod_destroy(a); nbfm_demod_destroy(b);
    free(input); free(a_pcm); free(b_pcm); free(a_raw); free(b_raw);
    printf("PASS %u Hz: arbitrary chunks/capacities and reset match a fresh receiver\n",rate);
}

static void voice_and_blocker(unsigned rate)
{
    size_t count=rate/5, capacity=9601;
    iq16_t* input=malloc(count*sizeof(*input));
    int16_t* pcm=malloc(capacity*sizeof(*pcm));
    float* raw=malloc(capacity*sizeof(*raw));
    assert(input && pcm && raw);
    nbfm_demod_config_t config={rate,48000,0,8000};
    nbfm_demod_t* state=nbfm_demod_create(&config); assert(state);
    for (unsigned voice=0; voice<2; ++voice) {
        double frequency=voice ? 3000 : 600;
        double clean_amplitude=0, clean_distortion=0;
        for (int scenario=0; scenario<4; ++scenario) {
            double offset=scenario==1 ? -500 : scenario==2 ? 500 : 0;
            double blocker=scenario==3 ? 10 : 0;
            make_fm(input,count,rate,frequency,offset,blocker);
            nbfm_demod_reset(state);
            size_t n=demodulate(state,input,count,pcm,raw,capacity,0);
            double distortion=0;
            double amplitude=tone_measure(raw,n,frequency,&distortion);
            if (!scenario) { clean_amplitude=amplitude; clean_distortion=distortion; }
            printf("%u Hz: %.0f Hz voice, %+.0f Hz tuning, adjacent %.0f dB: amplitude %.4f, residual %.2f%%\n",
                rate,frequency,offset,blocker?20*log10(blocker):0,amplitude,100*distortion);
            // A full-deviation tone must recover near unit amplitude. The old
            // small-angle formula loses approximately 5% and fails this check.
            assert(amplitude>0.97 && amplitude<1.03);
            assert(fabs(amplitude/clean_amplitude-1)<0.01);
            // Correct interpolation timing keeps deterministic raw residual
            // below 7.5% at 3 kHz and 1% at 600 Hz. Reversed endpoint weighting
            // produces about 23% and 4.4%, respectively, and fails both bounds.
            assert(distortion<(voice ? 0.075 : 0.01));
            assert(distortion<clean_distortion+0.015);
        }
    }
    nbfm_demod_destroy(state); free(input); free(pcm); free(raw);
}

static void cpu_budget(unsigned rate)
{
    size_t count=rate/100;
    iq16_t* input=malloc(count*sizeof(*input));
    assert(input);
    make_fm(input,count,rate,600,500,0);
    nbfm_demod_config_t config={rate,48000,50e-6f,8000};
    nbfm_demod_t* state=nbfm_demod_create(&config); assert(state);
    int16_t pcm[481];
    for (unsigned n=0; n<4; ++n) demodulate(state,input,count,pcm,NULL,481,0);
    double cpu=0, peak=0;
    for (unsigned n=0; n<50; ++n) {
        clock_t start=clock();
        demodulate(state,input,count,pcm,NULL,481,0);
        double elapsed=(double)(clock()-start)/CLOCKS_PER_SEC;
        cpu+=elapsed;
        if (elapsed>peak) peak=elapsed;
    }
    printf("%u Hz CPU: mean %.3f ms, maximum %.3f ms per 10 ms RF block\n",
        rate,1000*cpu/50,1000*peak);
    // CPU time excludes scheduler delays. The average must fit the actual
    // 10 ms audio deadline; peak is reported for review without a flaky limit.
    assert(cpu/50<0.010);
    nbfm_demod_destroy(state); free(input);
}

int main(void)
{
    setbuf(stdout,NULL);
    coefficient_response();
    for (unsigned rate=1000000; rate<=4000000; rate*=2) {
        measured_response(rate);
        full_range_dc(rate);
        streaming(rate);
        voice_and_blocker(rate);
        cpu_budget(rate);
    }
    puts("Production NBFM channel/filter FM checks pass at 1/2/4 MS/s");
}
