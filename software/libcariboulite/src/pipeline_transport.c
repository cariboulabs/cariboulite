#include "pipeline_transport.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

void aud10_fifo_init(aud10_fifo_t* f, size_t cap){
    memset(f,0,sizeof(*f));
    f->q = (aud10_frame_t*)calloc(cap,sizeof(aud10_frame_t));
    f->cap = cap;
    pthread_mutex_init(&f->m,NULL);
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&f->can_put, &attr);
    pthread_cond_init(&f->can_get, &attr);
    pthread_condattr_destroy(&attr);
}
void aud10_fifo_destroy(aud10_fifo_t* f){
    if(!f) return;
    pthread_mutex_destroy(&f->m);
    pthread_cond_destroy(&f->can_put);
    pthread_cond_destroy(&f->can_get);
    free(f->q);
}
void aud10_fifo_stop(aud10_fifo_t* f){
    pthread_mutex_lock(&f->m);
    f->stop = true;
    pthread_cond_broadcast(&f->can_put);
    pthread_cond_broadcast(&f->can_get);
    pthread_mutex_unlock(&f->m);
}
/* pthread_cond_wait reacquires the mutex before running cancellation cleanup. */
static void fifo_unlock_cleanup(void* mutex)
{
    pthread_mutex_unlock((pthread_mutex_t*)mutex);
}

bool aud10_fifo_put(aud10_fifo_t* f, const aud10_frame_t* frm, int timeout_ms){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    ts.tv_nsec += (long)timeout_ms*1000000L; while(ts.tv_nsec>=1000000000L){ts.tv_nsec-=1000000000L; ts.tv_sec++;}
    volatile bool result = false;
    pthread_mutex_lock(&f->m);
    pthread_cleanup_push(fifo_unlock_cleanup, &f->m);
    while(!f->stop && f->count==f->cap){
        if(timeout_ms<0){ pthread_cond_wait(&f->can_put,&f->m); }
        else if(pthread_cond_timedwait(&f->can_put,&f->m,&ts)==ETIMEDOUT){
            /* A concurrent stop may have woken/timed out this waiter. */
            if(f->stop){ f->shutdown_puts++; }
            else { f->timeouts_put++; f->drops++; }
            goto out;
        }
    }
    if(f->stop){ f->shutdown_puts++; goto out; }
    f->q[f->w] = *frm; f->w=(f->w+1)%f->cap; f->count++;
    f->puts++;
    if(f->count > f->max_depth) f->max_depth = f->count;
    pthread_cond_signal(&f->can_get);
    result = true;
out:
    pthread_cleanup_pop(1);
    return result;
}
bool aud10_fifo_get(aud10_fifo_t* f, aud10_frame_t* out, int timeout_ms){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    ts.tv_nsec += (long)timeout_ms*1000000L; while(ts.tv_nsec>=1000000000L){ts.tv_nsec-=1000000000L; ts.tv_sec++;}
    volatile bool result = false;
    pthread_mutex_lock(&f->m);
    pthread_cleanup_push(fifo_unlock_cleanup, &f->m);
    while(!f->stop && f->count==0){
        if(timeout_ms<0){ pthread_cond_wait(&f->can_get,&f->m); }
        else if(pthread_cond_timedwait(&f->can_get,&f->m,&ts)==ETIMEDOUT){
            if(f->stop){ f->shutdown_gets++; }
            else { f->timeouts_get++; }
            goto out;
        }
    }
    if(f->stop){ f->shutdown_gets++; goto out; }
    *out = f->q[f->r]; f->r=(f->r+1)%f->cap; f->count--;
    f->gets++;
    if(f->count < f->min_depth) f->min_depth = f->count;
    pthread_cond_signal(&f->can_put);
    result = true;
out:
    pthread_cleanup_pop(1);
    return result;
}

// Peek audio FIFO depth without disturbing it
void aud10_fifo_peek_depth(aud10_fifo_t* f, size_t* count, size_t* cap){
    pthread_mutex_lock(&f->m);
    *count = f->count;
    *cap   = f->cap;
    pthread_mutex_unlock(&f->m);
}

void aud10_fifo_reset_stats(aud10_fifo_t* f)
{
    pthread_mutex_lock(&f->m);
    f->puts = f->gets = f->drops = 0;
    f->timeouts_put = f->timeouts_get = 0;
    f->shutdown_puts = f->shutdown_gets = 0;
    f->max_depth = f->min_depth = f->count;
    pthread_mutex_unlock(&f->m);
}

void aud10_fifo_get_stats(aud10_fifo_t* f, aud10_stats_t* s)
{
    pthread_mutex_lock(&f->m);
    s->cap = f->cap;
    s->count = f->count;
    s->puts = f->puts;
    s->gets = f->gets;
    s->drops = f->drops;
    s->timeouts_put = f->timeouts_put;
    s->timeouts_get = f->timeouts_get;
    s->max_depth = f->max_depth;
    s->min_depth = f->min_depth;
    s->shutdown_puts = f->shutdown_puts;
    s->shutdown_gets = f->shutdown_gets;
    pthread_mutex_unlock(&f->m);
}

