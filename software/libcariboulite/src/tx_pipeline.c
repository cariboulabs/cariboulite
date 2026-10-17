#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "tx_pipeline.h"
#include "pipeline_runtime.h"
#include "mod_worker.h"
#include "tone_source.h"
#include "radio_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include "alsa_source.h"
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <sys/ioctl.h>

static void gate_unlock(void* mutex)
{
    pthread_mutex_unlock((pthread_mutex_t*)mutex);
}

bool tx_worker_begin(tx_writer_ctrl_st* ctrl, bool producer)
{
    if (!ctrl) return false;
    if (!ctrl->gate_initialized) {
        if (!nbfm_tx_active) {
            struct timespec delay = {.tv_nsec = 2000000};
            nanosleep(&delay, NULL);
            return false;
        }
        return true;
    }
    volatile bool ready = false;
    bool waited = false;
    pthread_mutex_lock(&ctrl->state_mutex);
    pthread_cleanup_push(gate_unlock, &ctrl->state_mutex);
    while (ctrl->active && (!ctrl->gate_enabled || atomic_load(&ctrl->abort_requested))) {
        waited = true;
        pthread_cond_wait(&ctrl->state_changed, &ctrl->state_mutex);
    }
    if (ctrl->active && ctrl->gate_enabled && !atomic_load(&ctrl->abort_requested)) {
        if (producer) {
            ctrl->producer_busy = true;
            ctrl->producer_resumed = waited;
        }
        else ctrl->writer_busy = true;
        ready = true;
    }
    pthread_cleanup_pop(1);
    return ready;
}

void tx_worker_end(tx_writer_ctrl_st* ctrl, bool producer)
{
    if (!ctrl || !ctrl->gate_initialized) return;
    pthread_mutex_lock(&ctrl->state_mutex);
    if (producer) ctrl->producer_busy = false;
    else ctrl->writer_busy = false;
    if (!ctrl->gate_enabled)
        pthread_cond_broadcast(&ctrl->state_changed);
    pthread_mutex_unlock(&ctrl->state_mutex);
}

static void tx_gate_set(tx_writer_ctrl_st* ctrl, bool enabled)
{
    if (!ctrl || !ctrl->gate_initialized) return;
    pthread_mutex_lock(&ctrl->state_mutex);
    ctrl->gate_enabled = enabled;
    pthread_cond_broadcast(&ctrl->state_changed);
    pthread_mutex_unlock(&ctrl->state_mutex);
    pthread_mutex_lock(&g_tx_injection_lock);
    pthread_cond_broadcast(&g_tx_injection_changed);
    pthread_mutex_unlock(&g_tx_injection_lock);
}

void tx_worker_fail(tx_writer_ctrl_st* ctrl, int error)
{
    if (!ctrl) return;
    (void)error;
    atomic_store(&ctrl->abort_requested, true);
    nbfm_tx_active = false;
    tx_gate_set(ctrl, false);
    if (ctrl->fifo) rf10_fifo_stop(ctrl->fifo);
    tx_audio_buffer_request_abort(ctrl->audio_buffer);
}

static void writer_exited(void* argument)
{
    tx_writer_ctrl_st* ctrl = argument;
    tx_worker_end(ctrl, false);
    atomic_store_explicit(&ctrl->alive, false, memory_order_relaxed);
}

static void* tx_writer_thread_func(void* arg)
{
    pthread_setname_np(pthread_self(), "tx_writer");
    //set_rt_and_affinity();
    //set_rt_and_affinity_prio(42,-1);


    tx_writer_ctrl_st* ctrl = (tx_writer_ctrl_st*)arg;
    if (!ctrl || !ctrl->radio || !ctrl->radio->sys) return NULL;
    set_rt_and_affinity_prio_record(48, ctrl->hardware_paced ? 2 : 0, &ctrl->scheduling);

    caribou_smi_st *smi = &ctrl->radio->sys->smi;
    if (!smi || smi->filedesc < 0) return NULL;

    rf10_fifo_t* fifo = ctrl->fifo;
    if (!fifo) return NULL;
    atomic_store_explicit(&ctrl->alive, true, memory_order_relaxed);
    pthread_cleanup_push(writer_exited, ctrl);

    const caribou_smi_channel_en ch =
        (ctrl->radio == &ctrl->radio->sys->radio_low) ?
            caribou_smi_channel_900 : caribou_smi_channel_2400;

    // non-blocking fd
    int flags = fcntl(smi->filedesc, F_GETFL, 0);
    if (flags != -1) fcntl(smi->filedesc, F_SETFL, flags | O_NONBLOCK);

    // The getter already returns IQ samples, not bytes.
    size_t native_samples = caribou_smi_get_native_batch_samples(smi);
    size_t quarter_samples = native_samples / 4;
    if (quarter_samples == 0) quarter_samples = 8192; // safe default if ioctl failed

    while (1) {
        pthread_testcancel();
        if (!ctrl->active) break;

        if (!tx_worker_begin(ctrl, false)) {
            continue;
        }

        // Get one 10 ms frame at the selected RF rate.
        rf10_frame_t frm;
        if (!rf10_fifo_get(fifo, &frm, /*timeout_ms=*/-1)) {
            tx_worker_end(ctrl, false);
            continue;
        }

        // Stream it out in chunks ≈ kernel quarter (keep kfifo topped up)
        size_t off = 0;
        const size_t total = ctrl->frame_samples; // Selected rate * 10 ms
        struct pollfd pfd = { .fd = smi->filedesc, .events = POLLOUT, .revents = 0 };

        while (off < total && nbfm_tx_active) {
            // Aim for quarter-sized writes; last piece can be smaller
            size_t todo = total - off;
            if (todo > quarter_samples) todo = quarter_samples;

            // Wait until driver is ready to accept bytes
            int pr = poll(&pfd, 1, 10);  // short timeout; loop if needed
            if ((pr < 0 && errno != EINTR) ||
                (pr > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)))) {
                int error = pr < 0 ? -errno : -EIO;
                atomic_fetch_add_explicit(&ctrl->write_errors, 1, memory_order_relaxed);
                atomic_store_explicit(&ctrl->last_error, error, memory_order_relaxed);
                tx_worker_fail(ctrl, error);
                break;
            }
            if (pr <= 0 || !(pfd.revents & POLLOUT)) continue;

            caribou_smi_sample_complex_int16 *p =
                (caribou_smi_sample_complex_int16 *)(frm.data + off);

            // Lifecycle calls own the shared stream. A poll may complete after
            // stop(), so recheck TX under the same lock used for the handoff.
            // The write also uses cancellation points and shared SMI buffers.
            int cancel_state;
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancel_state);
            HW_LOCK();
            if (!ctrl->active || !nbfm_tx_active) {
                HW_UNLOCK();
                pthread_setcancelstate(cancel_state, NULL);
                break;
            }
            /* Hardware admission may change after poll. Make one nonblocking
             * attempt under HW_LOCK; wait for DMA outside the shared lock. */
            int sent = ctrl->hardware_paced
                ? caribou_smi_write_timed(smi, ch, p, (int)todo, 0)
                : caribou_smi_write_samples(smi, ch, p, (int)todo);
            int write_error = errno;
            atomic_fetch_add_explicit(&ctrl->write_calls, 1, memory_order_relaxed);
            if (sent > 0 && (size_t)sent <= todo) {
                atomic_fetch_add_explicit(&ctrl->write_samples, (unsigned)sent, memory_order_relaxed);
                if ((size_t)sent < todo)
                    atomic_fetch_add_explicit(&ctrl->partial_writes, 1, memory_order_relaxed);
            } else if (!sent) {
                atomic_fetch_add_explicit(&ctrl->zero_writes, 1, memory_order_relaxed);
            } else if (sent < 0 && (write_error == EAGAIN || write_error == EWOULDBLOCK || write_error == EINTR)) {
                atomic_fetch_add_explicit(&ctrl->would_block, 1, memory_order_relaxed);
            } else {
                int error = sent > 0 ? -EPROTO : -(write_error ? write_error : EIO);
                atomic_fetch_add_explicit(&ctrl->write_errors, 1, memory_order_relaxed);
                atomic_store_explicit(&ctrl->last_error, error, memory_order_relaxed);
                tx_worker_fail(ctrl, error);
                caribou_smi_set_driver_streaming_state(smi, smi_stream_idle);
                sent = -1;
                write_error = -error;
            }
            HW_UNLOCK();
            pthread_setcancelstate(cancel_state, NULL);

            if (sent > 0) {
                off += (size_t)sent;
            } else if (sent == 0 || (sent < 0 && (write_error == EAGAIN || write_error == EWOULDBLOCK || write_error == EINTR))) {
                // transient backpressure -> try again
                continue;
            } else {
                // The hard error was shut down while holding the stream lock.
                break;
            }
        }
        if (off == total)
            atomic_store(&ctrl->written_sequence, frm.tx_sequence);
        tx_worker_end(ctrl, false);
    }
    pthread_cleanup_pop(1);
    return NULL;
}

