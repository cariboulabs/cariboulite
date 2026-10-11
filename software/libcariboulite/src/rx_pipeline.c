#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "rx_pipeline.h"
#include "pipeline_runtime.h"
#include "alsa_sink.h"
#include "noise_squelch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

cariboulite_sample_complex_int16 latest_rx_sample = {0};

static void worker_exited(void* flag)
{
    atomic_store_explicit((atomic_bool*)flag, false, memory_order_relaxed);
}

int write_audio_exact(audio_sink_t* sink, const int16_t* mono, size_t frames)
{
    while (frames) {
        pthread_testcancel();
        audio_sink_result_t r = audio_sink_write(sink, mono, frames);
        mono += r.frames;
        frames -= r.frames;
        if (r.status == AUDIO_SINK_ERROR) {
            fprintf(stderr, "Audio sink write error: %d\n", r.error);
            return r.error;
        }
        if (r.status == AUDIO_SINK_AGAIN) usleep(1000);
    }
    return 0;
}

void* audio_writer_thread(void* arg){
    
    audio_writer_ctrl_t* a = (audio_writer_ctrl_t*)arg;
    if (!a) return NULL;
    pthread_setname_np(pthread_self(),"audio_writer");
    
    //set_rt_and_affinity();
    //set_rt_and_affinity_prio(42,-1); 
    set_rt_and_affinity_prio_record(46, 0, &a->scheduling);
    atomic_store_explicit(&a->alive, true, memory_order_relaxed);
    pthread_cleanup_push(worker_exited, &a->alive);

    // optional: make period/blocking behavior nicer
    while(a->active){
        aud10_frame_t frm;
        if(!aud10_fifo_get(a->fifo,&frm, /*timeout_ms=*/-1)){
            break;
        }
        // Write one 10 ms block; adapter reports recovery and progress.
        int error = write_audio_exact(a->sink, frm.pcm, 480);
        if (error < 0) {
            atomic_store_explicit(&a->last_error, error, memory_order_relaxed);
            break;
        }
    }
    pthread_cleanup_pop(1);
    return NULL;
}

void* rx_reader_thread_func(void* arg)
{
    rx_reader_ctrl_st* ctrl = (rx_reader_ctrl_st*)arg;
    if (!ctrl || !ctrl->radio) return NULL;
    pthread_setname_np(pthread_self(),"rx_reader");
    //set_rt_and_affinity();
    //set_rt_and_affinity_prio(30, -1);
    // Highest prio; reader must never be blocked by DSP/ALSA
    set_rt_and_affinity_prio_record(70, 2, &ctrl->scheduling);
    atomic_store_explicit(&ctrl->alive, true, memory_order_relaxed);
    pthread_cleanup_push(worker_exited, &ctrl->alive);

    const size_t want = ctrl->rx_buffer_size; // Selected rate * 10 ms
    cariboulite_sample_complex_int16* buf = ctrl->rx_buffer;
    cariboulite_sample_meta* meta = malloc(sizeof(*meta) * want);
    if (!meta) {
        fprintf(stderr, "RX reader metadata allocation failed\n");
        atomic_store_explicit(&ctrl->last_error, -ENOMEM, memory_order_relaxed);
        goto done;
    }
    pthread_cleanup_push(free, meta);

    size_t have = 0;
    while (ctrl->active) {
        size_t requested = want - have;
        int ret = cariboulite_radio_read_samples(ctrl->radio, buf + have, meta, requested);
        atomic_fetch_add_explicit(&ctrl->read_calls, 1, memory_order_relaxed);
        if (ret > 0) {
            atomic_fetch_add_explicit(&ctrl->read_samples, (size_t)ret, memory_order_relaxed);
            if ((size_t)ret < requested)
                atomic_fetch_add_explicit(&ctrl->short_reads, 1, memory_order_relaxed);
            have += (size_t)ret;
        } else if (!ret) {
            atomic_fetch_add_explicit(&ctrl->zero_reads, 1, memory_order_relaxed);
        } else {
            atomic_fetch_add_explicit(&ctrl->read_errors, 1, memory_order_relaxed);
            atomic_store_explicit(&ctrl->last_error, ret, memory_order_relaxed);
        }

        if (have == want) {
            rf10_frame_t frm;
            frm.rssi_valid = false;
            frm.rssi_dbm = 127;
            if (ctrl->squelch_flags &&
                (atomic_load(ctrl->squelch_flags) & RX_SQUELCH_CARRIER)) {
                // Check the SPI result directly: the legacy RSSI helper cannot
                // distinguish a negative SPI error from a signed RSSI value.
                uint8_t value = 127;
                int cancel_state;
                pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancel_state);
                HW_LOCK();
                int rc = at86rf215_read_buffer(&ctrl->radio->sys->modem,
                    ctrl->radio->type == cariboulite_channel_s1g ? REG_RF09_RSSI : REG_RF24_RSSI,
                    &value, 1);
                HW_UNLOCK();
                pthread_setcancelstate(cancel_state, NULL);
                frm.rssi_dbm = (float)(int8_t)value;
                frm.rssi_valid = rc == 0 && frm.rssi_dbm >= -127 && frm.rssi_dbm <= 4;
            }
            for (size_t i=0;i<want;i++) {
                frm.data[i].i = buf[i].i;
                frm.data[i].q = buf[i].q;
            }

            latest_rx_sample = (cariboulite_sample_complex_int16){
                .i = frm.data[want / 2].i,
                .q = frm.data[want / 2].q
            };

            //rf10_fifo_put(ctrl->rx_fifo /*add to ctrl*/, &frm, -1);
            if (!rf10_fifo_put(ctrl->rx_fifo, &frm, /*timeout_ms=*/-1)) {
                // FIFO full -> overwrite oldest already happened; just continue
                // (you can keep a counter if you want to log drops)
            }
            have = 0;
        }
    }
    pthread_cleanup_pop(1);
