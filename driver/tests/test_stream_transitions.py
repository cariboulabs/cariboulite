#!/usr/bin/env python3
"""Exercise actual set_state with simulated hardware and real pthread mutexes."""
from pathlib import Path
import subprocess
import tempfile
s = (Path(__file__).resolve().parents[1]/'smi_stream_dev.c').read_text()
a=s.index('static int set_state(smi_stream_state_en new_state)\n{')
b=s.index('\n/***************************************************************************/',a)
body=s[a:b]
preamble=r'''
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
typedef enum {smi_stream_idle, smi_stream_rx_channel_0, smi_stream_rx_channel_1, smi_stream_tx_channel} smi_stream_state_en;
#define DMA_MEM_TO_DEV 1
#define DMA_DEV_TO_MEM 2
#define HRTIMER_MODE_REL_PINNED 0
#define WRITE_ONCE(a,b) ((a)=(b))
#define atomic64_inc(a) (++*(a))
#define atomic64_set(a,b) (*(a)=(b))
#define dev_info(...) ((void)0)
#define mb() ((void)0)
#define usecs_to_jiffies(a) (a)
struct instance {
    pthread_mutex_t transition_lock, write_lock;
    smi_stream_state_en state;
    bool transfer_thread_running, writeable;
    int tx_hr, tx_watch_work, tx_fifo, poll_event, tx_watch_period_us, tx_hr_period;
    long long tx_session_generation, tx_completed_periods, tx_underrun_periods, tx_accepted_bytes, tx_fifo_dequeued_bytes;
    void* smi_inst;
};
static struct instance instance = {.transition_lock=PTHREAD_MUTEX_INITIALIZER, .write_lock=PTHREAD_MUTEX_INITIALIZER};
static struct instance* inst=&instance;
static bool timer, work, fail_write;
static int fail_start, starts, address;
static void mutex_lock(pthread_mutex_t* m) { assert(pthread_mutex_lock(m)==0); }
static void mutex_unlock(pthread_mutex_t* m) { assert(pthread_mutex_unlock(m)==0); }
static int mutex_lock_interruptible(pthread_mutex_t* m) { if(fail_write) return -EINTR; mutex_lock(m); return 0; }
static void locked(void) {
    assert(pthread_mutex_trylock(&inst->transition_lock)==EBUSY);
}
static void hrtimer_cancel(int* p) { locked(); timer=false; }
static void cancel_delayed_work_sync(int* p) { locked(); work=false; }
static void transfer_thread_stop(struct instance* p) {
    locked(); assert(!timer && !work); usleep(100); p->transfer_thread_running=false;
}
static unsigned calc_address_from_state(smi_stream_state_en s) { return s; }
static void bcm2835_smi_set_address(void* p,unsigned a) { locked(); address=a; }
static void kfifo_reset(int* p) { locked(); assert(pthread_mutex_trylock(&inst->write_lock)==EBUSY); }
static void wake_up_interruptible(int* p) { locked(); }
static void stream_smi_write_dma_callback(void* p) {}
static void stream_smi_read_dma_callback(void* p) {}
static int transfer_thread_init(struct instance* p,int dir,void(*cb)(void*)) {
    locked(); assert(!p->transfer_thread_running); ++starts; usleep(100);
    if (fail_start) return fail_start;
    p->transfer_thread_running=true; return 0;
}
static void schedule_delayed_work(int* p,int delay) { locked(); work=true; }
static void hrtimer_start(int* p,int period,int mode) { locked(); timer=true; }
'''
checks=r'''
static void* competing(void* arg) {
    for(int n=0;n<40;++n) assert(set_state((smi_stream_state_en)((n+*(int*)arg)%4))==0);
    return NULL;
}
static void* tx_request(void* arg) { assert(set_state(smi_stream_tx_channel)==0); return NULL; }
int main(void) {
    assert(set_state(smi_stream_tx_channel)==0 && timer && work);
    int before=starts;
    assert(set_state(smi_stream_tx_channel)==0 && starts==before);
    assert(set_state(smi_stream_rx_channel_0)==0 && !timer && !work);
    fail_write=true;
    assert(set_state(smi_stream_tx_channel)==-EINTR);
    assert(inst->state==smi_stream_idle && address==smi_stream_idle && !inst->transfer_thread_running);
    fail_write=false; fail_start=-EIO;
    assert(set_state(smi_stream_tx_channel)==-EIO && !timer && !work);
    assert(inst->state==smi_stream_idle && address==smi_stream_idle);
    fail_start=0;
    /* Writer contention: the transition waits while holding a sleepable mutex. */
    pthread_t t;
    mutex_lock(&inst->write_lock);
    assert(pthread_create(&t,NULL,tx_request,NULL)==0);
    usleep(10000);
    mutex_unlock(&inst->write_lock);
    assert(pthread_join(t,NULL)==0);
    pthread_t threads[8]; int ids[8];
    for(int i=0;i<8;++i) { ids[i]=i; assert(pthread_create(&threads[i],NULL,competing,&ids[i])==0); }
    for(int i=0;i<8;++i) assert(pthread_join(threads[i],NULL)==0);
    assert(set_state(smi_stream_idle)==0);
    assert(!timer && !work && !inst->transfer_thread_running);
    puts("PASS: serialized transitions, writer contention, no-op, error recovery, helper shutdown order");
}
'''
with tempfile.TemporaryDirectory(prefix='smi-transitions-') as d:
    c=Path(d)/'test.c'; exe=Path(d)/'test'
    c.write_text(preamble+body+checks)
    subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-pthread',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=15)