static unsigned tx_tail_padding_frames(size_t native_samples,
                                       unsigned fifo_multiplier,
                                       size_t frame_samples)
{
    // Writes acknowledge the kernel FIFO, not RF output. Enough accepted
    // silence displaces the tail through the FIFO, cyclic DMA and FPGA FIFO.
    // Older drivers use kfifo_alloc (round up); newer preallocated kfifo_init
    // rounds down. The multiplier reports requested storage, not capacity.
    // Use the larger power-of-two bound so a fast flush is safe for either.
    uint64_t requested = (uint64_t)native_samples * fifo_multiplier;
    uint64_t kernel_capacity = 1;
    while (kernel_capacity < requested) kernel_capacity <<= 1;
    uint64_t pending = kernel_capacity + native_samples + native_samples / 4 + 1024;
    unsigned frames = (unsigned)((pending + frame_samples - 1) / frame_samples);
    return frames < 25 ? 25 : frames;
}

static int tx_detect_pacing(tx_writer_ctrl_st* ctrl, int fd, tx_pacing_mode_t mode)
{
    if (mode == TX_PACING_LEGACY) return 0;
    if (mode != TX_PACING_AUTO && mode != TX_PACING_HARDWARE_REQUIRED) return -EINVAL;
    struct smi_stream_tx_status status = {0};
    if (ioctl(fd, SMI_STREAM_IOC_GET_TX_STATUS, &status) < 0) {
        if (errno == ENOTTY && mode == TX_PACING_AUTO) return 0;
        fprintf(stderr, "[tx_pipeline] hardware-paced TX needs the updated SMI driver: %s\n",
                strerror(errno));
        return -ENOTSUP;
    }
    if (status.abi_version != SMI_STREAM_TX_ABI_VERSION ||
        status.struct_size != sizeof(status) || !status.period_bytes ||
        status.period_bytes % 4 || status.period_bytes > UINT32_MAX / 2 ||
        status.fifo_capacity_bytes < 2 * status.period_bytes ||
        status.ring_bytes < status.period_bytes || status.ring_bytes % 4)
        return -EPROTO;
    ctrl->hardware_paced = true;
    ctrl->driver_config = status;
    ctrl->driver_config.fifo_limit_bytes = 2 * status.period_bytes;
    /* Include admission, cyclic DMA, one extra quarter and FPGA capacity.
     * Accepted padding displaces the cue even when DMA retirement is delayed. */
    uint64_t pending = (uint64_t)ctrl->driver_config.fifo_limit_bytes / 4 +
        status.ring_bytes / 4 + status.period_bytes / 4 + 1024;
    unsigned frames = (unsigned)((pending + ctrl->frame_samples - 1) / ctrl->frame_samples);
    ctrl->tail_padding_frames = frames < 25 ? 25 : frames;
    return 0;
}

/* Called under the hardware lock with SMI idle, before a new TX session. */
static int tx_configure_admission(tx_pipeline_t* p)
{
    struct smi_stream_tx_buffer_limit limit = {
        .abi_version = SMI_STREAM_TX_ABI_VERSION, .struct_size = sizeof(limit),
        .fifo_limit_bytes = p->tx_ctrl.hardware_paced
            ? p->tx_ctrl.driver_config.fifo_limit_bytes : 0,
    };
    if (ioctl(p->sys->smi.filedesc, SMI_STREAM_IOC_SET_TX_BUFFER_LIMIT, &limit) < 0)
        return !p->tx_ctrl.hardware_paced && errno == ENOTTY ? 0 : -errno;
    if (p->tx_ctrl.hardware_paced) {
        struct smi_stream_tx_status status;
        if (ioctl(p->sys->smi.filedesc, SMI_STREAM_IOC_GET_TX_STATUS, &status) < 0)
            return -errno;
        if (status.fifo_limit_bytes != limit.fifo_limit_bytes ||
            !(status.flags & SMI_STREAM_TX_STATUS_BOUNDED_FIFO)) return -EPROTO;
    }
    return 0;
}

/* Compare named canonical fields, never struct padding or requested DMA values:
 * GET_SETTINGS decodes hardware thresholds (e.g. 254/495 become 62/47). */
