#define _GNU_SOURCE
#include "tx_pipeline.h"
#include "pipeline_runtime.h"
#include "mod_worker.h"
#include "tone_source.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Compile the current writer body, including its static implementation.
#include "tx_writer_thread.inc"

pthread_mutex_t g_hw_lock = PTHREAD_MUTEX_INITIALIZER;
volatile bool nbfm_tx_active, nbfm_rx_active;
bool nbfm_tx_ready, nbfm_rx_ready;
static uint64_t producer_now;
static unsigned producer_sleeps;
static bool check_pacing_deadline;
uint64_t mono_ns(void) { return producer_now += 10000000; }
int set_rt_and_affinity_prio(int priority, int cpu)
{ (void)priority; (void)cpu; return 0; }
int set_rt_and_affinity_prio_record(int priority, int cpu,
                                   pipeline_thread_diagnostics_t* diagnostics)
{
    (void)priority; (void)cpu;
    diagnostics->status.valid = true;
    atomic_store(&diagnostics->ready, true);
    return 0;
}
void rf10_fifo_stop(rf10_fifo_t* fifo) { (void)fifo; }
int __wrap_clock_nanosleep(clockid_t clock, int flags,
                          const struct timespec* request, struct timespec* remain)
{
    assert(clock==CLOCK_MONOTONIC && flags==TIMER_ABSTIME && !remain);
    assert(request->tv_nsec>=0 && request->tv_nsec<1000000000L);
    ++producer_sleeps;
    if (check_pacing_deadline) {
        uint64_t deadline=(uint64_t)request->tv_sec*1000000000ull+
                          (uint64_t)request->tv_nsec;
        // Fast padding advances monotonic time without absolute sleeps. The
        // next paced frame must use a future deadline, not a stale cue deadline.
        assert(deadline==producer_now+10000000ull);
    }
    return 0;
}

static unsigned tone_changes, mic_reads;
int __real_tone_source_set(audio_source_t*, float, float);
int __wrap_tone_source_set(audio_source_t* source, float hz, float amplitude)
{ ++tone_changes; return __real_tone_source_set(source, hz, amplitude); }
static audio_source_result_t read_mic(audio_source_t* source, float* dst, size_t count)
{
    (void)source;
    ++mic_reads;
    for (size_t i=0; i<count; ++i) dst[i]=0.75f;
    return (audio_source_result_t){count, AUDIO_SOURCE_OK, 0};
}
static const audio_source_ops_t mic_ops = { .read=read_mic };

static tx_writer_ctrl_st* producer_tx;
static dsp_producer_ctrl_t* producer;
static pthread_mutex_t gate_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_condition = PTHREAD_COND_INITIALIZER;
static bool gate_enabled, gate_entered, gate_release, gate_success;
static bool producer_initial_silent, producer_sequence_test;
static unsigned producer_puts;
static uint64_t blocked_sequence;

static void check_producer_sequence(void)
{
    // Two paced prezeros, two paced cue frames, three unpaced final zeros,
    // then paced hold silence, normal mic and normal tone frames. A stale
    // fast-padding flag must not accelerate a tone, exhausted injection or mic.
    static const unsigned sleeps[]={1,2,3,4,4,4,4,5,6,7};
    static const unsigned changes[]={1,2,3,4,5,6,7,7,7,8};
    static const int frames_left[]={2,1,2,1,3,2,1,0,0,0};
    unsigned frame=producer_puts;
    assert(frame>=1 && frame<=10);
    assert(producer_sleeps==sleeps[frame-1]);
    assert(tone_changes==changes[frame-1]);
    assert(mic_reads==(frame>=9?1u:0u));
    assert(producer_tx->inj.frames_left==frames_left[frame-1]);
    uint64_t previous_sequence=frame==1?8:17+(frame<=8?frame-1:7);
    assert(producer_tx->inj.last_sequence==previous_sequence);
    bool audible=frame==3 || frame==4 || frame==9 || frame==10;
    bool nonzero=false;
    for (size_t i=0; i<480; ++i) {
        nonzero |= producer_tx->a48k[i]!=0;
        if (!audible) assert(producer_tx->a48k[i]==0);
        if (frame==9) assert(producer_tx->a48k[i]==0.75f);
    }
    assert(nonzero==audible);

    pthread_mutex_lock(&g_tx_injection_lock);
    if (frame==2) {
        // The successful enqueue decrements the old injection once more after
        // this mock returns, so include that frame in the new stage's count.
        producer_tx->inj.frames_left=3;
        producer_tx->inj.hz=2475;
        producer_tx->inj.fast_padding=true;
    } else if (frame==4) {
        producer_tx->inj.frames_left=4;
        producer_tx->inj.hz=0;
        producer_tx->inj.fast_padding=true;
    } else if (frame==8) {
        producer_tx->inj.hold_silence=false;
    } else if (frame==9) {
        producer_tx->tone_mode=true;
    }
    pthread_mutex_unlock(&g_tx_injection_lock);
    if (frame==10) producer->active=false;
}