void rf10_fifo_init(rf10_fifo_t* f, size_t cap, bool drop_oldest)
{
    memset(f, 0, sizeof(*f));
	f->q = (rf10_frame_t*)calloc(cap, sizeof(rf10_frame_t));
    f->cap = cap;
	f->min_depth = cap;
	f->drop_oldest_on_full = drop_oldest;
    pthread_mutex_init(&f->m, NULL);
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&f->can_put, &attr);
    pthread_cond_init(&f->can_get, &attr);
    pthread_condattr_destroy(&attr);
}

void rf10_fifo_reset_stats(rf10_fifo_t* f)
{
    pthread_mutex_lock(&f->m);
    f->puts = f->gets = f->drops = 0;
    f->timeouts_put = f->timeouts_get = 0;
    f->max_depth = f->count;
    f->min_depth = f->count;
    pthread_mutex_unlock(&f->m);
}



void rf10_fifo_get_stats(rf10_fifo_t* f, rf10_stats_t* s)
{
    pthread_mutex_lock(&f->m);
    s->cap          = f->cap;
    s->count        = f->count;
    s->puts         = f->puts;
    s->gets         = f->gets;
    s->drops        = f->drops;
    s->timeouts_put = f->timeouts_put;
    s->timeouts_get = f->timeouts_get;
    s->max_depth    = f->max_depth;
    s->min_depth    = f->min_depth;
    pthread_mutex_unlock(&f->m);
}

void rf10_fifo_flush(rf10_fifo_t* f)
{
    if (!f) return;
    pthread_mutex_lock(&f->m);
    f->r = f->w = 0;
    f->count = 0;
    // keep stats or reset them—your choice:
    f->min_depth = 0;
    f->max_depth = 0;
    pthread_cond_broadcast(&f->can_put);
    pthread_cond_broadcast(&f->can_get);
    pthread_mutex_unlock(&f->m);
}

void rf10_fifo_destroy(rf10_fifo_t* f)
{
    if (!f) return;
    pthread_mutex_destroy(&f->m);
    pthread_cond_destroy(&f->can_put);
    pthread_cond_destroy(&f->can_get);
    free(f->q);
}

bool rf10_fifo_put(rf10_fifo_t* f, const rf10_frame_t* frm, int timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_nsec += (long)timeout_ms * 1000000L;
    while (ts.tv_nsec >= 1000000000L) { ts.tv_nsec -= 1000000000L; ts.tv_sec++; }

    volatile bool result = false;
    pthread_mutex_lock(&f->m);
    pthread_cleanup_push(fifo_unlock_cleanup, &f->m);
    while (!f->stop && f->count == f->cap && !f->drop_oldest_on_full) {
        if (timeout_ms < 0) {
            pthread_cond_wait(&f->can_put, &f->m);
        } else {
            if (pthread_cond_timedwait(&f->can_put, &f->m, &ts) == ETIMEDOUT) {
                f->timeouts_put++;                  // <-- count the timeout
                goto out;
            }
        }
    }
    if (f->stop) { goto out; }

    if (f->count == f->cap && f->drop_oldest_on_full) {
        // overwrite oldest
        f->r = (f->r + 1) % f->cap;
        f->count--;
        f->drops++;                               // <-- count the drop
    }

    f->q[f->w] = *frm;
    f->w = (f->w + 1) % f->cap;
    f->count++;
    f->puts++;                                    // <-- count after success
    if (f->count > f->max_depth) f->max_depth = f->count;

    pthread_cond_signal(&f->can_get);
    result = true;
out:
    pthread_cleanup_pop(1);
    return result;
}

bool rf10_fifo_get(rf10_fifo_t* f, rf10_frame_t* out, int timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_nsec += (long)timeout_ms * 1000000L;
    while (ts.tv_nsec >= 1000000000L) { ts.tv_nsec -= 1000000000L; ts.tv_sec++; }

    volatile bool result = false;
    pthread_mutex_lock(&f->m);
    pthread_cleanup_push(fifo_unlock_cleanup, &f->m);
    while (!f->stop && f->count == 0) {
        if (timeout_ms < 0) {
            pthread_cond_wait(&f->can_get, &f->m);
        } else {
            if (pthread_cond_timedwait(&f->can_get, &f->m, &ts) == ETIMEDOUT) {
                f->timeouts_get++;                 // <-- count the timeout
                goto out;
            }
        }
    }
    if (f->stop) { goto out; }

    *out = f->q[f->r];
    f->r = (f->r + 1) % f->cap;
    f->count--;
    f->gets++;                                    // <-- count after success
    if (f->count < f->min_depth) f->min_depth = f->count;

    pthread_cond_signal(&f->can_put);
    result = true;
out:
    pthread_cleanup_pop(1);
    return result;
}

void rf10_fifo_stop(rf10_fifo_t* f)
{
    pthread_mutex_lock(&f->m);
    f->stop = true;
    pthread_cond_broadcast(&f->can_put);
    pthread_cond_broadcast(&f->can_get);
    pthread_mutex_unlock(&f->m);
}

