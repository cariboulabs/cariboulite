#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "tx_pipeline.h"
#include "tone_source.h"
#include "pipeline_runtime.h"
#include <assert.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>

/* Run real producer/writer/capture threads and FIFO against bounded driver
 * admission retired in native 32768-IQ periods. No RF or audio device opens. */
static pthread_mutex_t driver_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct smi_stream_tx_status driver;
static bool abi_present = true, invalid_abi, refused_limit;
static atomic_bool stalled_driver;
static atomic_uint activations, timed_writes, legacy_writes, microphone_reads;
static atomic_uint opening_cues, closing_cues;
static atomic_uint profile_checks;
static uint8_t pac_code, modem_state;
static struct smi_settings driver_settings, original_settings;
static unsigned settings_reads, settings_writes, timing_verifications;
static unsigned primed_frames, first_refill_successes, session_mod_frames, stream_starts;
static bool timing_verified, timing_verification_attempted, first_producer_delayed;
static bool fail_restore_read_once, fail_restore_write_once;
enum { SETTINGS_OK, SNAPSHOT_FAIL, TIMING_WRITE_FAIL, VERIFY_READ_FAIL,
       WRONG_WRITE_TIMING, WRONG_READ_TIMING, WRONG_DMA_THRESHOLD,
       MODULATOR_FAIL, ACTIVATION_FAIL, PROFILE_FAIL, STREAM_FAIL, OWNER_ABORT };
static unsigned start_failure;
static unsigned rf_rate;
static uint64_t next_retirement_ns;
static tx_pipeline_t *current;

static bool hardware_lock_held(void)
{
    int result = pthread_mutex_trylock(&g_hw_lock);
    if (!result) pthread_mutex_unlock(&g_hw_lock);
    return result == EBUSY;
}
static void assert_hardware_lock(void) { assert(hardware_lock_held()); }
static bool same_settings(const struct smi_settings *a, const struct smi_settings *b)
{
    return a->data_width == b->data_width && a->pack_data == b->pack_data &&
        a->read_setup_time == b->read_setup_time && a->read_hold_time == b->read_hold_time &&
        a->read_pace_time == b->read_pace_time && a->read_strobe_time == b->read_strobe_time &&
        a->write_setup_time == b->write_setup_time && a->write_hold_time == b->write_hold_time &&
        a->write_pace_time == b->write_pace_time && a->write_strobe_time == b->write_strobe_time &&
        a->dma_enable == b->dma_enable && a->dma_passthrough_enable == b->dma_passthrough_enable &&
        a->dma_read_thresh == b->dma_read_thresh && a->dma_write_thresh == b->dma_write_thresh &&
        a->dma_panic_read_thresh == b->dma_panic_read_thresh &&
        a->dma_panic_write_thresh == b->dma_panic_write_thresh;
}
static void assert_original_write_timing(void)
{
    assert(driver_settings.write_setup_time == original_settings.write_setup_time);
    assert(driver_settings.write_strobe_time == original_settings.write_strobe_time);
    assert(driver_settings.write_hold_time == original_settings.write_hold_time);
}

