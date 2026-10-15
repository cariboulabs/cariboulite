#!/usr/bin/env python3
"""Run the driver's actual open/release bodies with mocked kernel services.

No device access. Run: python3 driver/tests/test_exclusive_open.py
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'smi_stream_dev.c').read_text()

def function(name):
    start = source.index('static int ' + name + '(')
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

preamble = r'''
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define DEFINE_MUTEX(name) pthread_mutex_t name = PTHREAD_MUTEX_INITIALIZER
#define mutex_trylock(m) (pthread_mutex_trylock(m) == 0)
#define mutex_lock(m) ((void)assert(pthread_mutex_lock(m) == 0))
#define mutex_unlock(m) ((void)assert(pthread_mutex_unlock(m) == 0))
#define WRITE_ONCE(a,b) ((a)=(b))
#define dev_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
#define dev_info(...) ((void)0)
#define printk(...) ((void)0)
#define DEVICE_MINOR 0
#define DMA_BOUNCE_BUFFER_SIZE 64
#define smi_stream_idle 0
struct inode { int minor; };
struct file { int unused; };
#define iminor(i) ((i)->minor)
struct kfifo { void *data; };
struct instance {
    uint8_t *rx_fifo_buffer, *tx_fifo_buffer;
    struct kfifo rx_fifo, tx_fifo;
    int address_changed;
    unsigned tx_fifo_limit_bytes;
};
static struct instance instance;
static struct instance *inst = &instance;
static int fifo_mtu_multiplier = 3;
static int allocations, live_allocations, fail_at, state_calls;
static void (*allocation_hook)(void), (*stop_hook)(void);
static void *vmalloc(size_t size) {
    ++allocations;
    if (allocation_hook) allocation_hook();
    if (allocations == fail_at) return NULL;
    void *p = malloc(size);
    assert(p);
    ++live_allocations;
    return p;
}
static void vfree(void *p) {
    assert(p && live_allocations > 0);
    --live_allocations;
    free(p);
}
static void kfifo_init(struct kfifo *f, void *p, size_t size) {
    assert(size > 0); f->data = p;
}
static int set_state(int state) {
    assert(state == smi_stream_idle);
    ++state_calls;
    if (stop_hook) stop_hook();
    return 0;
}
'''
# Include the production ownership declarations, not a separate test copy.
start = source.index('static DEFINE_MUTEX(open_lock);')
end = source.index(';', source.index('static bool device_open;', start)) + 1
production = source[start:end] + '\n' + function('smi_stream_open') + '\n' + function('smi_stream_release')
checks = r'''
static struct inode node = { DEVICE_MINOR };
static struct file file;
static void reject_open(void) {
    int before = allocations;
    assert(smi_stream_open(&node, &file) == -EBUSY);
    assert(allocations == before);
}
static void check_closing(void) {
    assert(live_allocations == 2); /* stop before free */
    reject_open();
}
static pthread_barrier_t gate;
static int results[16];
static void *contender(void *arg) {
    int i = *(int *)arg;
    pthread_barrier_wait(&gate);
    results[i] = smi_stream_open(&node, &file);
    pthread_barrier_wait(&gate); /* winner stays open until all have tried */
    if (results[i] == 0) assert(smi_stream_release(&node, &file) == 0);
    return NULL;
}
int main(void) {
    struct inode wrong = { DEVICE_MINOR + 1 };
    assert(smi_stream_open(&wrong, &file) == -ENXIO);
    assert(allocations == 0);
    for (int failure = 1; failure <= 2; ++failure) {
        allocations = 0; fail_at = failure;
        assert(smi_stream_open(&node, &file) == -ENOMEM);
        assert(live_allocations == 0);
        assert(!inst->rx_fifo_buffer && !inst->tx_fifo_buffer);
        fail_at = 0;
        assert(smi_stream_open(&node, &file) == 0);
        assert(smi_stream_release(&node, &file) == 0);
    }
    allocation_hook = reject_open; /* open attempted during allocation */
    assert(smi_stream_open(&node, &file) == 0);
    allocation_hook = NULL;
    void *rx = inst->rx_fifo_buffer, *tx = inst->tx_fifo_buffer;
    int before = state_calls;
    reject_open();
    assert(inst->rx_fifo_buffer == rx && inst->tx_fifo_buffer == tx);
    assert(state_calls == before); /* loser cannot stop the owner's stream */
    stop_hook = check_closing;
    assert(smi_stream_release(&node, &file) == 0);
    stop_hook = NULL;
    assert(live_allocations == 0);
    for (int round = 0; round < 100; ++round) {
        pthread_t threads[16]; int ids[16], winners = 0;
        assert(pthread_barrier_init(&gate, NULL, 16) == 0);
        for (int i = 0; i < 16; ++i) {
            ids[i] = i;
            assert(pthread_create(&threads[i], NULL, contender, &ids[i]) == 0);
        }
        for (int i = 0; i < 16; ++i) {
            assert(pthread_join(threads[i], NULL) == 0);
            assert(results[i] == 0 || results[i] == -EBUSY);
            winners += results[i] == 0;
        }
        assert(winners == 1 && live_allocations == 0);
        assert(pthread_barrier_destroy(&gate) == 0);
    }
    puts("PASS: exclusive open, allocation failure recovery, open/close exclusion, 100 concurrent rounds");
}
'''
with tempfile.TemporaryDirectory(prefix='smi-open-test-') as directory:
    c = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    c.write_text(preamble + production + checks)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-pthread', str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)