done:
    pthread_cleanup_pop(1);
    return NULL;
}

int rx_pipeline_init(rx_pipeline_t* p, sys_st* sys,
                     cariboulite_radio_state_st* radio,
                     const rx_params_t* par)
{
    if (!p || !sys || !radio || !par) return -1;
    memset(p, 0, sizeof(*p));
    atomic_init(&p->aw.alive, false);
    atomic_init(&p->aw.last_error, 0);
    atomic_init(&p->demod.alive, false);
    atomic_init(&p->demod.last_error, 0);
    atomic_init(&p->rx_ctrl.alive, false);
    pipeline_thread_diagnostics_init(&p->aw.scheduling);
    pipeline_thread_diagnostics_init(&p->demod.scheduling);
    pipeline_thread_diagnostics_init(&p->rx_ctrl.scheduling);
    atomic_init(&p->rx_ctrl.read_calls, 0);
    atomic_init(&p->rx_ctrl.read_samples, 0);
    atomic_init(&p->rx_ctrl.short_reads, 0);
    atomic_init(&p->rx_ctrl.zero_reads, 0);
    atomic_init(&p->rx_ctrl.read_errors, 0);
    atomic_init(&p->rx_ctrl.last_error, 0);
    p->sys   = sys;
    p->radio = radio;

    if ((par->fs_rf != 1000000 && par->fs_rf != 2000000 && par->fs_rf != 4000000) ||
        par->fs_audio != 48000 ||
        (par->mode != AUDIO_DEMOD_NBFM && par->mode != AUDIO_DEMOD_WBFM)) return -1;

    uint32_t noise_levels = noise_squelch_default_levels();
    if ((par->noise_squelch_open_rms != 0 || par->noise_squelch_close_rms != 0) &&
        !noise_squelch_pack_levels(par->noise_squelch_open_rms,
                                  par->noise_squelch_close_rms, &noise_levels)) return -1;

    // FIFOs
    rf10_fifo_init(&p->rxq,  /*cap=*/64, /*drop_oldest_on_full=*/true);
    aud10_fifo_init(&p->afifo, /*cap=*/24);

    p->inited = true; // FIFO synchronization objects are ready for cleanup.
    int error = -1;
    if (!p->rxq.q || !p->afifo.q) goto fail;

    // Open ALSA playback
    p->demod.sink = alsa_sink_open(par->pcm_dev, 48000);
    if (!p->demod.sink) {
        error = -2;
        goto fail;
    }

    // Audio writer
    p->aw.active   = true;
    p->aw.sink     = p->demod.sink;
    p->aw.fifo     = &p->afifo;
    if (pthread_create(&p->aw_thread, NULL, audio_writer_thread, &p->aw) != 0) {
        error = -3;
        goto fail;
    }
    p->aw_thread_created = true;

    atomic_init(&p->demod.squelch_flags,
        (par->noise_squelch_disabled || !(audio_demod_mode_capabilities(par->mode) & AUDIO_DEMOD_CAP_NOISE_SQUELCH) ? 0u : RX_SQUELCH_NOISE) |
        (par->carrier_squelch_enabled ? RX_SQUELCH_CARRIER : 0u));
    atomic_init(&p->demod.squelch_open, 0);
    atomic_init(&p->demod.noise_squelch_levels, noise_levels);
    atomic_init(&p->demod.noise_squelch_rms_milli, 0);
    atomic_init(&p->demod.noise_squelch_detector_open, 0);
    atomic_init(&p->demod.noise_squelch_valid, 0);
    // Demod setup
    p->demod.mode              = par->mode;
    p->demod.reset             = true;
    p->demod.prime_blocks_10ms = 20;    // 20 * 10ms = 200 ms
    p->demod.priming           = true;
    p->demod.active            = true;
    p->demod.fifo_in           = &p->rxq;
    p->demod.afifo_out         = &p->afifo;
    p->demod.fs_rf             = par->fs_rf;
    p->demod.fs_audio          = par->fs_audio;   // 48e3
    p->demod.deemph_tau        = par->deemph_tau_s;
    p->demod.pcm_gain          = par->pcm_gain;
    p->demod.pcm_total_frames  = 0;
    p->demod.pcm_channels      = alsa_sink_channels(p->demod.sink);
    p->demod.pcm_rate          = p->demod.sink->sample_rate;

    audio_demod_config_t dsp_config = {
        (unsigned)par->fs_rf, (unsigned)par->fs_audio, par->deemph_tau_s, par->pcm_gain
    };
    p->demod.dsp = audio_demod_create(par->mode, &dsp_config);
    if (!p->demod.dsp) { error = -4; goto fail; }

    if (pthread_create(&p->demod_thread, NULL, audio_demod_thread, &p->demod) != 0) {
        error = -4;
        goto fail;
    }
    p->demod_thread_created = true;

    // Reader (prepare only — start later in rx_pipeline_start)
    p->rx_ctrl.active         = false;
    p->rx_ctrl.radio          = radio;
    p->rx_ctrl.rx_buffer      = NULL;         // allocate on start
    p->rx_ctrl.rx_buffer_size = 0;
    p->rx_ctrl.rx_fifo        = &p->rxq;
    p->rx_ctrl.squelch_flags  = &p->demod.squelch_flags;

    // Set radio frequency
    HW_LOCK();
    double init_frequency = par->freq_hz;
    cariboulite_radio_set_frequency(radio, true, &init_frequency);
    HW_UNLOCK();

    p->inited = true;
    p->running = false;
    return 0;

fail:
    rx_pipeline_destroy(p);
    return error;
}