static uint64_t now_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;
}
static void delay_ms(unsigned ms)
{
    struct timespec delay = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&delay, NULL);
}
static void retire_locked(void)
{
    if (driver.state != smi_stream_tx_channel || atomic_load(&stalled_driver)) return;
    uint64_t now = now_ns();
    uint64_t period_ns = (uint64_t)driver.period_bytes / 4 * 1000000000ULL / rf_rate;
    while (now >= next_retirement_ns) {
        if (!driver.completed_periods && !atomic_load(&stalled_driver)) {
            /* A real 4 MS/s quarter completes after 8.192 ms, earlier than a
             * fresh 10 ms frame. The precomputed frame must cover it. */
            assert(driver.fifo_queued_bytes >= driver.period_bytes);
            ++first_refill_successes;
        }
        if (driver.fifo_queued_bytes >= driver.period_bytes) {
            driver.fifo_queued_bytes -= driver.period_bytes;
            driver.fifo_dequeued_bytes += driver.period_bytes;
        } else ++driver.underrun_periods;
        ++driver.completed_periods;
        next_retirement_ns += period_ns;
    }
}
int __wrap_ioctl(int descriptor, unsigned long request, ...)
{
    (void)descriptor;
    va_list arguments;
    va_start(arguments, request);
    void *argument = va_arg(arguments, void *);
    va_end(arguments);
    if (request == SMI_STREAM_IOC_GET_FIFO_MULT) {
        *(int *)argument = 16;
        return 0;
    }
    if (!abi_present) { errno = ENOTTY; return -1; }
    pthread_mutex_lock(&driver_mutex);
    int result = 0;
    if (request == BCM2835_SMI_IOC_GET_SETTINGS) {
        /* Passive diagnostics can read outside HW_LOCK. Lifecycle reads and
         * mutations are serialized, and alone consume injected failures. */
        if (!hardware_lock_held()) {
            *(struct smi_settings *)argument = driver_settings;
            pthread_mutex_unlock(&driver_mutex);
            return 0;
        }
        ++settings_reads;
        if (fail_restore_read_once && driver.state == smi_stream_idle &&
            timing_verification_attempted && settings_writes == 1) {
            fail_restore_read_once = false;
            errno = EIO; result = -1;
        } else if ((start_failure == SNAPSHOT_FAIL && !settings_writes) ||
            (start_failure == VERIFY_READ_FAIL && settings_writes == 1 &&
             !timing_verification_attempted)) {
            if (settings_writes == 1) timing_verification_attempted = true;
            errno = EIO; result = -1;
        } else {
            *(struct smi_settings *)argument = driver_settings;
            if (settings_writes == 1 && !timing_verification_attempted) {
                timing_verification_attempted = true;
                struct smi_settings *readback = argument;
                if (start_failure == WRONG_WRITE_TIMING) ++readback->write_strobe_time;
                if (start_failure == WRONG_READ_TIMING) ++readback->read_strobe_time;
                if (start_failure == WRONG_DMA_THRESHOLD) ++readback->dma_write_thresh;
                if (start_failure != WRONG_WRITE_TIMING && start_failure != WRONG_READ_TIMING &&
                    start_failure != WRONG_DMA_THRESHOLD) {
                    assert(readback->write_setup_time == 1 && readback->write_strobe_time == 4 &&
                           readback->write_hold_time == 2);
                    timing_verified = true;
                    ++timing_verifications;
                }
            }
        }
    } else if (request == BCM2835_SMI_IOC_WRITE_SETTINGS) {
        assert_hardware_lock();
        assert(driver.state == smi_stream_idle);
        const struct smi_settings *settings = argument;
        struct smi_settings expected = driver_settings;
        expected.write_setup_time = settings->write_setup_time;
        expected.write_strobe_time = settings->write_strobe_time;
        expected.write_hold_time = settings->write_hold_time;
        assert(same_settings(settings, &expected));
        ++settings_writes;
        if (settings_writes == 1) {
            assert(settings->write_setup_time == 1 && settings->write_strobe_time == 4 &&
                   settings->write_hold_time == 2);
            if (start_failure == TIMING_WRITE_FAIL) {
                timing_verification_attempted = true;
                errno = EIO; result = -1;
            }
            else driver_settings = *settings;
        } else {
            assert(settings->write_setup_time == original_settings.write_setup_time &&
                   settings->write_strobe_time == original_settings.write_strobe_time &&
                   settings->write_hold_time == original_settings.write_hold_time);
            if (fail_restore_write_once) {
                fail_restore_write_once = false;
                errno = EIO; result = -1;
            } else driver_settings = *settings;
        }
    } else if (request == SMI_STREAM_IOC_GET_TX_STATUS) {
        retire_locked();
        *(struct smi_stream_tx_status *)argument = driver;
        if (invalid_abi) ((struct smi_stream_tx_status *)argument)->abi_version = 99;
    } else if (request == SMI_STREAM_IOC_SET_TX_BUFFER_LIMIT) {
        struct smi_stream_tx_buffer_limit *limit = argument;
        assert(driver.state == smi_stream_idle);
        assert(limit->abi_version == SMI_STREAM_TX_ABI_VERSION);
        assert(limit->struct_size == sizeof(*limit));
        if (refused_limit) { errno = EBUSY; result = -1; }
        else {
            driver.fifo_limit_bytes = limit->fifo_limit_bytes
                ? limit->fifo_limit_bytes : driver.fifo_capacity_bytes;
            driver.flags = limit->fifo_limit_bytes ? SMI_STREAM_TX_STATUS_BOUNDED_FIFO : 0;
        }
    } else { errno = ENOTTY; result = -1; }
    pthread_mutex_unlock(&driver_mutex);
    return result;
}
int __wrap_poll(struct pollfd *descriptors, nfds_t count, int timeout)
{
    assert(count == 1 && descriptors[0].events == POLLOUT && timeout == 10);
    uint64_t deadline = now_ns() + (uint64_t)timeout * 1000000ULL;
    do {
        pthread_mutex_lock(&driver_mutex);
        retire_locked();
        bool ready = driver.state == smi_stream_tx_channel && !atomic_load(&stalled_driver)
            && driver.fifo_queued_bytes < driver.fifo_limit_bytes;
        pthread_mutex_unlock(&driver_mutex);
        if (ready) { descriptors[0].revents = POLLOUT; return 1; }
        delay_ms(1);
    } while (now_ns() < deadline);
    descriptors[0].revents = 0;
    return 0;
}
int __wrap_clock_nanosleep(clockid_t clock, int flags,
                           const struct timespec *request, struct timespec *remain)
{
    (void)clock; (void)flags; (void)request; (void)remain;
    assert(!"hardware-paced producer must not have a host frame timer");
    return EINVAL;
}
size_t __wrap_caribou_smi_get_native_batch_samples(caribou_smi_st *smi)
{ (void)smi; return 4 * 32768; }
int __wrap_caribou_smi_set_driver_streaming_state(caribou_smi_st *smi,
                                                smi_stream_state_en state)
{
    (void)smi;
    pthread_mutex_lock(&driver_mutex);
    if (state == smi_stream_tx_channel && start_failure == STREAM_FAIL && ++stream_starts == 2) {
        pthread_mutex_unlock(&driver_mutex);
        return -1;
    }
    if (driver.state == (__u32)state) {
        pthread_mutex_unlock(&driver_mutex);
        return 0;
    }
    driver.state = state;
    driver.fifo_queued_bytes = 0;
    if (state == smi_stream_tx_channel) {
        assert(timing_verified);
        assert(driver_settings.write_setup_time == 1 && driver_settings.write_strobe_time == 4 &&
               driver_settings.write_hold_time == 2);
        ++driver.session_generation;
        driver.completed_periods = driver.underrun_periods = 0;
        driver.accepted_bytes = driver.fifo_dequeued_bytes = 0;
        driver.flags |= SMI_STREAM_TX_STATUS_DMA_RUNNING;
        next_retirement_ns = now_ns() + 32768ULL * 1000000000ULL / rf_rate;
    } else driver.flags &= ~SMI_STREAM_TX_STATUS_DMA_RUNNING;
    pthread_mutex_unlock(&driver_mutex);
    return 0;
}
int __wrap_caribou_smi_write_timed(caribou_smi_st *smi, caribou_smi_channel_en channel,
                                  const caribou_smi_sample_complex_int16 *samples,
                                  size_t count, long timeout_us)
{
    (void)smi; (void)channel;
    assert(timeout_us == 0);
    atomic_fetch_add(&timed_writes, 1);
    pthread_mutex_lock(&driver_mutex);
    retire_locked();
    assert(driver.state == smi_stream_tx_channel);
    assert(timing_verified);
    assert(!current->tx_ctrl.verify_start_profile ||
           atomic_load(&profile_checks) == 15);
    if (!driver.accepted_bytes) {
        /* Zero-audio FM is a constant carrier at the retained oscillator
         * phase; its IQ need not be numerical zero or a fixed phase. */
        assert(samples[0].i & 1);
        assert(samples[0].i || samples[0].q);
        for (size_t index = 1; index < count; ++index)
            assert(samples[index].i == samples[0].i && samples[index].q == samples[0].q);
    }
    size_t credit = (driver.fifo_limit_bytes - driver.fifo_queued_bytes) / 4;
    size_t accepted = count < credit ? count : credit;
    driver.fifo_queued_bytes += accepted * 4;
    driver.accepted_bytes += accepted * 4;
    assert(driver.fifo_queued_bytes <= driver.fifo_limit_bytes);
    pthread_mutex_unlock(&driver_mutex);
    return (int)accepted;
}
int __wrap_caribou_smi_write_samples(caribou_smi_st *smi, caribou_smi_channel_en channel,
                                    const caribou_smi_sample_complex_int16 *samples, int count)
{
    (void)smi; (void)channel; (void)samples; (void)count;
    atomic_fetch_add(&legacy_writes, 1);
    assert(!"updated driver must use single nonblocking writes");
    return -1;
}
int __wrap_cariboulite_radio_set_frequency(cariboulite_radio_state_st *radio,
                                          bool break_first, double *frequency)
{ (void)break_first; radio->actual_rf_frequency = *frequency; return 0; }
int __wrap_cariboulite_radio_set_tx_power(cariboulite_radio_state_st *radio, int power)
{
    radio->tx_power = power;
    float x = power;
    float model = radio->type == cariboulite_channel_s1g
        ? .001502f*x*x*x + .020549f*x*x + .991045f*x + 13.727758f
        : .000710f*x*x*x*x + .010521f*x*x*x + .015169f*x*x + .914333f*x + 12.254084f;
    int result = (int)roundf(model);
    pac_code = 0x60 | (result < 0 ? 0 : result > 31 ? 31 : result);
    return 0;
}
int __wrap_cariboulite_radio_set_tx_samp_cutoff_flt(cariboulite_radio_state_st *radio, float rate)
{
    rf_rate = (unsigned)rate;
    radio->tx_fs = rate == 1000000 ? cariboulite_radio_rx_sample_rate_1000khz
                 : rate == 2000000 ? cariboulite_radio_rx_sample_rate_2000khz
                 : cariboulite_radio_rx_sample_rate_4000khz;
    return 0;
}
int __wrap_cariboulite_radio_get_tx_samp_cutoff_flt(cariboulite_radio_state_st *radio, float *rate)
{ (void)radio; *rate = rf_rate; return 0; }
int __wrap_caribou_fpga_get_sys_ctrl_tx_sample_gap(caribou_fpga_st *fpga, uint8_t *gap)
{
    (void)fpga;
    if (current->radio && current->radio->active) atomic_fetch_or(&profile_checks, 8);
    *gap = 4000000 / rf_rate - 1;
    return 0;
}
int __wrap_at86rf215_read_buffer(at86rf215_st *modem, uint16_t address,
                                 uint8_t *value, uint8_t length)
{
    (void)modem;
    assert(length == 1 && current->radio->active && timing_verified);
    bool low = current->radio->type == cariboulite_channel_s1g;
    if (address == (low ? REG_RF09_PAC : REG_RF24_PAC)) {
        atomic_fetch_or(&profile_checks, 1);
        *value = pac_code;
        if (start_failure == PROFILE_FAIL) *value ^= 1;
    } else if (address == (low ? REG_RF09_TXDFE : REG_RF24_TXDFE)) {
        atomic_fetch_or(&profile_checks, 2);
        *value = (current->radio->tx_fcut << 5) | current->radio->tx_fs;
    } else {
        assert(address == (low ? REG_RF09_STATE : REG_RF24_STATE));
        atomic_fetch_or(&profile_checks, 4);
        *value = modem_state;
    }
    return 0;
}
int __wrap_caribou_fpga_set_io_ctrl_mode(caribou_fpga_st *fpga, int debug,
                                        caribou_fpga_io_ctrl_rfm_en mode)
{ (void)fpga; (void)debug; (void)mode; return 0; }
int __wrap_cariboulite_radio_activate_channel(cariboulite_radio_state_st *radio,
                                             cariboulite_channel_dir_en direction, bool active)
{
    assert(direction == cariboulite_channel_dir_tx);
    radio->active = active;
    /* The real activation path owns SMI state too: it idles the transport,
     * activates the modem, and starts TX DMA before returning. Admission must
     * therefore already be configured at this earliest DMA transition. */
    __wrap_caribou_smi_set_driver_streaming_state(&radio->sys->smi, smi_stream_idle);
    if (active) {
        assert(driver.fifo_limit_bytes == 2 * driver.period_bytes);
        assert(timing_verified);
        pthread_mutex_lock(&current->tx_ctrl.state_mutex);
        assert(!current->tx_ctrl.gate_enabled && !current->tx_ctrl.producer_busy &&
               !current->tx_ctrl.writer_busy);
        pthread_mutex_unlock(&current->tx_ctrl.state_mutex);
        pthread_mutex_lock(&current->txq.m);
        assert(current->txq.count == 1 && session_mod_frames == 1);
        pthread_mutex_unlock(&current->txq.m);
        atomic_fetch_add(&activations, 1);
        atomic_store(&profile_checks, 0);
        modem_state = at86rf215_radio_state_cmd_tx_prep;
        if (start_failure == ACTIVATION_FAIL) return -1;
        if (start_failure == OWNER_ABORT) tx_pipeline_request_abort(current);
        __wrap_caribou_smi_set_driver_streaming_state(&radio->sys->smi, smi_stream_tx_channel);
    } else modem_state = at86rf215_radio_state_cmd_trx_off;
    return 0;
}
nbfm_result_t __real_nbfm_process(nbfm_mod_t *, const audio_f32_t *, size_t, iq16_t *, size_t);
nbfm_result_t __wrap_nbfm_process(nbfm_mod_t *modulator, const audio_f32_t *audio,
                                 size_t frames, iq16_t *output, size_t capacity)
{
    assert(frames == 480);
    bool before_activation = !current->radio->active;
    if (before_activation) {
        pthread_mutex_lock(&current->tx_ctrl.state_mutex);
        assert(!current->tx_ctrl.gate_enabled && !current->tx_ctrl.producer_busy &&
               !current->tx_ctrl.writer_busy);
        pthread_mutex_unlock(&current->tx_ctrl.state_mutex);
        assert(driver.state == smi_stream_idle);
        session_mod_frames = 0;
        first_producer_delayed = false;
        ++primed_frames;
    } else if (!first_producer_delayed && rf_rate == 4000000) {
        first_producer_delayed = true;
        /* Force first refill to happen before the newly resumed producer can
         * finish. Precomputation, rather than lucky scheduling, must cover it. */
        delay_ms(12);
    }
    if (session_mod_frames < 10)
        for (size_t index = 0; index < frames; ++index) assert(audio[index] == 0);
    ++session_mod_frames;
    if (before_activation && start_failure == MODULATOR_FAIL)
        return (nbfm_result_t){.error = -EIO};
    return __real_nbfm_process(modulator, audio, frames, output, capacity);
}
int __real_tone_source_set(audio_source_t *, float, float);
int __wrap_tone_source_set(audio_source_t *source, float hz, float amplitude)
{
    if (hz == 2525) {
        if (session_mod_frames <= 10) assert(session_mod_frames == 10);
        atomic_fetch_add(&opening_cues, 1);
    }
    if (hz == 2475) atomic_fetch_add(&closing_cues, 1);
    return __real_tone_source_set(source, hz, amplitude);
}
typedef struct { audio_source_t base; uint64_t next_capture_ns; } microphone_t;
static audio_source_result_t microphone_read(audio_source_t *source, float *out, size_t count)
{
    microphone_t *mic = (microphone_t *)source;
    atomic_fetch_add(&microphone_reads, 1);
    if (now_ns() < mic->next_capture_ns)
        return (audio_source_result_t){0, AUDIO_SOURCE_AGAIN, 0};
    mic->next_capture_ns += 10000000;
    if (count > 480) count = 480;
    for (size_t index = 0; index < count; ++index) out[index] = .25f;
    return (audio_source_result_t){count, AUDIO_SOURCE_OK, 0};
}
static void microphone_destroy(audio_source_t *source) { free(source); }
static const audio_source_ops_t microphone_ops = {microphone_read, microphone_destroy};
audio_source_t *__wrap_alsa_source_open(const char *device, float gain, audio_format_t format)
{
    (void)device; (void)gain;
    microphone_t *mic = calloc(1, sizeof(*mic));
    assert(mic);
    mic->base.format = format;
    mic->base.ops = &microphone_ops;
    return &mic->base;
}
int __wrap_alsa_source_reset(audio_source_t *source, bool prepare)
{
    assert(source->ops == &microphone_ops);
    ((microphone_t *)source)->next_capture_ns = prepare ? now_ns() + 10000000 : UINT64_MAX;
    return 0;
}
bool __wrap_alsa_source_get_stats(audio_source_t *source, alsa_source_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
    if (!source) return false;
    stats->read_calls = atomic_load(&microphone_reads);
    return true;
}
static void reset_driver(void)
{
    driver = (struct smi_stream_tx_status){
        .abi_version = SMI_STREAM_TX_ABI_VERSION, .struct_size = sizeof(driver),
        .state = smi_stream_idle, .fifo_capacity_bytes = 16 * 4 * 32768,
        .fifo_limit_bytes = 16 * 4 * 32768, .period_bytes = 4 * 32768,
        .ring_bytes = 16 * 32768,
    };
    driver_settings = (struct smi_settings){
        .data_width = SMI_WIDTH_8BIT, .pack_data = true,
        .read_setup_time = 1, .read_strobe_time = 5, .read_hold_time = 1, .read_pace_time = 1,
        .write_setup_time = 1, .write_strobe_time = 5, .write_hold_time = 1, .write_pace_time = 1,
        .dma_enable = true, .dma_passthrough_enable = true,
        .dma_read_thresh = 1, .dma_write_thresh = 62,
        .dma_panic_read_thresh = 16, .dma_panic_write_thresh = 47,
    };
    original_settings = driver_settings;
    settings_reads = settings_writes = session_mod_frames = stream_starts = 0;
    timing_verified = timing_verification_attempted = first_producer_delayed = false;
    fail_restore_read_once = fail_restore_write_once = false;
    atomic_store(&stalled_driver, false);
}
static void assert_quiet_with_health(tx_pipeline_t *pipeline, bool healthy)
{
    tx_pipeline_stats_t before, after;
    tx_pipeline_get_stats(pipeline, &before);
    unsigned reads = atomic_load(&microphone_reads);
    delay_ms(50);
    tx_pipeline_get_stats(pipeline, &after);
    assert(!tx_pipeline_running(pipeline) && tx_pipeline_healthy(pipeline) == healthy);
    assert(before.writer.calls == after.writer.calls && before.txq.puts == after.txq.puts);
    assert(reads == atomic_load(&microphone_reads));
    if (after.audio_buffer_valid)
        assert(after.audio_buffer.alive && !after.audio_buffer.running);
}
static void assert_quiet(tx_pipeline_t *pipeline)
{ assert_quiet_with_health(pipeline, true); }
int main(void)
{
    int pipes[2]; assert(!pipe(pipes));
    sys_st sys = {0};
    sys.smi.filedesc = pipes[1];
    sys.radio_high.sys = &sys;
    sys.radio_high.type = cariboulite_channel_hif;
    tx_pipeline_t pipeline;
    current = &pipeline;
    (void)current;
    tx_params_t parameters = {.freq_hz = 430100000, .tx_power_dbm = -3,
        .rf_fs = 1000000, .tone_mode = true, .tone_hz = 600, .tone_amp = .4f,
        .f_dev_hz = 2500, .out_scale = 4000, .verify_start_profile = true,
        .pacing_mode = TX_PACING_HARDWARE_REQUIRED};
    reset_driver();
    abi_present = false;
    assert(tx_pipeline_init(&pipeline, &sys, &sys.radio_high, &parameters) == -ENOTSUP);
    assert(!pipeline.inited && !pipeline.tx_ctrl.gate_initialized);
    assert(!atomic_load(&activations) && !atomic_load(&timed_writes));
    abi_present = true; invalid_abi = true;
    parameters.pacing_mode = TX_PACING_AUTO;
    assert(tx_pipeline_init(&pipeline, &sys, &sys.radio_high, &parameters) == -EPROTO);
    assert(!atomic_load(&activations));
    invalid_abi = false;
    for (unsigned rate = 1000000; rate <= 4000000; rate *= 4) {
        reset_driver();
        parameters.rf_fs = rate;
        parameters.tone_mode = rate == 1000000;
        parameters.mic_dev = parameters.tone_mode ? NULL : "mock";
        assert(!tx_pipeline_init(&pipeline, &sys, &sys.radio_high, &parameters));
        for (unsigned index = 0; index < 100 && !tx_pipeline_healthy(&pipeline); ++index) delay_ms(1);
        assert(pipeline.tx_ctrl.hardware_paced && pipeline.txq.cap == 4);
        assert_quiet(&pipeline);
        assert(!tx_pipeline_start(&pipeline));
        delay_ms(400);
        tx_pipeline_stats_t stats;
        tx_pipeline_get_stats(&pipeline, &stats);
        assert(stats.hardware_paced && stats.driver_valid && stats.writer.samples > 0);
        assert(stats.driver.fifo_limit_bytes == 2 * stats.driver.period_bytes);
        assert(stats.driver.fifo_queued_bytes <= stats.driver.fifo_limit_bytes);
        assert(stats.producer_worker.scheduling.requested_cpu == 1);
        assert(stats.writer_worker.scheduling.requested_cpu == 2);
        assert(!stats.writer.errors && !stats.producer_worker.last_error);
        if (!parameters.tone_mode) {
            assert(stats.audio_buffer_valid && stats.audio_buffer.output_frames > 0);
            assert(stats.audio_buffer.cue_discard_frames > 0);
            assert(!stats.audio_buffer.underflows && !stats.audio_buffer.overflows);
        }
        assert(settings_writes == 1 && timing_verified && settings_reads >= 2);
        assert(stats.smi_timing_valid && same_settings(&stats.smi_timing, &driver_settings));
        /* Restoration changes only the three write-timing fields, preserving
         * unrelated changes made after the startup snapshot. */
        pthread_mutex_lock(&driver_mutex);
        driver_settings.read_pace_time = 13;
        driver_settings.dma_read_thresh = 11;
        driver_settings.write_pace_time = 7;
        pthread_mutex_unlock(&driver_mutex);
        tx_pipeline_stop(&pipeline);
        assert(settings_writes == 2);
        assert_original_write_timing();
        assert(driver_settings.read_pace_time == 13 && driver_settings.dma_read_thresh == 11);
        assert(driver_settings.write_pace_time == 7);
        assert_quiet(&pipeline);
        assert(atomic_load(&opening_cues) == (rate == 1000000 ? 25u : 75u));
        assert(atomic_load(&closing_cues) == (rate == 1000000 ? 25u : 50u));
        if (parameters.tone_mode) {
            settings_reads = settings_writes = 0;
            timing_verified = timing_verification_attempted = false;
            original_settings = driver_settings;
            assert(!tx_pipeline_start(&pipeline));
            atomic_store(&stalled_driver, true);
            delay_ms(80);
            tx_pipeline_get_stats(&pipeline, &stats);
            assert(stats.txq.count == 4 && stats.txq.max_depth == 4);
            uint64_t puts = stats.txq.puts, calls = stats.writer.calls;
            delay_ms(40);
            tx_pipeline_get_stats(&pipeline, &stats);
            assert(stats.txq.puts == puts && stats.writer.calls == calls);
            uint64_t start = now_ns();
            tx_pipeline_request_abort(&pipeline);
            assert(!tx_pipeline_abort(&pipeline));
            assert(settings_writes == 2);
            assert_original_write_timing();
            assert(now_ns() - start < 250000000);
            assert_quiet(&pipeline);
        }
        tx_pipeline_destroy(&pipeline);
    }
    reset_driver(); refused_limit = true;
    assert(!tx_pipeline_init(&pipeline, &sys, &sys.radio_high, &parameters));
    unsigned writes = atomic_load(&timed_writes);
    unsigned active_before = atomic_load(&activations);
    assert(tx_pipeline_start(&pipeline) < 0);
    assert(atomic_load(&timed_writes) == writes && !sys.radio_high.active);
    assert(atomic_load(&activations) == active_before);
    assert_quiet(&pipeline);
    tx_pipeline_destroy(&pipeline);
    refused_limit = false;
    for (unsigned failure = SNAPSHOT_FAIL; failure <= OWNER_ABORT; ++failure) {
        reset_driver(); start_failure = failure;
        parameters.rf_fs = failure % 3 == 0 ? 1000000 : failure % 3 == 1 ? 2000000 : 4000000;
        parameters.tone_mode = true; parameters.mic_dev = NULL;
        assert(!tx_pipeline_init(&pipeline, &sys, &sys.radio_high, &parameters));
        unsigned writes_before = atomic_load(&timed_writes);
        unsigned cues_before = atomic_load(&opening_cues);
        assert(tx_pipeline_start(&pipeline) < 0);
        assert(atomic_load(&timed_writes) == writes_before);
        assert(atomic_load(&opening_cues) == cues_before);
        assert(driver.state == smi_stream_idle && !sys.radio_high.active);
        assert_original_write_timing();
        assert_quiet_with_health(&pipeline, failure != MODULATOR_FAIL);
        tx_pipeline_destroy(&pipeline);
        assert_original_write_timing();
    }
    start_failure = SETTINGS_OK;
    /* Cache and restore an existing nondefault tuple, rather than assuming
     * the library's initial 1/5/1 profile during cleanup. */
    reset_driver();
    driver_settings.write_setup_time = 2;
    driver_settings.write_strobe_time = 3;
    driver_settings.write_hold_time = 4;
    original_settings = driver_settings;
    parameters.rf_fs = 4000000;
    assert(!tx_pipeline_init(&pipeline, &sys, &sys.radio_high, &parameters));
    assert(!tx_pipeline_start(&pipeline));
    tx_pipeline_destroy(&pipeline);
    assert_original_write_timing();
    assert(settings_writes == 2);
    /* An unsuccessful restoration remains pending so destroy can retry it
     * before the retained object releases ownership of the hardware. */
    for (unsigned failed_write = 0; failed_write < 2; ++failed_write) {
        reset_driver();
        assert(!tx_pipeline_init(&pipeline, &sys, &sys.radio_high, &parameters));
        assert(!tx_pipeline_start(&pipeline));
        if (failed_write) fail_restore_write_once = true;
        else fail_restore_read_once = true;
        tx_pipeline_request_abort(&pipeline);
        assert(tx_pipeline_abort(&pipeline) < 0);
        assert(pipeline.tx_ctrl.smi_write_timing_saved);
        assert(driver_settings.write_strobe_time == 4);
        assert(driver.state == smi_stream_idle && !sys.radio_high.active);
        tx_pipeline_destroy(&pipeline);
        assert_original_write_timing();
        assert(settings_writes == (failed_write ? 3u : 2u));
    }
    assert(primed_frames >= 4 && first_refill_successes >= 4 && timing_verifications >= 4);
    assert(!atomic_load(&legacy_writes));
    close(pipes[0]); close(pipes[1]);
    puts("PASS: strict driver refusal, bounded native retirement/application backpressure, "
         "no host frame timer, nonblocking writes, independent cue capture, cores, "
         "Quindar counts, parked single-frame priming before 8.192 ms DMA refill, "
         "verified 1/4/2 write-only timing, canonical DMA/read preservation, "
         "idle locked restoration on stop/abort/start failure/destroy, "
         "calibrated pre-write profile, admission failure and bounded abort");
}
