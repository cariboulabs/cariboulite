#!/usr/bin/env python3
"""Exercise actual TX stop code against simulated producer, FIFO and cyclic DMA.

Each simulated RF sample carries a tone/silence marker. The driver accepts only
complete native quarters and refills its four-period ring as the real driver
does. The producer paces tones at 10 ms, while final silence uses the actual
stop code's fast-padding flag and bounded FIFO/writer backpressure. All time
and services are local; no radio hardware is accessed.
"""
from pathlib import Path
import subprocess
import tempfile

src = Path(__file__).resolve().parents[1] / 'src'
s = (src / 'tx_pipeline.c').read_text()
h = s.index('static unsigned tx_tail_padding_frames(')
i = s.index('\nint tx_pipeline_init(', h)
# Hardware admission setup is tested with real workers and a mock driver in
# test_tx_hardware_pacing.py; this simulator deliberately models legacy DMA.
tail_end = s.index('\nstatic int tx_detect_pacing(', h)
a = s.index('static inline int ms_to_frames_10ms(')
b = s.index('\nint tx_pipeline_start(', a)
c = s.index('static bool tx_wait_written(', b)
d = s.index('\nvoid tx_pipeline_request_abort(', c)
park_start = s.index('static int tx_park_workers(', a)
park_end = s.index('static void tx_clear_injection(', park_start)
# Model the exact cue/drain/shutdown phases; real retained-worker parking is
# exercised separately with pthreads by test_tx_worker_parking.py.
code = s[h:tail_end] + s[a:park_start] + s[park_end:b] + s[c:d]
pre = r'''
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <limits.h>
static pthread_mutex_t g_tx_injection_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_tx_injection_changed = PTHREAD_COND_INITIALIZER;
#define HW_LOCK() ((void)0)
#define HW_UNLOCK() ((void)0)
#define cariboulite_channel_dir_tx 1
#define caribou_fpga_io_ctrl_rfm_low_power 0
typedef int smi_stream_state_en;
typedef struct { size_t cap,count; } rf10_stats_t;
typedef struct { int smi,fpga; } sys_st;
typedef struct {
    bool inited,running;
    struct {
        bool active;
        void* audio_buffer;
        size_t frame_samples;
        unsigned tail_padding_frames;
        uint64_t next_sequence;
        atomic_uint_fast64_t written_sequence;
        atomic_int last_error;
        atomic_bool abort_requested;
        struct {
            float hz;
            int frames_left;
            uint64_t last_sequence;
            bool hold_silence,fast_padding;
        } inj;
    } tx_ctrl;
    struct { bool active; atomic_int last_error; } dsp_ctrl;
    sys_st* sys;
    void* radio;
    rf10_stats_t txq;
} tx_pipeline_t;
static bool nbfm_tx_active;
static uint64_t now;
static tx_pipeline_t* current;
static int idle_calls,off_calls,low_calls;
static uint64_t mono_ns(void) { return now; }
static int tx_park_workers(tx_pipeline_t* p) { (void)p; return 0; }
/* This simulator deliberately exercises legacy transport, whose restoration
 * helper is a no-op. Real timing ownership/failure retries use the pthread
 * hardware-demand mock in test_tx_hardware_pacing.py. */
int tx_restore_write_timing(tx_pipeline_t* p) { (void)p; return 0; }
static void tx_audio_buffer_set_cue(void* buffer,bool cue) {
    (void)cue;assert(!buffer);
}

/* kfifo and cyclic DMA operate in samples, matching the public SMI getter. */
#define NATIVE_SAMPLES 131072u
#define QUARTER_SAMPLES (NATIVE_SAMPLES / 4)
#define DRIVER_STORAGE (32 * NATIVE_SAMPLES)
static unsigned char kernel_fifo[DRIVER_STORAGE],dma_ring[NATIVE_SAMPLES];
static size_t kernel_head,kernel_tail,kernel_count,kernel_cap,kernel_upper_cap,dma_pos;
static unsigned rf_fs;
static uint64_t tone_samples,generated_tone_frames,last_rf_tone_ns,first_rf_tone_ns;
typedef struct { uint64_t sequence; bool tone,last; } frame_t;
static frame_t app_fifo[64],writer_frame;
static size_t app_head,app_tail,app_count,writer_off;
static bool writer_busy,freeze_producer,force_paced_padding;
static uint64_t writer_ready_ns,final_written_ns,final_queued_ns;
static unsigned post_frames,partial_writes;
enum { NORMAL, STALLED_PRODUCER, FAILED_WORKER, STALLED_WRITER,
       DELAY_LAST_WRITE, STALL_LAST_WRITE };
static int mode;

static size_t rounded_fifo_capacity(unsigned multiplier,bool legacy_alloc) {
    size_t requested=multiplier*NATIVE_SAMPLES,capacity=1;
    while(capacity<requested) capacity*=2;
    /* kfifo_alloc rounds upward; preallocated kfifo_init rounds downward. */
    return legacy_alloc || capacity==requested ? capacity : capacity/2;
}

static void push_kernel(bool tone,size_t samples) {
    assert(kernel_count+samples<=kernel_cap);
    for(size_t i=0;i<samples;++i) {
        kernel_fifo[kernel_tail]=tone;
        kernel_tail=(kernel_tail+1)%DRIVER_STORAGE;
    }
    kernel_count+=samples;
}
static void advance_rf(void) {
    for(unsigned i=0;i<rf_fs/500;++i) {
        if(dma_ring[dma_pos]) {
            ++tone_samples;
            last_rf_tone_ns=now-2000000ULL+(i+1)*1000000000ULL/rf_fs;
            if(!first_rf_tone_ns) first_rf_tone_ns=last_rf_tone_ns;
        }
        if(++dma_pos==NATIVE_SAMPLES) dma_pos=0;
        if(dma_pos%QUARTER_SAMPLES==0) {
            /* Refill the quarter that just finished; DMA reads it one lap later. */
            size_t completed=(dma_pos+NATIVE_SAMPLES-QUARTER_SAMPLES)%NATIVE_SAMPLES;
            size_t previous=(completed+NATIVE_SAMPLES-QUARTER_SAMPLES)%NATIVE_SAMPLES;
            if(kernel_count>=QUARTER_SAMPLES) {
                for(size_t j=0;j<QUARTER_SAMPLES;++j) {
                    dma_ring[completed+j]=kernel_fifo[kernel_head];
                    kernel_head=(kernel_head+1)%DRIVER_STORAGE;
                }
                kernel_count-=QUARTER_SAMPLES;
            } else {
                memcpy(dma_ring+completed,dma_ring+previous,QUARTER_SAMPLES);
            }
        }
    }
}
static void produce_frame(void) {
    if(mode==STALLED_PRODUCER || freeze_producer || app_count==64) return;
    if(current->tx_ctrl.inj.frames_left<=0) return;
    bool tone=current->tx_ctrl.inj.hz==2475.0f;
    assert(tone || current->tx_ctrl.inj.hz==0.0f);
    if(tone || !generated_tone_frames)
        assert(!current->tx_ctrl.inj.fast_padding);
    bool last=!tone && generated_tone_frames==25 &&
              post_frames+1==current->tx_ctrl.tail_padding_frames;
    frame_t f={.sequence=++current->tx_ctrl.next_sequence,.tone=tone,.last=last};
    app_fifo[app_tail]=f;app_tail=(app_tail+1)%64;++app_count;
    /* Match producer acknowledgement only after successful enqueue. */
    current->tx_ctrl.inj.last_sequence=f.sequence;
    --current->tx_ctrl.inj.frames_left;
    if(tone) ++generated_tone_frames;
    else if(generated_tone_frames==25) ++post_frames;
    if(last) final_queued_ns=now;
    if(last && (mode==DELAY_LAST_WRITE || mode==STALL_LAST_WRITE)) freeze_producer=true;
}
static void write_frame(void) {
    if(mode==STALLED_WRITER) return;
    for(;;) {
        if(!writer_busy) {
            if(!app_count) return;
            writer_frame=app_fifo[app_head];app_head=(app_head+1)%64;--app_count;
            writer_off=0;writer_busy=true;
            writer_ready_ns=now;
            if(writer_frame.last && mode==DELAY_LAST_WRITE) writer_ready_ns+=300000000;
        }
        if(writer_frame.last && mode==STALL_LAST_WRITE) return;
        if(now<writer_ready_ns) return;
        size_t count=current->tx_ctrl.frame_samples-writer_off;
        if(count>kernel_cap-kernel_count) count=kernel_cap-kernel_count;
        if(!count) return;
        if(count<current->tx_ctrl.frame_samples-writer_off) ++partial_writes;
        push_kernel(writer_frame.tone,count);
        writer_off+=count;
        if(writer_off<current->tx_ctrl.frame_samples) return;
        /* A dequeued FIFO frame remains pending until every sample is written. */
        atomic_store(&current->tx_ctrl.written_sequence,writer_frame.sequence);
        if(writer_frame.last) final_written_ns=now;
        writer_busy=false;
    }
}
static int fake_sleep(const struct timespec* t,struct timespec* rest) {
    assert(t->tv_sec==0 && t->tv_nsec==2000000);
    now+=2000000;
    advance_rf();
    if(mode==FAILED_WORKER && now>=20000000) {
        atomic_store(&current->tx_ctrl.last_error,-EIO);
        nbfm_tx_active=false;
    }
    if(nbfm_tx_active && current->tx_ctrl.active && current->dsp_ctrl.active) {
        bool fast=current->tx_ctrl.inj.fast_padding &&
                  current->tx_ctrl.inj.frames_left>0 &&
                  current->tx_ctrl.inj.hz==0.0f && !force_paced_padding;
        if(fast) {
            /* Burst only until producer capacity or kernel backpressure stops
             * progress. At most the 64-frame app FIFO plus writer can queue. */
            write_frame();
            while(current->tx_ctrl.inj.frames_left>0 && app_count<64 &&
                  !freeze_producer && mode!=STALLED_PRODUCER) {
                produce_frame();
                write_frame();
            }
        } else {
            if(now%10000000==0) produce_frame();
            write_frame();
        }
    }
    current->txq.count=app_count;
    return 0;
}
#define nanosleep fake_sleep
static int caribou_smi_set_driver_streaming_state(int* s,int state) {
    assert(state==0);++idle_calls;return 0;
}
static int cariboulite_radio_activate_channel(void* r,int dir,bool active) {
    assert(!active);++off_calls;return 0;
}
static int caribou_fpga_set_io_ctrl_mode(int* f,int debug,int setting) {
    ++low_calls;return 0;
}
'''
post = r'''
static void reset(tx_pipeline_t* p,sys_st* sys,unsigned fs,unsigned multiplier,
                  int scenario,unsigned occupancy,bool legacy_alloc) {
    *p=(tx_pipeline_t){.sys=sys,.inited=true,.running=true,.txq={.cap=64}};
    p->tx_ctrl.active=p->dsp_ctrl.active=true;
    p->tx_ctrl.frame_samples=fs/100;
    p->tx_ctrl.tail_padding_frames=tx_tail_padding_frames(NATIVE_SAMPLES,multiplier,fs/100);
    atomic_init(&p->tx_ctrl.written_sequence,0);
    current=p;nbfm_tx_active=true;now=0;mode=scenario;
    idle_calls=off_calls=low_calls=0;
    rf_fs=fs;tone_samples=generated_tone_frames=post_frames=0;
    first_rf_tone_ns=last_rf_tone_ns=0;
    partial_writes=0;
    kernel_head=kernel_tail=kernel_count=dma_pos=0;
    kernel_cap=rounded_fifo_capacity(multiplier,legacy_alloc);
    kernel_upper_cap=rounded_fifo_capacity(multiplier,true);
    memset(kernel_fifo,0,sizeof(kernel_fifo));memset(dma_ring,0,sizeof(dma_ring));
    /* Include an empty FIFO, a deliberately unaligned partial backlog, and
     * the worst full driver backlog at the moment Stop is pressed. */
    if(occupancy==1) push_kernel(false,kernel_cap/2+123);
    if(occupancy==2) push_kernel(false,kernel_cap);
    app_head=app_tail=app_count=writer_off=0;
    writer_busy=freeze_producer=force_paced_padding=false;
    writer_ready_ns=final_written_ns=final_queued_ns=0;
}
static uint64_t carrier_tail_bound(const tx_pipeline_t* p) {
    /* Generous RF drain guard: native ring, one refill quarter, one rounded
     * 10 ms frame, FPGA space, and two simulation scheduling quanta. The stop
     * code must support legacy round-up FIFO capacities, so a newer round-down
     * driver incurs the conservative difference as additional RF silence. */
    assert(kernel_upper_cap>=kernel_cap);
    return (NATIVE_SAMPLES+QUARTER_SAMPLES+p->tx_ctrl.frame_samples+1024+
            kernel_upper_cap-kernel_cap)*
           1000000000ULL/rf_fs+4000000ULL;
}
static uint64_t deadline_bound(const tx_pipeline_t* p) {
    uint64_t budget=(5+25+p->tx_ctrl.tail_padding_frames)*10000000ULL+
                    (p->txq.cap+1)*10000000ULL+600000000ULL;
    if(budget<1000000000ULL) budget=1000000000ULL;
    return budget+(p->txq.cap+1)*10000000ULL+602000000ULL;
}
static void assert_shutdown(tx_pipeline_t* p) {
    assert(!p->running && !nbfm_tx_active && p->tx_ctrl.inj.frames_left==0);
    assert(idle_calls==1 && off_calls==1 && low_calls==1);
    assert(now<=deadline_bound(p));
    tx_pipeline_stop(p);assert(idle_calls==1 && off_calls==1 && low_calls==1);
}
int main(void) {
    setvbuf(stdout,NULL,_IONBF,0);
    sys_st sys={0};tx_pipeline_t p;
    const unsigned rates[]={1000000,2000000,4000000};
    const unsigned multipliers[]={6,16};
    for(unsigned r=0;r<3;++r) for(unsigned m=0;m<2;++m)
    for(unsigned legacy_alloc=0;legacy_alloc<2;++legacy_alloc) {
        uint64_t longest_tail=0;
        for(unsigned occupancy=0;occupancy<3;++occupancy)
        for(int scenario=NORMAL;scenario<=STALL_LAST_WRITE;++scenario) {
            reset(&p,&sys,rates[r],multipliers[m],scenario,occupancy,legacy_alloc);
            assert(p.tx_ctrl.tail_padding_frames>=25);
            assert((size_t)p.tx_ctrl.tail_padding_frames*p.tx_ctrl.frame_samples>=
                   kernel_upper_cap+NATIVE_SAMPLES+QUARTER_SAMPLES+1024);
            tx_pipeline_stop(&p);
            assert_shutdown(&p);
            if(scenario==NORMAL || scenario==DELAY_LAST_WRITE) {
                assert(atomic_load(&p.tx_ctrl.last_error)==0);
                assert(generated_tone_frames==25);
                assert(tone_samples==25*p.tx_ctrl.frame_samples);
                assert(last_rf_tone_ns-first_rf_tone_ns+
                       1000000000ULL/rf_fs==250000000ULL);
                assert(atomic_load(&p.tx_ctrl.written_sequence)>=p.tx_ctrl.inj.last_sequence);
                assert(final_written_ns && now>=final_written_ns);
                if(scenario==DELAY_LAST_WRITE) assert(freeze_producer && p.txq.count==0);
                if(scenario==NORMAL) {
                    assert(partial_writes>0);
                    uint64_t tail=now-last_rf_tone_ns;
                    assert(tail<=carrier_tail_bound(&p));
                    if(tail>longest_tail) longest_tail=tail;
                }
            }
            if(scenario==STALLED_PRODUCER) {
                assert(now>=1000000000ULL);
                assert(atomic_load(&p.tx_ctrl.last_error)==-ETIMEDOUT);
            }
            if(scenario==STALLED_WRITER)
                assert(atomic_load(&p.tx_ctrl.last_error)==-ETIMEDOUT);
            if(scenario==FAILED_WORKER) {
                assert(now==20000000);
                assert(atomic_load(&p.tx_ctrl.last_error)==-EIO);
            }
            if(scenario==STALL_LAST_WRITE) {
                assert(atomic_load(&p.tx_ctrl.last_error)==-ETIMEDOUT);
                assert(writer_busy && p.txq.count==0);
                assert(atomic_load(&p.tx_ctrl.written_sequence)<p.tx_ctrl.inj.last_sequence);
                assert(now>=final_queued_ns+1250000000ULL &&
                       now<=final_queued_ns+1252000000ULL);
            }
        }
        printf("PASS %u MS/s, FIFO multiplier %u, %s capacity %zu native: "
               "empty/partial/full backlog, "
               "complete 250 ms tone, carrier off <= %.3f ms after RF tone, "
               "writer completion, bounded failures\n",
               rates[r]/1000000,multipliers[m],
               legacy_alloc ? "legacy round-up" : "current round-down",
               kernel_cap/NATIVE_SAMPLES,longest_tail/1000000.0);
    }
    for(unsigned worker=0;worker<3;++worker) {
        reset(&p,&sys,1000000,6,NORMAL,2,false);
        if(worker==2) p.tx_ctrl.active=false;
        else if(worker==1) p.dsp_ctrl.active=false;
        else nbfm_tx_active=false;
        tx_pipeline_stop(&p);assert_shutdown(&p);assert(now==0);
    }
    /* A retained producer error remains authoritative when the tail cannot
     * run; shutdown must not manufacture a competing writer timeout. */
    reset(&p,&sys,1000000,6,NORMAL,2,false);
    atomic_store(&p.dsp_ctrl.last_error,-ENODEV);
    nbfm_tx_active=false;
    tx_pipeline_stop(&p);assert_shutdown(&p);
    assert(atomic_load(&p.dsp_ctrl.last_error)==-ENODEV);
    assert(atomic_load(&p.tx_ctrl.last_error)==0);
    /* Demonstrate the regression: fixed 250 ms padding truncates at 1 MS/s. */
    reset(&p,&sys,1000000,6,NORMAL,0,false);
    p.tx_ctrl.tail_padding_frames=25;
    kernel_head=kernel_tail=kernel_count=0;
    push_kernel(false,NATIVE_SAMPLES/2); /* Enough queued data for a partial cutoff. */
    tx_pipeline_stop(&p);
    assert(tone_samples>0 && tone_samples<25*p.tx_ctrl.frame_samples);
    /* Retaining the full padding but pacing it recreates the long carrier
     * hang. This catches a regression back to normal producer pacing. */
    reset(&p,&sys,1000000,16,NORMAL,0,false);
    force_paced_padding=true;
    tx_pipeline_stop(&p);
    assert_shutdown(&p);
    assert(tone_samples==25*p.tx_ctrl.frame_samples);
    assert(now-last_rf_tone_ns>carrier_tail_bound(&p));
    printf("PASS: paced full-capacity padding recreates %.3f ms carrier hang; "
           "fixed 250 ms padding recreates cutoff; inactive/repeated stop safe\n",
           (now-last_rf_tone_ns)/1000000.0);
}
'''
with tempfile.TemporaryDirectory(prefix='tx-stop-') as directory:
    c = Path(directory) / 'test.c'
    exe = Path(directory) / 'test'
    c.write_text(pre + code + post)
    # This exhaustive simulator visits individual RF samples across 216 cases.
    # Optimization preserves every assertion and virtual deadline while keeping
    # its wall-time watchdog practical on the Zero's slower CPU.
    subprocess.run(['cc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', str(c),
                    '-pthread', '-o', str(exe)], check=True)
    try:
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired as error:
        # communicate() retains progress on timeout, but the default traceback
        # omits it. Preserve those completed scenario rows for diagnosis.
        for partial in (error.stdout, error.stderr):
            if partial:
                print(partial.decode(errors='replace') if isinstance(partial, bytes)
                      else partial, end='')
        raise
    print(result.stdout, end='')
    if result.returncode:
        print(result.stderr, end='')
        result.check_returncode()
