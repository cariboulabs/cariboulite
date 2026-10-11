/* Exercise the real FIFO without ALSA, DSP, or radio hardware. */
#include "pipeline_transport.h"
#include <assert.h>
#include <errno.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>

static atomic_uint wait_entries;
static aud10_fifo_t* stop_during_timeout;
int __real_pthread_cond_wait(pthread_cond_t*, pthread_mutex_t*);
int __wrap_pthread_cond_wait(pthread_cond_t* c, pthread_mutex_t* m)
{
    atomic_fetch_add(&wait_entries, 1);
    return __real_pthread_cond_wait(c, m);
}

int __real_pthread_cond_timedwait(pthread_cond_t*, pthread_mutex_t*, const struct timespec*);
int __wrap_pthread_cond_timedwait(pthread_cond_t* c, pthread_mutex_t* m,
                                const struct timespec* deadline)
{
    if (stop_during_timeout) {
        /* Model the timed-wait race: stop is set before reacquiring the lock,
         * but the condition wait still reports ETIMEDOUT. */
        aud10_fifo_t* f = stop_during_timeout;
        stop_during_timeout = NULL;
        pthread_mutex_unlock(m);
        aud10_fifo_stop(f);
        pthread_mutex_lock(m);
        return ETIMEDOUT;
    }
    return __real_pthread_cond_timedwait(c, m, deadline);
}

static aud10_stats_t stats(aud10_fifo_t* f)
{
    aud10_stats_t s;
    aud10_fifo_get_stats(f, &s);
    assert(s.min_depth <= s.count && s.count <= s.max_depth);
    assert(s.max_depth <= s.cap);
    return s;
}

static void test_transfers_timeouts_and_reset(void)
{
    aud10_fifo_t f;
    aud10_frame_t input = { .pcm = { 11 } }, output;
    aud10_fifo_init(&f, 3);
    aud10_stats_t s = stats(&f);
    assert(s.cap == 3 && s.count == 0 && s.min_depth == 0 && s.max_depth == 0);
    assert(!aud10_fifo_get(&f, &output, 0));
    assert(!aud10_fifo_get(&f, &output, 2));
    for (int i = 0; i < 3; ++i) {
        input.pcm[0] = (int16_t)(11 + i);
        assert(aud10_fifo_put(&f, &input, 0));
    }
    input.pcm[0] = 99;
    assert(!aud10_fifo_put(&f, &input, 0));
    assert(!aud10_fifo_put(&f, &input, 2));
    s = stats(&f);
    assert(s.puts == 3 && s.gets == 0 && s.count == 3);
    assert(s.drops == 2 && s.timeouts_put == 2 && s.timeouts_get == 2);
    assert(s.shutdown_puts == 0 && s.shutdown_gets == 0);
    assert(s.min_depth == 0 && s.max_depth == 3);
    assert(f.drops == s.drops);
    assert(aud10_fifo_get(&f, &output, 0) && output.pcm[0] == 11);
    aud10_fifo_reset_stats(&f);
    s = stats(&f);
    assert(s.count == 2 && s.min_depth == 2 && s.max_depth == 2);
    assert(s.puts == 0 && s.gets == 0 && s.drops == 0);
    assert(s.timeouts_put == 0 && s.timeouts_get == 0);
    assert(aud10_fifo_get(&f, &output, 0) && output.pcm[0] == 12);
    s = stats(&f);
    assert(s.count == 1 && s.gets == 1 && s.min_depth == 1 && s.max_depth == 2);
    assert(2 + s.puts - s.gets == s.count);
    assert(aud10_fifo_get(&f, &output, 0) && output.pcm[0] == 13);
    s = stats(&f);
    assert(s.count == 0 && s.gets == 2 && s.min_depth == 0 && s.max_depth == 2);
    aud10_fifo_destroy(&f);
}

typedef struct {
    aud10_fifo_t* fifo;
    bool put;
    bool result;
} waiter_t;

static void* wait_forever(void* argument)
{
    waiter_t* waiter = argument;
    aud10_frame_t frame = { .pcm = { 42 } };
    waiter->result = waiter->put ? aud10_fifo_put(waiter->fifo, &frame, -1) :
                                  aud10_fifo_get(waiter->fifo, &frame, -1);
    return NULL;
}

static void await_waiter(void)
{
    while (atomic_load(&wait_entries) == 0) sched_yield();
}

static void test_shutdown(bool put)
{
    aud10_fifo_t f;
    aud10_frame_t frame = { .pcm = { 17 } };
    aud10_fifo_init(&f, 1);
    if (put) assert(aud10_fifo_put(&f, &frame, 0));
    waiter_t waiter = { .fifo = &f, .put = put, .result = true };
    pthread_t thread;
    atomic_store(&wait_entries, 0);
    assert(pthread_create(&thread, NULL, wait_forever, &waiter) == 0);
    await_waiter();
    aud10_fifo_stop(&f);
    assert(pthread_join(thread, NULL) == 0 && !waiter.result);
    aud10_stats_t s = stats(&f);
    assert(s.drops == 0 && s.timeouts_put == 0 && s.timeouts_get == 0);
    assert(s.count == (put ? 1U : 0U));
    assert(s.shutdown_puts == (put ? 1U : 0U));
    assert(s.shutdown_gets == (put ? 0U : 1U));
    /* A stopped FIFO rejects even ready transfers, without discarding its data. */
    assert(!aud10_fifo_put(&f, &frame, 0));
    assert(!aud10_fifo_get(&f, &frame, 0));
    s = stats(&f);
    assert(s.shutdown_puts == (put ? 2U : 1U));
    assert(s.shutdown_gets == (put ? 1U : 2U));
    assert(s.drops == 0 && s.timeouts_put == 0 && s.timeouts_get == 0);
    aud10_fifo_reset_stats(&f);
    s = stats(&f);
    assert(s.shutdown_puts == 0 && s.shutdown_gets == 0);
    assert(s.min_depth == s.count && s.max_depth == s.count);
    assert(!aud10_fifo_put(&f, &frame, 0));
    assert(stats(&f).shutdown_puts == 1);
    aud10_fifo_destroy(&f);
}

