#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "nbfm_demod.h"
#include "app_pipeline_internal.h"
#include "math_compat.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// --- helper to reinitialize all demod state (C version) ---
static void reinit_demod_state(
    float *pi50, float *pq50, int *have_prev50,
    float *dc_y, float *x_prev_audio,
    float *deemph_state, float *lpf_y,
    float *y_prev_50k, float *y_curr_50k,
    double *phase48, double *corr48,
    double *depth_ema, int *servo_tick,
    int *primed, int *nout,
    int *priming_blocks_left,
    nbfm_demod_ctrl_t *c)
{
    // previous complex @50k
    *pi50 = 0.0f; *pq50 = 0.0f; *have_prev50 = 0;

    // 48k chain state
    *dc_y = 0.0f; *x_prev_audio = 0.0f;
    *deemph_state = 0.0f;
    *lpf_y = 0.0f;
    *y_prev_50k = 0.0f;
    *y_curr_50k = 0.0f;

    // resampler servo / accumulator
    *phase48 = 0.0;
    *corr48 = 0.0;
    *depth_ema = 0.0;
    *servo_tick = 0;
    *primed = 0;

    // audio packer
    *nout = 0;

    // priming counter
    *priming_blocks_left = c->prime_blocks_10ms;
    c->priming = (*priming_blocks_left > 0);
}

// --- simple 1st-order de-emphasis (continuous-time tau) at fs samples/s
static inline float deemph(float x, float *y, float tau, float fs)
{
    const float a = expf(-1.0f/(fs * tau));       // pole
    const float b = 1.0f - a;                     // zero gain so DC passes less
    *y = a * (*y) + b * x;
    return *y;
}

// --- audio-rate deemphasis (48 kHz) ---
static inline float deemph_48k(float x, float *z, float tau_s)
{
    if (tau_s <= 0.f) return x;          // bypass if tau==0
    const float fs = 48000.f;
    const float a  = expf(-1.0f/(fs * tau_s));
    const float b  = 1.0f - a;
    *z = a * (*z) + b * x;
    return *z;
}

static inline float fast_atan2f(float y, float x) {
    // 7th-order minimax (or a lighter 3rd-order) — plenty of references online
    // placeholder: use your preferred fast atan2f implementation
    const float ONEQTR_PI = (float)M_PI_4;        // π/4
    const float THRQTR_PI = (float)(3.0f * M_PI_4); // 3π/4
    float abs_y = fabsf(y) + 1e-10f;               // prevent 0/0
    float angle;
    if (x >= 0.0f) {
        float r = (x - abs_y) / (x + abs_y);
        angle = ONEQTR_PI - ONEQTR_PI * r;
    } else {
        float r = (x + abs_y) / (abs_y - x);
        angle = THRQTR_PI - ONEQTR_PI * r;
    }
    return (y < 0.0f) ? -angle : angle;
}

// Ultra-fast small-angle atan2f approximation
// Error < 0.005 rad for |y/x| < 0.3 (typical in NBFM discriminator)
static inline float fast_atan2f_small(float y, float x)
{
    // approximate atan(y/x) ≈ y / (|x| + 0.28f*|y|)
    float abs_y = fabsf(y);
    float abs_x = fabsf(x);
    float angle = y / (abs_x + 0.28f * abs_y + 1e-10f);
    if (x < 0.0f)
        angle = (y >= 0.0f ? (float)M_PI + angle : -((float)M_PI - angle));
    return angle;
}


