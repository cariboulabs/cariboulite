#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "tx_pipeline.h"
#include "mod_worker.h"
#include "tone_source.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static tx_pipeline_t* current;
static atomic_uint mic_reads, source_resets, accepted_writes;
static atomic_bool missing_mic;
static atomic_bool fail_writer;
static atomic_uint start_tone_frames, stop_tone_frames;
static unsigned current_rate;
static uint8_t pac_code, modem_state;
static atomic_uint profile_checks;
enum { PROFILE_OK, PAC_READ_FAIL, DFE_READ_FAIL, STATE_READ_FAIL,
       GAP_READ_FAIL, WRONG_POWER, WRONG_CURRENT, WRONG_RATE, WRONG_CUTOFF,
       DIRECT_MODULATION, WRONG_STATE, WRONG_GAP, POWER_SET_FAIL, OWNER_ABORT, STREAM_FAIL };
static unsigned profile_failure;
static unsigned expected_power_code(const cariboulite_radio_state_st* radio, int power)
{
    float x = power;
    float model = radio->type == cariboulite_channel_s1g
        ? .001502f*x*x*x + .020549f*x*x + .991045f*x + 13.727758f
        : .000710f*x*x*x*x + .010521f*x*x*x + .015169f*x*x + .914333f*x + 12.254084f;
    int result = (int)roundf(model);
    return result < 0 ? 0u : result > 31 ? 31u : (unsigned)result;
}
int __real_tone_source_set(audio_source_t*, float, float);
int __wrap_tone_source_set(audio_source_t* source, float hz, float amplitude)
{
    if (hz == 2525) atomic_fetch_add(&start_tone_frames, 1);
    if (hz == 2475) atomic_fetch_add(&stop_tone_frames, 1);
    return __real_tone_source_set(source, hz, amplitude);
}