static bool tx_smi_settings_equal(const struct smi_settings* a,
                                  const struct smi_settings* b)
{
#define SAME(field) (a->field == b->field)
    return SAME(data_width) && SAME(pack_data) && SAME(read_setup_time) &&
        SAME(read_hold_time) && SAME(read_pace_time) && SAME(read_strobe_time) &&
        SAME(write_setup_time) && SAME(write_hold_time) && SAME(write_pace_time) &&
        SAME(write_strobe_time) && SAME(dma_enable) && SAME(dma_passthrough_enable) &&
        SAME(dma_read_thresh) && SAME(dma_write_thresh) &&
        SAME(dma_panic_read_thresh) && SAME(dma_panic_write_thresh);
#undef SAME
}

static int tx_smi_require_idle(tx_pipeline_t* p)
{
    struct smi_stream_tx_status status = {0};
    if (ioctl(p->sys->smi.filedesc, SMI_STREAM_IOC_GET_TX_STATUS, &status) < 0)
        return -(errno ? errno : EIO);
    return status.state == smi_stream_idle ? 0 : -EBUSY;
}

/* Lifecycle owner holds HW_LOCK, with both workers parked and SMI idle. Only
 * setup/strobe/hold change; read timings, pace and canonical DMA fields retain
 * the actual device profile. Cache before WRITE so even a failed verification
 * leaves enough information for the abort path to restore the old write tuple. */
static int tx_apply_write_timing(tx_pipeline_t* p)
{
    tx_writer_ctrl_st* ctrl = &p->tx_ctrl;
    if (!ctrl->hardware_paced) return 0;
    int error = tx_smi_require_idle(p);
    if (error) return error;
    if (ctrl->smi_write_timing_saved) return -EBUSY;
    struct smi_settings before = {0};
    if (ioctl(p->sys->smi.filedesc, BCM2835_SMI_IOC_GET_SETTINGS, &before) < 0)
        return -(errno ? errno : EIO);
    ctrl->saved_write_setup = before.write_setup_time;
    ctrl->saved_write_strobe = before.write_strobe_time;
    ctrl->saved_write_hold = before.write_hold_time;
    ctrl->smi_write_timing_saved = true;
    struct smi_settings expected = before;
    expected.write_setup_time = 1;
    expected.write_strobe_time = 4;
    expected.write_hold_time = 2;
    if (ioctl(p->sys->smi.filedesc, BCM2835_SMI_IOC_WRITE_SETTINGS, &expected) < 0)
        return -(errno ? errno : EIO);
    struct smi_settings actual = {0};
    if (ioctl(p->sys->smi.filedesc, BCM2835_SMI_IOC_GET_SETTINGS, &actual) < 0)
        return -(errno ? errno : EIO);
    return tx_smi_settings_equal(&actual, &expected) ? 0 : -EPROTO;
}

/* Restore only the saved write tuple into a newly read current profile. A
 * stopped pipeline may be retained while RX settings evolve independently. */
static int tx_restore_write_timing(tx_pipeline_t* p)
{
    tx_writer_ctrl_st* ctrl = &p->tx_ctrl;
    if (!ctrl->hardware_paced || !ctrl->smi_write_timing_saved) return 0;
    int error = tx_smi_require_idle(p);
    if (error) return error;
    struct smi_settings expected = {0};
    if (ioctl(p->sys->smi.filedesc, BCM2835_SMI_IOC_GET_SETTINGS, &expected) < 0)
        return -(errno ? errno : EIO);
    expected.write_setup_time = ctrl->saved_write_setup;
    expected.write_strobe_time = ctrl->saved_write_strobe;
    expected.write_hold_time = ctrl->saved_write_hold;
    if (ioctl(p->sys->smi.filedesc, BCM2835_SMI_IOC_WRITE_SETTINGS, &expected) < 0)
        return -(errno ? errno : EIO);
    struct smi_settings actual = {0};
    if (ioctl(p->sys->smi.filedesc, BCM2835_SMI_IOC_GET_SETTINGS, &actual) < 0)
        return -(errno ? errno : EIO);
    if (!tx_smi_settings_equal(&actual, &expected)) return -EPROTO;
    ctrl->smi_write_timing_saved = false;
    return 0;
}

static cariboulite_radio_sample_rate_en tx_rate_code(unsigned rate)
{
    radio_profile_t profile;
    return radio_profile_tx(rate, &profile) == 0 ? profile.rate : 0;
}

/* NBFM uses the narrowest modem TX filters: analog 80 kHz and digital Fs/8.
 * The lifecycle owner holds HW_LOCK with both workers gated. Tuning can leave
 * TXPREP active, so confirm TRXOFF before configuring the frontend. */
static int tx_configure_nbfm_profile(tx_pipeline_t* p, int requested_power)
{
    cariboulite_radio_state_st* radio = p->radio;
    const bool low = radio->type == cariboulite_channel_s1g;
    const unsigned rate = (unsigned)p->tx_ctrl.frame_samples * 100;
    radio_profile_t profile;
    if (radio_profile_tx(rate, &profile)) return -1;
    const cariboulite_radio_sample_rate_en rate_code = profile.rate;
    uint8_t state = 0;
    if (cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_tx, false) != 0 ||
        at86rf215_read_buffer(&p->sys->modem, low ? REG_RF09_STATE : REG_RF24_STATE,
                             &state, 1) != 0 ||
        (state & 7) != at86rf215_radio_state_cmd_trx_off) {
        fprintf(stderr, "[tx_pipeline] NBFM TX idle verification failed\n");
        return -1;
    }
    int bandwidth_error = cariboulite_radio_set_tx_bandwidth(radio,
        profile.tx_bandwidth);
    int rate_error = cariboulite_radio_set_tx_samp_cutoff(radio, rate_code,
        profile.cutoff);
    /* Both filter setters rewrite PAC using an uncalibrated power mapping.
     * Restore requested calibrated power, including after a setter failure.
     * These APIs retain the existing 16 us PA ramp and disable direct mode. */
    int power_error = cariboulite_radio_set_tx_power(radio, requested_power);
    if (bandwidth_error || rate_error || power_error) {
        fprintf(stderr, "[tx_pipeline] NBFM TX filter setup failed\n");
        return -1;
    }
    uint8_t filters[2] = {0}, gap = 255;
    int filter_error = at86rf215_read_buffer(&p->sys->modem,
        low ? REG_RF09_TXCUTC : REG_RF24_TXCUTC, filters, sizeof(filters));
    int gap_error = caribou_fpga_get_sys_ctrl_tx_sample_gap(&p->sys->fpga, &gap);
    if (filter_error || gap_error || (filters[0] & 0xcf) != 0x80 ||
        filters[1] != (unsigned)rate_code || gap != profile.tx_sample_gap) {
        fprintf(stderr, "[tx_pipeline] NBFM TX filter/rate/gap verification failed: "
            "TXCUTC=0x%02X TXDFE=0x%02X expected=0x%02X rc=%d gap=%u rc=%d\n",
            filters[0], filters[1], (unsigned)rate_code, filter_error, gap, gap_error);
        return -1;
    }
    return 0;
}