// The app owns this mode-specific profile; generic radio/Soapy callers retain
// their settings. Called under HW_LOCK, before enabling RX or its reader.
static int configure_rx_modem(rx_pipeline_t* p)
{
    cariboulite_radio_state_st* radio = p->radio;
    at86rf215_st* modem = &p->sys->modem;
    const bool nbfm = p->demod.mode == AUDIO_DEMOD_NBFM;
    const bool s1g = radio->type == cariboulite_channel_s1g;
    cariboulite_radio_sample_rate_en rate;
    switch ((unsigned)p->demod.fs_rf) {
        case 1000000: rate = cariboulite_radio_rx_sample_rate_1000khz; break;
        case 2000000: rate = cariboulite_radio_rx_sample_rate_2000khz; break;
        case 4000000: rate = cariboulite_radio_rx_sample_rate_4000khz; break;
        default: return -1;
    }
    const cariboulite_radio_rx_bw_en bw = nbfm ?
        cariboulite_radio_rx_bw_160KHz : cariboulite_radio_rx_bw_250KHz;
    // WBFM's software channel filter cuts at +/-100 kHz. The 250 kHz analog
    // bandwidth and minimum digital cutoff (Fs/8: +/-125 kHz at 1 MS/s)
    // leave margin around it without changing IQ rate or DSP processing.
    const cariboulite_radio_f_cut_en cutoff = cariboulite_radio_rx_f_cut_0_25_half_fs;

    // Frontend configuration requires TRXOFF. Tuning can leave TXPREP active,
    // and the modem's TRXOFF command has an erratum, so confirm actual state.
    uint8_t state = 0;
    if (cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_rx, false) != 0 ||
        at86rf215_read_buffer(modem, s1g ? REG_RF09_STATE : REG_RF24_STATE,
                             &state, 1) != 0 ||
        (state & 0x07) != cariboulite_radio_state_cmd_trx_off) return -1;

    // The bandwidth setter resets RCUT, so set the sample rate/cutoff last.
    // The radio setters preserve IFS=1 and IFI=0, and update SMI timeouts.
    if (cariboulite_radio_set_rx_bandwidth(radio, bw) != 0 ||
        cariboulite_radio_set_rx_samp_cutoff(radio, rate, cutoff) != 0) return -1;
    uint8_t filters[2] = {0};
    if (at86rf215_read_buffer(modem, s1g ? REG_RF09_RXBWC : REG_RF24_RXBWC,
                             filters, 2) != 0 ||
        (filters[0] & 0x3f) != (0x10 | (unsigned)bw) ||
        (filters[1] & 0xef) != (((unsigned)cutoff << 5) | (unsigned)rate)) return -1;

    if (nbfm) {
        // AGCI=0: measure after modem filtering. EN=1, FRZC=0, AVGS=0
        // (8 samples), TGT=0 (-21 dBFS), matching fresh modem initialization.
        // GCW is ignored with AGC enabled; RX entry resets automatic gain.
        // Use a checked write/read rather than the unchecked setup helper.
        uint8_t agc[2] = {0x01, 0x00};
        const uint16_t reg = s1g ? REG_RF09_AGCC : REG_RF24_AGCC;
        if (at86rf215_write_buffer(modem, reg, agc, 2) != 0 ||
            at86rf215_read_buffer(modem, reg, agc, 2) != 0 ||
            (agc[0] & 0x73) != 0x01 || (agc[1] & 0xe0) != 0) return -1;
        radio->rx_agc_on = true;
        radio->rx_gain_value_db = (agc[1] & 0x1f) * 3;
    }
    return 0;
}