static audio_source_result_t mic_read(audio_source_t* source, float* out, size_t count)
{
    (void)source;
    atomic_fetch_add(&mic_reads, 1);
    if (atomic_load(&missing_mic))
        return (audio_source_result_t){0, AUDIO_SOURCE_AGAIN, 0};
    for (size_t i = 0; i < count; ++i) out[i] = 0.25f;
    return (audio_source_result_t){count, AUDIO_SOURCE_OK, 0};
}
static void mic_destroy(audio_source_t* source) { free(source); }
static const audio_source_ops_t mic_ops = {mic_read, mic_destroy};
audio_source_t* __wrap_alsa_source_open(const char* device, float gain, audio_format_t format)
{
    (void)device; (void)gain;
    audio_source_t* source = calloc(1, sizeof(*source));
    assert(source);
    source->format = format; source->ops = &mic_ops;
    return source;
}
int __wrap_alsa_source_reset(audio_source_t* source, bool prepare)
{
    (void)prepare;
    assert(source && source->ops == &mic_ops);
    pthread_mutex_lock(&current->tx_ctrl.state_mutex);
    assert(!current->tx_ctrl.producer_busy);
    pthread_mutex_unlock(&current->tx_ctrl.state_mutex);
    atomic_fetch_add(&source_resets, 1);
    return 0;
}
bool __wrap_alsa_source_get_stats(audio_source_t* source, alsa_source_stats_t* out)
{
    memset(out, 0, sizeof(*out));
    if (!source) return false;
    out->read_calls = atomic_load(&mic_reads);
    return true;
}
int __wrap_cariboulite_radio_set_frequency(cariboulite_radio_state_st* radio,
                                           bool break_first, double* frequency)
{
    (void)break_first; radio->actual_rf_frequency = *frequency; return 0;
}
int __wrap_cariboulite_radio_set_tx_power(cariboulite_radio_state_st* radio, int power)
{
    if (radio->active && profile_failure == POWER_SET_FAIL) return -1;
    radio->tx_power = power;
    pac_code = 0x60 | expected_power_code(radio, power);
    if (radio->active && profile_failure == WRONG_POWER) pac_code ^= 1;
    if (radio->active && profile_failure == WRONG_CURRENT) pac_code &= ~0x20;
    return 0;
}
int __wrap_cariboulite_radio_set_tx_samp_cutoff_flt(cariboulite_radio_state_st* radio, float rate)
{
    current_rate = (unsigned)rate;
    radio->tx_fs = rate == 1000000 ? cariboulite_radio_rx_sample_rate_1000khz
                 : rate == 2000000 ? cariboulite_radio_rx_sample_rate_2000khz
                 : cariboulite_radio_rx_sample_rate_4000khz;
    pac_code = 0x60 | ((18 + radio->tx_power) & 31);
    return 0;
}
int __wrap_cariboulite_radio_get_tx_samp_cutoff_flt(cariboulite_radio_state_st* radio, float* rate)
{
    /* The real getter also replaces the requested power with an estimate. */
    radio->tx_power = 3;
    *rate = current_rate;
    return 0;
}
int __wrap_caribou_fpga_get_sys_ctrl_tx_sample_gap(caribou_fpga_st* fpga, uint8_t* gap)
{
    (void)fpga;
    bool active = current->radio && current->radio->active;
    if (active) atomic_fetch_or(&profile_checks, 8);
    if (active && profile_failure == GAP_READ_FAIL) return -1;
    *gap = 4000000 / current_rate - 1;
    if (active && profile_failure == WRONG_GAP) *gap ^= 1;
    return 0;
}
int __wrap_at86rf215_read_buffer(at86rf215_st* modem, uint16_t address,
                                 uint8_t* value, uint8_t length)
{
    (void)modem;
    assert(length == 1 && current->radio->active);
    bool low = current->radio->type == cariboulite_channel_s1g;
    if (address == (low ? REG_RF09_PAC : REG_RF24_PAC)) {
        atomic_fetch_or(&profile_checks, 1);
        if (profile_failure == PAC_READ_FAIL) return -1;
        *value = pac_code;
    } else if (address == (low ? REG_RF09_TXDFE : REG_RF24_TXDFE)) {
        atomic_fetch_or(&profile_checks, 2);
        if (profile_failure == DFE_READ_FAIL) return -1;
        *value = (current->radio->tx_fcut << 5) | current->radio->tx_fs;
        if (profile_failure == WRONG_RATE) *value ^= 1;
        if (profile_failure == WRONG_CUTOFF) *value ^= 0x20;
        if (profile_failure == DIRECT_MODULATION) *value |= 0x10;
    } else {
        assert(address == (low ? REG_RF09_STATE : REG_RF24_STATE));
        atomic_fetch_or(&profile_checks, 4);
        if (profile_failure == STATE_READ_FAIL) return -1;
        *value = profile_failure == WRONG_STATE ? at86rf215_radio_state_cmd_rx : modem_state;
    }
    return 0;
}
size_t __wrap_caribou_smi_get_native_batch_samples(caribou_smi_st* smi)
{ (void)smi; return 1024; }
int __wrap_ioctl(int descriptor, unsigned long request, ...)
{
    (void)descriptor;
    /* Missing demand ABI uses the unchanged legacy transport. Its lifecycle
     * must never apply or restore the hardware-paced SMI timing profile. */
    assert(request != BCM2835_SMI_IOC_WRITE_SETTINGS);
    errno = ENOTTY;
    return -1;
}
int __wrap_caribou_fpga_set_io_ctrl_mode(caribou_fpga_st* fpga, int debug,
                                       caribou_fpga_io_ctrl_rfm_en mode)
{ (void)fpga; (void)debug; (void)mode; return 0; }
int __wrap_cariboulite_radio_activate_channel(cariboulite_radio_state_st* radio,
                                              cariboulite_channel_dir_en direction,
                                              bool active)
{
    assert(direction == cariboulite_channel_dir_tx);
    radio->active = active;
    if (active) {
        pac_code = 0x60 | ((18 + radio->tx_power) & 31);
        modem_state = at86rf215_radio_state_cmd_tx_prep;
        atomic_store(&profile_checks, 0);
        if (profile_failure == OWNER_ABORT) tx_pipeline_request_abort(current);
    } else modem_state = at86rf215_radio_state_cmd_trx_off;
    return 0;
}
int __wrap_caribou_smi_set_driver_streaming_state(caribou_smi_st* smi, smi_stream_state_en state)
{
    (void)smi;
    if (state == smi_stream_tx_channel) {
        atomic_fetch_or(&profile_checks, 16);
        if (profile_failure == STREAM_FAIL) return -1;
    }
    return 0;
}
int __wrap_caribou_smi_write_samples(caribou_smi_st* smi, caribou_smi_channel_en channel,
                                     const caribou_smi_sample_complex_int16* samples, int count)
{
    (void)smi; (void)channel; (void)samples;
    if (current->tx_ctrl.verify_start_profile) assert(atomic_load(&profile_checks) == 31);
    assert((pac_code & 31) == expected_power_code(current->radio, -3));
    assert(current->radio->tx_power == -3);
    modem_state = at86rf215_radio_state_cmd_tx;
    atomic_fetch_add(&accepted_writes, 1);
    if (atomic_load(&fail_writer)) { errno = EIO; return -1; }
    return count;
}

