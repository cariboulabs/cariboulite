#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "tx_pipeline.h"
#include "pipeline_runtime.h"
#include "mod_worker.h"
#include "tone_source.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

pthread_mutex_t g_tx_injection_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t g_tx_injection_changed = PTHREAD_COND_INITIALIZER;
cariboulite_sample_complex_int16 latest_tx_sample = {0};

int tx_generate_frame(tx_writer_ctrl_st* tx, rf10_frame_t* frame)
{
    if (!tx || !frame || !tx->fm || !tx->a48k || !tx->iq_rf || !tx->frame_samples ||
        tx->frame_samples > sizeof(frame->data) / sizeof(frame->data[0]))
        return -EINVAL;
    nbfm_result_t mod = nbfm_process(tx->fm, tx->a48k, 480,
                                    tx->iq_rf, tx->frame_samples);
    if (mod.error || mod.consumed != 480 || mod.produced != tx->frame_samples || mod.held_audio) {
        fprintf(stderr, "TX modulator error: %d, audio=%zu, IQ=%zu, held=%zu\n",
                mod.error, mod.consumed, mod.produced, mod.held_audio);
        return mod.error < 0 ? mod.error : -EINVAL;
    }
    memset(frame, 0, sizeof(*frame));
    frame->tx_sequence = ++tx->next_sequence;
    for (size_t index = 0; index < tx->frame_samples; ++index) {
        frame->data[index].i = tx->iq_rf[index].i | 0x0001;
        frame->data[index].q = tx->iq_rf[index].q;
    }
    latest_tx_sample.i = frame->data[tx->frame_samples / 2].i;
    latest_tx_sample.q = frame->data[tx->frame_samples / 2].q;
    return 0;
}

static void injection_unlock(void* mutex)
{
    pthread_mutex_unlock(mutex);
}

static inline void fill_tone_48k(tx_writer_ctrl_st* ctrl, float* buf, size_t n)
{
    tone_source_set(ctrl->tone, ctrl->tone_hz, ctrl->tone_amp);
    audio_source_read(ctrl->tone, buf, n);
}

static int read_audio_exact(audio_source_t* mic, float* buf, size_t need,
                            tx_writer_ctrl_st* tx)
{
    size_t have = 0;
    while (have < need) {
        if (!nbfm_tx_active || atomic_load(&tx->abort_requested)) return -ECANCELED;
        audio_source_result_t result = audio_source_read(mic, buf + have, need - have);
        if (result.status == AUDIO_SOURCE_ERROR || result.status == AUDIO_SOURCE_EOF)
            return result.error < 0 ? result.error : -EIO;
        size_t got = result.frames;
        if (got == 0) {
            // tiny sleep to avoid hot spin if device is momentarily empty
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 2 * 1000 * 1000 }; // 2 ms
            nanosleep(&ts, NULL);
        }
        have += got;
    }
    return 0;
}

static void producer_exited(void* argument)
{
    dsp_producer_ctrl_t* ctrl = argument;
    tx_worker_end(ctrl->tx, true);
    atomic_store_explicit(&ctrl->alive, false, memory_order_relaxed);
}