int rx_pipeline_start(rx_pipeline_t* p)
{
    if (!p || !p->inited || p->running) return -1;

    if (!p->rx_ctrl.rx_buffer) {
        p->rx_ctrl.rx_buffer = malloc(sizeof(cariboulite_sample_complex_int16) * (size_t)(p->demod.fs_rf / 100));
        if (!p->rx_ctrl.rx_buffer) return -2;
        p->rx_ctrl.rx_buffer_size = (size_t)(p->demod.fs_rf / 100);
    }

    // Stop TX if needed
    if (nbfm_tx_active) {
        nbfm_tx_active = false;
        __sync_synchronize();
        HW_LOCK();
        caribou_smi_set_driver_streaming_state(&p->sys->smi, (smi_stream_state_en)0);
        cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_tx, false);
        caribou_fpga_set_io_ctrl_mode(&p->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
        HW_UNLOCK();
    }

    HW_LOCK();
    if (configure_rx_modem(p) != 0) {
        HW_UNLOCK();
        fprintf(stderr, "[rx] modem filter/AGC configuration failed; RX not started\n");
        return -4;
    }
    caribou_fpga_set_io_ctrl_mode(&p->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_rx_lowpass);
    cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_rx, true);
    caribou_smi_set_driver_streaming_state(&p->sys->smi,
        p->radio == &p->sys->radio_high ? smi_stream_rx_channel_1 : smi_stream_rx_channel_0);
    HW_UNLOCK();

    // start reader now (only when RX is active)
    p->rx_ctrl.active        = true;
    p->rx_ctrl.radio         = p->radio;
    pipeline_thread_diagnostics_reset(&p->rx_ctrl.scheduling);
    p->running = true; // stop() must unwind hardware if thread creation fails.
    if (pthread_create(&p->rx_thread, NULL, rx_reader_thread_func, &p->rx_ctrl) != 0) {
        rx_pipeline_stop(p);
        return -3;
    }
    p->rx_thread_created = true;

    __sync_synchronize();

    p->demod.prime_blocks_10ms = 8;   // gentle start whenever RX is toggled on
    p->demod.reset = true;            // demod thread will reinit on next loop

    nbfm_rx_active = true;

    p->running = true;
    return 0;
}

