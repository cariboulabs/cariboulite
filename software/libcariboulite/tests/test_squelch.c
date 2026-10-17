#define _GNU_SOURCE
#include "nbfm_demod.h"
#include "noise_squelch.h"
#include "carrier_squelch.h"
#include "demod_worker.h"
#include "pipeline_transport.h"
#include "nbfm_mod.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned block, out_block, rate, mode, seed;
static double energy[4];
static unsigned rms[4], detector_open[4], metric_valid[4], effective_open[4];
static nbfm_demod_ctrl_t* worker;
static nbfm_mod_t* mod;
static double phase;
static const char* test_sink_state(audio_sink_t* sink) { (void)sink; return "TEST"; }
static const audio_sink_ops_t test_sink_ops = {.state=test_sink_state};
int set_rt_and_affinity_prio(int prio, int cpu) { (void)prio; (void)cpu; return 0; }
int set_rt_and_affinity_prio_record(int prio, int cpu, pipeline_thread_diagnostics_t* d)
{
    if (d) {
        d->status=(pipeline_thread_status_t){.valid=true,.requested_priority=prio,
            .requested_cpu=cpu,.applied_cpu=cpu,.policy=SCHED_OTHER,.priority=0};
        atomic_store_explicit(&d->ready,true,memory_order_release);
    }
    return set_rt_and_affinity_prio(prio,cpu);
}
bool rf10_fifo_get(rf10_fifo_t* f, rf10_frame_t* frame, int timeout)
{
    (void)f; (void)timeout;
    assert(worker && atomic_load(&worker->alive));
    if (block == 240) { worker->active = false; return false; }
    unsigned stage = block / 60;
    if (block % 60 == 59) {
        rms[stage] = atomic_load(&worker->noise_squelch_rms_milli);
        detector_open[stage] = atomic_load(&worker->noise_squelch_detector_open);
        metric_valid[stage] = atomic_load(&worker->noise_squelch_valid);
        effective_open[stage] = atomic_load(&worker->squelch_open);
    }
    if (mode == 6 && block % 60 == 0) {
        uint32_t levels;
        assert(noise_squelch_pack_levels(stage == 1 ? 0.01f : 0.12f,
                                         stage == 1 ? 0.02f : 0.18f, &levels));
        if (stage == 3) levels = (180u << 16) | 120u; // malformed update ignored
        atomic_store(&worker->noise_squelch_levels, levels);
    }
    float audio[480];
    for (unsigned i=0; i<480; ++i) {
        audio[i] = (mode == 6 ? 1.0f : 0.4f) * sinf(phase);
        phase += 2*M_PI*(mode == 6 ? 3000 : 600)/48000;
    }
    nbfm_result_t r=nbfm_process(mod,audio,480,frame->data,rate/100);
    assert(r.consumed==480 && r.produced==rate/100 && !r.error);
    if ((mode == 0 || mode == 3 || mode == 4) && stage == 1) {
        // Broadband RF noise, not just a manufactured audio detection signal.
        for (unsigned i=0;i<rate/100;++i) {
            seed=seed*1664525u+1013904223u; frame->data[i].i=(int16_t)(seed>>16);
            seed=seed*1664525u+1013904223u; frame->data[i].q=(int16_t)(seed>>16);
        }
    }
    frame->rssi_valid = !(mode == 1 && stage == 3);
    frame->rssi_dbm = (mode == 3 ? stage == 3 : stage == 1 || (mode == 2 && stage == 3)) ? -115 : -80;
    // Exercise live bypass while noise is enabled and carrier blocks audio.
    if (mode == 2 && stage == 3) atomic_store(&worker->squelch_flags,0);
    ++block;
    return true;
}
bool aud10_fifo_put(aud10_fifo_t* f, const aud10_frame_t* frame, int timeout)
{
    (void)f; (void)timeout;
    unsigned stage=out_block/60, offset=out_block%60;
    if (offset>=40 && stage<4)
        for (unsigned i=0;i<480;++i) energy[stage]+=(double)frame->pcm[i]*frame->pcm[i];
    ++out_block;
    return true;
}
void aud10_fifo_peek_depth(aud10_fifo_t* f,size_t* count,size_t* cap)
{ (void)f; *count=12; *cap=24; }