bool rf10_fifo_put(rf10_fifo_t* fifo, const rf10_frame_t* frame, int timeout)
{
    assert(fifo==producer->fifo && timeout==-1);
    ++producer_puts;
    assert(frame->tx_sequence==17+producer_puts);
    for (size_t i=0; i<producer_tx->frame_samples; ++i) {
        assert(frame->data[i].i==(producer_tx->iq_rf[i].i | 1));
        assert(frame->data[i].q==producer_tx->iq_rf[i].q);
    }
    if (gate_enabled) {
        pthread_mutex_lock(&gate_lock);
        blocked_sequence=frame->tx_sequence;
        gate_entered=true;
        pthread_cond_signal(&gate_condition);
        while (!gate_release) pthread_cond_wait(&gate_condition, &gate_lock);
        bool success=gate_success;
        pthread_mutex_unlock(&gate_lock);
        producer->active=false;
        return success;
    }
    if (producer_sequence_test) {
        check_producer_sequence();
        return true;
    }
    if (producer_puts==1) {
        bool nonzero=false;
        for (size_t i=0; i<480; ++i) nonzero |= producer_tx->a48k[i]!=0;
        assert(nonzero!=producer_initial_silent);
    } else {
        for (size_t i=0; i<480; ++i) assert(producer_tx->a48k[i]==0);
        assert(!mic_reads && tone_changes==1);
    }
    if (producer_puts==3) producer->active=false;
    return true;
}

static void producer_case(unsigned rate, bool block, bool success, bool tone_mode,
                          bool fast_padding)
{
    rf10_fifo_t fifo={0};
    float audio[480];
    iq16_t* iq=calloc(rate/100, sizeof(*iq));
    nbfm_cfg_t config={48000,rate,2500,0,4000,1};
    audio_source_t mic={ {48000,1}, &mic_ops };
    tx_writer_ctrl_st tx={ .fm=nbfm_create(&config), .a48k=audio, .iq_rf=iq,
        .tone=tone_source_open(600,0.4f,(audio_format_t){48000,1}),
        .tone_mode=tone_mode, .mic=&mic, .tone_hz=600, .tone_amp=0.4f,
        .frame_samples=rate/100, .next_sequence=17, .fifo=&fifo,
        .inj={ .frames_left=1, .hz=fast_padding?0:2475, .last_sequence=8,
               .hold_silence=true, .fast_padding=fast_padding } };
    dsp_producer_ctrl_t ctrl={ .active=true, .tx=&tx, .fifo=&fifo };
    assert(iq && tx.fm && tx.tone);
    producer_tx=&tx; producer=&ctrl;
    producer_puts=tone_changes=mic_reads=0;
    producer_now=producer_sleeps=0;
    producer_initial_silent=fast_padding; producer_sequence_test=false;
    check_pacing_deadline=true;
    nbfm_tx_active=true;
    gate_enabled=block; gate_entered=gate_release=false; gate_success=success;
    if (block) {
        pthread_t thread;
        assert(!pthread_create(&thread,NULL,nbfm_mod_thread,&ctrl));
        pthread_mutex_lock(&gate_lock);
        struct timespec deadline;
        assert(!clock_gettime(CLOCK_REALTIME,&deadline)); deadline.tv_sec+=2;
        while (!gate_entered)
            assert(!pthread_cond_timedwait(&gate_condition,&gate_lock,&deadline));
        assert(blocked_sequence==18);
        pthread_mutex_lock(&g_tx_injection_lock);
        assert(tx.inj.frames_left==1 && tx.inj.last_sequence==8);
        pthread_mutex_unlock(&g_tx_injection_lock);
        gate_release=true;
        pthread_cond_signal(&gate_condition);
        pthread_mutex_unlock(&gate_lock);
        assert(!pthread_join(thread,NULL));
        assert(producer_puts==1);
        assert(tx.inj.frames_left==(success?0:1));
        assert(tx.inj.last_sequence==(success?18u:8u));
        assert(producer_sleeps==(fast_padding?0u:1u));
    } else {
        nbfm_mod_thread(&ctrl);
        assert(producer_puts==3 && tx.next_sequence==20);
        assert(tx.inj.frames_left==0 && tx.inj.last_sequence==18);
        assert(!mic_reads && tone_changes==1);
        assert(producer_sleeps==(fast_padding?2u:3u));
    }
    audio_source_destroy(tx.tone); nbfm_destroy(tx.fm); free(iq);
}