void rx_pipeline_stop(rx_pipeline_t* p)
{
    if (!p || !p->inited || !p->running) return;

    nbfm_rx_active = false;
    __sync_synchronize();

    HW_LOCK();
    cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_rx, false);
    caribou_smi_set_driver_streaming_state(&p->sys->smi, (smi_stream_state_en)0);
    caribou_fpga_set_io_ctrl_mode(&p->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
    HW_UNLOCK();

    // join reader here
    p->rx_ctrl.active = false;
    if (p->rx_thread_created) {
        pthread_cancel(p->rx_thread);
        pthread_join(p->rx_thread, NULL);
        p->rx_thread_created = false;
    }


    p->running = false;
}

void rx_pipeline_destroy(rx_pipeline_t* p)
{
    if (!p || !p->inited) return;

    rx_pipeline_stop(p);

    // stop threads and free
    p->rx_ctrl.active = false;
    p->demod.active   = false;
    p->aw.active      = false;
    rf10_fifo_stop(&p->rxq);
    aud10_fifo_stop(&p->afifo);

    if (p->demod_thread_created) {
        pthread_cancel(p->demod_thread);
        pthread_join(p->demod_thread, NULL);
        p->demod_thread_created = false;
    }
    if (p->aw_thread_created) {
        pthread_cancel(p->aw_thread);
        pthread_join(p->aw_thread, NULL);
        p->aw_thread_created = false;
    }

    if (p->rx_ctrl.rx_buffer) {
        free(p->rx_ctrl.rx_buffer);
        p->rx_ctrl.rx_buffer = NULL;
    }

    audio_demod_destroy(p->demod.dsp);
    p->demod.dsp = NULL;
    audio_sink_destroy(p->demod.sink);
    p->demod.sink = NULL;
    aud10_fifo_destroy(&p->afifo);
    rf10_fifo_destroy(&p->rxq);

    p->inited = false;
}

bool rx_pipeline_running(const rx_pipeline_t* p) { return p && p->running; }

int rx_pipeline_set_freq(rx_pipeline_t* p, double freq_hz)
{
    if (!p || !p->sys || !p->radio) return -1;
    HW_LOCK();
    int rc = cariboulite_radio_set_frequency(p->radio, true, &freq_hz);
    HW_UNLOCK();
    if (rc != 0) return rc;
    
    // If running, gently reset demod so clicks/flicker are avoided post-retune
    if (p->running) {
        p->demod.prime_blocks_10ms = 4;  // ~40 ms is often enough on retunes
        p->demod.reset = true;
    }
    return 0;
}

int rx_pipeline_set_pcm_gain(rx_pipeline_t* p, float gain)
{
    if (!p) return -1;
    p->demod.pcm_gain = gain;
    return 0;
}

int rx_pipeline_set_deemph(rx_pipeline_t* p, float tau_s)
{
    if (!p) return -1;
    p->demod.deemph_tau = tau_s;
    return 0;
}

