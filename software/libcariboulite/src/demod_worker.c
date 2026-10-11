#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "demod_worker.h"
#include "noise_squelch.h"
#include "carrier_squelch.h"
#include "pipeline_transport.h"
#include "pipeline_runtime.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

static void demod_exited(void* arg)
{
    audio_demod_ctrl_t* c = arg;
    atomic_store_explicit(&c->alive, false, memory_order_relaxed);
}

void* audio_demod_thread(void* arg)
{
    audio_demod_ctrl_t* c = arg;
    if (!c || !c->fifo_in || !c->sink || !c->dsp) return NULL;
    pthread_setname_np(pthread_self(), "audio_demod");
    set_rt_and_affinity_prio_record(55, 1, &c->scheduling);
    atomic_store_explicit(&c->alive, true, memory_order_relaxed);
    pthread_cleanup_push(demod_exited, c);

    // The FIFO carries complete 10 ms RF blocks. Sample depth immediately before
    // processing the last IQ pair: this is the same 500th 50k interval at which
    // the previous worker updated correction, before its resampler output.
    const size_t frame_samples = (size_t)(c->fs_rf / 100);
    double corr48 = 0.0, depth_ema = 0.0;
    int primed = 0;
    size_t nout = 0;
    aud10_frame_t audio;
    float raw[480];
    noise_squelch_t noise = {0};
    carrier_squelch_t carrier = {0};
    const bool noise_supported =
        audio_demod_capabilities(c->dsp) & AUDIO_DEMOD_CAP_NOISE_SQUELCH;
    float open_rms = NOISE_SQUELCH_OPEN_RMS, close_rms = NOISE_SQUELCH_CLOSE_RMS;
    unsigned previous_flags = ~0u;
    float gate_gain = 0.0f;
    uint64_t last_log_ms = 0;
    if (c->prime_blocks_10ms <= 0) c->prime_blocks_10ms = 8;
    c->reset = true;
    while (c->active) {
        if (c->reset) {
            audio_demod_reset(c->dsp);
            noise_squelch_reset(&noise);
            carrier_squelch_reset(&carrier);
            gate_gain = 0.0f;
            atomic_store(&c->squelch_open, 0);
            atomic_store(&c->noise_squelch_valid, 0);
            atomic_store(&c->noise_squelch_rms_milli, 0);
            atomic_store(&c->noise_squelch_detector_open, 0);
            corr48 = depth_ema = 0.0;
            primed = 0;
            nout = 0;
            c->priming = c->prime_blocks_10ms > 0;
            c->reset = false;
        }
        rf10_frame_t frm;
        if (!rf10_fifo_get(c->fifo_in, &frm, -1)) continue;
        unsigned flags = atomic_load(&c->squelch_flags);
        if (flags != previous_flags) {
            noise_squelch_reset(&noise);
            carrier_squelch_reset(&carrier);
            previous_flags = flags;
        }
        // Invalid control words leave the last valid pair unchanged. Reapply
        // after detector reset without clearing history for unchanged levels.
        noise_squelch_unpack_levels(atomic_load(&c->noise_squelch_levels), &open_rms, &close_rms);
        noise_squelch_set_thresholds(&noise, open_rms, close_rms);
        bool carrier_open = !(flags & RX_SQUELCH_CARRIER) ||
            carrier_squelch_process(&carrier, frm.rssi_dbm, frm.rssi_valid);
        size_t offset = 0;
        while (offset < frame_samples) {
            size_t count = frame_samples - 1 - offset;
            if (!count) {
                const double alpha = 0.05;
                const double ki = 2.0e-4;
                const double corr_ppm_cap = 3.0e-4;
                const double corr_ppm_slew = 1.0e-5;
                const double target_fill = 0.50;
                const double deadband = 0.01;
                size_t acnt = 0, acap = 0;
                aud10_fifo_peek_depth(c->afifo_out, &acnt, &acap);
                const double fill = (acap ? (double)acnt / (double)acap : 0.0);

                // smooth depth
                if (depth_ema == 0.0) depth_ema = (double)acnt;  // init on first call
                depth_ema = (1.0 - alpha) * depth_ema + alpha * (double)acnt;

                // engage after we’re in the neighborhood (prevents big initial pulls)
                if (!primed) {
                    if (fill >= 0.35) primed = 1;   // start controlling once buffer >35%
                }

                double err = 0.0;
                if (primed) {
                    const double target = target_fill * (double)acap;
                    const double err_raw = (double)depth_ema - target; // +err => overfilling
                    if (fabs(err_raw) > deadband * (double)acap)
                        err = err_raw;
                }

                // integral update with slew limit
                double corr_prev = corr48;
                if (acap) corr48 += -ki * (err / (double)acap);  // unitless; negative feedback

                // slew limit (per update) to avoid pitch steps
                double dc = corr48 - corr_prev;
                if (dc >  corr_ppm_slew) corr48 = corr_prev + corr_ppm_slew;
                if (dc < -corr_ppm_slew) corr48 = corr_prev - corr_ppm_slew;

                // hard clamp
                if (corr48 >  corr_ppm_cap) corr48 =  corr_ppm_cap;
                if (corr48 < -corr_ppm_cap) corr48 = -corr_ppm_cap;

                // emergency nudges
                if (fill > 0.95) corr48 = fmin(corr48, -5e-4);
                if (fill < 0.05) corr48 = fmax(corr48,  5e-4);
                count = 1;
            }
            if (audio_demod_set_audio(c->dsp, c->deemph_tau, c->pcm_gain) != 0) {
                fprintf(stderr, "DEMOD: invalid audio configuration\n");
                atomic_store_explicit(&c->last_error, -EINVAL, memory_order_relaxed);
                goto done;
            }
            audio_demod_result_t result = audio_demod_process_with_raw(c->dsp,
                frm.data + offset, count, audio.pcm + nout, raw, 480 - nout, corr48);
            if (result.error || !result.consumed) {
                fprintf(stderr, "DEMOD: processing failed (%d)\n", result.error);
                atomic_store_explicit(&c->last_error,
                    result.error ? result.error : -EIO, memory_order_relaxed);
                goto done;
            }
            bool open = false;
            for (size_t i = 0; i < result.produced; ++i) {
                // Continue measuring while bypassed so live threshold tuning
                // observes the same pre-volume, pre-de-emphasis detector.
                bool detector_open = noise_supported && noise_squelch_process(&noise, raw[i]);
                bool noise_open = !noise_supported || !(flags & RX_SQUELCH_NOISE) || detector_open;
                open = noise_open && carrier_open;
                // Keep clock/queues running while closed; ramp over 5 ms.
                if (!flags) gate_gain = 1.0f; // exact bypass for diagnostics
                else if (open) gate_gain = fminf(1.0f, gate_gain + 1.0f/240);
                else gate_gain = fmaxf(0.0f, gate_gain - 1.0f/240);
                audio.pcm[nout+i] = (int16_t)lrintf(audio.pcm[nout+i] * gate_gain);
            }
            if (result.produced) atomic_store(&c->squelch_open, open);
            offset += result.consumed;
            nout += result.produced;
            if (nout == 480) {
                aud10_fifo_put(c->afifo_out, &audio, 10);
                c->pcm_total_frames += 480;
                nout = 0;
                struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
                uint64_t ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
                if (!last_log_ms) last_log_ms = ms;
                if (ms - last_log_ms >= 1000) {
                    size_t acnt = 0, acap = 0;
                    aud10_fifo_peek_depth(c->afifo_out, &acnt, &acap);
                    fprintf(stderr,
                        "DEMOD: produced_frames=%llu (%.1fs) ALSA=%s  audio_fifo=%zu/%zu (%.0f%%)  corr=%.5f",
                        (unsigned long long)c->pcm_total_frames,
                        (double)c->pcm_total_frames / (double)c->pcm_rate,
                        audio_sink_state(c->sink), acnt, acap,
                        100.0 * (double)acnt / (double)acap, corr48);
                    if (noise_supported)
                        fprintf(stderr, "  noise_rms=%.3f open=%.3f close=%.3f detector=%s gate=%s",
                            atomic_load(&c->noise_squelch_rms_milli) / 1000.0,
                            (double)open_rms, (double)close_rms,
                            noise.open ? "OPEN" : "CLOSED", open ? "OPEN" : "CLOSED");
                    fputc('\n', stderr);
                    last_log_ms = ms;
                }
            }
        }
        // Publish at RF-block rate; no square root in the sample loop.
        if (noise_supported) {
            atomic_store(&c->noise_squelch_rms_milli,
                         (unsigned)lroundf(sqrtf(noise.power) * 1000.0f));
            atomic_store(&c->noise_squelch_detector_open, noise.open);
            atomic_store(&c->noise_squelch_valid, 1);
        }
    }
done:
    pthread_cleanup_pop(1);
    return NULL;
}

void* nbfm_demod_thread(void* arg) { return audio_demod_thread(arg); }