static void producer_pacing_case(unsigned rate)
{
    rf10_fifo_t fifo={0};
    float audio[480];
    iq16_t* iq=calloc(rate/100, sizeof(*iq));
    nbfm_cfg_t config={48000,rate,2500,0,4000,1};
    audio_source_t mic={ {48000,1}, &mic_ops };
    tx_writer_ctrl_st tx={ .fm=nbfm_create(&config), .a48k=audio, .iq_rf=iq,
        .tone=tone_source_open(600,0.4f,(audio_format_t){48000,1}),
        .tone_mode=false, .mic=&mic, .tone_hz=600, .tone_amp=0.4f,
        .frame_samples=rate/100, .next_sequence=17, .fifo=&fifo,
        .inj={ .frames_left=2, .hz=0, .last_sequence=8, .hold_silence=true } };
    dsp_producer_ctrl_t ctrl={ .active=true, .tx=&tx, .fifo=&fifo };
    assert(iq && tx.fm && tx.tone);
    producer_tx=&tx; producer=&ctrl;
    producer_puts=tone_changes=mic_reads=0;
    producer_now=producer_sleeps=0;
    producer_sequence_test=true; gate_enabled=false; check_pacing_deadline=true;
    nbfm_tx_active=true;
    nbfm_mod_thread(&ctrl);
    assert(producer_puts==10 && tx.next_sequence==27 && producer_sleeps==7);
    assert(tx.inj.frames_left==0 && tx.inj.last_sequence==24);
    assert(mic_reads==1 && tone_changes==8);
    producer_sequence_test=false;
    audio_source_destroy(tx.tone); nbfm_destroy(tx.fm); free(iq);
}

static tx_writer_ctrl_st* writer;
static unsigned fetched, write_calls, poll_calls, completed;
static size_t accepted;
static bool hard_failure, midframe_stop;
static uint64_t frame_sequence;
typedef enum { HANDOFF_NONE, HANDOFF_FIFO, HANDOFF_POLL, HANDOFF_WRITE } handoff_t;
static handoff_t handoff;
static smi_stream_state_en stream_state;
static bool handoff_entered, handoff_release, shutdown_started, shutdown_done;
static unsigned writes_after_rx;

static void handoff_pause(void)
{
    pthread_mutex_lock(&gate_lock);
    handoff_entered=true;
    pthread_cond_broadcast(&gate_condition);
    while (!handoff_release) pthread_cond_wait(&gate_condition,&gate_lock);
    pthread_mutex_unlock(&gate_lock);
}
static void await_flag(const bool* flag)
{
    struct timespec deadline;
    assert(!clock_gettime(CLOCK_REALTIME,&deadline)); deadline.tv_sec+=2;
    while (!*flag)
        assert(!pthread_cond_timedwait(&gate_condition,&gate_lock,&deadline));
}
int __wrap_nanosleep(const struct timespec* request, struct timespec* remain)
{
    (void)request; (void)remain;
    // Terminate after the writer reaches its idle path, then check any exit work.
    if (handoff!=HANDOFF_NONE) writer->active=false;
    return 0;
}
static int16_t marker_i(uint64_t sequence, size_t sample)
{ return (int16_t)((sequence*3+sample*23)%30000-15000); }
static int16_t marker_q(uint64_t sequence, size_t sample)
{ return (int16_t)((sequence*101+sample*19)%29000-14500); }