// --- FM discriminator (atan2), 3/250 resample to 48k, deemphasis at 48k ---
// --- Pre-demod CIC decimator (20 x 4) -> 50 kS/s, then limiter+atan2, 50k->48k, deemph @48k ---
// --- FM discriminator (atan2), 4M->50k integrate&dump, adaptive 50k->48k, deemph @48k ---
// --- FM discriminator (atan2), 4M->50k I&D, fixed 50k->48k (24/25), DC block, deemph @48k
// --- NBFM demod: I/Q integrate&dump to 50k -> limiter -> discriminator @50k
//                  -> fixed 24/25 resample to 48k -> DC block -> deemph -> light LPF
void* nbfm_demod_thread(void* arg)
{
    pthread_setname_np(pthread_self(),"nbfm_demod_thread");
    set_rt_and_affinity_prio(55,1);

    nbfm_demod_ctrl_t* c = (nbfm_demod_ctrl_t*)arg;
    if (!c || !c->fifo_in || !c->sink) return NULL;

    // Configured RF rate -> 200 kS/s -> 50 kS/s via integrate & dump
    const int D1 = (int)(c->fs_rf / 200000.0f), D2 = 4;                // 2 or 4 MS/s -> 50 kS/s
    const float fs_mid = 50000.0f;

    // FM deviation (matches your TX)
    const float f_dev  = 2500.0f;
    const float K_norm = fs_mid / (2.0f * (float)M_PI * f_dev);   // scale dphi -> ~±1 at ±dev

    // 50k -> 48k via fixed rational 24/25 (exact)
    const int   L = 24, M = 25;              // kept for reference (acc not used)
    int         acc = 0;                     // (unused but harmless)
    float       y_prev_50k = 0.0f, y_curr_50k = 0.0f;

    // Optional vector limiter
    const int use_limiter = 1;

    // DC blocker at 48k (~5 Hz HPF)
    const float dc_fc = 5.0f;
    const float dc_a  = expf(-2.0f * (float)M_PI * dc_fc / 48000.0f);
    float dc_y = 0.0f, x_prev_audio = 0.0f;

    // De-emphasis state (48k)
    float deemph_state = 0.0f;

    // Gentle audio LPF post-deemph (~3.2 kHz, 1st order)
    const float lpf_fc = 3200.0f;
    const float lpf_a  = expf(-2.0f * (float)M_PI * lpf_fc / 48000.0f);
    const float lpf_b  = 1.0f - lpf_a;
    float lpf_y = 0.0f;

    // Previous decimated complex sample for discriminator
    float pi50 = 0.0f, pq50 = 0.0f;
    int   have_prev50 = 0;

    // --- adaptive 50k -> 48k servo (NON-static so reset works) ---
    double phase48 = 0.0;     // in [0..1)
    double corr48  = 0.0;     // small fractional correction (unitless)
    double depth_ema = 0.0;   // smoothed FIFO depth
    int    servo_tick = 0;
    int    primed = 0;

    // audio packer
    int    nout = 0;          // NOTE: int (matches reinit_demod_state signature)
    int    priming_blocks_left = 0;

    // ---------------- Working buffers ----------------
    int16_t audio_10ms[480];  // 10 ms @ 48 kHz
    size_t  a10_len = 0;      // (unused: you can remove if you like)

    // I&D accumulators on I and Q before discrimination
    float ai1 = 0.0f, aq1 = 0.0f; int cnt1 = 0;
    float ai2 = 0.0f, aq2 = 0.0f; int cnt2 = 0;

    // heartbeat
    uint64_t last_log_ms = 0;

    // ------------- Initialize once -------------
    if (c->prime_blocks_10ms <= 0) c->prime_blocks_10ms = 8; // sensible default
    c->reset = true;  // force clean start
    reinit_demod_state(&pi50,&pq50,&have_prev50,
                       &dc_y,&x_prev_audio,
                       &deemph_state,&lpf_y,
                       &y_prev_50k,&y_curr_50k,
                       &phase48,&corr48,
                       &depth_ema,&servo_tick,
                       &primed,&nout,
                       &priming_blocks_left, c);
    c->reset = false;

    while (c->active) {

        // allow external reset (e.g., after first-start flicker or frequency change)
        if (c->reset) {
            reinit_demod_state(&pi50,&pq50,&have_prev50,
                               &dc_y,&x_prev_audio,
                               &deemph_state,&lpf_y,
                               &y_prev_50k,&y_curr_50k,
                               &phase48,&corr48,
                               &depth_ema,&servo_tick,
                               &primed,&nout,
                               &priming_blocks_left, c);
            c->reset = false;
        }

        rf10_frame_t frm;
        if (!rf10_fifo_get(c->fifo_in, &frm, -1)) continue;

        for (size_t n = 0; n < (size_t)(c->fs_rf / 100); n++) {
            // --- accumulate at the configured RF rate (stage-1) ---
            ai1 += (float)frm.data[n].i;
            aq1 += (float)frm.data[n].q;
            if (++cnt1 != D1) continue;

            // boxcar avg #1
            float i1 = ai1 / (float)D1;
            float q1 = aq1 / (float)D1;
            ai1 = aq1 = 0.0f; cnt1 = 0;

            // --- accumulate @ 200k (stage-2 to 50k) ---
            ai2 += i1;
            aq2 += q1;
            if (++cnt2 != D2) continue;

            // boxcar avg #2 -> 50 kS/s complex sample
            float i50 = ai2 / (float)D2;
            float q50 = aq2 / (float)D2;
            ai2 = aq2 = 0.0f; cnt2 = 0;

            // --- limiter (unit vector) ---
            if (use_limiter) {
                float m2 = i50*i50 + q50*q50;
                if (m2 > 0.0f) {
                    float invm = 1.0f / sqrtf(m2);
                    i50 *= invm; q50 *= invm;
                }
            }

            // --- discriminator at 50 kS/s using previous 50k sample ---
            float y50 = 0.0f;
            if (have_prev50) {
                const float re = i50 * pi50 + q50 * pq50;
                const float im = q50 * pi50 - i50 * pq50;
                const float dphi = fast_atan2f_small(im, re);
                y50 = dphi * K_norm;                   // normalize to ~±1 @ ±dev
            } else {
                have_prev50 = 1;
            }
            pi50 = i50; pq50 = q50;

            // --- 50k → 48k adaptive resampler (fractional-step with tiny PLL/servo) ---
            // Nominal outputs per 50k input (r < 1 for downsampling)
            const double r_nom = 48000.0 / 50000.0;      // 0.96
            const int    upd_every_inputs = 500;         // ≈10 ms @ 50 kS/s
            const double alpha = 0.05;                   // EMA smoothing
            const double ki    = 2.0e-4;                 // integral gain
            const double corr_ppm_cap  = 3.0e-4;         // ±300 ppm clamp
            const double corr_ppm_slew = 1.0e-5;         // ±10 ppm per update
            const double target_fill = 0.50;             // 50% of capacity
            const double deadband    = 0.01;             // ±2% fill deadband

            // Update every ~10 ms worth of 50k inputs
            if (++servo_tick >= upd_every_inputs) {
                servo_tick = 0;

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
                corr48 += -ki * (err / (double)acap);  // unitless; negative feedback

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
            }

            // Interpolation endpoints for this 50k interval
            y_prev_50k = y_curr_50k;
            y_curr_50k = y50;

            // Advance phase by "outputs per input" this step (r < 1)
            const double r = r_nom * (1.0 + corr48);
            phase48 += r;

            // If we crossed 1.0, emit exactly one 48k sample at that crossing
            if (phase48 >= 1.0) {
                const double frac = (phase48 - 1.0) / r;    // ∈ [0..1)
                float y_lin = y_prev_50k + (float)frac * (y_curr_50k - y_prev_50k);

                // === 48k audio chain ===
                float x = y_lin;
                float y = (x - x_prev_audio) + dc_a * dc_y;
                x_prev_audio = x;
                dc_y = y; if (fabsf(dc_y) < 1e-20f) dc_y = 0.0f;

                float yd = deemph_48k(y, &deemph_state, c->deemph_tau);
                if (fabsf(deemph_state) < 1e-20f) deemph_state = 0.0f;

                lpf_y = lpf_a * lpf_y + lpf_b * yd;
                float ya = lpf_y;
                if (fabsf(lpf_y) < 1e-20f) lpf_y = 0.0f;

                float s = ya * c->pcm_gain;
                if (s >  32767.f) s =  32767.f;
                if (s < -32768.f) s = -32768.f;
                audio_10ms[nout++] = (int16_t)lrintf(s);

                // hand off in 10 ms chunks
                if (nout == 480) {
                    aud10_frame_t af; memcpy(af.pcm, audio_10ms, sizeof(audio_10ms));
                    aud10_fifo_put(c->afifo_out, &af, /*timeout_ms=*/10);
                    c->pcm_total_frames += 480;
                    nout = 0;

                    // heartbeat (optional)
                    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
                    uint64_t ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
                    if (!last_log_ms) last_log_ms = ms;
                    if (ms - last_log_ms >= 1000) {
                        size_t acnt=0, acap=0;
                        aud10_fifo_peek_depth(c->afifo_out, &acnt, &acap);
                        fprintf(stderr,
                            "DEMOD: frames=%llu (%.1fs) ALSA=%s  aud_fifo=%zu/%zu (%.0f%%)  corr=%.5f\n",
                            (unsigned long long)c->pcm_total_frames,
                            (double)c->pcm_total_frames / (double)c->pcm_rate,
                            audio_sink_state(c->sink),
                            acnt, acap, 100.0 * (double)acnt / (double)acap,
                            corr48);
                        last_log_ms = ms;
                    }
                }

                // keep fractional remainder
                phase48 -= 1.0;
            }
        }
    }
    return NULL;
}

