#pragma once
// Internal 10 ms audio/IQ transport shared by pipeline workers.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "iq16.h"

typedef struct {
    int16_t pcm[480];   // mono, 48 kHz, 10 ms
} aud10_frame_t;

typedef struct aud10_fifo_s {
    aud10_frame_t* q;
    size_t cap, r, w, count;
    pthread_mutex_t m;
    pthread_cond_t  can_put, can_get;
    bool stop;
    /* Protected by m. A drop is a frame rejected by a full-queue timeout;
     * stopping or cancelling a worker does not count as lost audio. */
    size_t puts, gets, drops, timeouts_put, timeouts_get;
    size_t max_depth, min_depth;
    size_t shutdown_puts, shutdown_gets;
} aud10_fifo_t;

typedef struct {
    size_t cap, count;
    size_t puts, gets, drops;
    size_t timeouts_put, timeouts_get;
    size_t max_depth, min_depth;
    size_t shutdown_puts, shutdown_gets;
} aud10_stats_t;

typedef struct rf10_fifo_s rf10_fifo_t;
typedef struct rf10_frame_s rf10_frame_t;

struct rf10_frame_s {
    // One 10 ms RF frame @ 4 MS/s = 40,000 IQ16 pairs
    // Reuse your iq16_t type: struct { int16_t i, q; };
    iq16_t data[40000];
    uint64_t tx_sequence; // TX completion marker; unused by RX
    float rssi_dbm;       // RX measurement taken at capture; ignored for TX
    bool rssi_valid;      // false when disabled, unavailable or read failed
};

struct rf10_fifo_s {
    rf10_frame_t*   q;
    size_t          cap;         // number of frames (e.g., 8)
    size_t          r;           // read index (consumer)
    size_t          w;           // write index (producer)
    size_t          count;       // how many frames ready
    pthread_mutex_t m;
    pthread_cond_t  can_put;
    pthread_cond_t  can_get;
    bool            drop_oldest_on_full;  // if true, overwrite oldest when full
    bool            stop;

	// diagnostics
	size_t max_depth, min_depth;
	size_t drops, puts, gets, timeouts_put, timeouts_get;
};


typedef struct {
    size_t cap, count;
    size_t puts, gets, drops;
    size_t timeouts_put, timeouts_get;
    size_t max_depth, min_depth;
} rf10_stats_t;

void aud10_fifo_init(aud10_fifo_t*, size_t cap);
void aud10_fifo_destroy(aud10_fifo_t*);
void aud10_fifo_stop(aud10_fifo_t*);
bool aud10_fifo_put(aud10_fifo_t*, const aud10_frame_t*, int timeout_ms);
bool aud10_fifo_get(aud10_fifo_t*, aud10_frame_t*, int timeout_ms);
void aud10_fifo_peek_depth(aud10_fifo_t*, size_t* count, size_t* cap);
/* Reset counters/extrema only: queued frames and stop state are unchanged. */
void aud10_fifo_reset_stats(aud10_fifo_t*);
void aud10_fifo_get_stats(aud10_fifo_t*, aud10_stats_t*);
void rf10_fifo_init(rf10_fifo_t*, size_t cap, bool drop_oldest);
void rf10_fifo_reset_stats(rf10_fifo_t*);
void rf10_fifo_get_stats(rf10_fifo_t*, rf10_stats_t*);
void rf10_fifo_flush(rf10_fifo_t*);
void rf10_fifo_destroy(rf10_fifo_t*);
bool rf10_fifo_put(rf10_fifo_t*, const rf10_frame_t*, int timeout_ms);
bool rf10_fifo_get(rf10_fifo_t*, rf10_frame_t*, int timeout_ms);
void rf10_fifo_stop(rf10_fifo_t*);