int tx_pipeline_init(tx_pipeline_t* p, sys_st* sys,
                     cariboulite_radio_state_st* radio,
                     const tx_params_t* par)
{
    if (!p || !sys || !radio || !par) return -1;
    memset(p, 0, sizeof(*p));
    p->sys   = sys;
    p->radio = radio;

    unsigned rf_fs = par->rf_fs ? par->rf_fs : 4000000;
    radio_profile_t profile;
    if (radio_profile_tx(rf_fs, &profile) != 0 ||
        !radio_profile_frequency_valid(radio->type, sys->board_info.numeric_product_id,
                                       par->freq_hz) ||
        !radio_profile_tx_power_valid(radio->type, par->tx_power_dbm) ||
        !radio_profile_finite(par->f_dev_hz) || par->f_dev_hz < 0 || par->f_dev_hz > 24000 ||
        !radio_profile_finite(par->out_scale) || par->out_scale < 0 || par->out_scale > 32767 ||
        !radio_profile_finite(par->tone_hz) || par->tone_hz < 0 || par->tone_hz >= 24000 ||
        !radio_profile_finite(par->tone_amp) || par->tone_amp < 0 || par->tone_amp > 1 ||
        par->pacing_mode < TX_PACING_AUTO || par->pacing_mode > TX_PACING_LEGACY) return -1;
    atomic_init(&p->running, false);
    atomic_init(&p->tx_ctrl.active, false);
    atomic_init(&p->tx_ctrl.alive, false);
    atomic_init(&p->tx_ctrl.last_error, 0);
    atomic_init(&p->tx_ctrl.abort_requested, false);
    atomic_init(&p->tx_ctrl.write_calls, 0);
    atomic_init(&p->tx_ctrl.write_samples, 0);
    atomic_init(&p->tx_ctrl.partial_writes, 0);
    atomic_init(&p->tx_ctrl.zero_writes, 0);
    atomic_init(&p->tx_ctrl.would_block, 0);
    atomic_init(&p->tx_ctrl.write_errors, 0);
    atomic_init(&p->dsp_ctrl.active, false);
    atomic_init(&p->dsp_ctrl.alive, false);
    atomic_init(&p->dsp_ctrl.last_error, 0);
    pipeline_thread_diagnostics_init(&p->tx_ctrl.scheduling);
    pipeline_thread_diagnostics_init(&p->dsp_ctrl.scheduling);
    if (pthread_mutex_init(&p->tx_ctrl.state_mutex, NULL) != 0) return -2;
    pthread_condattr_t gate_attr;
    pthread_condattr_init(&gate_attr);
    pthread_condattr_setclock(&gate_attr, CLOCK_MONOTONIC);
    int gate_error = pthread_cond_init(&p->tx_ctrl.state_changed, &gate_attr);
    pthread_condattr_destroy(&gate_attr);
    if (gate_error) {
        pthread_mutex_destroy(&p->tx_ctrl.state_mutex);
        return -2;
    }
    p->tx_ctrl.gate_initialized = true;
    p->tx_ctrl.verify_start_profile = par->verify_start_profile;
    p->tx_ctrl.frame_samples = rf_fs / 100;
    atomic_init(&p->tx_ctrl.written_sequence, 0);
    // Cache the buffer configuration before starting workers. If the query
    // is unavailable, use the driver's largest supported FIFO multiplier.
    int fifo_multiplier = 32;
    if (ioctl(sys->smi.filedesc, SMI_STREAM_IOC_GET_FIFO_MULT, &fifo_multiplier) != 0 ||
        fifo_multiplier < 2 || fifo_multiplier > 32) fifo_multiplier = 32;
    size_t native_samples = caribou_smi_get_native_batch_samples(&sys->smi);
    if (!native_samples) native_samples = 524288 / sizeof(caribou_smi_sample_complex_int16);
    p->tx_ctrl.tail_padding_frames = tx_tail_padding_frames(native_samples,
        (unsigned)fifo_multiplier, p->tx_ctrl.frame_samples);

    int pacing_error = tx_detect_pacing(&p->tx_ctrl, sys->smi.filedesc, par->pacing_mode);
    if (pacing_error) {
        pthread_cond_destroy(&p->tx_ctrl.state_changed);
        pthread_mutex_destroy(&p->tx_ctrl.state_mutex);
        p->tx_ctrl.gate_initialized = false;
        return pacing_error;
    }
    // Hardware backpressure bounds latency; legacy retains its original queue.
    rf10_fifo_init(&p->txq, p->tx_ctrl.hardware_paced ? 4 : 64, false);

    p->inited = true; // FIFO synchronization is ready for staged cleanup.
    int error = -2;
    if (!p->txq.q) goto fail;

    // tx_ctrl wiring (reuse your structures/threads)
    p->tx_ctrl.active         = true;
    p->tx_ctrl.radio          = radio;
    p->tx_ctrl.fifo           = &p->txq;
    p->tx_ctrl.live_from_mic  = (par->mic_dev && *par->mic_dev);
    p->tx_ctrl.mic            = NULL;            // (open later if needed)
    p->tx_ctrl.tone_mode      = par->tone_mode;
    p->tx_ctrl.tone_hz        = par->tone_hz;
    p->tx_ctrl.tone_amp       = par->tone_amp;
    p->tx_ctrl.tone = tone_source_open(par->tone_hz, par->tone_amp,
                                        (audio_format_t){48000, 1});
    if (!p->tx_ctrl.tone) goto fail;
    p->tx_ctrl.fm             = NULL;
    p->tx_ctrl.a48k           = NULL;
    p->tx_ctrl.iq_rf           = NULL;

    // NBFM mod init
    nbfm_cfg_t cfg = {
        .audio_fs      = 48000.0,
        .rf_fs         = rf_fs,
        .f_dev_hz      = par->f_dev_hz,
        .preemph_tau_s = 0.0,
        .out_scale     = par->out_scale,    // 4000.0
        .linear_interp = 1,
    };
    p->tx_ctrl.fm   = nbfm_create(&cfg);
    p->tx_ctrl.a48k = (float*) calloc(480,    sizeof(float));
    p->tx_ctrl.iq_rf = (iq16_t*)calloc(p->tx_ctrl.frame_samples,  sizeof(iq16_t));
    if (!p->tx_ctrl.fm || !p->tx_ctrl.a48k || !p->tx_ctrl.iq_rf) {
        goto fail;
    }
    
    p->tx_ctrl.inj.frames_left = 0;
    p->tx_ctrl.inj.hz = 0.0f;

    // Optional mic
    if (p->tx_ctrl.live_from_mic) {
        p->tx_ctrl.mic = alsa_source_open(par->mic_dev, 1.0f, (audio_format_t){48000, 1});
        if (!p->tx_ctrl.mic) {
            fprintf(stderr, "[tx_pipeline] ALSA capture open failed (%s)\n",
                    par->mic_dev ? par->mic_dev : "(null)");
            error = -5;
            goto fail;
        }
        if (alsa_source_reset(p->tx_ctrl.mic, false) < 0) {
            error = -5;
            goto fail;
        }
        if (p->tx_ctrl.hardware_paced) {
            p->tx_ctrl.audio_buffer = tx_audio_buffer_create(p->tx_ctrl.mic);
            if (!p->tx_ctrl.audio_buffer) { error = -5; goto fail; }
        }
    }

    // Radio basic set
    HW_LOCK();
    double init_frequency = par->freq_hz;
    int tune_error = cariboulite_radio_set_frequency(radio, true, &init_frequency);
    int power_error = cariboulite_radio_set_tx_power (radio, par->tx_power_dbm);
    HW_UNLOCK();
    if (tune_error || power_error) goto fail;

    // Configure and verify filters/rate before creating threads or allowing TX.
    HW_LOCK();
    int profile_error = tx_configure_nbfm_profile(p, par->tx_power_dbm);
    HW_UNLOCK();
    if (profile_error) goto fail;

    // Prepare DSP/Writer threads (running idle until .start)
    p->dsp_ctrl.active = true;
    p->dsp_ctrl.tx     = &p->tx_ctrl;
    p->dsp_ctrl.fifo   = &p->txq;
    if (pthread_create(&p->dsp_thread, NULL, nbfm_mod_thread, &p->dsp_ctrl) != 0) {
        error = -3;
        goto fail;
    }
    p->dsp_thread_created = true;

    if (pthread_create(&p->tx_thread,  NULL, tx_writer_thread_func,    &p->tx_ctrl) != 0) {
        error = -4;
        goto fail;
    }
    p->tx_thread_created = true;

    p->inited = true;
    p->running = false;
    return 0;

fail:
    tx_pipeline_destroy(p);
    return error;
}