static void detectors(void)
{
    uint32_t levels = 12345;
    float open_rms = -1, close_rms = -1;
    assert(noise_squelch_pack_levels(0.12f, 0.18f, &levels));
    assert(levels == ((120u << 16) | 180u));
    assert(noise_squelch_unpack_levels(levels, &open_rms, &close_rms));
    assert(fabsf(open_rms - 0.12f) < 1e-6f && fabsf(close_rms - 0.18f) < 1e-6f);
    assert(noise_squelch_pack_levels(NOISE_SQUELCH_OPEN_RMS, NOISE_SQUELCH_CLOSE_RMS, &levels));
    assert(levels == ((200u << 16) | 300u));
    assert(levels == noise_squelch_default_levels());
    assert(noise_squelch_unpack_levels(0, &open_rms, &close_rms));
    assert(open_rms == NOISE_SQUELCH_OPEN_RMS && close_rms == NOISE_SQUELCH_CLOSE_RMS);
    const float invalid[][2] = {{0,.18f},{-.1f,.18f},{.18f,.12f},{.18f,.18f},
        {.12f,8.001f},{NAN,.18f},{.12f,NAN},{INFINITY,.18f},{.12f,INFINITY}};
    for (unsigned i=0; i<sizeof(invalid)/sizeof(invalid[0]); ++i) {
        levels = 12345;
        assert(!noise_squelch_pack_levels(invalid[i][0], invalid[i][1], &levels));
        assert(levels == 12345);
    }
    assert(!noise_squelch_pack_levels(.1201f, .1202f, &levels)); // quantization collapses pair
    assert(!noise_squelch_unpack_levels((180u << 16) | 120u, &open_rms, &close_rms));
    assert(!noise_squelch_unpack_levels((120u << 16) | 8001u, &open_rms, &close_rms));
    assert(!noise_squelch_unpack_levels(180u, &open_rms, &close_rms));
    assert(noise_squelch_pack_levels(7.0f, 8.0f, &levels));
    assert(!noise_squelch_pack_levels(.12f, .18f, NULL));
    assert(!noise_squelch_unpack_levels(0, NULL, &close_rms));
    noise_squelch_t n={0};
    for(unsigned i=0;i<1439;++i) assert(!noise_squelch_process(&n,0));
    assert(noise_squelch_process(&n,0));
    for(unsigned i=0;i<24000;++i) noise_squelch_process(&n,0.8f*sinf(2*M_PI*3000*i/48000));
    assert(n.open); // speech-band energy must not trip the detector
    for(unsigned i=0;i<2400;++i) noise_squelch_process(&n,0.8f*sinf(2*M_PI*10000*i/48000));
    assert(n.open); // brief noise burst does not close the gate
    for(unsigned i=0;i<24000;++i) noise_squelch_process(&n,0.8f*sinf(2*M_PI*10000*i/48000));
    assert(!n.open);
    for(unsigned i=0;i<24000;++i) noise_squelch_process(&n,0.4f*sinf(2*M_PI*600*i/48000));
    assert(n.open);
    noise_squelch_t old = n;
    n.qualify = 123;
    assert(noise_squelch_set_thresholds(&n, .14f, .20f));
    assert(n.open == old.open && n.power == old.power && n.qualify == 0);
    assert(!memcmp(n.z1, old.z1, sizeof(n.z1)) && !memcmp(n.z2, old.z2, sizeof(n.z2)));
    n.qualify = 123;
    assert(noise_squelch_set_thresholds(&n, .14f, .20f));
    assert(n.qualify == 123); // unchanged update must not postpone opening/closing
    old = n;
    for (unsigned i=0; i<sizeof(invalid)/sizeof(invalid[0]); ++i) {
        assert(!noise_squelch_set_thresholds(&n, invalid[i][0], invalid[i][1]));
        assert(!memcmp(&n, &old, sizeof(n)));
    }
    assert(!noise_squelch_process(&n,NAN));
    assert(!noise_squelch_process(&n,INFINITY));
    assert(n.open_power == .14f*.14f && n.close_power == .20f*.20f);
    noise_squelch_reset(&n);
    assert(n.open_power == NOISE_SQUELCH_OPEN_RMS*NOISE_SQUELCH_OPEN_RMS);
    carrier_squelch_t c={0};
    assert(!carrier_squelch_process(&c,-97,true));
    assert(!carrier_squelch_process(&c,-97,true));
    assert(carrier_squelch_process(&c,-97,true));
    for(unsigned i=0;i<50;++i) assert(carrier_squelch_process(&c,-100,true));
    for(unsigned i=0;i<50;++i) assert(carrier_squelch_process(&c,-102,true)); // exact close boundary retains state
    for(unsigned i=0;i<14;++i) assert(carrier_squelch_process(&c,-103,true));
    assert(!carrier_squelch_process(&c,-103,true));
    for(unsigned i=0;i<50;++i) assert(!carrier_squelch_process(&c,-100,true));
    for(unsigned i=0;i<3;++i) carrier_squelch_process(&c,-80,true);
    assert(c.open && !carrier_squelch_process(&c,127,true));
    assert(!carrier_squelch_process(&c,NAN,true));
    assert(!carrier_squelch_process(&c,-80,false));
}
static void measured_noise_levels(void)
{
    // Deterministic detector-band tones represent measured weak activity
    // (about 0.15-0.18 RMS) and idle noise (about 1.2 RMS). Their detector RMS is
    // checked directly; this is gate behavior, not a receiver sensitivity test.
    noise_squelch_t current = {0}, previous = {0};
    assert(noise_squelch_set_thresholds(&previous, .12f, .18f));
    for (unsigned i=0; i<24000; ++i) {
        float envelope = .26f + .015f*sinf(2*M_PI*2*i/48000);
        float weak = envelope*sinf(2*M_PI*10000*i/48000);
        noise_squelch_process(&current, weak);
        assert(!noise_squelch_process(&previous, weak));
    }
    float measured = sqrtf(current.power);
    assert(measured > .15f && measured < .18f);
    assert(current.open && !previous.open);
    for (unsigned i=0; i<2400; ++i)
        assert(noise_squelch_process(&current, 1.84f*sinf(2*M_PI*10000*i/48000)));
    for (unsigned i=2400; i<24000; ++i)
        noise_squelch_process(&current, 1.84f*sinf(2*M_PI*10000*i/48000));
    measured = sqrtf(current.power);
    assert(measured > 1.15f && measured < 1.25f && !current.open);
}
static void raw_tap(void)
{
    nbfm_demod_config_t a={rate,48000,50e-6f,8000}, b={rate,48000,0,1234};
    nbfm_demod_t *d1=nbfm_demod_create(&a), *d2=nbfm_demod_create(&b);
    assert(d1 && d2);
    iq16_t iq[40000];
    for(unsigned i=0;i<rate/100;++i) {
        double ph=0.5*sin(2*M_PI*600*i/rate);
        iq[i]=(iq16_t){(int16_t)(10000*cos(ph)),(int16_t)(10000*sin(ph))};
    }
    float r1[480],r2[480]; int16_t p1[480],p2[480];
    nbfm_demod_result_t x=nbfm_demod_process_with_raw(d1,iq,rate/100,p1,r1,480,0);
    nbfm_demod_result_t y=nbfm_demod_process_with_raw(d2,iq,rate/100,p2,r2,480,0);
    assert(!x.error && !y.error && x.consumed==y.consumed && x.produced==y.produced);
    assert(!memcmp(r1,r2,x.produced*sizeof(float))); // independent of gain and filtering
    assert(memcmp(p1,p2,x.produced*sizeof(int16_t)));
    nbfm_demod_destroy(d1); nbfm_demod_destroy(d2);
}
static void receive_deviation(void)
{
    // Leave headroom for the initial channel-filter transient so clipping
    // cannot mask the expected audio scaling relationship.
    nbfm_demod_config_t config={rate,48000,0,1000};
    audio_demod_t* nominal=audio_demod_create(AUDIO_DEMOD_NBFM, &config);
    audio_demod_t* explicit_default=audio_demod_create(AUDIO_DEMOD_NBFM, &config);
    nbfm_demod_t* wider=nbfm_demod_create(&config);
    audio_demod_t* broadcast=audio_demod_create(AUDIO_DEMOD_WBFM, &config);
    assert(nominal && explicit_default && wider && broadcast);
    assert(audio_demod_set_nbfm_deviation(NULL,2500)==-EINVAL);
    assert(nbfm_demod_set_deviation(NULL,2500)==-EINVAL);
    assert(audio_demod_set_nbfm_deviation(broadcast,2500)==-EINVAL);
    assert(nbfm_demod_set_deviation(broadcast,2500)==-EINVAL);
    assert(!audio_demod_set_nbfm_deviation(explicit_default,NBFM_RX_MIN_DEVIATION_HZ));
    assert(!audio_demod_set_nbfm_deviation(explicit_default,NBFM_RX_MAX_DEVIATION_HZ));
    assert(!audio_demod_set_nbfm_deviation(explicit_default,NBFM_DEFAULT_DEVIATION_HZ));
    assert(!nbfm_demod_set_deviation(wider,2*NBFM_DEFAULT_DEVIATION_HZ));

    iq16_t iq[40000];
    for (unsigned i=0;i<rate/100;++i) {
        double ph=2500.0/600*cos(2*M_PI*600*i/rate);
        iq[i]=(iq16_t){(int16_t)(11000*cos(ph)),(int16_t)(11000*sin(ph))};
    }
    const float invalid[]={0,-0.0f,-1,0.999f,24000.1f,25000,NAN,INFINITY,-INFINITY};
    double nominal_energy=0,wider_energy=0;
    for (unsigned part=0;part<12;++part) {
        if (part==3 || part==8) {
            for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
                assert(audio_demod_set_nbfm_deviation(wider,invalid[i])==-EINVAL);
                assert(nbfm_demod_set_deviation(wider,invalid[i])==-EINVAL);
            }
        }
        if (part==6) {
            audio_demod_reset(nominal);
            audio_demod_reset(explicit_default);
            nbfm_demod_reset(wider);
        }
        float raw1[512],raw2[512],raw3[512];
        int16_t pcm1[512],pcm2[512],pcm3[512];
        audio_demod_result_t x=audio_demod_process_with_raw(nominal,iq,rate/100,pcm1,raw1,512,0);
        audio_demod_result_t y=audio_demod_process_with_raw(explicit_default,iq,rate/100,pcm2,raw2,512,0);
        nbfm_demod_result_t z=nbfm_demod_process_with_raw(wider,iq,rate/100,pcm3,raw3,512,0);
        assert(!x.error && !y.error && !z.error && x.consumed==rate/100);
        assert(x.consumed==y.consumed && x.consumed==z.consumed);
        assert(x.produced==y.produced && x.produced==z.produced);
        assert(!memcmp(raw1,raw2,x.produced*sizeof(float)));
        assert(!memcmp(raw1,raw3,x.produced*sizeof(float)));
        assert(!memcmp(pcm1,pcm2,x.produced*sizeof(int16_t)));
        for (size_t i=0;i<x.produced;++i) {
            // Twice the expected deviation halves recovered audio, within
            // independent signed-16-bit rounding. The raw detector tap stays
            // identical through rejected updates and signal-history resets.
            assert(fabs((double)pcm1[i]-2.0*pcm3[i])<=1.0);
            nominal_energy+=(double)pcm1[i]*pcm1[i];
            wider_energy+=(double)pcm3[i]*pcm3[i];
        }
    }
    assert(nominal_energy>1e8 && wider_energy>1e8);
    assert(fabs(sqrt(nominal_energy/wider_energy)-2.0)<0.001);
    audio_demod_destroy(nominal);
    audio_demod_destroy(explicit_default);
    nbfm_demod_destroy(wider);
    audio_demod_destroy(broadcast);
    printf("rate=%u configurable RX deviation preserves raw squelch scale and reset setting\n",rate);
}
static void integration(void)
{
    rf10_fifo_t rf={0}; aud10_fifo_t af={0};
    audio_sink_t sink={.sample_rate=48000,.ops=&test_sink_ops};
    nbfm_demod_ctrl_t c={.active=true,.fifo_in=&rf,.afifo_out=&af,
        .sink=&sink,.fs_rf=rate,.fs_audio=48000,.pcm_rate=48000,
        .pcm_gain=8000,.deemph_tau=50e-6f};
    unsigned flags = mode == 4 ? 0 : mode == 0 || mode == 5 || mode == 6 ? RX_SQUELCH_NOISE :
        mode == 1 ? RX_SQUELCH_CARRIER : RX_SQUELCH_NOISE|RX_SQUELCH_CARRIER;
    atomic_init(&c.alive,false);
    atomic_init(&c.last_error,0);
    atomic_init(&c.scheduling.ready,false);
    atomic_init(&c.squelch_flags, flags);
    atomic_init(&c.squelch_open, 0);
    atomic_init(&c.noise_squelch_levels, 0); // existing/offline callers retain defaults
    atomic_init(&c.noise_squelch_rms_milli, 0);
    atomic_init(&c.noise_squelch_detector_open, 0);
    atomic_init(&c.noise_squelch_valid, 0);
    nbfm_demod_config_t dc={rate,48000,50e-6f,8000};
    nbfm_cfg_t mc={48000,rate,2500,0,4000,1};
    c.mode = mode == 5 ? AUDIO_DEMOD_WBFM : AUDIO_DEMOD_NBFM;
    c.dsp=audio_demod_create(c.mode, &dc); mod=nbfm_create(&mc); assert(c.dsp && mod);
    worker=&c; block=out_block=0; phase=0; seed=123; memset(energy,0,sizeof(energy));
    memset(rms, 0, sizeof(rms)); memset(detector_open, 0, sizeof(detector_open));
    memset(metric_valid, 0, sizeof(metric_valid)); memset(effective_open, 0, sizeof(effective_open));
    nbfm_demod_thread(&c);
    assert(!atomic_load(&c.alive) && atomic_load(&c.scheduling.ready));
    assert(out_block>=239); // closed squelch must still enqueue audio on time
    if (mode < 4 || mode == 6) {
        assert(energy[0]>1e8 && energy[1]==0 && energy[2]>1e8);
        if(mode==1 || mode==3) assert(energy[3]==0); else assert(energy[3]>1e8);
    } else {
        for (unsigned stage=0; stage<4; ++stage) assert(energy[stage]>1e5);
    }
    for (unsigned stage=0; stage<4; ++stage) {
        assert(metric_valid[stage] == (mode != 5));
        if (mode == 5) assert(!rms[stage] && !detector_open[stage]);
    }
    if (mode == 4) {
        assert(rms[1] > NOISE_SQUELCH_CLOSE_RMS*1000 && !detector_open[1] && effective_open[1]);
        assert(rms[0] < NOISE_SQUELCH_OPEN_RMS*1000 && detector_open[0] && effective_open[0]);
    }
    if (mode == 6) {
        for (unsigned stage=0; stage<4; ++stage) {
            assert(rms[stage] > 20 && rms[stage] < 120);
            assert(detector_open[stage] == (stage != 1));
            assert(effective_open[stage] == (stage != 1));
        }
        // Live threshold changes do not reset DSP progress or the output queue.
        assert(c.pcm_total_frames == out_block * 480u);
    }
    printf("rate=%u mode=%u RMS=[%u %u %u %u]\n", rate, mode, rms[0], rms[1], rms[2], rms[3]);
    nbfm_demod_destroy(c.dsp); nbfm_destroy(mod);
}
int main(void)
{
    detectors();
    measured_noise_levels();
    for(rate=1000000;rate<=4000000;rate*=2) {
        raw_tap();
        receive_deviation();
        for(mode=0;mode<7;++mode) integration();
    }
    puts("Squelch detectors and RX worker integration pass at 1/2/4 MS/s");
}