void* nbfm_mod_thread(void* arg)
{
    pthread_setname_np(pthread_self(), "tx_producer");
    //set_rt_and_affinity();   // make sure this logs failures
    //set_rt_and_affinity_prio(45,-1);

    dsp_producer_ctrl_t* ctrl = (dsp_producer_ctrl_t*)arg;
    if (!ctrl || !ctrl->tx || !ctrl->tx->fm || !ctrl->fifo ||
        !ctrl->tx->a48k || !ctrl->tx->iq_rf)
        return NULL;
    set_rt_and_affinity_prio_record(40, ctrl->tx->hardware_paced ? 1 : 0, &ctrl->scheduling);
    atomic_store_explicit(&ctrl->alive, true, memory_order_relaxed);
    pthread_cleanup_push(producer_exited, ctrl);

    const uint64_t PERIOD_NS = 10ull * 1000ull * 1000ull; // 10 ms
    volatile uint64_t next_ns = mono_ns();   // anchor current time
    volatile uint64_t last_wake = 0;
    volatile size_t frame_idx = 0;

    while (ctrl->active) {

        // ============================================================
        // TX OFF: do not generate or enqueue frames
        // ============================================================
        if (!tx_worker_begin(ctrl->tx, true)) {
            // Re-anchor timing so we don't accumulate drift
            next_ns = mono_ns();
            last_wake = 0;
            continue;
        }
        if (ctrl->tx->gate_initialized && ctrl->tx->producer_resumed) {
            next_ns = mono_ns();
            last_wake = 0;
        }

        if (!nbfm_tx_active || atomic_load(&ctrl->tx->abort_requested)) {
            tx_worker_end(ctrl->tx, true);
            continue;
        }

        // ============================================================
        // Normal audio uses 10 ms cadence. Final stop padding is finite and
        // silent; queue/driver backpressure paces it so an empty kernel FIFO
        // does not leave an unmodulated carrier for its full capacity duration.
        // ============================================================
        // ---- schedule next absolute wake ----
        pthread_mutex_lock(&g_tx_injection_lock);
        bool fast_padding = ctrl->tx->inj.fast_padding &&
                            ctrl->tx->inj.frames_left > 0 && ctrl->tx->inj.hz == 0.0f;
        pthread_mutex_unlock(&g_tx_injection_lock);
        if (fast_padding || ctrl->tx->hardware_paced) {
            // Re-anchor each burst frame; never accumulate future deadlines.
            next_ns = mono_ns();
            last_wake = 0;
        } else {
            next_ns += PERIOD_NS;
            struct timespec next_ts = {
                .tv_sec  = (time_t)(next_ns / 1000000000ull),
                .tv_nsec = (long)(next_ns % 1000000000ull)
            };

            // ---- sleep until the absolute deadline, handle EINTR ----
            int rc;
            do {
                rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_ts, NULL);
            } while (rc == EINTR);

            // ---- timing diagnostics ----
            uint64_t now = mono_ns();
            if (last_wake) {
                double dt_ms = (now - last_wake) / 1e6;
                if ((frame_idx++ % 50) == 0)
                    fprintf(stderr, "producer: dt = %.3f ms  rc = %d\n", dt_ms, rc);
            }
            last_wake = now;

            // ---- if sleep failed or we drifted >50 ms, re-anchor ----
            if (rc != 0 || now > next_ns + 5 * PERIOD_NS) {
                next_ns = now;
                fprintf(stderr, "producer: re-anchor (rc=%d)\n", rc);
            }
        }

        if (!nbfm_tx_active || atomic_load(&ctrl->tx->abort_requested)) {
            tx_worker_end(ctrl->tx, true);
            continue;
        }

        // ============================================================
        // 1) Generate 10 ms of audio @ 48 kHz (480 samples)
        // ============================================================
        
        // if (ctrl->tx->tone_mode) {
        //     fill_tone_48k(ctrl->tx, ctrl->tx->a48k, 480);
        // } else if (ctrl->tx->mic) {
        //     read_audio_exact(ctrl->tx->mic, ctrl->tx->a48k, 480);
        // } else {
        //     memset(ctrl->tx->a48k, 0, 480 * sizeof(float));
        // }
        
        // 1) Generate 10 ms of audio @ 48 kHz (480 samples)
        // Injector overrides: hz==0 => silence, else tone(hz)
        volatile int inj_left = 0;
        volatile float inj_hz = 0.0f;
        volatile bool hold_silence = false;
        pthread_mutex_lock(&g_tx_injection_lock);
        pthread_cleanup_push(injection_unlock, &g_tx_injection_lock);
        /* Hardware-paced cues have exact sample counts. When a stage is fully
         * enqueued, wait for the owner instead of filling the control-poll gap
         * with uncounted silence. Gate/abort and stage changes wake this wait. */
        while (ctrl->tx->hardware_paced && ctrl->tx->gate_initialized &&
               ctrl->tx->inj.hold_silence && ctrl->tx->inj.frames_left <= 0 &&
               ctrl->active && nbfm_tx_active && !atomic_load(&ctrl->tx->abort_requested))
            pthread_cond_wait(&g_tx_injection_changed, &g_tx_injection_lock);
        inj_left = ctrl->tx->inj.frames_left;
        inj_hz = ctrl->tx->inj.hz;
        hold_silence = ctrl->tx->inj.hold_silence;
        pthread_cleanup_pop(1);
        if (!nbfm_tx_active || atomic_load(&ctrl->tx->abort_requested)) {
            tx_worker_end(ctrl->tx, true);
            continue;
        }

        if (inj_left > 0) {

            tone_source_set(ctrl->tx->tone, inj_hz, ctrl->tx->tone_amp);
            audio_source_read(ctrl->tx->tone, ctrl->tx->a48k, 480);

            __sync_synchronize();
        } else if (hold_silence) {
            memset(ctrl->tx->a48k, 0, 480 * sizeof(float));
        } else {
            // normal path
            if (ctrl->tx->tone_mode) {
                fill_tone_48k(ctrl->tx, ctrl->tx->a48k, 480);
            } else if (ctrl->tx->mic) {
                int error = ctrl->tx->audio_buffer
                    ? tx_audio_buffer_read(ctrl->tx->audio_buffer, ctrl->tx->a48k, 480, 100)
                    : read_audio_exact(ctrl->tx->mic, ctrl->tx->a48k, 480, ctrl->tx);
                if (error) {
                    if (error != -ECANCELED) {
                        fprintf(stderr, "TX audio source failed; stopping stream\n");
                        atomic_store_explicit(&ctrl->last_error, error, memory_order_relaxed);
                        tx_worker_fail(ctrl->tx, error);
                    }
                    tx_worker_end(ctrl->tx, true);
                    continue;
                }
            } else {
                memset(ctrl->tx->a48k, 0, 480 * sizeof(float));
            }
        }


        // ============================================================
        // 2) Modulate 10 ms of audio at the selected RF rate
        // ============================================================
        rf10_frame_t frm;
        int error = tx_generate_frame(ctrl->tx, &frm);
        if (error) {
            atomic_store_explicit(&ctrl->last_error, error, memory_order_relaxed);
            tx_worker_fail(ctrl->tx, error);
            tx_worker_end(ctrl->tx, true);
            continue;
        }

        // ============================================================
        // 3) Pack one rf10_frame_t and push to FIFO (tag TX_EN)
        // ============================================================
        // Blocking put; don’t drop frames
        bool ok = rf10_fifo_put(ctrl->fifo, &frm, -1);
        if (!ok) {
            tx_worker_end(ctrl->tx, true);
            if (!ctrl->active) break;
            /* Owner interrupted the queue while retaining stopped workers. */
            if (!nbfm_tx_active || atomic_load(&ctrl->tx->abort_requested)) continue;
            break;
        }
        if (inj_left > 0) {
            pthread_mutex_lock(&g_tx_injection_lock);
            ctrl->tx->inj.last_sequence = frm.tx_sequence;
            if (ctrl->tx->inj.frames_left > 0) --ctrl->tx->inj.frames_left;
            pthread_cond_broadcast(&g_tx_injection_changed);
            pthread_mutex_unlock(&g_tx_injection_lock);
        }
        tx_worker_end(ctrl->tx, true);
    }
    pthread_cleanup_pop(1);
    return NULL;
}