static void delay_ms(unsigned ms)
{
    struct timespec wait = {.tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000};
    nanosleep(&wait, NULL);
}
static uint64_t voluntary_switches(const char* wanted)
{
    DIR* tasks = opendir("/proc/self/task");
    assert(tasks);
    struct dirent* entry;
    uint64_t result = UINT64_MAX;
    while ((entry = readdir(tasks))) {
        if (entry->d_name[0] == '.') continue;
        char path[512], line[256];
        snprintf(path, sizeof(path), "/proc/self/task/%s/comm", entry->d_name);
        FILE* file = fopen(path, "r");
        if (!file) continue;
        bool found = fgets(line, sizeof(line), file) &&
            !strncmp(line, wanted, strlen(wanted)) && line[strlen(wanted)] == '\n';
        fclose(file);
        if (!found) continue;
        snprintf(path, sizeof(path), "/proc/self/task/%s/status", entry->d_name);
        file = fopen(path, "r"); assert(file);
        while (fgets(line, sizeof(line), file)) {
            unsigned long long value;
            if (sscanf(line, "voluntary_ctxt_switches: %llu", &value) == 1)
                result = value;
        }
        fclose(file); break;
    }
    closedir(tasks);
    assert(result != UINT64_MAX);
    return result;
}
static void assert_parked(tx_pipeline_t* p, bool healthy)
{
    delay_ms(20);
    tx_pipeline_stats_t before, after;
    tx_pipeline_get_stats(p, &before);
    unsigned reads = atomic_load(&mic_reads);
    uint64_t producer_switches = voluntary_switches("tx_producer");
    uint64_t writer_switches = voluntary_switches("tx_writer");
    delay_ms(100);
    tx_pipeline_get_stats(p, &after);
    assert(before.writer.calls == after.writer.calls);
    assert(before.txq.puts == after.txq.puts && before.txq.gets == after.txq.gets);
    assert(reads == atomic_load(&mic_reads));
    assert(producer_switches == voluntary_switches("tx_producer"));
    assert(writer_switches == voluntary_switches("tx_writer"));
    assert(tx_pipeline_healthy(p) == healthy && !tx_pipeline_running(p));
    assert(after.producer_worker.alive && after.writer_worker.alive);
    assert(after.producer_worker.scheduling.valid && after.writer_worker.scheduling.valid);
}

