/* The frozen worker checks queue framing and clock control, not obsolete
 * boxcar PCM. Independent current workers check deterministic filtered PCM. */
#define nbfm_demod_ctrl_t legacy_demod_ctrl_t
#define nbfm_demod_thread legacy_demod_thread
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "fixtures/legacy_nbfm_demod/nbfm_demod.c"
#pragma GCC diagnostic pop
#undef nbfm_demod_ctrl_t
#undef nbfm_demod_thread
#include "nbfm_demod.h"
#include "demod_worker.h"
#include <assert.h>
#include <stdlib.h>

#define BLOCKS 180
#define AUDIO_CAP ((BLOCKS + 2) * 480)
static iq16_t waveform[40000];
static int16_t before[AUDIO_CAP], after[AUDIO_CAP];
static size_t frames, captured, rate, put_count;
static size_t old_puts[BLOCKS+2], old_depths[BLOCKS*3];
static size_t depth_calls;
static int16_t* capture;
static legacy_demod_ctrl_t* legacy;
static nbfm_demod_ctrl_t* current;
static bool reset_case;
int __wrap_clock_gettime(clockid_t id, struct timespec* ts)
{ (void)id; *ts=(struct timespec){0}; return 0; }
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
    if (!legacy) assert(current && atomic_load(&current->alive));
    if (frames == BLOCKS) {
        if (legacy) legacy->active=false; else current->active=false;
        return false;
    }
    if (reset_case && (frames == 57 || frames == 99)) {
        if (legacy) legacy->reset=true; else current->reset=true;
    }
    if (frames == 75) {
        if (legacy) { legacy->pcm_gain=12000; legacy->deemph_tau=75e-6f; }
        else { current->pcm_gain=12000; current->deemph_tau=75e-6f; }
    }
    if (frames == 130) {
        if (legacy) { legacy->pcm_gain=1000000; legacy->deemph_tau=0; }
        else { current->pcm_gain=1000000; current->deemph_tau=0; }
    }
    memcpy(frame->data,waveform,rate/100*sizeof(iq16_t));
    ++frames;
    return true;
}
bool aud10_fifo_put(aud10_fifo_t* f, const aud10_frame_t* frame, int timeout)
{
    (void)f; assert(timeout==10);
    assert(captured+480 <= AUDIO_CAP);
    memcpy(capture+captured,frame->pcm,sizeof(frame->pcm));
    captured+=480;
    ++put_count;
    if (legacy) old_puts[put_count-1]=frames;
    else assert(old_puts[put_count-1]==frames);
    return put_count%17 != 0; // preserve the existing timed-out-frame/drop behavior too
}
void aud10_fifo_peek_depth(aud10_fifo_t* f, size_t* count, size_t* cap)
{
    (void)f;
    // Include empty, full, engagement, slew, clamp and both emergency nudges.
    const size_t depths[]={0,0,9,12,18,24,24,12,2,0,12};
    *count=depths[(frames/9)%(sizeof(depths)/sizeof(*depths))]; *cap=24;
    assert(depth_calls < BLOCKS*3);
    if (legacy) old_depths[depth_calls]=put_count;
    else assert(old_depths[depth_calls]==put_count);
    ++depth_calls;
}
static void make_waveform(void)
{
    double phase=0;
    unsigned noise=123;
    for(size_t n=0;n<rate/100;++n) {
        noise=noise*1664525u+1013904223u;
        double audio=0.6*sin(2*M_PI*600*n/rate)+0.1*sin(2*M_PI*2100*n/rate);
        phase+=2*M_PI*2500/rate*audio;
        double amplitude=(n>rate/250 && n<rate/240)?0:11000;
        waveform[n].i=(int16_t)lrint(amplitude*cos(phase)+(int)(noise%101)-50);
        waveform[n].q=(int16_t)lrint(amplitude*sin(phase)+(int)((noise>>8)%101)-50);
    }
}
static void compare_workers(void)
{
    rf10_fifo_t rf={0}; aud10_fifo_t af={0}; audio_sink_t sink={0};
    legacy_demod_ctrl_t old={.active=true,.fifo_in=&rf,.afifo_out=&af,
        .sink=&sink,.fs_rf=rate,.fs_audio=48000,.pcm_rate=48000,
        .pcm_gain=8000,.deemph_tau=50e-6f,.prime_blocks_10ms=20};
    nbfm_demod_ctrl_t now={.active=true,.fifo_in=&rf,.afifo_out=&af,
        .sink=&sink,.fs_rf=rate,.fs_audio=48000,.pcm_rate=48000,
        .pcm_gain=8000,.deemph_tau=50e-6f,.prime_blocks_10ms=20};
    nbfm_demod_config_t config={rate,48000,50e-6f,8000};
    now.dsp=nbfm_demod_create(&config); assert(now.dsp);
    atomic_init(&now.alive,false);
    atomic_init(&now.last_error,0);
    atomic_init(&now.scheduling.ready,false);
    nbfm_demod_ctrl_t repeat=now;
    atomic_init(&repeat.alive,false);
    atomic_init(&repeat.last_error,0);
    atomic_init(&repeat.scheduling.ready,false);
    repeat.dsp=nbfm_demod_create(&config); assert(repeat.dsp);
    frames=captured=put_count=depth_calls=0; capture=before; legacy=&old; current=NULL;
    memset(old_puts,0,sizeof(old_puts));
    legacy_demod_thread(&old);
    size_t expected=captured, expected_depth_calls=depth_calls;
    frames=captured=put_count=depth_calls=0; capture=before; legacy=NULL; current=&now;
    nbfm_demod_thread(&now);
    assert(!atomic_load(&now.alive) && atomic_load(&now.scheduling.ready));
    assert(!atomic_load(&now.last_error));
    assert(expected==captured && expected_depth_calls==depth_calls);
    assert(old.pcm_total_frames==now.pcm_total_frames && old.priming==now.priming);
    assert(now.pcm_gain==1000000 && now.deemph_tau==0);
    frames=captured=put_count=depth_calls=0; capture=after; current=&repeat;
    nbfm_demod_thread(&repeat);
    assert(!atomic_load(&repeat.alive) && atomic_load(&repeat.scheduling.ready));
    assert(!atomic_load(&repeat.last_error));
    assert(expected==captured && expected_depth_calls==depth_calls);
    for(size_t i=0;i<captured;++i) {
        if(before[i]!=after[i]) {
            fprintf(stderr,"Independent worker PCM mismatch at %zu: %d != %d\n",i,before[i],after[i]);
            abort();
        }
    }
    assert(now.pcm_total_frames==repeat.pcm_total_frames && now.priming==repeat.priming);
    nbfm_demod_destroy(now.dsp);
    nbfm_demod_destroy(repeat.dsp);
    printf("PASS: %zu Hz, resets=%d, legacy queue framing and %zu deterministic filtered PCM samples\n",
        rate,reset_case,captured);
}
static void compare_blocks(double correction)
{
    nbfm_demod_config_t config={rate,48000,50e-6f,8000};
    nbfm_demod_t* a=nbfm_demod_create(&config); assert(a);
    nbfm_demod_t* b=nbfm_demod_create(&config); assert(b);
    size_t total=0;
    // Reset at a non-decimation boundary to exercise clearing partial filters.
    size_t cuts[]={13,rate/100-13,rate/100};
    for(size_t part=0;part<3;++part) {
        if(part==1) { nbfm_demod_reset(a); nbfm_demod_reset(b); }
        if(part==2) {
            assert(!nbfm_demod_set_audio(a,0,1000000));
            assert(!nbfm_demod_set_audio(b,0,1000000));
        }
        nbfm_demod_result_t r=nbfm_demod_process(a,waveform,cuts[part],before,AUDIO_CAP,correction);
        assert(!r.error && r.consumed==cuts[part]);
        size_t consumed=0,produced=0,iteration=0;
        while(consumed<cuts[part]) {
            const size_t sizes[]={1,17,83,4093,3,211};
            const size_t caps[]={1,2,7,480,3};
            size_t count=sizes[iteration%6]; if(count>cuts[part]-consumed) count=cuts[part]-consumed;
            nbfm_demod_result_t q=nbfm_demod_process(b,waveform+consumed,count,
                after+produced,caps[iteration%5],correction);
            assert(!q.error && q.consumed && q.consumed<=count && q.produced<=caps[iteration%5]);
            consumed+=q.consumed; produced+=q.produced; ++iteration;
        }
        assert(produced==r.produced && !memcmp(before,after,produced*sizeof(*before)));
        total+=produced;
    }
    nbfm_demod_destroy(a); nbfm_demod_destroy(b);
    printf("PASS: %zu Hz, correction=%g, varying input/capacity and partial-decimator reset (%zu samples)\n",rate,correction,total);
}
int main(void)
{
    nbfm_demod_config_t config={4000000,48000,50e-6f,8000};
    assert(!nbfm_demod_create(NULL));
    config.rf_rate=3000000; assert(!nbfm_demod_create(&config)); config.rf_rate=4000000;
    config.audio_rate=44100; assert(!nbfm_demod_create(&config)); config.audio_rate=48000;
    nbfm_demod_t* s=nbfm_demod_create(&config); assert(s);
    int16_t out[1]; iq16_t in={1,1};
    assert(nbfm_demod_process(s,&in,1,NULL,0,0).consumed==0);
    assert(nbfm_demod_process(s,NULL,0,out,1,0).produced==0);
    assert(nbfm_demod_process(s,NULL,1,out,1,0).error==-EINVAL);
    assert(nbfm_demod_process(s,&in,1,NULL,1,0).error==-EINVAL);
    assert(nbfm_demod_process(s,&in,1,out,1,NAN).error==-EINVAL);
    assert(nbfm_demod_process(s,&in,1,out,1,0.001).error==-EINVAL);
    assert(nbfm_demod_set_audio(s,NAN,8000)==-EINVAL);
    nbfm_demod_destroy(s); nbfm_demod_destroy(NULL);
    for(rate=1000000;rate<=4000000;rate*=2) {
        make_waveform();
        reset_case=false; compare_workers(); reset_case=true; compare_workers();
        compare_blocks(0); compare_blocks(0.0005); compare_blocks(-0.0005);
    }
}