void rx_pipeline_get_stats(rx_pipeline_t* p, rx_pipeline_stats_t* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!p || !p->inited) return;
    rf10_fifo_get_stats(&p->rxq, &out->rxq);
    aud10_fifo_get_stats(&p->afifo, &out->audioq);
    out->playback_valid = audio_sink_get_stats(p->demod.sink, &out->playback);
    out->reader.calls = atomic_load_explicit(&p->rx_ctrl.read_calls, memory_order_relaxed);
    out->reader.samples = atomic_load_explicit(&p->rx_ctrl.read_samples, memory_order_relaxed);
    out->reader.short_reads = atomic_load_explicit(&p->rx_ctrl.short_reads, memory_order_relaxed);
    out->reader.zero_reads = atomic_load_explicit(&p->rx_ctrl.zero_reads, memory_order_relaxed);
    out->reader.errors = atomic_load_explicit(&p->rx_ctrl.read_errors, memory_order_relaxed);
    out->reader.last_error = atomic_load_explicit(&p->rx_ctrl.last_error, memory_order_relaxed);
    out->reader_worker.created = p->rx_thread_created;
    out->demod_worker.created = p->demod_thread_created;
    out->audio_worker.created = p->aw_thread_created;
    out->reader_worker.alive = atomic_load_explicit(&p->rx_ctrl.alive, memory_order_relaxed);
    out->demod_worker.alive = atomic_load_explicit(&p->demod.alive, memory_order_relaxed);
    out->audio_worker.alive = atomic_load_explicit(&p->aw.alive, memory_order_relaxed);
    out->reader_worker.last_error = out->reader.last_error;
    out->demod_worker.last_error = atomic_load_explicit(&p->demod.last_error, memory_order_relaxed);
    out->audio_worker.last_error = atomic_load_explicit(&p->aw.last_error, memory_order_relaxed);
    pipeline_thread_diagnostics_get(&p->rx_ctrl.scheduling, &out->reader_worker.scheduling);
    pipeline_thread_diagnostics_get(&p->demod.scheduling, &out->demod_worker.scheduling);
    pipeline_thread_diagnostics_get(&p->aw.scheduling, &out->audio_worker.scheduling);
}

size_t rx_pipeline_frame_samples(const rx_pipeline_t* p)
{
    return p ? (size_t)(p->demod.fs_rf / 100) : 0;
}

void rx_pipeline_reset_stats(rx_pipeline_t* p)
{
    if (p && p->inited) {
        rf10_fifo_reset_stats(&p->rxq);
        aud10_fifo_reset_stats(&p->afifo);
    }
}

void rx_pipeline_set_squelch(rx_pipeline_t* p, bool noise, bool carrier)
{
    if (p && p->inited) atomic_store(&p->demod.squelch_flags,
        (noise && (audio_demod_capabilities(p->demod.dsp) & AUDIO_DEMOD_CAP_NOISE_SQUELCH) ? RX_SQUELCH_NOISE : 0u) | (carrier ? RX_SQUELCH_CARRIER : 0u));
}
bool rx_pipeline_squelch_open(const rx_pipeline_t* p)
{
    return p && p->running && atomic_load(&p->demod.squelch_open);
}

int rx_pipeline_set_noise_squelch_levels(rx_pipeline_t* p, float open_rms, float close_rms)
{
    uint32_t levels;
    if (!p || !p->inited || !noise_squelch_pack_levels(open_rms, close_rms, &levels)) return -1;
    atomic_store(&p->demod.noise_squelch_levels, levels);
    return 0;
}

int rx_pipeline_get_noise_squelch_status(const rx_pipeline_t* p, rx_noise_squelch_status_t* out)
{
    if (!p || !p->inited || !out) return -1;
    memset(out, 0, sizeof(*out));
    if (!noise_squelch_unpack_levels(atomic_load(&p->demod.noise_squelch_levels),
                                    &out->open_rms, &out->close_rms)) return -1;
    out->valid = p->running && p->demod.mode == AUDIO_DEMOD_NBFM &&
        atomic_load(&p->demod.noise_squelch_valid);
    if (out->valid) {
        out->rms = atomic_load(&p->demod.noise_squelch_rms_milli) / 1000.0f;
        out->detector_open = atomic_load(&p->demod.noise_squelch_detector_open);
    }
    return 0;
}