int main(void)
{
    alarm(25);
    int pipe_fds[2]; assert(pipe(pipe_fds) == 0);
    sys_st sys = {0};
    sys.smi.filedesc = pipe_fds[1];
    sys.radio_high.sys = &sys;
    sys.radio_high.type = cariboulite_channel_hif;
    tx_pipeline_t pipeline;
    current = &pipeline;
    tx_params_t params = {.freq_hz = 430125000, .rf_fs = 1000000,
        .mic_dev = "mock", .tone_hz = 600, .tone_amp = .4f,
        .tx_power_dbm = -3, .f_dev_hz = 2500, .out_scale = 4000,
        .verify_start_profile = true, .pacing_mode = TX_PACING_LEGACY};
    assert(tx_pipeline_init(&pipeline, &sys, &sys.radio_high, &params) == 0);
    for (unsigned i = 0; i < 100 && !tx_pipeline_healthy(&pipeline); ++i) delay_ms(1);
    assert_parked(&pipeline, true);
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        assert(tx_pipeline_start(&pipeline) == 0);
        assert(atomic_load(&start_tone_frames) == (repeat + 1) * 25);
        delay_ms(25);
        tx_pipeline_stats_t stats;
        tx_pipeline_get_stats(&pipeline, &stats);
        assert(stats.writer.samples > 0 && stats.writer.written_sequence > 0);
        assert(stats.capture_valid && stats.capture.read_calls > 0);
        assert(!stats.writer.errors && !stats.writer.last_error);
        tx_pipeline_stop(&pipeline);
        assert(atomic_load(&stop_tone_frames) == (repeat + 1) * 25);
        assert_parked(&pipeline, true);
    }
    /* Missing nonblocking capture must not prevent fault cancellation. */
    atomic_store(&missing_mic, true);
    assert(tx_pipeline_start(&pipeline) == 0);
    delay_ms(25);
    uint64_t before = mono_ns();
    tx_pipeline_request_abort(&pipeline);
    assert(tx_pipeline_abort(&pipeline) == 0);
    assert(atomic_load(&start_tone_frames) == 75 && atomic_load(&stop_tone_frames) == 50);
    assert(mono_ns() - before < 250000000);
    assert_parked(&pipeline, true);
    /* An owner abort while profile setup is in progress must survive start.
     * Reject a retained strict pipeline before the cue or any sample write. */
    unsigned aborted_writes = atomic_load(&accepted_writes);
    assert(tx_pipeline_start(&pipeline) < 0);
    assert(atomic_load(&pipeline.tx_ctrl.abort_requested));
    assert(!sys.radio_high.active && modem_state == at86rf215_radio_state_cmd_trx_off);
    assert(atomic_load(&accepted_writes) == aborted_writes);
    assert(atomic_load(&start_tone_frames) == 75 && atomic_load(&stop_tone_frames) == 50);
    assert_parked(&pipeline, true);
    assert(atomic_load(&source_resets) >= 7);
    tx_pipeline_destroy(&pipeline);
    tx_pipeline_destroy(&pipeline);
    assert(!pipeline.inited && !pipeline.tx_ctrl.gate_initialized);
    atomic_store(&missing_mic, false);
    /* Read errors and mismatched hardware reject start before a cue frame or
     * SMI write. Failed starts fully unkey and retain parked healthy workers. */
    params.pacing_mode = TX_PACING_AUTO;
    unsigned initial_cues = atomic_load(&start_tone_frames);
    for (unsigned failure = PAC_READ_FAIL; failure <= STREAM_FAIL; ++failure) {
        profile_failure = failure;
        params.rf_fs = failure % 3 == 0 ? 1000000 : failure % 3 == 1 ? 2000000 : 4000000;
        sys.radio_high.type = failure % 2 ? cariboulite_channel_hif : cariboulite_channel_s1g;
        unsigned writes = atomic_load(&accepted_writes);
        assert(tx_pipeline_init(&pipeline, &sys, &sys.radio_high, &params) == 0);
        assert(sys.radio_high.tx_power == -3);
        for (unsigned i = 0; i < 100 && !tx_pipeline_healthy(&pipeline); ++i) delay_ms(1);
        assert(tx_pipeline_start(&pipeline) < 0);
        assert(!sys.radio_high.active && modem_state == at86rf215_radio_state_cmd_trx_off);
        assert(atomic_load(&start_tone_frames) == initial_cues);
        assert(atomic_load(&accepted_writes) == writes);
        assert_parked(&pipeline, true);
        tx_pipeline_destroy(&pipeline);
    }
    profile_failure = PROFILE_OK;
    params.rf_fs = 1000000;
    sys.radio_high.type = cariboulite_channel_hif;
    atomic_store(&fail_writer, true);
    assert(tx_pipeline_init(&pipeline, &sys, &sys.radio_high, &params) == 0);
    for (unsigned i = 0; i < 100 && !tx_pipeline_healthy(&pipeline); ++i) delay_ms(1);
    assert(tx_pipeline_start(&pipeline) < 0);
    tx_pipeline_stats_t failed;
    tx_pipeline_get_stats(&pipeline, &failed);
    assert(failed.writer.errors == 1 && failed.writer.last_error == -EIO);
    assert(failed.writer_worker.last_error == -EIO && !failed.writer.samples);
    assert_parked(&pipeline, false);
    assert(tx_pipeline_abort(&pipeline) == 0);
    tx_pipeline_destroy(&pipeline);
    close(pipe_fds[0]); close(pipe_fds[1]);
    puts("PASS: retained TX workers park without wakeups/read/write/DSP; repeated "
         "Quindar sessions, calibrated pre-cue profile, readback failures before "
         "any IQ write, preserved owner cancellation, live diagnostics, "
         "reader-owned resets, unchanged legacy SMI timing and bounded abort");
}