/* The public setters hide some SPI failures. The backend opts into direct,
 * checked readback before workers can write the opening Quindar cue. Called
 * under the lifecycle hardware lock, immediately after activation and power
 * restoration. TXPREP is valid until IQ interface control receives samples. */
static int tx_verify_start_profile(tx_pipeline_t* p, int requested_power)
{
    bool low = p->radio->type == cariboulite_channel_s1g;
    int power_code = radio_profile_tx_power_code(p->radio->type, requested_power);
    if (power_code < 0) return -1;
    unsigned rate = (unsigned)p->tx_ctrl.frame_samples * 100;
    unsigned rate_code = tx_rate_code(rate);
    uint8_t pac = 0, txcutc = 0, txdfe = 0, state = 0, gap = 255;
    int pac_error = at86rf215_read_buffer(&p->sys->modem,
        low ? REG_RF09_PAC : REG_RF24_PAC, &pac, 1);
    int cutoff_error = at86rf215_read_buffer(&p->sys->modem,
        low ? REG_RF09_TXCUTC : REG_RF24_TXCUTC, &txcutc, 1);
    int rate_error = at86rf215_read_buffer(&p->sys->modem,
        low ? REG_RF09_TXDFE : REG_RF24_TXDFE, &txdfe, 1);
    int state_error = at86rf215_read_buffer(&p->sys->modem,
        low ? REG_RF09_STATE : REG_RF24_STATE, &state, 1);
    int gap_error = caribou_fpga_get_sys_ctrl_tx_sample_gap(&p->sys->fpga, &gap);
    unsigned expected_dfe = rate_code; /* RCUT=0 (Fs/8), DM=0. */
    bool mismatch = pac_error || cutoff_error || rate_error || state_error || gap_error
        || (pac & 0x7f) != (0x60 | power_code) || (txcutc & 0xcf) != 0x80
        || txdfe != expected_dfe
        || gap != 4000000 / rate - 1
        || ((state & 7) != at86rf215_radio_state_cmd_tx_prep
            && (state & 7) != at86rf215_radio_state_cmd_tx);
    if (mismatch)
        fprintf(stderr, "[tx_pipeline] start profile mismatch: "
            "PAC=0x%02X expected=0x%02X rc=%d TXCUTC=0x%02X expected=0x80 rc=%d "
            "TXDFE=0x%02X expected=0x%02X rc=%d "
            "STATE=0x%02X rc=%d gap=%u expected=%u rc=%d\n",
            pac, 0x60 | power_code, pac_error, txcutc, cutoff_error,
            txdfe, expected_dfe, rate_error,
            state, state_error, gap, 4000000 / rate - 1, gap_error);
    return mismatch ? -1 : 0;
}

static inline int ms_to_frames_10ms(int ms) { return (ms + 9) / 10; }

static int tx_park_workers(tx_pipeline_t* p)
{
    tx_writer_ctrl_st* ctrl = &p->tx_ctrl;
    if (!ctrl->gate_initialized) return 0;
    tx_gate_set(ctrl, false);
    rf10_fifo_stop(&p->txq);
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_nsec += 250000000;
    if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_nsec -= 1000000000;
        ++deadline.tv_sec;
    }
    int rc = 0;
    pthread_mutex_lock(&ctrl->state_mutex);
    while (ctrl->producer_busy || ctrl->writer_busy) {
        int wait_error = pthread_cond_timedwait(&ctrl->state_changed,
                                               &ctrl->state_mutex, &deadline);
        if (wait_error) { rc = -wait_error; break; }
    }
    pthread_mutex_unlock(&ctrl->state_mutex);
    if (!rc && ctrl->audio_buffer) tx_audio_buffer_park(ctrl->audio_buffer);
    if (!rc && ctrl->mic) rc = alsa_source_reset(ctrl->mic, false);
    return rc;
}

static void tx_clear_injection(tx_pipeline_t* p)
{
    pthread_mutex_lock(&g_tx_injection_lock);
    p->tx_ctrl.inj.frames_left = 0;
    p->tx_ctrl.inj.fast_padding = false;
    pthread_cond_broadcast(&g_tx_injection_changed);
    pthread_mutex_unlock(&g_tx_injection_lock);
}

static bool tx_injection_can_run(const tx_pipeline_t* p)
{
    return p && p->running && nbfm_tx_active && !atomic_load(&p->tx_ctrl.abort_requested) &&
           p->tx_ctrl.active && p->dsp_ctrl.active;
}

static bool tx_wait_injection(tx_pipeline_t* p, uint64_t deadline)
{
    while (true) {
        pthread_mutex_lock(&g_tx_injection_lock);
        int left = p->tx_ctrl.inj.frames_left;
        pthread_mutex_unlock(&g_tx_injection_lock);
        if (left <= 0) break;
        if (!tx_injection_can_run(p) || mono_ns() >= deadline) {
            tx_clear_injection(p);
            return false;
        }
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 2*1000*1000 };
        nanosleep(&ts, NULL);
    }
    return tx_injection_can_run(p);
}

