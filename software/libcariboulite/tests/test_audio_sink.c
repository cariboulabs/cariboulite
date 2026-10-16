#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdatomic.h>
#include "alsa_sink.h"
#include <alsa/asoundlib.h>

static int opens, closes, stereo, fail_sw, fail_prepare, writes;
static int real_writes;
static atomic_bool stress_done;
static snd_pcm_sframes_t next_write;
static const int16_t* expected;
static size_t expected_frames;
int __real_snd_pcm_open(snd_pcm_t**, const char*, snd_pcm_stream_t, int);
int __wrap_snd_pcm_open(snd_pcm_t** p, const char* d, snd_pcm_stream_t st, int mode) {
    ++opens; return __real_snd_pcm_open(p,d,st,mode);
}
int __real_snd_pcm_close(snd_pcm_t*);
int __wrap_snd_pcm_close(snd_pcm_t* p) { ++closes; return __real_snd_pcm_close(p); }
int __real_snd_pcm_hw_params_set_channels(snd_pcm_t*, snd_pcm_hw_params_t*, unsigned);
int __wrap_snd_pcm_hw_params_set_channels(snd_pcm_t* p, snd_pcm_hw_params_t* hw, unsigned ch) {
    if (stereo && ch==1) return -EINVAL;
    return __real_snd_pcm_hw_params_set_channels(p,hw,ch);
}
int __real_snd_pcm_sw_params(snd_pcm_t*, snd_pcm_sw_params_t*);
int __wrap_snd_pcm_sw_params(snd_pcm_t* p, snd_pcm_sw_params_t* sw) {
    return fail_sw ? -EIO : __real_snd_pcm_sw_params(p,sw);
}
int __real_snd_pcm_prepare(snd_pcm_t*);
int __wrap_snd_pcm_prepare(snd_pcm_t* p) {
    return fail_prepare ? -EIO : __real_snd_pcm_prepare(p);
}
snd_pcm_sframes_t __real_snd_pcm_writei(snd_pcm_t*, const void*, snd_pcm_uframes_t);
snd_pcm_sframes_t __wrap_snd_pcm_writei(snd_pcm_t* p, const void* data, snd_pcm_uframes_t frames) {
    if (real_writes) return __real_snd_pcm_writei(p, data, frames);
    (void)p; ++writes;
    assert(frames==expected_frames);
    const int16_t* v=data;
    for(size_t i=0;i<frames;++i) {
        assert(v[i*(stereo?2:1)]==expected[i]);
        if(stereo) assert(v[2*i+1]==expected[i]);
    }
    return next_write;
}
static void* retry_writer(void* arg) {
    int16_t samples[480]={0};
    // Use the production pipeline helper, included by the Python runner.
    extern int test_write_exact(audio_sink_t*, const int16_t*, size_t);
    test_write_exact(arg,samples,480);
    return NULL;
}
static void* stress_writer(void* arg) {
    int16_t samples[480]={0};
    for (int i=0; i<5000; ++i) {
        audio_sink_result_t r=audio_sink_write(arg,samples,480);
        assert(r.status==AUDIO_SINK_OK && r.frames==480);
    }
    atomic_store_explicit(&stress_done,true,memory_order_release);
    return NULL;
}
int main(void) {
    audio_sink_stats_t stats;
    const audio_sink_ops_t unsupported_ops={0};
    audio_sink_t unsupported={48000,&unsupported_ops};
    assert(!audio_sink_get_stats(NULL,&stats) && stats.write_calls==0);
    stats.write_calls=99;
    assert(!audio_sink_get_stats(&unsupported,&stats) && stats.write_calls==0);
    assert(!audio_sink_get_stats(&unsupported,NULL));
    assert(!alsa_sink_open("null",44100) && errno==EINVAL && opens==0);
    int16_t samples[960];
    for(int i=0;i<960;++i) samples[i]=(int16_t)(i*67-32768);
    for(stereo=0;stereo<=1;++stereo) {
        audio_sink_t* s=alsa_sink_open("null",48000); assert(s);
        assert(alsa_sink_channels(s)==(unsigned)(stereo?2:1));
        assert(audio_sink_state(s));
        assert(audio_sink_get_stats(s,&stats));
        assert(stats.write_calls==0 && stats.frames_requested==0 && stats.frames_written==0);
        assert(stats.partial_writes==0 && stats.would_block==0 && stats.zero_writes==0);
        assert(stats.underruns==0 && stats.recovery_attempts==0 && stats.recovery_successes==0);
        assert(stats.recovery_failures==0 && stats.write_errors==0 && stats.last_error==0);
        assert(audio_sink_write(s,NULL,1).error==-EINVAL);
        int before=writes;
        assert(audio_sink_write(s,NULL,0).status==AUDIO_SINK_OK && writes==before);
        expected=samples; expected_frames=480; next_write=480;
        assert(audio_sink_write(s,samples,480).frames==480);
        expected=samples; expected_frames=stereo?480:960; next_write=123;
        audio_sink_result_t r=audio_sink_write(s,samples,960);
        assert(r.frames==123 && r.status==AUDIO_SINK_OK);
        expected=samples+123; expected_frames=stereo?480:837; next_write=37;
        r=audio_sink_write(s,samples+123,837); assert(r.frames==37);
        expected=samples; expected_frames=480;
        next_write=-EPIPE; r=audio_sink_write(s,samples,480);
        assert(r.frames==0 && r.status==AUDIO_SINK_AGAIN);
        fail_prepare=1; r=audio_sink_write(s,samples,480);
        assert(r.status==AUDIO_SINK_ERROR && r.error==-EIO); fail_prepare=0;
        assert(audio_sink_get_stats(s,&stats));
        assert(stats.underruns==2 && stats.recovery_attempts==2);
        assert(stats.recovery_successes==1 && stats.recovery_failures==1);
        assert(stats.write_errors==1 && stats.last_error==-EIO);
        next_write=-EAGAIN; assert(audio_sink_write(s,samples,480).status==AUDIO_SINK_AGAIN);
        next_write=0; assert(audio_sink_write(s,samples,480).status==AUDIO_SINK_AGAIN);
        next_write=-ENODEV; assert(audio_sink_write(s,samples,480).error==-ENODEV);
        assert(audio_sink_get_stats(s,&stats));
        assert(stats.write_calls==8 && stats.frames_requested==(uint64_t)(stereo?3840:4677));
        assert(stats.frames_written==640 && stats.partial_writes==2);
        assert(stats.would_block==1 && stats.zero_writes==1);
        assert(stats.write_errors==2 && stats.last_error==-ENODEV);
        next_write=480;
        assert(audio_sink_write(s,samples,480).status==AUDIO_SINK_OK);
        assert(audio_sink_get_stats(s,&stats) && stats.last_error==-ENODEV);
        audio_sink_destroy(s);
    }
    stereo=0;
    fail_sw=1; assert(!alsa_sink_open("null",48000) && errno==EIO); fail_sw=0;
    fail_prepare=1; assert(!alsa_sink_open("null",48000) && errno==EIO); fail_prepare=0;
    assert(opens==closes);
    audio_sink_t* s=alsa_sink_open("null",48000); assert(s);
    int16_t zeros[480]={0}; expected=zeros; expected_frames=480; next_write=-EAGAIN;
    pthread_t thread; assert(!pthread_create(&thread,NULL,retry_writer,s));
    usleep(10000); pthread_cancel(thread);
    void* result; pthread_join(thread,&result); assert(result==PTHREAD_CANCELED);
    next_write=0;
    assert(!pthread_create(&thread,NULL,retry_writer,s));
    usleep(10000); pthread_cancel(thread); pthread_join(thread,&result);
    assert(result==PTHREAD_CANCELED);
    audio_sink_destroy(s); audio_sink_destroy(NULL); assert(opens==closes);
    s=alsa_sink_open("null",48000); assert(s);
    next_write=480; atomic_init(&stress_done,false);
    assert(!pthread_create(&thread,NULL,stress_writer,s));
    audio_sink_stats_t previous={0};
    do {
        assert(audio_sink_get_stats(s,&stats));
        assert(stats.write_calls>=previous.write_calls);
        assert(stats.frames_requested>=previous.frames_requested);
        assert(stats.frames_written>=previous.frames_written);
        assert(stats.write_errors==0 && stats.last_error==0);
        previous=stats;
    } while (!atomic_load_explicit(&stress_done,memory_order_acquire));
    pthread_join(thread,NULL);
    assert(audio_sink_get_stats(s,&stats));
    assert(stats.write_calls==5000 && stats.frames_requested==2400000);
    assert(stats.frames_written==2400000 && stats.partial_writes==0);
    audio_sink_destroy(s);
    s=alsa_sink_open("null",48000); assert(s);
    real_writes=1;
    for (int i=0; i<10; ++i) {
        audio_sink_result_t r=audio_sink_write(s,zeros,480);
        assert(r.status==AUDIO_SINK_OK && r.frames==480);
    }
    real_writes=0;
    assert(audio_sink_get_stats(s,&stats));
    assert(stats.write_calls==10 && stats.frames_requested==4800 && stats.frames_written==4800);
    assert(stats.underruns==0 && stats.write_errors==0);
    audio_sink_destroy(s); assert(opens==closes);
    puts("PASS: sink PCM preservation, stereo fallback, write/recovery counters, concurrent snapshots, null playback, cleanup and retry cancellation");
}
