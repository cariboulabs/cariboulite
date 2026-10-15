#!/usr/bin/env python3
"""Exercise the actual TX ABI, admission, callbacks and DMA startup without hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'smi_stream_dev.c').read_text()

def function(signature):
    start = source.index(signature + '\n{')
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

preamble = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "bcm2835_smi.h"
#include "smi_stream_dev.h"
#undef DMA_BOUNCE_BUFFER_SIZE
#define DMA_BOUNCE_BUFFER_SIZE 64
#define __user
#define READ_ONCE(a) (a)
#define WRITE_ONCE(a,b) ((a)=(b))
#define dev_info(...) ((void)0)
#define dev_warn(...) ((void)0)
#define dev_err(...) ((void)0)
#define mb() ((void)0)
#define dma_wmb() ((void)0)
#define min_t(type,a,b) ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#define SMI_REFRESH_CHUNKS 1024
#define DMA_MEM_TO_DEV 1
#define DMA_DEV_TO_MEM 2
#define SMICS_START 8
#define SMICS 0
#define SMIL 4
#define atomic64_inc(p) (++*(p))
#define atomic64_set(p,v) (*(p)=(v))
#define atomic64_read(p) (*(p))
#define atomic64_add(v,p) (*(p)+=(v))
typedef uint32_t u32;
typedef void (*dma_async_tx_callback)(void*);
struct dma_async_tx_descriptor { int unused; };
struct fake_semaphore { unsigned count; };
struct bcm2835_smi_instance {
    struct { void* buffer[1]; struct fake_semaphore callback_sem; } bounce;
    pthread_mutex_t transaction_lock;
    void *dma_chan, *dev;
};
struct kfifo {
    struct { unsigned in, out; } kfifo;
    unsigned size;
    uint8_t bytes[128];
};
struct bcm2835_smi_dev_instance {
    struct bcm2835_smi_instance* smi_inst;
    void* dev;
    struct kfifo tx_fifo, rx_fifo;
    pthread_mutex_t transition_lock, write_lock;
    u32 tx_fifo_limit_bytes;
    smi_stream_state_en state;
    long long tx_session_generation, tx_completed_periods, tx_underrun_periods;
    long long tx_accepted_bytes, tx_fifo_dequeued_bytes;
    unsigned current_read_chunk, counter_missed, count_since_refresh;
    int poll_event;
    bool writeable, readable, transfer_thread_running;
};
static uint32_t ring[16];
static struct bcm2835_smi_instance smi = {.bounce.buffer={ring}, .transaction_lock=PTHREAD_MUTEX_INITIALIZER};
static struct bcm2835_smi_dev_instance instance = {
    .smi_inst=&smi, .transition_lock=PTHREAD_MUTEX_INITIALIZER,
    .write_lock=PTHREAD_MUTEX_INITIALIZER, .tx_fifo.size=128
};
static struct bcm2835_smi_dev_instance* inst=&instance;
static int user_copy_fault, fifo_copy_fault=-1, wakes, starts, prepared, fail_dma;
static void (*fifo_publish_hook)(void);
static unsigned kfifo_len(struct kfifo* f) { return f->kfifo.in-f->kfifo.out; }
static unsigned kfifo_size(struct kfifo* f) { return f->size; }
static unsigned kfifo_avail(struct kfifo* f) { return f->size-kfifo_len(f); }
static bool kfifo_is_empty(struct kfifo* f) { return !kfifo_len(f); }
static unsigned kfifo_in(struct kfifo* f,const void* b,unsigned n) {
    n=min_t(unsigned,n,kfifo_avail(f));
    for(unsigned i=0;i<n;i++) f->bytes[(f->kfifo.in+i)%f->size]=((const uint8_t*)b)[i];
    f->kfifo.in+=n; return n;
}
static unsigned kfifo_out(struct kfifo* f,void* b,unsigned n) {
    n=min_t(unsigned,n,kfifo_len(f));
    for(unsigned i=0;i<n;i++) ((uint8_t*)b)[i]=f->bytes[(f->kfifo.out+i)%f->size];
    f->kfifo.out+=n; return n;
}
static int kfifo_from_user(struct kfifo* f,const void* b,unsigned n,unsigned* copied) {
    bool fault=fifo_copy_fault>=0 && (unsigned)fifo_copy_fault<n;
    if(fault) n=fifo_copy_fault;
    *copied=kfifo_in(f,b,n);
    if(fifo_publish_hook) fifo_publish_hook();
    return fault ? -EFAULT:0;
}
static int copy_from_user(void* to,const void* from,unsigned n) {
    if(user_copy_fault) return 1;
    memcpy(to,from,n); return 0;
}
static int copy_to_user(void* to,const void* from,unsigned n) {return copy_from_user(to,from,n);}
static void mutex_lock(pthread_mutex_t* p) {assert(!pthread_mutex_lock(p));}
static void mutex_unlock(pthread_mutex_t* p) {assert(!pthread_mutex_unlock(p));}
static int mutex_lock_interruptible(pthread_mutex_t* p) {mutex_lock(p);return 0;}
#define spin_lock mutex_lock
#define spin_unlock mutex_unlock
static void wake_up_interruptible(int* p) {(void)p;++wakes;}
static void up(struct fake_semaphore* p) {++p->count;}
static void sema_init(struct fake_semaphore* p,unsigned n) {p->count=n;}
static int smi_disable_sync(struct bcm2835_smi_instance* p) {(void)p;return 0;}
static int smi_init_programmed_transfer(struct bcm2835_smi_instance* p,int d,int n) {(void)p;(void)d;(void)n;return 0;}
static struct dma_async_tx_descriptor descriptor;
static struct dma_async_tx_descriptor* stream_smi_dma_init_cyclic(struct bcm2835_smi_instance* p,int d,dma_async_tx_callback cb,void* arg) {
    (void)p;(void)cb;(void)arg;
    if(d==DMA_MEM_TO_DEV && inst->tx_fifo_limit_bytes) {
        for(unsigned i=0;i<16;i++) assert(ring[i]==0x80000000U);
    }
    ++prepared; return fail_dma ? NULL:&descriptor;
}
static void dma_async_issue_pending(void* p) {
    (void)p; assert(prepared>starts); ++starts;
    if(inst->tx_fifo_limit_bytes) for(unsigned i=0;i<16;i++) assert(ring[i]==0x80000000U);
}
static void smi_enable_streaming(struct bcm2835_smi_instance* p,int d) {(void)p;(void)d;assert(starts>0);}
static void write_smi_reg(struct bcm2835_smi_instance* p,u32 v,unsigned r) {(void)p;(void)v;(void)r;}
static u32 read_smi_reg(struct bcm2835_smi_instance* p,unsigned r) {(void)p;(void)r;return 0;}
static void print_smil_registers_ext(const char* s) {(void)s;}
struct file {int unused;};
struct poll_table_struct {int unused;};
typedef unsigned __poll_t;
static void poll_wait(struct file* f,int* e,struct poll_table_struct* p) {(void)f;(void)e;(void)p;}
'''
production = '\n'.join(function(sig) for sig in [
    'static unsigned int smi_tx_fifo_available(struct bcm2835_smi_dev_instance *i)',
    'static void smi_tx_fill_quiet(void *buffer, unsigned int bytes)',
    'static long smi_tx_set_buffer_limit(unsigned long arg)',
    'static long smi_tx_get_status(unsigned long arg)',
    'static void stream_smi_write_dma_callback(void *param)',
    'int transfer_thread_init(struct bcm2835_smi_dev_instance *inst,\n                         enum dma_transfer_direction dir,\n                         dma_async_tx_callback callback)',
    'static ssize_t smi_stream_write_file(struct file *f, const char __user *user_ptr, size_t count, loff_t *offs)',
    'static unsigned int smi_stream_poll(struct file *filp, struct poll_table_struct *wait)',
])
# The kernel enum is opaque to this userspace stub; use the same numeric direction constants.
production=production.replace('enum dma_transfer_direction dir','int dir')
checks = r'''
static struct file file;
static uint32_t payload[32];
static void reset(void) {
    memset(&inst->tx_fifo.kfifo,0,sizeof(inst->tx_fifo.kfifo));
    inst->tx_fifo_limit_bytes=0; inst->state=smi_stream_idle;
    inst->tx_completed_periods=inst->tx_underrun_periods=inst->tx_accepted_bytes=inst->tx_fifo_dequeued_bytes=0;
    inst->current_read_chunk=inst->counter_missed=0;
    for(unsigned i=0;i<32;i++) payload[i]=0xe0102030+i;
    for(unsigned i=0;i<16;i++) ring[i]=0xdeadbeef;
    user_copy_fault=0; fifo_copy_fault=-1;
}
static long configure(unsigned limit,unsigned version,unsigned size,unsigned reserved) {
    struct smi_stream_tx_buffer_limit request={version,size,limit,reserved};
    return smi_tx_set_buffer_limit((unsigned long)&request);
}
static void retire_during_publication(void) {stream_smi_write_dma_callback(inst);}
int main(void) {
    _Static_assert(sizeof(struct smi_stream_tx_status)==80,"stable ABI");
    _Static_assert(sizeof(struct smi_stream_tx_buffer_limit)==16,"stable ABI");
    _Static_assert(__builtin_offsetof(struct smi_stream_tx_status,session_generation)==40,"32/64-bit alignment");
    reset();
    assert(configure(32,2,16,0)==-EINVAL);
    assert(configure(32,1,15,0)==-EINVAL);
    assert(configure(32,1,16,1)==-EINVAL);
    assert(configure(12,1,16,0)==-EINVAL);
    assert(configure(130,1,16,0)==-EINVAL);
    assert(configure(18,1,16,0)==-EINVAL);
    user_copy_fault=1; assert(configure(32,1,16,0)==-EFAULT); user_copy_fault=0;
    inst->state=smi_stream_rx_channel_0; assert(configure(32,1,16,0)==-EBUSY);
    inst->state=smi_stream_idle; assert(configure(32,1,16,0)==0);
    assert(smi_tx_fifo_available(inst)==32);
    assert(smi_stream_poll(&file,NULL)&POLLOUT);
    assert(smi_stream_write_file(&file,(char*)payload,40,NULL)==32);
    assert(smi_tx_fifo_available(inst)==0 && kfifo_len(&inst->tx_fifo)==32);
    assert(!(smi_stream_poll(&file,NULL)&POLLOUT));
    assert(smi_stream_write_file(&file,(char*)payload,4,NULL)==-EAGAIN);
    assert(smi_stream_write_file(&file,(char*)payload,3,NULL)==-EINVAL);
    assert(smi_stream_write_file(&file,(char*)payload,0,NULL)==0);
    assert(configure(16,1,16,0)==-EBUSY);
    inst->state=smi_stream_tx_channel;
    stream_smi_write_dma_callback(inst);
    assert(ring[0]==payload[0] && ring[3]==payload[3]);
    assert(smi_tx_fifo_available(inst)==16 && kfifo_len(&inst->tx_fifo)==16);
    assert(smi_stream_poll(&file,NULL)&POLLOUT);
    stream_smi_write_dma_callback(inst);
    assert(ring[4]==payload[4] && ring[7]==payload[7]);
    assert(inst->tx_completed_periods==2 && inst->tx_underrun_periods==0 && inst->tx_fifo_dequeued_bytes==32);
    assert(configure(0,1,16,0)==-EBUSY);
    /* A stalled producer emits framed zero IQ rather than stale content. */
    stream_smi_write_dma_callback(inst);
    for(unsigned i=8;i<12;i++) assert(ring[i]==0x80000000U);
    assert(inst->tx_underrun_periods==1);
    /* Partial user fault acknowledges only complete words, then pads a short period. */
    fifo_copy_fault=7;
    assert(smi_stream_write_file(&file,(char*)payload,16,NULL)==4);
    assert(kfifo_len(&inst->tx_fifo)==4);
    fifo_copy_fault=-1;
    stream_smi_write_dma_callback(inst);
    assert(ring[12]==payload[0]);
    for(unsigned i=13;i<16;i++) assert(ring[i]==0x80000000U);
    assert(inst->tx_accepted_bytes==36 && inst->tx_fifo_dequeued_bytes==36 && inst->tx_underrun_periods==2);
    struct smi_stream_tx_status status;
    inst->tx_session_generation=23;inst->transfer_thread_running=true;
    assert(smi_tx_get_status((unsigned long)&status)==0);
    assert(status.abi_version==1 && status.struct_size==80 && status.reserved==0);
    assert(status.state==smi_stream_tx_channel && status.flags==3 && status.session_generation==23);
    assert(status.fifo_queued_bytes==0 && status.fifo_capacity_bytes==128 && status.fifo_limit_bytes==32);
    assert(status.period_bytes==16 && status.ring_bytes==64 && status.completed_periods==4);
    user_copy_fault=1; assert(smi_tx_get_status((unsigned long)&status)==-EFAULT);
    /* Preexisting words from another session cannot survive startup priming. */
    reset(); assert(configure(32,1,16,0)==0);
    assert(transfer_thread_init(inst,DMA_MEM_TO_DEV,stream_smi_write_dma_callback)==0);
    assert(starts==1 && prepared==1 && inst->transfer_thread_running);
    fail_dma=1;
    assert(transfer_thread_init(inst,DMA_MEM_TO_DEV,stream_smi_write_dma_callback)==-2);
    assert(starts==1);fail_dma=0;
    /* Fault publication can race DMA: the whole-word consumer must never
     * observe/emit the incomplete trailing bytes before producer rollback.
     */
    reset(); assert(configure(32,1,16,0)==0);
    fifo_copy_fault=7;fifo_publish_hook=retire_during_publication;
    assert(smi_stream_write_file(&file,(char*)payload,16,NULL)==4);
    fifo_publish_hook=NULL;fifo_copy_fault=-1;
    assert(kfifo_len(&inst->tx_fifo)==0 && ring[0]==payload[0]);
    for(unsigned i=1;i<4;i++) assert(ring[i]==0x80000000U);
    assert(inst->tx_accepted_bytes==4 && inst->tx_fifo_dequeued_bytes==4);
    /* Legacy full-capacity admission and repeat policy stay unchanged. */
    reset(); assert(smi_stream_write_file(&file,(char*)payload,120,NULL)==120);
    assert(smi_tx_fifo_available(inst)==8);
    memset(&inst->tx_fifo.kfifo,0,sizeof(inst->tx_fifo.kfifo));
    ring[12]=1;ring[13]=2;ring[14]=3;ring[15]=4;
    stream_smi_write_dma_callback(inst);
    assert(ring[0]==1 && ring[1]==2 && ring[2]==3 && ring[3]==4);
    assert(smi_tx_get_status((unsigned long)&status)==0);
    assert(status.fifo_limit_bytes==128 && !(status.flags&SMI_STREAM_TX_STATUS_BOUNDED_FIFO));
    puts("PASS: TX ABI validation, 2-period admission/backpressure, copy-fault alignment, retirement/shortage accounting, safe pre-issue priming and legacy repeats");
}
'''
with tempfile.TemporaryDirectory(prefix='smi-tx-demand-') as directory:
    c=Path(directory)/'test.c'; exe=Path(directory)/'test'
    c.write_text(preamble+production+checks)
    subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
                    '-I',str(root),'-pthread',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