static bool tx_inject_frames(tx_pipeline_t* p, float hz, int frames,
                             uint64_t deadline, bool fast_padding)
{
    if (!tx_injection_can_run(p) || frames <= 0 || mono_ns() >= deadline)
        return false;
    pthread_mutex_lock(&g_tx_injection_lock);
    p->tx_ctrl.inj.hz = hz;
    p->tx_ctrl.inj.fast_padding = fast_padding;
    p->tx_ctrl.inj.frames_left = frames;
    pthread_cond_broadcast(&g_tx_injection_changed);
    pthread_mutex_unlock(&g_tx_injection_lock);
    return tx_wait_injection(p, deadline);
}

static bool tx_inject_tone_with_zeros(tx_pipeline_t* p,
                                      float hz, int tone_ms,
                                      int pre_zero_frames,
                                      int post_zero_frames, bool fast_padding)
{
    if (pre_zero_frames  < 1) pre_zero_frames  = 1;
    if (post_zero_frames < 1) post_zero_frames = 1;

    /* One bounded budget covers the rate-dependent padding and queued frames. */
    uint64_t budget_ms = (uint64_t)(pre_zero_frames + ms_to_frames_10ms(tone_ms) +
                                   post_zero_frames) * 10 + (p->txq.cap + 1) * 10 + 600;
    if (budget_ms < 1000) budget_ms = 1000;
    const uint64_t deadline = mono_ns() + budget_ms * 1000000ULL;
    return tx_inject_frames(p, 0.0f, pre_zero_frames, deadline, false) &&
           tx_inject_frames(p, hz, ms_to_frames_10ms(tone_ms), deadline, false) &&
           tx_inject_frames(p, 0.0f, post_zero_frames, deadline, fast_padding);
}

int tx_pipeline_start(tx_pipeline_t* p)
{
    if (!p || !p->inited || p->running) return -1;
    if (atomic_load(&p->tx_ctrl.last_error) || atomic_load(&p->dsp_ctrl.last_error))
        return -3;
    /* The backend may request cancellation while preparing the TX profile.
     * A strict start must preserve that request until its owner rebuilds the
     * stopped pipeline. Legacy callers keep the existing retry behavior. */
    if (p->tx_ctrl.verify_start_profile) {
        if (atomic_load(&p->tx_ctrl.abort_requested)) {
            tx_pipeline_abort(p);
            return -3;
        }
    } else atomic_store(&p->tx_ctrl.abort_requested, false);
    if (tx_park_workers(p) < 0) return -3;
    if (p->tx_ctrl.audio_buffer) {
        if (tx_audio_buffer_resume(p->tx_ctrl.audio_buffer) < 0) return -3;
        tx_audio_buffer_set_cue(p->tx_ctrl.audio_buffer, true);
    } else if (p->tx_ctrl.mic && alsa_source_reset(p->tx_ctrl.mic, true) < 0) return -3;

    // Disable RX if it was on and arm TX chain
    if (nbfm_rx_active) {
        nbfm_rx_active = false;
        HW_LOCK();
        cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_rx, false);
        HW_UNLOCK();
        usleep(2000);
    }

    // Clear any stale queued frames before starting TX
    rf10_fifo_flush(&p->txq);
    pthread_mutex_lock(&p->txq.m);
    p->txq.stop = false;
    pthread_mutex_unlock(&p->txq.m);
    pthread_mutex_lock(&g_tx_injection_lock);
    p->tx_ctrl.inj.hold_silence = true;
    p->tx_ctrl.inj.fast_padding = false;
    pthread_mutex_unlock(&g_tx_injection_lock);

    if (p->tx_ctrl.hardware_paced) {
        /* The owner is the sole FM user after tx_park_workers acknowledges the
         * producer. Precompute one of the ten opening zero-audio frames before
         * DMA activation; the writer stays gated until the hardware profile is
         * verified. This supplies its first refill without waiting for DSP. */
        memset(p->tx_ctrl.a48k, 0, 480 * sizeof(float));
        rf10_frame_t opening;
        int prime_error = tx_generate_frame(&p->tx_ctrl, &opening);
        if (prime_error || !rf10_fifo_put(&p->txq, &opening, 0)) {
            atomic_store(&p->dsp_ctrl.last_error, prime_error ? prime_error : -EIO);
            tx_pipeline_abort(p);
            return -3;
        }
        pthread_mutex_lock(&g_tx_injection_lock);
        p->tx_ctrl.inj.hz = 0.0f;
        p->tx_ctrl.inj.frames_left = 9;
        pthread_cond_broadcast(&g_tx_injection_changed);
        pthread_mutex_unlock(&g_tx_injection_lock);
    }

    HW_LOCK();
    int requested_power = p->radio->tx_power;
    /* Activation itself enables the SMI TX stream. Configure admission first
     * while idle so its initial DMA ring uses the bounded framing policy. */
    int idle_error = caribou_smi_set_driver_streaming_state(&p->sys->smi, smi_stream_idle);
    int admission_error = idle_error ? idle_error : tx_configure_admission(p);
    int timing_error = admission_error ? admission_error : tx_apply_write_timing(p);
    if (timing_error && p->tx_ctrl.hardware_paced)
        fprintf(stderr, "[tx_pipeline] SMI TX write-profile setup failed: %d\n", timing_error);
    /* A retained allocation can outlive RX or generic radio configuration.
     * Reapply the NBFM profile on every start while the sample gate is shut. */
    int filter_error = timing_error ? -1 : tx_configure_nbfm_profile(p, requested_power);
    int fpga_error = filter_error ? -1 :
        caribou_fpga_set_io_ctrl_mode(&p->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_tx_lowpass);
    int radio_error = timing_error || filter_error || fpga_error ? -1 :
        cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_tx, true);
    /* Activation's bandwidth setter uses an uncalibrated PAC mapping. Restore
     * the requested calibrated power before the first opening-cue sample. */
    int power_error = cariboulite_radio_set_tx_power(p->radio, requested_power);
    int profile_error = !radio_error && !power_error && p->tx_ctrl.verify_start_profile
        ? tx_verify_start_profile(p, requested_power) : 0;
    /* Activation waits for PLL lock and has already started DMA. Do not hold
     * the new producer off for another 30 ms (several 4 MS/s periods). */
    if (!p->tx_ctrl.hardware_paced) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 30*1000*1000 };
        nanosleep(&ts, NULL);
    }
    int stream_error = fpga_error || radio_error || power_error || profile_error || timing_error || filter_error
        ? -1 : caribou_smi_set_driver_streaming_state(&p->sys->smi, smi_stream_tx_channel);
    HW_UNLOCK();
    if (fpga_error || radio_error || power_error || profile_error || timing_error || filter_error || stream_error
        || atomic_load(&p->tx_ctrl.abort_requested)) {
        tx_pipeline_abort(p);
        return -3;
    }

    __sync_synchronize();
    nbfm_tx_active = true;

    p->running = true;
    tx_gate_set(&p->tx_ctrl, true);
    
    // --- Quindar "start" tone: 2525 Hz for 250 ms with 5 frames of padding ---
    bool cue_ok;
    if (p->tx_ctrl.hardware_paced) {
        const uint64_t deadline = mono_ns() + 1000000000ULL;
        cue_ok = tx_wait_injection(p, deadline) &&
            tx_inject_frames(p, 2525.0f, 25, deadline, false) &&
            tx_inject_frames(p, 0.0f, 5, deadline, false);
    } else {
        cue_ok = tx_inject_tone_with_zeros(p, 2525.0f, 250, 10, 5, false);
    }
    if (!cue_ok) {
        fprintf(stderr, "TX start tone aborted; stopping TX\n");
        nbfm_tx_active = false; // stop must skip another tone attempt
        tx_pipeline_stop(p);
        return -2;
    }
    tx_audio_buffer_set_cue(p->tx_ctrl.audio_buffer, false);
    pthread_mutex_lock(&g_tx_injection_lock);
    p->tx_ctrl.inj.hold_silence = false;
    pthread_cond_broadcast(&g_tx_injection_changed);
    pthread_mutex_unlock(&g_tx_injection_lock);
    
    return 0;
}