static void test_cancellation(bool put)
{
    aud10_fifo_t f;
    aud10_frame_t frame = { .pcm = { 17 } };
    aud10_fifo_init(&f, 1);
    if (put) assert(aud10_fifo_put(&f, &frame, 0));
    waiter_t waiter = { .fifo = &f, .put = put };
    pthread_t thread;
    atomic_store(&wait_entries, 0);
    assert(pthread_create(&thread, NULL, wait_forever, &waiter) == 0);
    await_waiter();
    assert(pthread_cancel(thread) == 0);
    void* result = NULL;
    assert(pthread_join(thread, &result) == 0 && result == PTHREAD_CANCELED);
    /* Cancellation cleanup must release the queue mutex. */
    assert(pthread_mutex_trylock(&f.m) == 0);
    pthread_mutex_unlock(&f.m);
    aud10_stats_t s = stats(&f);
    assert(s.drops == 0 && s.timeouts_put == 0 && s.timeouts_get == 0);
    assert(s.shutdown_puts == 0 && s.shutdown_gets == 0);
    assert(s.count == (put ? 1U : 0U));
    aud10_fifo_destroy(&f);
}

static void test_stop_timeout_race(bool put)
{
    aud10_fifo_t f;
    aud10_frame_t frame = { .pcm = { 17 } };
    aud10_fifo_init(&f, 1);
    if (put) assert(aud10_fifo_put(&f, &frame, 0));
    stop_during_timeout = &f;
    assert(!(put ? aud10_fifo_put(&f, &frame, 10) : aud10_fifo_get(&f, &frame, 10)));
    assert(stop_during_timeout == NULL);
    aud10_stats_t s = stats(&f);
    assert(s.drops == 0 && s.timeouts_put == 0 && s.timeouts_get == 0);
    assert(s.shutdown_puts == (put ? 1U : 0U));
    assert(s.shutdown_gets == (put ? 0U : 1U));
    assert(s.count == (put ? 1U : 0U));
    aud10_fifo_destroy(&f);
}

enum { TRANSFERS = 20000 };
typedef struct {
    aud10_fifo_t* fifo;
    atomic_bool producer_done, consumer_done;
} concurrent_t;

static void* producer(void* argument)
{
    concurrent_t* context = argument;
    aud10_frame_t frame = { 0 };
    for (int i = 0; i < TRANSFERS; ++i) {
        frame.pcm[0] = (int16_t)i;
        assert(aud10_fifo_put(context->fifo, &frame, -1));
    }
    atomic_store(&context->producer_done, true);
    return NULL;
}

static void* consumer(void* argument)
{
    concurrent_t* context = argument;
    aud10_frame_t frame;
    for (int i = 0; i < TRANSFERS; ++i) {
        assert(aud10_fifo_get(context->fifo, &frame, -1));
        assert(frame.pcm[0] == (int16_t)i);
    }
    atomic_store(&context->consumer_done, true);
    return NULL;
}

static void test_concurrent_snapshots(void)
{
    aud10_fifo_t f;
    aud10_fifo_init(&f, 4);
    concurrent_t context = { .fifo = &f };
    pthread_t producer_thread, consumer_thread;
    assert(pthread_create(&producer_thread, NULL, producer, &context) == 0);
    assert(pthread_create(&consumer_thread, NULL, consumer, &context) == 0);
    size_t snapshots = 0;
    do {
        aud10_stats_t s = stats(&f);
        assert(s.puts >= s.gets && s.puts - s.gets == s.count);
        assert(s.drops == 0 && s.timeouts_put == 0 && s.timeouts_get == 0);
        assert(s.shutdown_puts == 0 && s.shutdown_gets == 0);
        ++snapshots;
        sched_yield();
    } while (!atomic_load(&context.producer_done) || !atomic_load(&context.consumer_done));
    assert(pthread_join(producer_thread, NULL) == 0);
    assert(pthread_join(consumer_thread, NULL) == 0);
    aud10_stats_t s = stats(&f);
    assert(s.puts == TRANSFERS && s.gets == TRANSFERS && s.count == 0);
    assert(s.min_depth == 0 && s.max_depth > 0 && snapshots > 0);
    aud10_fifo_destroy(&f);
}

int main(void)
{
    test_transfers_timeouts_and_reset();
    test_shutdown(false);
    test_shutdown(true);
    test_cancellation(false);
    test_cancellation(true);
    test_stop_timeout_race(false);
    test_stop_timeout_race(true);
    test_concurrent_snapshots();
    puts("Audio FIFO accounting, concurrent snapshots, stop/reset and cancellation: passed");
    return 0;
}