bool rf10_fifo_get(rf10_fifo_t* fifo, rf10_frame_t* frame, int timeout)
{
    assert(fifo==writer->fifo && timeout==-1);
    if (handoff==HANDOFF_FIFO && !fetched) handoff_pause();
    if (fetched) {
        assert(accepted==writer->frame_samples);
        assert(atomic_load(&writer->written_sequence)==frame_sequence);
        ++completed;
    }
    if (fetched==3) { writer->active=false; return false; }
    frame_sequence=21+fetched++;
    frame->tx_sequence=frame_sequence;
    for (size_t i=0; i<writer->frame_samples; ++i)
        frame->data[i]=(iq16_t){marker_i(frame_sequence,i),marker_q(frame_sequence,i)};
    accepted=0;
    return true;
}
size_t caribou_smi_get_native_batch_samples(caribou_smi_st* smi)
{ (void)smi; return 8192; }
int caribou_smi_set_driver_streaming_state(caribou_smi_st* smi, smi_stream_state_en state)
{
    assert(smi==&writer->radio->sys->smi);
    assert(state==smi_stream_idle || state==smi_stream_tx_channel ||
           state==smi_stream_rx_channel_0);
    stream_state=state;
    return 0;
}
int cariboulite_radio_activate_channel(cariboulite_radio_state_st* radio,
                                      cariboulite_channel_dir_en direction, bool active)
{ assert(radio==writer->radio && direction==cariboulite_channel_dir_tx && !active); return 0; }
int __wrap_poll(struct pollfd* fds, nfds_t count, int timeout)
{
    assert(count==1 && timeout==10 && fds[0].events==POLLOUT);
    ++poll_calls;
    if (handoff==HANDOFF_POLL && poll_calls==1) handoff_pause();
    if (handoff==HANDOFF_WRITE && poll_calls>1) {
        // The second poll occurs outside HW_LOCK, allowing shutdown to finish
        // before the worker could retry its partially accepted first write.
        pthread_mutex_lock(&gate_lock);
        await_flag(&shutdown_done);
        pthread_mutex_unlock(&gate_lock);
    }
    fds[0].revents=poll_calls%13==0?0:POLLOUT;
    return poll_calls%11==0?0:1;
}
int caribou_smi_write_samples(caribou_smi_st* smi, caribou_smi_channel_en channel,
                             const caribou_smi_sample_complex_int16* samples, int count)
{
    assert(smi==&writer->radio->sys->smi && channel==caribou_smi_channel_900);
    if (handoff!=HANDOFF_NONE && stream_state==smi_stream_rx_channel_0)
        ++writes_after_rx;
    assert(stream_state==smi_stream_tx_channel);
    assert(atomic_load(&writer->written_sequence)==frame_sequence-1);
    size_t expected=writer->frame_samples-accepted;
    if (expected>2048) expected=2048; // Native getter returns samples; quarter=8192/4.
    assert(count==(int)expected);
    for (int i=0; i<count; ++i) {
        assert(samples[i].i==marker_i(frame_sequence,accepted+(size_t)i));
        assert(samples[i].q==marker_q(frame_sequence,accepted+(size_t)i));
    }
    ++write_calls;
    if (handoff==HANDOFF_WRITE && write_calls==1) handoff_pause();
    if (write_calls==2 && hard_failure) {
        errno=EIO; writer->active=false; return -1;
    }
    if (write_calls==2 && midframe_stop) {
        nbfm_tx_active=false; writer->active=false;
        accepted+=7; return 7;
    }
    if (write_calls%7==2) return 0;
    if (write_calls%7==3) { errno=EAGAIN; return -1; }
    size_t sent=write_calls%7==1?17:503+accepted%97;
    if (sent>(size_t)count) sent=(size_t)count;
    accepted+=sent;
    return (int)sent;
}
int caribou_smi_write_timed(caribou_smi_st* smi, caribou_smi_channel_en channel,
                          const caribou_smi_sample_complex_int16* samples,
                          size_t count, long timeout_us)
{
    (void)smi; (void)channel; (void)samples; (void)count; (void)timeout_us;
    assert(!"legacy writer fixture unexpectedly used hardware paced writes");
    return -1;
}
static void writer_case(unsigned rate, bool fail, bool stop)
{
    sys_st sys={0}; rf10_fifo_t fifo={0};
    sys.smi.filedesc=123456; // fcntl may fail; the SMI and poll calls are mocked.
    sys.radio_low.sys=&sys;
    tx_writer_ctrl_st tx={ .active=true, .radio=&sys.radio_low,
                          .frame_samples=rate/100, .fifo=&fifo };
    atomic_init(&tx.written_sequence,20);
    writer=&tx;
    fetched=write_calls=poll_calls=completed=0; accepted=0;
    handoff=HANDOFF_NONE; stream_state=smi_stream_tx_channel;
    hard_failure=fail; midframe_stop=stop; nbfm_tx_active=true;
    tx_writer_thread_func(&tx);
    assert(atomic_load(&tx.write_calls) == write_calls);
    assert(!atomic_load(&tx.alive) && tx.scheduling.status.valid);
    if (fail || stop) {
        assert(fetched==1 && accepted<tx.frame_samples);
        assert(atomic_load(&tx.written_sequence)==20);
        assert(!completed);
        if (fail) assert(!nbfm_tx_active);
        assert(atomic_load(&tx.write_samples) == accepted);
        assert(atomic_load(&tx.write_errors) == (fail ? 1u : 0u));
        assert(atomic_load(&tx.last_error) == (fail ? -EIO : 0));
    } else {
        assert(fetched==3 && completed==3);
        assert(atomic_load(&tx.written_sequence)==23);
        assert(atomic_load(&tx.write_samples) == 3 * tx.frame_samples);
        assert(atomic_load(&tx.partial_writes) && atomic_load(&tx.zero_writes));
        assert(atomic_load(&tx.would_block) && !atomic_load(&tx.write_errors));
    }
}
static void stop_then_start_rx(void)
{
    // Simulate stop() clearing TX before its locked shutdown, then RX arming.
    nbfm_tx_active=false;
    HW_LOCK();
    caribou_smi_set_driver_streaming_state(&writer->radio->sys->smi,smi_stream_idle);
    caribou_smi_set_driver_streaming_state(&writer->radio->sys->smi,smi_stream_rx_channel_0);
    HW_UNLOCK();
}
static void* shutdown_thread(void* unused)
{
    (void)unused;
    pthread_mutex_lock(&gate_lock);
    shutdown_started=true;
    pthread_cond_broadcast(&gate_condition);
    pthread_mutex_unlock(&gate_lock);
    stop_then_start_rx();
    pthread_mutex_lock(&gate_lock);
    shutdown_done=true;
    pthread_cond_broadcast(&gate_condition);
    pthread_mutex_unlock(&gate_lock);
    return NULL;
}
static void handoff_case(unsigned rate, handoff_t at)
{
    sys_st sys={0}; rf10_fifo_t fifo={0};
    sys.smi.filedesc=123456;
    sys.radio_low.sys=&sys;
    tx_writer_ctrl_st tx={ .active=true, .radio=&sys.radio_low,
                          .frame_samples=rate/100, .fifo=&fifo };
    atomic_init(&tx.written_sequence,20);
    writer=&tx;
    fetched=write_calls=poll_calls=completed=0; accepted=0;
    hard_failure=midframe_stop=false; nbfm_tx_active=true;
    handoff=at; stream_state=smi_stream_tx_channel; writes_after_rx=0;
    handoff_entered=handoff_release=shutdown_started=shutdown_done=false;
    pthread_t thread, stopper;
    assert(!pthread_create(&thread,NULL,tx_writer_thread_func,&tx));
    pthread_mutex_lock(&gate_lock);
    await_flag(&handoff_entered);
    if (at==HANDOFF_WRITE) {
        // A suspended write must hold HW_LOCK so mode changes cannot overtake it.
        int busy=pthread_mutex_trylock(&g_hw_lock);
        if (!busy) pthread_mutex_unlock(&g_hw_lock);
        assert(busy==EBUSY);
        assert(!pthread_create(&stopper,NULL,shutdown_thread,NULL));
        await_flag(&shutdown_started);
        assert(!shutdown_done && stream_state==smi_stream_tx_channel);
    } else {
        stop_then_start_rx();
    }
    handoff_release=true;
    pthread_cond_broadcast(&gate_condition);
    pthread_mutex_unlock(&gate_lock);
    if (at==HANDOFF_WRITE) assert(!pthread_join(stopper,NULL));
    assert(!pthread_join(thread,NULL));
    assert(stream_state==smi_stream_rx_channel_0 && !writes_after_rx);
    assert(write_calls==(at==HANDOFF_WRITE?1u:0u));
    assert(atomic_load(&tx.written_sequence)==20);
    handoff=HANDOFF_NONE;
}
int main(void)
{
    for (unsigned rate=1000000; rate<=4000000; rate*=2) {
        producer_case(rate,true,true,true,false);
        producer_case(rate,true,false,true,false);
        producer_case(rate,false,true,true,false);
        producer_case(rate,false,true,false,false);
        producer_case(rate,true,true,true,true);
        producer_case(rate,true,false,true,true);
        producer_case(rate,false,true,true,true);
        producer_pacing_case(rate);
        writer_case(rate,false,false);
        writer_case(rate,true,false);
        writer_case(rate,false,true);
        handoff_case(rate,HANDOFF_FIFO);
        handoff_case(rate,HANDOFF_POLL);
        handoff_case(rate,HANDOFF_WRITE);
        printf("PASS: %u MS/s: blocked/failed enqueue preserves injection progress, "
               "only finite final padding bypasses 10 ms pacing and stays silent, "
               "pacing resumes after padding, partial writes preserve markers, complete-frame acknowledgment, "
               "TX-to-RX handoff preserves RX after delayed/active writes and writer exit\n",
               rate/1000000);
    }
}