static bool tx_wait_written(tx_pipeline_t* p, uint64_t sequence, int timeout_ms)
{
    if (!p || !sequence) return false;
    const uint64_t deadline = mono_ns() + (uint64_t)timeout_ms * 1000000ULL;
    while (tx_injection_can_run(p)) {
        if (atomic_load(&p->tx_ctrl.written_sequence) >= sequence) return true;
        if (mono_ns() >= deadline) return false;

        struct timespec ts = { .tv_sec = 0, .tv_nsec = 2*1000*1000 };
        nanosleep(&ts, NULL);
    }
    return false;
}

static void tx_record_stop_error(tx_pipeline_t* p, int error)
{
    /* Keep the original worker failure as the explanation for a skipped cue. */
    if (atomic_load_explicit(&p->dsp_ctrl.last_error, memory_order_relaxed)) return;
    int none = 0;
    atomic_compare_exchange_strong_explicit(&p->tx_ctrl.last_error, &none, error,
                                            memory_order_relaxed, memory_order_relaxed);
}

void tx_pipeline_stop(tx_pipeline_t* p)
{
    if (!p || !p->inited || !p->running) return;
    
    // Enough accepted silence displaces the complete 250 ms tail through the
    // kernel/DMA queues. Generate final padding without audio-clock pacing:
    // writer backpressure, rather than worst-case FIFO capacity, sets the wait.
    pthread_mutex_lock(&g_tx_injection_lock);
    p->tx_ctrl.inj.hold_silence = true;
    pthread_mutex_unlock(&g_tx_injection_lock);
    tx_audio_buffer_set_cue(p->tx_ctrl.audio_buffer, true);
    if (tx_inject_tone_with_zeros(p, 2475.0f, 250, 5,
                                p->tx_ctrl.tail_padding_frames, true)) {
        pthread_mutex_lock(&g_tx_injection_lock);
        uint64_t sequence = p->tx_ctrl.inj.last_sequence;
        pthread_mutex_unlock(&g_tx_injection_lock);
        if (!tx_wait_written(p, sequence, 600 + (p->txq.cap + 1) * 10)) {
            tx_record_stop_error(p, -ETIMEDOUT);
            fprintf(stderr, "TX tail drain timed out or writer failed; continuing hardware shutdown\n");
        }
    } else {
        tx_record_stop_error(p, -ETIMEDOUT);
        fprintf(stderr, "TX tail tone aborted; continuing hardware shutdown\n");
    }
    tx_clear_injection(p);

    nbfm_tx_active = false;
    __sync_synchronize();

    // Wait for any in-flight writer call before releasing the stream to RX.
    HW_LOCK();
    int stream_error = caribou_smi_set_driver_streaming_state(&p->sys->smi, (smi_stream_state_en)0); // idle
    int radio_error = cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_tx, false);
    int fpga_error = caribou_fpga_set_io_ctrl_mode(&p->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
    int timing_error = tx_restore_write_timing(p);
    if (timing_error)
        fprintf(stderr, "[tx_pipeline] SMI write-profile restore failed: %d\n", timing_error);
    HW_UNLOCK();

    p->running = false;
    int park_error = tx_park_workers(p);
    if (park_error)
        tx_record_stop_error(p, park_error);
    else if (stream_error || radio_error || fpga_error || timing_error)
        tx_record_stop_error(p, -EIO);
}

void tx_pipeline_request_abort(tx_pipeline_t* p)
{
    if (!p || !p->inited) return;
    atomic_store(&p->tx_ctrl.abort_requested, true);
    nbfm_tx_active = false;
    tx_gate_set(&p->tx_ctrl, false);
    rf10_fifo_stop(&p->txq);
    tx_audio_buffer_request_abort(p->tx_ctrl.audio_buffer);
}

int tx_pipeline_abort(tx_pipeline_t* p)
{
    if (!p || !p->inited) return 0;
    tx_pipeline_request_abort(p);
    tx_clear_injection(p);
    HW_LOCK();
    int stream_error = caribou_smi_set_driver_streaming_state(&p->sys->smi, smi_stream_idle);
    int radio_error = cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_tx, false);
    int fpga_error = caribou_fpga_set_io_ctrl_mode(&p->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
    int timing_error = tx_restore_write_timing(p);
    if (timing_error)
        fprintf(stderr, "[tx_pipeline] SMI write-profile restore failed: %d\n", timing_error);
    HW_UNLOCK();
    p->running = false;
    int park_error = tx_park_workers(p);
    if (park_error)
        tx_record_stop_error(p, park_error);
    else if (stream_error || radio_error || fpga_error || timing_error)
        tx_record_stop_error(p, -EIO);
    return stream_error || radio_error || fpga_error || timing_error || park_error ? -1 : 0;
}

