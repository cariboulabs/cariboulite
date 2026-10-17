#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "rx_pipeline.h"
#include "pipeline_runtime.h"
#include "alsa_sink.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include "modem_selftest.h"
#include "nbfm_mod.h"
#include "tone_source.h"

static void selftest_audio_cue(audio_sink_t* sink, float frequency)
{
    audio_source_t* cue = tone_source_open_cue(frequency);
    if (!cue) { fprintf(stderr, "[selftest] cue allocation failed\n"); return; }
    float samples[480];
    int16_t ping[12000];
    for (size_t offset = 0; offset < 12000; offset += 480) {
        audio_source_read(cue, samples, 480);
        for (size_t i = 0; i < 480; ++i)
            ping[offset + i] = (int16_t)lrintf(0.6f * 32767.f * samples[i]);
    }
    audio_source_destroy(cue);
    write_audio_exact(sink, ping, 12000);
}

void nbfm_modem_selftest(sys_st *sys)
{
    (void)sys;

    // 1) Create RX FIFO and start the existing demod thread pointing to ALSA
    rf10_fifo_t rxq;
    rf10_fifo_init(&rxq, /*cap=*/128, /*drop_oldest_on_full=*/false);

    audio_demod_ctrl_t dm = {
        .active      = true,
        .fifo_in     = &rxq,
        .deemph_tau  = 50e-6f,      // or 75e-6f
        .fs_rf       = 4000000.0f,
        .fs_audio    = 48000.0f,
        .deemph_y    = 0.0f,
        .last_i      = 0,
        .last_q      = 0,
        .sink        = NULL,
    };

    // before starting demod_th
    aud10_fifo_t afifo;
    aud10_fifo_init(&afifo, 64);

    // Use the same playback bridge as the RX menus (Loopback -> Jabra).
    dm.sink = alsa_sink_open("plughw:Loopback,0,0", 48000);
    if (!dm.sink) {
        fprintf(stderr, "[selftest] alsa_sink_open failed\n");
        aud10_fifo_destroy(&afifo);
        rf10_fifo_destroy(&rxq);
        return;
    }
    
    dm.pcm_rate = dm.sink->sample_rate;
    dm.pcm_channels = alsa_sink_channels(dm.sink);

    // ping: quick ping of 2.525 kHz quindar tone to verify audio path is working
    selftest_audio_cue(dm.sink, 2525.0f);


    audio_writer_ctrl_t aw = {
        .active   = true,
        .sink     = dm.sink,
        .fifo     = &afifo,
    };
    dm.afifo_out = &afifo;
    dm.pcm_gain = 12000.0f;
    audio_demod_config_t dsp_config = {4000000, 48000, dm.deemph_tau, dm.pcm_gain};
    dm.dsp = audio_demod_create(AUDIO_DEMOD_NBFM, &dsp_config);
    pthread_t aw_th, demod_th;
    if (!dm.dsp || pthread_create(&aw_th, NULL, audio_writer_thread, &aw) != 0) {
        audio_demod_destroy(dm.dsp);
        audio_sink_destroy(dm.sink);
        aud10_fifo_destroy(&afifo);
        rf10_fifo_destroy(&rxq);
        return;
    }
    if (pthread_create(&demod_th, NULL, audio_demod_thread, &dm) != 0) {
        aw.active = false;
        aud10_fifo_stop(&afifo);
        pthread_cancel(aw_th);
        pthread_join(aw_th, NULL);
        audio_demod_destroy(dm.dsp);
        audio_sink_destroy(dm.sink);
        aud10_fifo_destroy(&afifo);
        rf10_fifo_destroy(&rxq);
        return;
    }

    // 2) Build the NBFM modulator you already use in TX
    nbfm_cfg_t cfg = {
        .audio_fs      = 48000.0,
        .rf_fs         = 4000000.0,
        .f_dev_hz      = NBFM_DEFAULT_DEVIATION_HZ,
        .preemph_tau_s = 0.0,
        .out_scale     = 4000.0f,
        .linear_interp = 1,
    };
    nbfm_mod_t* fm = nbfm_create(&cfg);
    float*  a48k   = (float*)calloc(480,    sizeof(float));  // 10 ms audio
    iq16_t* iq_rf   = (iq16_t*)calloc(40000, sizeof(iq16_t)); // 10 ms RF

    if (!fm || !a48k || !iq_rf) {
        fprintf(stderr, "[selftest] alloc/mod create failed\n");
        if (fm) nbfm_destroy(fm);
        free(a48k); free(iq_rf);
        dm.active = false;
        rf10_fifo_stop(&rxq);
        pthread_join(demod_th, NULL);
        aw.active = false;
        aud10_fifo_stop(&afifo);
        pthread_cancel(aw_th);
        pthread_join(aw_th, NULL);
        aud10_fifo_destroy(&afifo);
        audio_demod_destroy(dm.dsp);
        audio_sink_destroy(dm.sink);
        rf10_fifo_destroy(&rxq);
        return;
    }

    // 3) Run for N seconds: generate 600 Hz tone audio -> mod -> pull 40k IQ -> push to demod FIFO
    const double seconds = 15.0;
    const size_t loops   = (size_t)(seconds * 100.0); // 100 * 10ms per second
    audio_source_t* tone = tone_source_open(600.0f, 0.6f, (audio_format_t){48000, 1});
    if (!tone) fprintf(stderr, "[selftest] tone allocation failed\n");

    for (size_t k = 0; tone && k < loops; k++) {
        audio_source_read(tone, a48k, 480);

        nbfm_result_t mod = nbfm_process(fm, a48k, 480, iq_rf, 40000);
        if (mod.error || mod.consumed != 480 || mod.produced != 40000 || mod.held_audio) {
            fprintf(stderr, "[selftest] modulator failed (%d)\n", mod.error);
            break;
        }

        // diagnostics: peak amplitude of the 10 ms IQ frame
        int16_t peak = 0;
        for (size_t i = 0; i < 40000; i++) {
            int16_t ai = (int16_t)abs(iq_rf[i].i);
            int16_t aq = (int16_t)abs(iq_rf[i].q);
            if (ai > peak) peak = ai;
            if (aq > peak) peak = aq;
        }
        static int frames_gen = 0;
        if ((frames_gen++ % 50) == 0) {
            fprintf(stderr, "MOD: iq_peak=%d (out_scale=%g)\n", peak, cfg.out_scale);
        }

        rf10_frame_t frm;
        for (size_t i = 0; i < 40000; i++) {
            frm.data[i].i = iq_rf[i].i;
            frm.data[i].q = iq_rf[i].q;
        }

        // Block until demod thread consumes (no drops in self-test)
        if (!rf10_fifo_put(&rxq, &frm, -1)) break;
    }

    // 4) Teardown
    dm.active = false;
    aw.active = false;
    rf10_fifo_stop(&rxq);
    aud10_fifo_stop(&afifo);
    pthread_join(demod_th, NULL);
    pthread_cancel(aw_th);
    pthread_join(aw_th, NULL);

    // ping: quick ping of 2.475 kHz tone to signal audio path is closing
    selftest_audio_cue(dm.sink, 2475.0f);
    usleep(250 * 1000); // wait a little so the tone doesn't get cut off

    audio_demod_destroy(dm.dsp);
    audio_sink_destroy(dm.sink);
    aud10_fifo_destroy(&afifo);
    rf10_fifo_destroy(&rxq);
    nbfm_destroy(fm);
    audio_source_destroy(tone);
    
    free(a48k);
    free(iq_rf);
    
    fprintf(stderr, "[selftest] done — you should have heard a 600 Hz tone.\n");
}