void tx_pipeline_destroy(tx_pipeline_t* p)
{
    if (!p || !p->inited) return;

    tx_pipeline_stop(p);

    // stop threads and free
    p->tx_ctrl.active  = false;
    p->dsp_ctrl.active = false;
    tx_gate_set(&p->tx_ctrl, false);
    rf10_fifo_stop(&p->txq);
    tx_audio_buffer_request_abort(p->tx_ctrl.audio_buffer);

    if (p->tx_thread_created) {
        pthread_cancel(p->tx_thread);
        pthread_join(p->tx_thread, NULL);
        p->tx_thread_created = false;
    }
    if (p->dsp_thread_created) {
        pthread_cancel(p->dsp_thread);
        pthread_join(p->dsp_thread, NULL);
        p->dsp_thread_created = false;
    }
    /* A failed stopped start/restore can leave a saved timing tuple. Retry
     * after both workers joined, before freeing the pipeline or RX handoff. */
    if (p->tx_ctrl.smi_write_timing_saved) {
        HW_LOCK();
        int idle_error = caribou_smi_set_driver_streaming_state(&p->sys->smi, smi_stream_idle);
        int timing_error = idle_error ? idle_error : tx_restore_write_timing(p);
        HW_UNLOCK();
        if (timing_error) tx_record_stop_error(p, timing_error);
    }
    if (p->tx_ctrl.gate_initialized) {
        pthread_cond_destroy(&p->tx_ctrl.state_changed);
        pthread_mutex_destroy(&p->tx_ctrl.state_mutex);
        p->tx_ctrl.gate_initialized = false;
    }

    rf10_fifo_destroy(&p->txq);

    if (p->tx_ctrl.iq_rf) free(p->tx_ctrl.iq_rf);
    if (p->tx_ctrl.a48k) free(p->tx_ctrl.a48k);
    if (p->tx_ctrl.fm)   nbfm_destroy(p->tx_ctrl.fm);
    tx_audio_buffer_destroy(p->tx_ctrl.audio_buffer);
    p->tx_ctrl.audio_buffer = NULL;
    if (p->tx_ctrl.mic)  audio_source_destroy(p->tx_ctrl.mic);
    audio_source_destroy(p->tx_ctrl.tone);
    p->tx_ctrl.tone = NULL;

    p->tx_ctrl.iq_rf = NULL;
    p->tx_ctrl.a48k = NULL;
    p->tx_ctrl.fm = NULL;
    p->tx_ctrl.mic = NULL;
    p->inited = false;
}

bool tx_pipeline_running(const tx_pipeline_t* p) { return p && p->running; }

bool tx_pipeline_healthy(const tx_pipeline_t* p)
{
    tx_audio_buffer_stats_t audio = {0};
    if (p && p->tx_ctrl.audio_buffer)
        tx_audio_buffer_get_stats(p->tx_ctrl.audio_buffer, &audio);
    return p && p->inited && p->dsp_thread_created && p->tx_thread_created
        && (!p->tx_ctrl.audio_buffer || (audio.alive && !audio.last_error))
        && atomic_load_explicit(&p->dsp_ctrl.alive, memory_order_relaxed)
        && atomic_load_explicit(&p->tx_ctrl.alive, memory_order_relaxed)
        && !atomic_load_explicit(&p->dsp_ctrl.last_error, memory_order_relaxed)
        && !atomic_load_explicit(&p->tx_ctrl.last_error, memory_order_relaxed);
}

int tx_pipeline_set_freq_power(tx_pipeline_t* p, double freq_hz, int tx_power_dbm)
{
    if (!p || !p->sys || !p->radio ||
        !radio_profile_frequency_valid(p->radio->type,
            p->sys->board_info.numeric_product_id, freq_hz) ||
        !radio_profile_tx_power_valid(p->radio->type, tx_power_dbm)) return -1;
    HW_LOCK();
    int rc = cariboulite_radio_set_frequency(p->radio, true, &freq_hz);
    if (rc == 0) rc = cariboulite_radio_set_tx_power(p->radio, tx_power_dbm);
    HW_UNLOCK();
    return rc;
}

void tx_pipeline_get_stats(tx_pipeline_t* p, tx_pipeline_stats_t* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!p || !p->inited) return;
    rf10_fifo_get_stats(&p->txq, &out->txq);
    out->writer.calls = atomic_load_explicit(&p->tx_ctrl.write_calls, memory_order_relaxed);
    out->writer.samples = atomic_load_explicit(&p->tx_ctrl.write_samples, memory_order_relaxed);
    out->writer.partial_writes = atomic_load_explicit(&p->tx_ctrl.partial_writes, memory_order_relaxed);
    out->writer.zero_writes = atomic_load_explicit(&p->tx_ctrl.zero_writes, memory_order_relaxed);
    out->writer.would_block = atomic_load_explicit(&p->tx_ctrl.would_block, memory_order_relaxed);
    out->writer.errors = atomic_load_explicit(&p->tx_ctrl.write_errors, memory_order_relaxed);
    out->writer.last_error = atomic_load_explicit(&p->tx_ctrl.last_error, memory_order_relaxed);
    out->writer.written_sequence = atomic_load_explicit(&p->tx_ctrl.written_sequence, memory_order_relaxed);
    out->producer_worker.created = p->dsp_thread_created;
    out->producer_worker.alive = atomic_load_explicit(&p->dsp_ctrl.alive, memory_order_relaxed);
    out->producer_worker.last_error = atomic_load_explicit(&p->dsp_ctrl.last_error, memory_order_relaxed);
    pipeline_thread_diagnostics_get(&p->dsp_ctrl.scheduling, &out->producer_worker.scheduling);
    out->writer_worker.created = p->tx_thread_created;
    out->writer_worker.alive = atomic_load_explicit(&p->tx_ctrl.alive, memory_order_relaxed);
    out->writer_worker.last_error = atomic_load_explicit(&p->tx_ctrl.last_error, memory_order_relaxed);
    pipeline_thread_diagnostics_get(&p->tx_ctrl.scheduling, &out->writer_worker.scheduling);
    out->capture_valid = alsa_source_get_stats(p->tx_ctrl.mic, &out->capture);
    out->hardware_paced = p->tx_ctrl.hardware_paced;
    out->driver_valid = out->hardware_paced &&
        ioctl(p->sys->smi.filedesc, SMI_STREAM_IOC_GET_TX_STATUS, &out->driver) == 0;
    out->smi_timing_valid = out->hardware_paced &&
        ioctl(p->sys->smi.filedesc, BCM2835_SMI_IOC_GET_SETTINGS, &out->smi_timing) == 0;
    out->audio_buffer_valid = p->tx_ctrl.audio_buffer != NULL;
    if (out->audio_buffer_valid)
        tx_audio_buffer_get_stats(p->tx_ctrl.audio_buffer, &out->audio_buffer);
}

size_t tx_pipeline_frame_samples(const tx_pipeline_t* p)
{
    return p ? (size_t)(p->tx_ctrl.frame_samples) : 0;
}

void tx_pipeline_reset_stats(tx_pipeline_t* p)
{
    if (p && p->inited) {
        rf10_fifo_reset_stats(&p->txq);
        tx_audio_buffer_reset_stats(p->tx_ctrl.audio_buffer);
    }
}
