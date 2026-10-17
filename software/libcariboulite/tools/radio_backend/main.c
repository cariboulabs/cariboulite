/* One radio owner, passive status clients and half-duplex radio controls. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "cariboulite_setup.h"
#include "rx_pipeline.h"
#include "tx_pipeline.h"
#include "nbfm_mod.h"
#include "pipeline_runtime.h"
#include "noise_squelch.h"
#include "radio_profile.h"
#include "modem_snapshot.h"
#include "status_publisher.h"
#include "control_server.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Match menu option 3: resolve the file from the radio owner's working directory. */
#define BACKEND_FPGA_FIRMWARE "firmware/top.bin"

typedef struct {
    const char *channel, *playback, *microphone, *socket_path, *control_socket_path;
    double frequency, seconds, status_hz;
    rx_params_t rx;
    tx_params_t tx;
    bool dry_run, start_stopped;
    bool rx_requested, control_fault, fpga_needs_programming;
    uint64_t config_generation, pipeline_generation;
    bool tx_requested, rx_resume, tx_initialized;
    unsigned transition;
    uint64_t tx_pipeline_generation;
    double actual_rx_frequency, actual_tx_frequency;
    int actual_tx_power;
    const char *tx_error;
} options_t;

static cariboulite_channel_en options_channel(const options_t *o)
{
    return !strcmp(o->channel, "RF09") ? cariboulite_channel_s1g : cariboulite_channel_hif;
}

static volatile sig_atomic_t stop_requested;

static void stop_signal(int number)
{
    int saved_errno = errno;
    (void)number;
    stop_requested = 1;
    errno = saved_errno;
}

static void driver_signal(sys_st *sys, void *context, int number, siginfo_t *info)
{
    int saved_errno = errno;
    (void)sys; (void)context; (void)info;
    if (number == SIGINT || number == SIGTERM || number == SIGHUP || number == SIGQUIT)
        stop_requested = 1;
    else
        _Exit(128 + number);
    errno = saved_errno;
}

static int install_stop_signals(void)
{
    struct sigaction action = {0};
    action.sa_handler = stop_signal;
    sigemptyset(&action.sa_mask);
    return sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)
        || sigaction(SIGHUP, &action, NULL) || sigaction(SIGQUIT, &action, NULL);
}

static double clock_seconds(clockid_t clock)
{
    struct timespec now;
    clock_gettime(clock, &now);
    return (double)now.tv_sec + now.tv_nsec * 1e-9;
}

static void usage(FILE *out, const char *program)
{
    fprintf(out,
        "Usage: %s [options]\n"
        "Half-duplex radio owner; RX and TX start stopped.\n"
        "Shared options:\n"
        "  --channel RF24|RF09       shared RX/TX radio; default RF24 (HiF)\n"
        "  --socket PATH             default /tmp/cariboulite-monitor-<UID>/status.sock\n"
        "  --control-socket PATH     default control.sock beside the status socket\n"
        "  --seconds SECONDS         exit after this wall time (default until Ctrl+C)\n"
        "  --status-hz HZ            0.5..20, default 5\n"
        "  --dry-run                 exercise status IPC without hardware or audio\n"
        "  --help                    show this help\n"
        "Receive options:\n"
        "  --rx-start                start reception after initialization\n"
        "  --rx-frequency HZ         default 430125000\n"
        "  --rx-sample-rate HZ       1000000, 2000000 or 4000000 (default 1000000)\n"
        "  --rx-mode nbfm|wbfm       default nbfm; WBFM is mono\n"
        "  --rx-deviation HZ         NBFM audio normalization 1..24000 (default %.0f)\n"
        "  --rx-playback DEVICE      default plughw:Loopback,0,0\n"
        "  --rx-no-noise-squelch      bypass noise squelch\n"
        "  --rx-carrier-squelch       enable carrier squelch (default off)\n"
        "  --rx-noise-open RMS        default %.2f\n"
        "  --rx-noise-close RMS       default %.2f\n"
        "  --rx-gain GAIN            PCM gain, default 8000\n"
        "  --rx-deemphasis-us US      50 or 75, default 50\n"
        "Transmit options:\n"
        "  --tx-frequency HZ         default 430125000\n"
        "  --tx-power DBM            default -12; RF24 -12..14, RF09 -14..14\n"
        "  --tx-mode nbfm            only supported TX mode (default nbfm)\n"
        "  --tx-deviation HZ         nominal NBFM deviation 0..24000 (default %.0f)\n"
        "  --tx-sample-rate HZ       1000000, 2000000 or 4000000 (default 1000000)\n"
        "  --tx-source mic|tone      default mic; tone 600 Hz, amplitude 0.4\n"
        "  --tx-microphone DEVICE    default plughw:Loopback,1,1\n"
        "Legacy unprefixed RX options, --microphone and --start-stopped remain accepted.\n",
        program, (double)NBFM_DEFAULT_DEVIATION_HZ,
        (double)NOISE_SQUELCH_OPEN_RMS, (double)NOISE_SQUELCH_CLOSE_RMS,
        (double)NBFM_DEFAULT_DEVIATION_HZ);
}

static int number(const char *text, double *result, double low, double high)
{
    char *end;
    errno = 0;
    double value = strtod(text, &end);
    if (errno || end == text || *end || !radio_profile_finite(value) || value < low || value > high)
        return -1;
    *result = value;
    return 0;
}

static int parse_options(int argc, char **argv, options_t *o)
{
    *o = (options_t){
        .channel = "RF24", .playback = "plughw:Loopback,0,0",
        .microphone = "plughw:Loopback,1,1", .actual_tx_power = INT_MIN,
        .start_stopped = true,
        .frequency = 430125000, .status_hz = 5, .config_generation = 1,
        .tx = {.freq_hz = 430125000, .tx_power_dbm = -12, .rf_fs = 1000000,
            .tone_hz = 600, .tone_amp = .4f, .out_scale = 4000,
            .f_dev_hz = NBFM_DEFAULT_DEVIATION_HZ, .verify_start_profile = true,
            .pacing_mode = TX_PACING_HARDWARE_REQUIRED},
        .rx = {.freq_hz = 430125000, .fs_rf = 1000000, .fs_audio = 48000,
            .nbfm_deviation_hz = NBFM_DEFAULT_DEVIATION_HZ,
            .deemph_tau_s = 50e-6f, .pcm_gain = 8000,
            .noise_squelch_open_rms = NOISE_SQUELCH_OPEN_RMS,
            .noise_squelch_close_rms = NOISE_SQUELCH_CLOSE_RMS,
            .mode = AUDIO_DEMOD_NBFM},
    };
    enum { CHANNEL = 1000, FREQ, RATE, MODE, PLAYBACK, SOCKET, SECONDS,
        STATUS_HZ, NO_NOISE, CARRIER, NOISE_OPEN, NOISE_CLOSE, GAIN, DEEMPH, DRY_RUN,
        CONTROL_SOCKET, START_STOPPED, MICROPHONE, TX_FREQ, TX_POWER, TX_RATE, TX_SOURCE,
        RX_START, TX_MODE, TX_DEVIATION, RX_DEVIATION };
    static const struct option long_options[] = {
        {"channel", required_argument, NULL, CHANNEL},
        {"rx-start", no_argument, NULL, RX_START},
        {"rx-frequency", required_argument, NULL, FREQ},
        {"rx-sample-rate", required_argument, NULL, RATE},
        {"rx-mode", required_argument, NULL, MODE},
        {"rx-deviation", required_argument, NULL, RX_DEVIATION},
        {"rx-playback", required_argument, NULL, PLAYBACK},
        {"rx-no-noise-squelch", no_argument, NULL, NO_NOISE},
        {"rx-carrier-squelch", no_argument, NULL, CARRIER},
        {"rx-noise-open", required_argument, NULL, NOISE_OPEN},
        {"rx-noise-close", required_argument, NULL, NOISE_CLOSE},
        {"rx-gain", required_argument, NULL, GAIN},
        {"rx-deemphasis-us", required_argument, NULL, DEEMPH},
        {"tx-microphone", required_argument, NULL, MICROPHONE},
        {"tx-frequency", required_argument, NULL, TX_FREQ},
        {"tx-power", required_argument, NULL, TX_POWER},
        {"tx-mode", required_argument, NULL, TX_MODE},
        {"tx-deviation", required_argument, NULL, TX_DEVIATION},
        {"tx-sample-rate", required_argument, NULL, TX_RATE},
        {"tx-source", required_argument, NULL, TX_SOURCE},
        {"socket", required_argument, NULL, SOCKET},
        {"control-socket", required_argument, NULL, CONTROL_SOCKET},
        {"seconds", required_argument, NULL, SECONDS},
        {"status-hz", required_argument, NULL, STATUS_HZ},
        {"dry-run", no_argument, NULL, DRY_RUN},
        {"help", no_argument, NULL, 'h'},
        /* Compatibility names keep existing launch scripts usable. */
        {"frequency", required_argument, NULL, FREQ},
        {"sample-rate", required_argument, NULL, RATE},
        {"mode", required_argument, NULL, MODE},
        {"playback", required_argument, NULL, PLAYBACK},
        {"microphone", required_argument, NULL, MICROPHONE},
        {"start-stopped", no_argument, NULL, START_STOPPED},
        {"rx-start-stopped", no_argument, NULL, START_STOPPED},
        {"no-noise-squelch", no_argument, NULL, NO_NOISE},
        {"carrier-squelch", no_argument, NULL, CARRIER},
        {"noise-open", required_argument, NULL, NOISE_OPEN},
        {"noise-close", required_argument, NULL, NOISE_CLOSE},
        {"gain", required_argument, NULL, GAIN},
        {"deemphasis-us", required_argument, NULL, DEEMPH},
        {0},
    };
    int key;
    double value;
    radio_profile_t profile;
    while ((key = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        switch (key) {
        case CHANNEL:
            if (strcmp(optarg, "RF24") && strcmp(optarg, "RF09")) return -1;
            o->channel = optarg; break;
        case FREQ:
            if (number(optarg, &o->frequency, 1e6, 6e9)) return -1;
            break;
        case RATE:
            if (number(optarg, &value, 1e6, 4e6)
                || value != (unsigned)value
                || radio_profile_rx(o->rx.mode, (unsigned)value, &profile)) return -1;
            o->rx.fs_rf = (float)value; break;
        case MODE:
            if (!strcmp(optarg, "nbfm")) o->rx.mode = AUDIO_DEMOD_NBFM;
            else if (!strcmp(optarg, "wbfm")) o->rx.mode = AUDIO_DEMOD_WBFM;
            else return -1;
            break;
        case RX_DEVIATION:
            if (number(optarg, &value, NBFM_RX_MIN_DEVIATION_HZ,
                       NBFM_RX_MAX_DEVIATION_HZ)) return -1;
            o->rx.nbfm_deviation_hz = (float)value;
            break;
        case PLAYBACK:
            if (!*optarg || strlen(optarg) > 255) return -1;
            o->playback = optarg; break;
        case MICROPHONE:
            if (!*optarg || strlen(optarg) > 255) return -1;
            o->microphone = optarg; break;
        case TX_FREQ:
            if (number(optarg, &o->tx.freq_hz, 1e6, 6e9)) return -1;
            break;
        case TX_POWER:
            if (number(optarg, &value, -14, 14) || floor(value) != value) return -1;
            o->tx.tx_power_dbm = (int)value; break;
        case TX_MODE:
            if (strcmp(optarg, "nbfm")) return -1;
            break;
        case TX_DEVIATION:
            if (number(optarg, &value, 0, 24000)) return -1;
            o->tx.f_dev_hz = (float)value;
            if (value != 0 && o->tx.f_dev_hz == 0) return -1;
            break;
        case TX_RATE:
            if (number(optarg, &value, 1e6, 4e6)
                || value != (unsigned)value
                || radio_profile_tx((unsigned)value, &profile)) return -1;
            o->tx.rf_fs = (unsigned)value; break;
        case TX_SOURCE:
            if (strcmp(optarg, "mic") && strcmp(optarg, "tone")) return -1;
            o->tx.tone_mode = !strcmp(optarg, "tone"); break;
        case SOCKET: o->socket_path = optarg; break;
        case CONTROL_SOCKET: o->control_socket_path = optarg; break;
        case RX_START: o->start_stopped = false; break;
        case START_STOPPED: o->start_stopped = true; break;
        case SECONDS:
            if (number(optarg, &o->seconds, 0.1, 86400)) return -1;
            break;
        case STATUS_HZ:
            if (number(optarg, &o->status_hz, 0.5, 20)) return -1;
            break;
        case NO_NOISE: o->rx.noise_squelch_disabled = true; break;
        case CARRIER: o->rx.carrier_squelch_enabled = true; break;
        case NOISE_OPEN: case NOISE_CLOSE:
            if (number(optarg, &value, 0.001, 8)) return -1;
            if (key == NOISE_OPEN) o->rx.noise_squelch_open_rms = (float)value;
            else o->rx.noise_squelch_close_rms = (float)value;
            break;
        case GAIN:
            if (number(optarg, &value, 0, 1000000)) return -1;
            o->rx.pcm_gain = (float)value; break;
        case DEEMPH:
            if (number(optarg, &value, 50, 75) || (value != 50 && value != 75)) return -1;
            o->rx.deemph_tau_s = (float)(value * 1e-6); break;
        case DRY_RUN: o->dry_run = true; break;
        case 'h': usage(stdout, argv[0]); return 1;
        default: return -1;
        }
    }
    uint32_t levels;
    if (optind != argc || !noise_squelch_pack_levels(o->rx.noise_squelch_open_rms,
        o->rx.noise_squelch_close_rms, &levels)) return -1;
    noise_squelch_unpack_levels(levels, &o->rx.noise_squelch_open_rms,
                               &o->rx.noise_squelch_close_rms);
    if (!radio_profile_frequency_valid(options_channel(o), system_type_cariboulite_full,
                                       o->frequency) ||
        !radio_profile_frequency_valid(options_channel(o), system_type_cariboulite_full,
                                       o->tx.freq_hz) ||
        radio_profile_rx(o->rx.mode, (unsigned)o->rx.fs_rf, &profile) ||
        radio_profile_tx(o->tx.rf_fs, &profile))
        return -1;
    o->rx.freq_hz = o->frequency;
    o->rx.pcm_dev = o->playback;
    o->tx.mic_dev = o->tx.tone_mode ? NULL : o->microphone;
    if (!radio_profile_tx_power_valid(options_channel(o), o->tx.tx_power_dbm)) return -1;
    if (o->dry_run) {
        o->actual_rx_frequency = o->frequency;
        o->actual_tx_frequency = o->tx.freq_hz;
        o->actual_tx_power = o->tx.tx_power_dbm;
    }
    return 0;
}

typedef struct {
    options_t *options;
    sys_st *sys;
    cariboulite_radio_state_st *radio;
    rx_pipeline_t *pipeline;
    tx_pipeline_t *tx_pipeline;
    pthread_t lifecycle_thread;
    bool lifecycle_active;
    atomic_bool lifecycle_done;
    atomic_uint lifecycle_phase;
    atomic_bool lifecycle_no_resume;
    atomic_bool lifecycle_tx_ready;
    unsigned lifecycle_action;
    options_t lifecycle_options;
    int lifecycle_result;
    bool lifecycle_resume_failed;
    bool lifecycle_rx_resumed;
    bool tx_configuration_dirty;
    bool stop_after_start;
    bool shutdown;
    uint64_t tx_controller_generation;
    double lifecycle_started;
    uint64_t previous_tx_samples;
    double last_tx_progress;
    double tx_state_deadline, tx_state_next_check;
    control_server_t *server;
} backend_control_t;

static const char *backend_control_state(const options_t *o)
{
    if (o->transition == 1) return "TX_STARTING";
    if (o->transition == 2) return "TX_STOPPING";
    if (o->transition == 3) return "RX_STARTING";
    if (o->transition == 4) return "FPGA_PROGRAMMING";
    if (o->transition == 5) return "FPGA_RESETTING";
    return o->control_fault ? "FAULT" : o->fpga_needs_programming ? "FPGA_NEEDS_PROGRAM"
        : o->tx_requested ? "TX" : o->rx_requested ? "RX" : "IDLE";
}

static bool backend_lifecycle_fpga(unsigned action)
{
    return action >= 4 && action <= 6;
}

/* Match the driver's double-precision limits before any register is touched.
 * Its full-board upper endpoint is exclusive; the general availability API
 * uses float and includes that endpoint, so cannot validate this command. */
static bool control_frequency_valid(const options_t *o, const sys_st *sys, double hz)
{
    return radio_profile_frequency_valid(options_channel(o),
        o->dry_run ? system_type_cariboulite_full : sys->board_info.numeric_product_id, hz);
}

static int backend_read_modem_state(backend_control_t *c, uint8_t *state)
{
    uint16_t reg = !strcmp(c->options->channel, "RF09") ? REG_RF09_STATE : REG_RF24_STATE;
    return at86rf215_read_buffer(&c->sys->modem, reg, state, 1);
}

static int backend_modem_state(backend_control_t *c, unsigned wanted)
{
    uint8_t state = 0;
    int rc = backend_read_modem_state(c, &state);
    return rc == 0 && (state & 7) == wanted ? 0 : -1;
}

/* Fully release queues/audio on stop so a later retune cannot play old PCM.
 * The shared pipeline is unchanged; these extra checked hardware operations
 * belong solely to this backend's command boundary. */
static int backend_force_idle(backend_control_t *c)
{
    if (c->options->dry_run) return 0;
    rx_pipeline_stop(c->pipeline);
    rx_pipeline_destroy(c->pipeline);
    HW_LOCK();
    int radio_rc = cariboulite_radio_activate_channel(c->radio, cariboulite_channel_dir_rx, false);
    int smi_rc = caribou_smi_set_driver_streaming_state(&c->sys->smi, smi_stream_idle);
    int fpga_rc = caribou_fpga_set_io_ctrl_mode(&c->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
    int state_rc = backend_modem_state(c, cariboulite_radio_state_cmd_trx_off);
    HW_UNLOCK();
    return radio_rc || smi_rc || fpga_rc || state_rc ? -1 : 0;
}

static int backend_tune(backend_control_t *c, double hz)
{
    if (c->options->dry_run) { c->options->actual_rx_frequency = hz; return 0; }
    HW_LOCK();
    int rc = cariboulite_radio_set_frequency(c->radio, true, &hz);
    HW_UNLOCK();
    /* Tuning must leave confirmed idle hardware even if the tuner failed. */
    int idle_rc = backend_force_idle(c);
    if (!rc && !idle_rc) c->options->actual_rx_frequency = c->radio->actual_rf_frequency;
    return rc || idle_rc || !isfinite(hz) || hz <= 0 ? -1 : 0;
}

static int backend_start(backend_control_t *c)
{
    options_t *o = c->options;
    if (o->dry_run) { ++o->pipeline_generation; o->actual_rx_frequency = o->frequency; return 0; }
    if (!control_frequency_valid(o, c->sys, o->frequency)) return -1;
    if (backend_force_idle(c) != 0) return -1;
    if (rx_pipeline_init(c->pipeline, c->sys, c->radio, &o->rx) != 0) goto fail;
    ++o->pipeline_generation;
    /* init's internal tune and start's SMI operation have unchecked results.
     * Verify them here without changing the accepted shared implementation. */
    if (rx_pipeline_set_freq(c->pipeline, o->frequency) != 0
        || rx_pipeline_start(c->pipeline) != 0) goto fail;
    HW_LOCK();
    int state_rc = backend_modem_state(c, cariboulite_radio_state_cmd_rx);
    int smi_rc = caribou_smi_set_driver_streaming_state(&c->sys->smi,
        !strcmp(o->channel, "RF09") ? smi_stream_rx_channel_0 : smi_stream_rx_channel_1);
    HW_UNLOCK();
    if (state_rc || smi_rc) goto fail;
    double deadline = clock_seconds(CLOCK_MONOTONIC) + 0.5;
    do {
        rx_pipeline_stats_t stats;
        rx_pipeline_get_stats(c->pipeline, &stats);
        if (stats.reader.errors || stats.reader_worker.last_error
            || stats.demod_worker.last_error || stats.audio_worker.last_error) goto fail;
        if (stats.reader.samples && stats.reader_worker.alive
            && stats.demod_worker.alive && stats.audio_worker.alive) {
            o->actual_rx_frequency = c->radio->actual_rf_frequency;
            return 0;
        }
        if (stop_requested) goto fail;
        struct timespec delay = {.tv_nsec = 10000000};
        nanosleep(&delay, NULL);
    } while (clock_seconds(CLOCK_MONOTONIC) < deadline);
fail:
    backend_force_idle(c);
    return -1;
}

/* These jobs serialize all lifecycle/hardware mutations. The owner keeps
 * serving IPC and cached status, but never inspects changing pipeline objects.
 * Job results become visible only after the release/acquire completion flag. */
static int backend_tx_idle(backend_control_t *c, bool abort)
{
    if (c->options->dry_run) return 0;
    int pipeline_rc = 0;
    if (abort) pipeline_rc = tx_pipeline_abort(c->tx_pipeline);
    else {
        tx_pipeline_stop(c->tx_pipeline);
        tx_pipeline_stats_t stats;
        tx_pipeline_get_stats(c->tx_pipeline, &stats);
        if (stats.writer.last_error || stats.producer_worker.last_error) pipeline_rc = -1;
    }
    HW_LOCK();
    int radio_rc = cariboulite_radio_activate_channel(c->radio, cariboulite_channel_dir_tx, false);
    int smi_rc = caribou_smi_set_driver_streaming_state(&c->sys->smi, smi_stream_idle);
    int fpga_rc = caribou_fpga_set_io_ctrl_mode(&c->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
    int state_rc = backend_modem_state(c, cariboulite_radio_state_cmd_trx_off);
    HW_UNLOCK();
    return pipeline_rc || radio_rc || smi_rc || fpga_rc || state_rc ? -1 : 0;
}

static int backend_fpga_firmware_ready(void)
{
    int fd = open(BACKEND_FPGA_FIRMWARE, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return -1;
    struct stat info;
    bool ready = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
    close(fd);
    return ready ? 0 : -1;
}

/* Legacy version/mode getters discard SPI errors. Check communication and the
 * final low-power mode directly before reporting programming completion.
 * Read opcodes match caribou_fpga.c: system/version, manufacturer, module
 * versions (system, IO, SMI), then IO mode. Version numbers alone do not
 * distinguish this bitstream from older firmware. Caller holds HW_LOCK. */
static int backend_fpga_readback(caribou_fpga_st *fpga)
{
    static const uint8_t opcodes[] = {0x01, 0x02, 0x00, 0x20, 0x40, 0x21};
    uint8_t values[sizeof(opcodes)];
    for (size_t i = 0; i < sizeof(opcodes); ++i) {
        uint8_t tx[2] = {opcodes[i], 0}, rx[2] = {0};
        if (io_utils_spi_transmit(fpga->io_spi, fpga->io_spi_handle,
                tx, rx, sizeof(tx), io_utils_spi_read_write) != 0) return -1;
        values[i] = rx[1];
    }
    if (values[1] != CARIBOU_SDR_MANU_CODE || values[5] != 0) return -1;
    fpga->versions = (caribou_fpga_versions_st){.sys_ver = values[0],
        .sys_manu_id = values[1], .sys_ctrl_mod_ver = values[2],
        .io_ctrl_mod_ver = values[3], .smi_ctrl_mod_ver = values[4]};
    return 0;
}

/* Caller holds HW_LOCK. Programming can restore a shared SPI bus that was
 * unavailable after CRESET; give the final TRXOFF command a bounded settling
 * window, and log the actual component instead of only the IDLE stage. */
static int backend_fpga_modem_idle(backend_control_t *c, bool settle)
{
    int radio_rc = cariboulite_radio_activate_channel(c->radio, cariboulite_channel_dir_rx, false);
    int smi_rc = caribou_smi_set_driver_streaming_state(&c->sys->smi, smi_stream_idle);
    uint8_t state = 0;
    int state_rc;
    double deadline = clock_seconds(CLOCK_MONOTONIC) + (settle ? .05 : 0);
    do {
        state_rc = backend_read_modem_state(c, &state);
        if (!radio_rc && !smi_rc && !state_rc
            && (state & 7) == cariboulite_radio_state_cmd_trx_off) return 0;
        if (radio_rc || smi_rc || clock_seconds(CLOCK_MONOTONIC) >= deadline) break;
        struct timespec delay = {.tv_nsec = 1000000};
        nanosleep(&delay, NULL);
    } while (true);
    fprintf(stderr, "[radio_backend] FPGA modem/SMI idle check failed: "
        "radio_rc=%d smi_rc=%d state_rc=%d STATE=0x%02X\n",
        radio_rc, smi_rc, state_rc, state);
    return -1;
}

/* Join workers before acquiring HW_LOCK: parked TX workers can still own
 * allocated queues, and destroying them while holding their lock can deadlock.
 * No FPGA commands here, so recovery also works after configuration is lost. */
static int backend_fpga_quiesce(backend_control_t *c)
{
    rx_pipeline_stop(c->pipeline);
    rx_pipeline_destroy(c->pipeline);
    tx_pipeline_destroy(c->tx_pipeline);
    c->options->tx_initialized = false;
    HW_LOCK();
    int rc;
    if (c->options->fpga_needs_programming) {
        /* Every path setting this flag confirmed idle before CRESET. No
         * radio start is accepted afterward. An unconfigured FPGA may use
         * the shared SPI pins for boot/configuration, so modem reads cannot
         * be a prerequisite for restoring it. Check them after programming. */
        rc = caribou_smi_set_driver_streaming_state(&c->sys->smi, smi_stream_idle);
        if (rc) fprintf(stderr, "[radio_backend] FPGA recovery SMI idle failed: rc=%d\n", rc);
    } else rc = backend_fpga_modem_idle(c, false);
    HW_UNLOCK();
    return rc ? -1 : 0;
}

static int backend_fpga_program(backend_control_t *c)
{
    if (c->options->dry_run) {
        c->options->tx_initialized = c->options->fpga_needs_programming = false;
        return 0;
    }
    /* Repeat the preflight in the job before touching hardware. */
    if (backend_fpga_firmware_ready() != 0) {
        fprintf(stderr, "[radio_backend] FPGA programming failed: FIRMWARE_FILE (%s)\n",
            BACKEND_FPGA_FIRMWARE);
        return -1;
    }
    const char *failure = "IDLE";
    if (backend_fpga_quiesce(c) != 0) goto idle_fail;
    HW_LOCK();
    /* Preserve app_fpga_programming/menu 3, including its additional soft
     * reset after configure_fpga's own programming/reset/status operations. */
    failure = "HARD_RESET";
    c->options->fpga_needs_programming = true;
    if (caribou_fpga_hard_reset(&c->sys->fpga) != 0) goto fail;
    c->sys->force_fpga_reprogramming = true;
    failure = "CONFIGURE";
    if (cariboulite_configure_fpga(c->sys, cariboulite_firmware_source_file,
            BACKEND_FPGA_FIRMWARE) < 0) goto fail;
    failure = "SOFT_RESET";
    if (caribou_fpga_soft_reset(&c->sys->fpga) != 0) goto fail;
    io_utils_usleep(100000);
    failure = "VERSIONS";
    if (caribou_fpga_get_versions(&c->sys->fpga, NULL) != 0) goto fail;
    failure = "LOW_POWER";
    if (caribou_fpga_set_io_ctrl_mode(&c->sys->fpga, 0,
            caribou_fpga_io_ctrl_rfm_low_power) != 0) goto fail;
    failure = "READBACK";
    if (backend_fpga_readback(&c->sys->fpga) != 0) goto fail;
    failure = "FINAL_IDLE";
    if (backend_fpga_modem_idle(c, true) != 0) goto fail;
    c->options->fpga_needs_programming = false;
    HW_UNLOCK();
    fprintf(stderr, "[radio_backend] FPGA programmed from %s; RX/TX remain stopped.\n",
        BACKEND_FPGA_FIRMWARE);
    return 0;
fail:
    HW_UNLOCK();
idle_fail:
    fprintf(stderr, "[radio_backend] FPGA programming failed: %s\n", failure);
    return -1;
}

static int backend_fpga_reset(backend_control_t *c, bool hard)
{
    if (c->options->dry_run) {
        c->options->tx_initialized = false;
        if (hard) c->options->fpga_needs_programming = true;
        return 0;
    }
    const char *failure = "IDLE";
    if (backend_fpga_quiesce(c) != 0) goto idle_fail;
    HW_LOCK();
    if (hard) {
        /* CRESET discards the running configuration; any boot image it reloads
         * is not proof that it matches this software. Only program clears this. */
        c->options->fpga_needs_programming = true;
        c->sys->fpga.versions = (caribou_fpga_versions_st){0};
        c->sys->fpga.status = caribou_fpga_status_not_programmed;
        failure = "HARD_RESET";
        if (caribou_fpga_hard_reset(&c->sys->fpga) != 0) goto fail;
    } else {
        failure = "SOFT_RESET";
        if (caribou_fpga_soft_reset(&c->sys->fpga) != 0) goto fail;
        failure = "LOW_POWER";
        if (caribou_fpga_set_io_ctrl_mode(&c->sys->fpga, 0,
                caribou_fpga_io_ctrl_rfm_low_power) != 0) goto fail;
        failure = "READBACK";
        if (backend_fpga_readback(&c->sys->fpga) != 0) goto fail;
    }
    HW_UNLOCK();
    fprintf(stderr, "[radio_backend] FPGA %s reset completed; %s\n", hard ? "hard" : "soft",
        hard ? "run fpga program before RX/TX." : "RX/TX remain stopped.");
    return 0;
fail:
    HW_UNLOCK();
idle_fail:
    fprintf(stderr, "[radio_backend] FPGA %s reset failed: %s\n", hard ? "hard" : "soft", failure);
    return -1;
}

/* Capture the link before TRXOFF clears its synchronization status. These
 * reads run only after a failed start, never in the periodic monitor scan.
 * Reading IQIFC2 clears SYNC; this diagnostic deliberately consumes it once. */
static void backend_tx_link_diagnostic(backend_control_t *c)
{
    uint8_t iq[3] = {0}, gap = 0, debug = 0, fifo_raw = 0, pins_raw = 0;
    caribou_fpga_smi_fifo_status_st fifo = {0};
    caribou_fpga_rf_pin_st pins = {0};
    caribou_fpga_io_ctrl_rfm_en mode = caribou_fpga_io_ctrl_rfm_low_power;
    HW_LOCK();
    int iq_rc = at86rf215_read_buffer(&c->sys->modem, REG_RF_IQIFC0, iq, sizeof(iq));
    int fifo_rc = caribou_fpga_get_smi_ctrl_fifo_status(&c->sys->fpga, &fifo);
    int mode_rc = caribou_fpga_get_io_ctrl_mode(&c->sys->fpga, &debug, &mode);
    int gap_rc = caribou_fpga_get_sys_ctrl_tx_sample_gap(&c->sys->fpga, &gap);
    int pins_rc = caribou_fpga_get_io_ctrl_rf_state(&c->sys->fpga, &pins);
    HW_UNLOCK();
    memcpy(&fifo_raw, &fifo, sizeof(fifo_raw));
    memcpy(&pins_raw, &pins, sizeof(pins_raw));
    fprintf(stderr, "[radio_backend] TX link: IQIFC0=0x%02X EEC=%u SF=%u EXTLB=%u "
        "IQIFC1=0x%02X CHPM=%u FAILSF=%u IQIFC2=0x%02X SYNC=%u rc=%d\n",
        iq[0], iq[0] & 1u, (iq[0] >> 6) & 1u, iq[0] >> 7,
        iq[1], (iq[1] >> 4) & 7u, iq[1] >> 7, iq[2], iq[2] >> 7, iq_rc);
    fprintf(stderr, "[radio_backend] TX route: FPGA_FIFO=0x%02X DIR=%u(TX=0) "
        "CH=%u TX_FULL=%u rc=%d MODE=%u DEBUG=%u rc=%d GAP=%u rc=%d RF_PINS=0x%02X rc=%d\n",
        fifo_raw, (unsigned)fifo.smi_direction, (unsigned)fifo.smi_channel,
        (unsigned)fifo.tx_fifo_full, fifo_rc, (unsigned)mode, debug, mode_rc,
        gap, gap_rc, pins_raw, pins_rc);
}

static int backend_tx_start(backend_control_t *c, bool recreate, atomic_bool *ready)
{
    options_t *o = c->options;
    (void)recreate; /* Every TX start now follows menu 14's FPGA clean start. */
    if (o->dry_run) {
        ++o->tx_pipeline_generation;
        o->tx_initialized = true;
        o->actual_tx_frequency = o->tx.freq_hz;
        o->actual_tx_power = o->tx.tx_power_dbm;
        return 0;
    }
    const char *failure = "TX_IDLE";
    int failure_rc = backend_force_idle(c);
    uint8_t pac = 0, state = 0xff;
    tx_pipeline_stats_t stats = {0};
    bool start_attempted = false;
    if (failure_rc != 0) goto fail;
    /* Recheck loopback after RX is released, before changing TX hardware. */
    uint8_t iq = 0;
    HW_LOCK();
    int loop_rc = at86rf215_read_buffer(&c->sys->modem, REG_RF_IQIFC0, &iq, 1);
    HW_UNLOCK();
    failure = "TX_LOOPBACK";
    failure_rc = loop_rc;
    if (loop_rc || (iq & 0x80)) goto fail;
    /* Menu 14 joins both pipelines before resetting shared FPGA FIFOs and
     * serializer state. A parked allocation must also be joined: no worker
     * may access the hardware across the reset. RX was released above. */
    if (c->tx_pipeline->inited) {
        tx_pipeline_destroy(c->tx_pipeline);
    }
    o->tx_initialized = false;
    failure = "TX_FPGA_RESET";
    HW_LOCK();
    failure_rc = caribou_fpga_soft_reset(&c->sys->fpga);
    HW_UNLOCK();
    if (failure_rc != 0) goto fail;
    /* Initialization restores the selected rate/gap after reset defaults. */
    if (!c->tx_pipeline->inited) {
        failure = "TX_INIT";
        failure_rc = tx_pipeline_init(c->tx_pipeline, c->sys, c->radio, &o->tx);
        if (failure_rc != 0) goto fail;
        ++o->tx_pipeline_generation;
    }
    o->tx_initialized = true;
    atomic_store_explicit(ready, true, memory_order_release);
    tx_pipeline_stats_t previous;
    tx_pipeline_get_stats(c->tx_pipeline, &previous);
    failure = "TX_TUNE";
    failure_rc = tx_pipeline_set_freq_power(c->tx_pipeline, o->tx.freq_hz, o->tx.tx_power_dbm);
    if (failure_rc != 0) goto fail;
    /* The power API suppresses low-level SPI errors. Confirm PAC itself and
     * compare its code with the same board-specific mapping as the driver. */
    int expected = radio_profile_tx_power_code(options_channel(o), o->tx.tx_power_dbm);
    if (expected < 0) { failure = "TX_POWER"; failure_rc = -1; goto fail; }
    HW_LOCK();
    int power_rc = at86rf215_read_buffer(&c->sys->modem,
        !strcmp(o->channel, "RF09") ? REG_RF09_PAC : REG_RF24_PAC, &pac, 1);
    HW_UNLOCK();
    failure = "TX_POWER";
    failure_rc = power_rc;
    if (power_rc || (pac & 31) != expected) goto fail;
    float x = pac & 31;
    o->actual_tx_power = (int)(!strcmp(o->channel, "RF09")
        ? -.000546f*x*x*x + .014352f*x*x + .902754f*x - 13.954753f
        : .000031f*x*x*x*x - .002344f*x*x*x + .040478f*x*x + .712209f*x - 11.168502f);
    o->actual_tx_frequency = c->radio->actual_rf_frequency;
    failure = "TX_FREQUENCY";
    if (!radio_profile_finite(o->actual_tx_frequency) || o->actual_tx_frequency <= 0) goto fail;
    failure = "TX_CANCELLED";
    if (atomic_load_explicit(&c->tx_pipeline->tx_ctrl.abort_requested, memory_order_acquire)) goto fail;
    failure = "TX_PIPELINE_START";
    start_attempted = true;
    failure_rc = tx_pipeline_start(c->tx_pipeline);
    if (failure_rc != 0) goto fail;
    HW_LOCK();
    power_rc = at86rf215_read_buffer(&c->sys->modem,
        !strcmp(o->channel, "RF09") ? REG_RF09_PAC : REG_RF24_PAC, &pac, 1);
    int smi_rc = caribou_smi_set_driver_streaming_state(&c->sys->smi, smi_stream_tx_channel);
    HW_UNLOCK();
    failure = "TX_POWER";
    failure_rc = power_rc;
    if (power_rc || (pac & 31) != expected) goto fail;
    failure = "TX_STREAM";
    failure_rc = smi_rc;
    if (smi_rc) goto fail;
    double deadline = clock_seconds(CLOCK_MONOTONIC) + .5;
    /* With embedded IQ control, TXPREP can be observed between frames and
     * while the opening cue is still buffered downstream. Wait for actual TX
     * and writer progress together; accepted IQ writes alone do not prove TX. */
    do {
        tx_pipeline_get_stats(c->tx_pipeline, &stats);
        failure = "TX_CANCELLED";
        if (atomic_load_explicit(&c->tx_pipeline->tx_ctrl.abort_requested, memory_order_acquire)) goto fail;
        failure = "TX_WORKER";
        if (stats.writer.errors || stats.producer_worker.last_error
            || stats.writer_worker.last_error) goto fail;
        HW_LOCK();
        int state_rc = backend_read_modem_state(c, &state);
        HW_UNLOCK();
        failure = "TX_STATE_READ";
        failure_rc = state_rc;
        if (state_rc) goto fail;
        unsigned current = state & 7;
        failure = "TX_STATE";
        if (current != cariboulite_radio_state_cmd_tx
            && current != cariboulite_radio_state_cmd_tx_prep
            && current != cariboulite_radio_state_transition) goto fail;
        if (current == cariboulite_radio_state_cmd_tx
            && stats.writer.samples > previous.writer.samples && stats.producer_worker.alive
            && stats.writer_worker.alive && tx_pipeline_healthy(c->tx_pipeline)) return 0;
        failure = current == cariboulite_radio_state_cmd_tx ? "TX_PROGRESS_TIMEOUT" : "TX_STATE_TIMEOUT";
        struct timespec delay = {.tv_nsec = 5000000};
        nanosleep(&delay, NULL);
    } while (clock_seconds(CLOCK_MONOTONIC) < deadline);
fail:
    o->tx_error = failure;
    fprintf(stderr, "[radio_backend] TX start failed: %s rc=%d STATE=0x%02X PAC=0x%02X "
        "samples=%" PRIu64 " writer_error=%d producer_error=%d\n",
        failure, failure_rc, state, pac, stats.writer.samples,
        stats.writer.last_error, stats.producer_worker.last_error);
    if (start_attempted) backend_tx_link_diagnostic(c);
    backend_tx_idle(c, true);
    return -1;
}

static void *backend_lifecycle_worker(void *opaque)
{
    backend_control_t *owner = opaque;
    backend_control_t worker = {.options = &owner->lifecycle_options,
        .sys = owner->sys, .radio = owner->radio, .pipeline = owner->pipeline,
        .tx_pipeline = owner->tx_pipeline};
    int result = 0;
    if (worker.options->dry_run) {
        struct timespec delay = {.tv_nsec = 100000000};
        nanosleep(&delay, NULL);
    }
    if (owner->lifecycle_action == 1)
        result = backend_tx_start(&worker, owner->tx_configuration_dirty, &owner->lifecycle_tx_ready);
    else if (owner->lifecycle_action == 4)
        result = backend_fpga_program(&worker);
    else if (owner->lifecycle_action == 5 || owner->lifecycle_action == 6)
        result = backend_fpga_reset(&worker, owner->lifecycle_action == 6);
    else
        result = backend_tx_idle(&worker, owner->lifecycle_action == 3);
    /* Restore accepted RX settings after a normal stop.
     * A fault abort releases RF and leaves explicit recovery to the user. */
    if (owner->lifecycle_action == 2
        && worker.options->rx_resume
        && !atomic_load_explicit(&owner->lifecycle_no_resume, memory_order_acquire)
        && result == 0) {
        atomic_store_explicit(&owner->lifecycle_phase, 3, memory_order_release);
        int resume_rc = backend_start(&worker);
        owner->lifecycle_rx_resumed = resume_rc == 0;
        owner->lifecycle_resume_failed = resume_rc != 0;
        if (resume_rc) result = -1;
    }
    owner->lifecycle_result = result;
    atomic_store_explicit(&owner->lifecycle_done, true, memory_order_release);
    return NULL;
}

static int backend_launch_lifecycle(backend_control_t *c, unsigned action)
{
    if (c->lifecycle_active || !c->tx_pipeline) return -1;
    c->lifecycle_options = *c->options;
    c->lifecycle_action = action;
    c->lifecycle_resume_failed = false;
    c->lifecycle_rx_resumed = false;
    c->lifecycle_result = -1;
    c->lifecycle_started = clock_seconds(CLOCK_MONOTONIC);
    atomic_init(&c->lifecycle_done, false);
    unsigned phase = action == 4 ? 4 : action == 5 || action == 6 ? 5 : action == 1 ? 1 : 2;
    atomic_init(&c->lifecycle_phase, phase);
    atomic_init(&c->lifecycle_no_resume, c->shutdown);
    atomic_init(&c->lifecycle_tx_ready, (action == 2 || action == 3) && !c->options->dry_run);
    int rc = pthread_create(&c->lifecycle_thread, NULL, backend_lifecycle_worker, c);
    if (rc) return -1;
    c->lifecycle_active = true;
    c->tx_state_deadline = c->tx_state_next_check = 0;
    c->options->transition = phase;
    return 0;
}

/* Embedded IQ control can briefly return to TXPREP between buffered frames.
 * Keep normal status polling inexpensive, then sample at a different cadence
 * for at most 500 ms after a transient state. Actual TX must be seen again;
 * accepted writes alone never keep an indefinitely unconfirmed TX alive. */
static int backend_tx_watchdog(backend_control_t *c, int observed_state,
                               const tx_pipeline_stats_t *stats, double now)
{
    if (!c->options->tx_requested || c->lifecycle_active) return 0;
    if (stats->writer.samples != c->previous_tx_samples) {
        c->previous_tx_samples = stats->writer.samples;
        c->last_tx_progress = now;
    }
    const char *failure = NULL;
    if (!tx_pipeline_healthy(c->tx_pipeline) || stats->writer.errors
        || stats->producer_worker.last_error || stats->writer_worker.last_error)
        failure = "TX_WORKER";
    else if (observed_state < 0) failure = "TX_STATE_READ";
    else {
        unsigned state = observed_state & 7;
        if (state == cariboulite_radio_state_cmd_tx)
            c->tx_state_deadline = c->tx_state_next_check = 0;
        else if (state == cariboulite_radio_state_cmd_tx_prep
            || state == cariboulite_radio_state_transition) {
            if (!c->tx_state_deadline) c->tx_state_deadline = now + .5;
            if (now >= c->tx_state_deadline) failure = "TX_STATE_TIMEOUT";
            else c->tx_state_next_check = now + .007;
        } else failure = "TX_STATE";
    }
    if (!failure && now - c->last_tx_progress > 2) failure = "TX_PROGRESS_TIMEOUT";
    if (!failure) return 0;
    fprintf(stderr, "[radio_backend] TX runtime failed: %s STATE=%d samples=%" PRIu64 "\n",
        failure, observed_state, stats->writer.samples);
    c->options->control_fault = true;
    c->options->tx_error = failure;
    ++c->options->config_generation;
    return backend_launch_lifecycle(c, 3);
}

static int backend_tx_recheck(backend_control_t *c, double now)
{
    tx_pipeline_stats_t stats;
    tx_pipeline_get_stats(c->tx_pipeline, &stats);
    uint8_t state = 0;
    HW_LOCK();
    int rc = backend_read_modem_state(c, &state);
    HW_UNLOCK();
    return backend_tx_watchdog(c, rc ? -1 : state, &stats, now);
}

static void backend_poll_lifecycle(backend_control_t *c)
{
    if (!c->lifecycle_active) return;
    options_t *o = c->options;
    unsigned phase = atomic_load_explicit(&c->lifecycle_phase, memory_order_acquire);
    if (o->transition != phase) { o->transition = phase; ++o->config_generation; }
    if (!atomic_load_explicit(&c->lifecycle_done, memory_order_acquire)) return;
    pthread_join(c->lifecycle_thread, NULL);
    c->lifecycle_active = false;
    /* A timeout may arrive after the worker's final readiness check but before
     * its completion is consumed. Never accept that late TX success. */
    if (c->lifecycle_action == 1 && !c->lifecycle_result
        && (o->control_fault || (!o->dry_run && c->tx_pipeline->inited
            && atomic_load_explicit(&c->tx_pipeline->tx_ctrl.abort_requested, memory_order_acquire)))) {
        backend_tx_idle(c, true);
        c->lifecycle_result = -1;
    }
    o->transition = 0;
    o->pipeline_generation = c->lifecycle_options.pipeline_generation;
    o->tx_pipeline_generation = c->lifecycle_options.tx_pipeline_generation;
    o->tx_initialized = c->lifecycle_options.tx_initialized;
    o->fpga_needs_programming = c->lifecycle_options.fpga_needs_programming;
    o->actual_rx_frequency = c->lifecycle_options.actual_rx_frequency;
    o->actual_tx_frequency = c->lifecycle_options.actual_tx_frequency;
    o->actual_tx_power = c->lifecycle_options.actual_tx_power;
    if (backend_lifecycle_fpga(c->lifecycle_action)) {
        o->tx_requested = o->rx_requested = o->rx_resume = false;
        o->control_fault = c->lifecycle_result != 0;
        o->tx_error = NULL;
        c->tx_configuration_dirty = true;
        c->tx_controller_generation = 0;
    } else if (c->lifecycle_action == 1 && !c->lifecycle_result) {
        o->tx_requested = true;
        o->rx_requested = false;
        o->tx_error = NULL;
        c->tx_configuration_dirty = false;
        c->previous_tx_samples = 0;
        c->last_tx_progress = clock_seconds(CLOCK_MONOTONIC);
    } else {
        o->tx_requested = false;
        o->rx_requested = c->lifecycle_rx_resumed;
        if (c->lifecycle_result || c->lifecycle_action == 3) {
            o->control_fault = true;
            o->tx_error = c->lifecycle_resume_failed ? "RX_RESUME_FAILED"
                : o->tx_error && !strcmp(o->tx_error, "TRANSITION_TIMEOUT") ? o->tx_error
                : c->lifecycle_options.tx_error ? c->lifecycle_options.tx_error : "TX_FAILED";
            c->tx_configuration_dirty = true;
        }
        o->rx_resume = false;
        c->tx_controller_generation = 0;
    }
    ++o->config_generation;
    if (c->stop_after_start && c->lifecycle_action == 1 && o->tx_requested) {
        c->stop_after_start = false;
        if (backend_launch_lifecycle(c, 2) != 0) {
            backend_tx_idle(c, true);
            o->tx_requested = false; o->rx_resume = false;
            c->tx_configuration_dirty = true;
            o->control_fault = true; o->tx_error = "STOP_FAILED";
        }
    } else if (!o->tx_requested) c->stop_after_start = false;
}

static void backend_control_command(void *opaque, const control_request_t *request,
                                    control_reply_t *reply)
{
    backend_control_t *c = opaque;
    options_t *o = c->options;
    const char *code = "OK";
    bool changed = false;
    radio_profile_t profile;
    const bool both = request->command == CONTROL_BOTH_FREQUENCY ||
        request->command == CONTROL_BOTH_MODE || request->command == CONTROL_BOTH_SAMPLE_RATE;
    if (request->command == CONTROL_INVALID) code = "PROTOCOL";
    else if (request->command == CONTROL_BUSY) code = "BUSY";
    else if (c->lifecycle_active && request->command != CONTROL_GET) {
        if (request->command == CONTROL_TX_STOP && !backend_lifecycle_fpga(c->lifecycle_action)) {
            if (c->lifecycle_action == 1) c->stop_after_start = true;
            code = "ACCEPTED";
        } else if (request->command == CONTROL_TX_START && c->lifecycle_action == 1)
            code = "ACCEPTED";
        else code = "TRANSITION";
    }
    else if (o->fpga_needs_programming && request->command != CONTROL_GET
             && request->command != CONTROL_FPGA_PROGRAM && request->command != CONTROL_FPGA_HARD_RESET
             && request->command != CONTROL_STOP && request->command != CONTROL_TX_STOP) code = "FPGA_REQUIRED";
    else if (o->control_fault && request->command != CONTROL_GET
             && request->command != CONTROL_STOP && request->command != CONTROL_TX_STOP
             && request->command != CONTROL_FPGA_PROGRAM && request->command != CONTROL_FPGA_SOFT_RESET
             && request->command != CONTROL_FPGA_HARD_RESET) code = "FAULT";
    else if (o->tx_requested && request->command != CONTROL_GET
             && request->command != CONTROL_TX_START && request->command != CONTROL_TX_STOP
             && request->command != CONTROL_SQUELCH) code = "TX_RUNNING";
    else if (both && (o->rx_requested || (!o->dry_run && c->pipeline->running)))
        code = "RX_RUNNING";
    else if (both && !o->dry_run && c->tx_pipeline && c->tx_pipeline->running)
        code = "TX_RUNNING";
    else switch (request->command) {
    case CONTROL_GET: break;
    case CONTROL_FPGA_PROGRAM:
    case CONTROL_FPGA_SOFT_RESET:
    case CONTROL_FPGA_HARD_RESET:
        if (o->rx_requested || (!o->dry_run && c->pipeline->running)) code = "RX_RUNNING";
        else if (o->tx_requested || (!o->dry_run && c->tx_pipeline->running)) code = "TX_RUNNING";
        else if (request->command == CONTROL_FPGA_PROGRAM
            && !o->dry_run && backend_fpga_firmware_ready() != 0) {
            code = "FIRMWARE_FILE";
            fprintf(stderr, "[radio_backend] FPGA programming requires readable, nonempty %s\n",
                BACKEND_FPGA_FIRMWARE);
        } else {
            unsigned action = request->command == CONTROL_FPGA_PROGRAM ? 4
                : request->command == CONTROL_FPGA_SOFT_RESET ? 5 : 6;
            if (backend_launch_lifecycle(c, action) != 0) code = "HARDWARE";
            else { code = "ACCEPTED"; changed = true; }
        }
        break;
    case CONTROL_START:
        if (!o->rx_requested) {
            if (backend_start(c) != 0) { code = "HARDWARE"; o->control_fault = true; }
            else o->rx_requested = true;
            changed = true;
        }
        break;
    case CONTROL_STOP:
        if (o->fpga_needs_programming) break; /* Already stopped; this cannot restore firmware. */
        if (o->rx_requested || o->control_fault) {
            int rc = c->tx_pipeline && c->tx_pipeline->inited
                ? backend_tx_idle(c, true) : 0;
            if (backend_force_idle(c) != 0) rc = -1;
            o->rx_requested = false;
            o->control_fault = rc != 0;
            if (rc) code = "HARDWARE";
            changed = true;
            if (!rc) o->tx_error = NULL;
        }
        break;
    case CONTROL_FREQUENCY:
    case CONTROL_BOTH_FREQUENCY:
        if (!control_frequency_valid(o, c->sys, request->frequency_hz)) code = "FREQ_RANGE";
        else if (o->rx_requested) code = "RX_RUNNING";
        else {
            if (o->frequency != request->frequency_hz &&
                backend_tune(c, request->frequency_hz) != 0) {
                code = "HARDWARE";
                /* Preserve accepted configuration and attempt its checked
                 * restoration. An unsuccessful rollback is explicit FAULT. */
                if (backend_tune(c, o->frequency) != 0) {
                    o->control_fault = true;
                    changed = true;
                }
                break;
            }
            /* Commit both saved frequencies only after RX tuning succeeds. */
            if (o->frequency != request->frequency_hz) {
                o->frequency = o->rx.freq_hz = request->frequency_hz;
                changed = true;
            }
            if (both && o->tx.freq_hz != request->frequency_hz) {
                o->tx.freq_hz = request->frequency_hz;
                c->tx_configuration_dirty = true;
                changed = true;
            }
        }
        break;
    case CONTROL_MODE:
    case CONTROL_BOTH_MODE: {
        /* TX supports NBFM only; WBFM changes just the saved RX mode. */
        audio_demod_mode_t mode = request->wbfm ? AUDIO_DEMOD_WBFM : AUDIO_DEMOD_NBFM;
        if (radio_profile_rx(mode, (unsigned)o->rx.fs_rf, &profile)) code = "INVALID";
        else if (o->rx_requested) code = "RX_RUNNING";
        else if (mode != o->rx.mode) { o->rx.mode = mode; changed = true; }
        break;
    }
    case CONTROL_SAMPLE_RATE:
    case CONTROL_BOTH_SAMPLE_RATE:
        if (radio_profile_rx(o->rx.mode, request->sample_rate_hz, &profile) ||
            (both && radio_profile_tx(request->sample_rate_hz, &profile))) code = "INVALID";
        else if (o->rx_requested) code = "RX_RUNNING";
        else {
            if (o->rx.fs_rf != request->sample_rate_hz) {
                o->rx.fs_rf = (float)request->sample_rate_hz; changed = true;
            }
            if (both && o->tx.rf_fs != request->sample_rate_hz) {
                o->tx.rf_fs = request->sample_rate_hz;
                c->tx_configuration_dirty = true; changed = true;
            }
        }
        break;
    case CONTROL_SQUELCH: {
        uint32_t levels;
        if (!noise_squelch_pack_levels(request->noise_open_rms, request->noise_close_rms, &levels))
            code = "INVALID";
        else if (o->rx.mode == AUDIO_DEMOD_WBFM && request->noise_enabled) code = "WBFM_NOISE";
        else {
            /* WBFM has no noise detector. Its disabled effective flag must
             * not discard the user's saved NBFM noise-squelch preference. */
            bool disabled = o->rx.mode == AUDIO_DEMOD_NBFM
                ? !request->noise_enabled : o->rx.noise_squelch_disabled;
            float open, close;
            noise_squelch_unpack_levels(levels, &open, &close);
            if (!o->dry_run && c->pipeline->inited
                && rx_pipeline_set_noise_squelch_levels(c->pipeline, open, close) != 0) code = "HARDWARE";
            else {
                changed = disabled != o->rx.noise_squelch_disabled
                    || request->carrier_enabled != o->rx.carrier_squelch_enabled
                    || open != o->rx.noise_squelch_open_rms || close != o->rx.noise_squelch_close_rms;
                o->rx.noise_squelch_disabled = disabled;
                o->rx.carrier_squelch_enabled = request->carrier_enabled;
                o->rx.noise_squelch_open_rms = open; o->rx.noise_squelch_close_rms = close;
                if (!o->dry_run && c->pipeline->inited)
                    rx_pipeline_set_squelch(c->pipeline, request->noise_enabled, request->carrier_enabled);
            }
        }
        break;
    }
    case CONTROL_TX_START:
        if (o->tx_requested) break;
        if (!control_frequency_valid(o, c->sys, o->tx.freq_hz)) { code = "FREQ_RANGE"; break; }
        if (!o->dry_run) {
            uint8_t iq = 0;
            HW_LOCK();
            int rc = at86rf215_read_buffer(&c->sys->modem, REG_RF_IQIFC0, &iq, 1);
            HW_UNLOCK();
            if (rc || (iq & 0x80)) { code = "LOOPBACK"; break; }
        }
        o->rx_resume = o->rx_requested;
        o->rx_requested = false;
        c->tx_controller_generation = c->server ? c->server->controller_generation : 0;
        if (backend_launch_lifecycle(c, 1) != 0) {
            o->rx_requested = o->rx_resume;
            o->rx_resume = false;
            c->tx_controller_generation = 0;
            code = "HARDWARE";
        } else { o->tx_requested = true; code = "ACCEPTED"; changed = true; }
        break;
    case CONTROL_TX_STOP:
        if (o->tx_requested) {
            if (backend_launch_lifecycle(c, 2) != 0) {
                backend_tx_idle(c, true);
                o->tx_requested = false; o->rx_resume = false;
                o->control_fault = true; o->tx_error = "STOP_FAILED";
                c->tx_configuration_dirty = true;
                changed = true; code = "HARDWARE";
            }
            else { code = "ACCEPTED"; changed = true; }
        }
        break;
    case CONTROL_TX_FREQUENCY:
        if (!control_frequency_valid(o, c->sys, request->tx_frequency_hz)) code = "FREQ_RANGE";
        else if (o->tx.freq_hz != request->tx_frequency_hz) {
            o->tx.freq_hz = request->tx_frequency_hz;
            c->tx_configuration_dirty = true; changed = true;
        }
        break;
    case CONTROL_TX_POWER:
        if (!radio_profile_tx_power_valid(options_channel(o), request->tx_power_dbm)) code = "INVALID";
        else if (o->tx.tx_power_dbm != request->tx_power_dbm) {
            o->tx.tx_power_dbm = request->tx_power_dbm;
            c->tx_configuration_dirty = true; changed = true;
        }
        break;
    case CONTROL_TX_SAMPLE_RATE:
        if (radio_profile_tx(request->tx_sample_rate_hz, &profile)) code = "INVALID";
        else if (o->tx.rf_fs != request->tx_sample_rate_hz) {
            o->tx.rf_fs = request->tx_sample_rate_hz;
            c->tx_configuration_dirty = true; changed = true;
        }
        break;
    case CONTROL_TX_SOURCE:
        if (o->tx.tone_mode != request->tx_tone_mode) {
            o->tx.tone_mode = request->tx_tone_mode;
            o->tx.mic_dev = o->tx.tone_mode ? NULL : o->microphone;
            c->tx_configuration_dirty = true; changed = true;
        }
        break;
    case CONTROL_TX_TONE:
        if (!radio_profile_finite(request->tx_tone_hz) || request->tx_tone_hz <= 0 || request->tx_tone_hz > 3000
            || !radio_profile_finite(request->tx_tone_amp) || request->tx_tone_amp <= 0 || request->tx_tone_amp > 1)
            code = "INVALID";
        else if (o->tx.tone_hz != request->tx_tone_hz || o->tx.tone_amp != request->tx_tone_amp) {
            o->tx.tone_hz = request->tx_tone_hz; o->tx.tone_amp = request->tx_tone_amp;
            c->tx_configuration_dirty = true; changed = true;
        }
        break;
    default: code = "PROTOCOL"; break;
    }
    if (changed) ++o->config_generation;
    *reply = (control_reply_t){.id = request->id, .protocol_version = request->protocol_version,
        .ok = !strcmp(code, "OK") || !strcmp(code, "ACCEPTED"),
        .config_generation = o->config_generation, .pipeline_generation = o->pipeline_generation,
        .simulated = o->dry_run, .requested_frequency_hz = o->frequency,
        .actual_frequency_hz = o->actual_rx_frequency ? o->actual_rx_frequency
            : c->radio && !c->lifecycle_active ? c->radio->actual_rf_frequency : o->frequency,
        .sample_rate_hz = (unsigned)o->rx.fs_rf, .wbfm = o->rx.mode == AUDIO_DEMOD_WBFM,
        .noise_enabled = o->rx.mode == AUDIO_DEMOD_NBFM && !o->rx.noise_squelch_disabled,
        .carrier_enabled = o->rx.carrier_squelch_enabled,
        .noise_open_rms = o->rx.noise_squelch_open_rms, .noise_close_rms = o->rx.noise_squelch_close_rms,
        .tx_pipeline_generation = o->tx_pipeline_generation,
        .tx_requested_frequency_hz = o->tx.freq_hz, .tx_actual_frequency_hz = o->actual_tx_frequency,
        .tx_sample_rate_hz = o->tx.rf_fs, .tx_requested_power_dbm = o->tx.tx_power_dbm,
        .tx_actual_power_dbm = o->actual_tx_power, .tx_tone_mode = o->tx.tone_mode,
        .tx_tone_hz = o->tx.tone_hz, .tx_tone_amp = o->tx.tone_amp,
        .tx_deviation_hz = o->tx.f_dev_hz, .tx_requested = o->tx_requested,
        .rx_resume = o->rx_resume, .transition = c->lifecycle_active};
    snprintf(reply->code, sizeof(reply->code), "%s", code);
    if (!isfinite(reply->actual_frequency_hz) || reply->actual_frequency_hz < 0)
        reply->actual_frequency_hz = 0; /* Unknown after a hardware failure. */
    snprintf(reply->state, sizeof(reply->state), "%s", backend_control_state(o));
    snprintf(reply->channel, sizeof(reply->channel), "%s", o->channel);
}

/* JSON string encoding also makes unusual ALSA device names safe. */
static int json_string(const char *input, char *output, size_t capacity)
{
    size_t used = 0;
    if (capacity < 3) return -1;
    output[used++] = '"';
    for (const unsigned char *p = (const unsigned char *)input; *p; ++p) {
        char encoded[7];
        const char *part = encoded;
        size_t length;
        if (*p < 0x20 || *p >= 0x80) {
            snprintf(encoded, sizeof(encoded), "\\u%04x", *p);
            length = 6;
        } else if (*p == '"' || *p == '\\') {
            encoded[0] = '\\'; encoded[1] = (char)*p; length = 2;
        } else { encoded[0] = (char)*p; length = 1; }
        if (used + length + 2 > capacity) return -1;
        memcpy(output + used, part, length); used += length;
    }
    output[used++] = '"'; output[used] = 0;
    return 0;
}

static int worker_json(const rx_worker_status_t *worker, char *out, size_t capacity)
{
    const pipeline_thread_status_t *s = worker ? &worker->scheduling : NULL;
    int result;
    if (!s || !s->valid) {
        result = snprintf(out, capacity, "{\"created\":%s,\"alive\":%s,\"last_error\":%d,"
            "\"scheduling\":{\"valid\":false}}",
            worker && worker->created ? "true" : "false",
            worker && worker->alive ? "true" : "false", worker ? worker->last_error : 0);
    } else {
        char affinity[800];
        if (json_string(s->affinity, affinity, sizeof(affinity))) return -1;
        result = snprintf(out, capacity, "{\"created\":%s,\"alive\":%s,\"last_error\":%d,"
            "\"scheduling\":{\"valid\":true,\"requested_priority\":%d,"
            "\"requested_cpu\":%d,\"applied_cpu\":%d,\"policy\":%d,"
            "\"policy_name\":\"%s\",\"priority\":%d,\"affinity\":%s,"
            "\"affinity_truncated\":%s,\"rt_error\":%d,\"fallback_error\":%d,"
            "\"affinity_error\":%d,\"mlock_attempted\":%s,\"mlock_error\":%d,"
            "\"query_error\":%d}}",
            worker->created ? "true" : "false", worker->alive ? "true" : "false", worker->last_error,
            s->requested_priority, s->requested_cpu, s->applied_cpu, s->policy,
            pipeline_scheduler_policy_name(s->policy), s->priority, affinity,
            s->affinity_truncated ? "true" : "false", s->rt_error, s->fallback_error,
            s->affinity_error, s->mlock_attempted ? "true" : "false", s->mlock_error,
            s->query_error);
    }
    return result < 0 || (size_t)result >= capacity ? -1 : result;
}

static int playback_json(const rx_pipeline_stats_t *stats, char *out, size_t capacity)
{
    int result;
    if (!stats || !stats->playback_valid) {
        result = snprintf(out, capacity, "{\"valid\":false}");
    } else {
        const audio_sink_stats_t *p = &stats->playback;
        result = snprintf(out, capacity, "{\"valid\":true,"
            "\"write_calls\":%" PRIu64 ",\"frames_requested\":%" PRIu64 ","
            "\"frames_written\":%" PRIu64 ",\"partial_writes\":%" PRIu64 ","
            "\"would_block\":%" PRIu64 ",\"zero_writes\":%" PRIu64 ","
            "\"underruns\":%" PRIu64 ",\"recovery_attempts\":%" PRIu64 ","
            "\"recovery_successes\":%" PRIu64 ",\"recovery_failures\":%" PRIu64 ","
            "\"write_errors\":%" PRIu64 ",\"last_error\":%d}",
            p->write_calls, p->frames_requested, p->frames_written, p->partial_writes,
            p->would_block, p->zero_writes, p->underruns, p->recovery_attempts,
            p->recovery_successes, p->recovery_failures, p->write_errors, p->last_error);
    }
    return result < 0 || (size_t)result >= capacity ? -1 : result;
}

static int tx_driver_json(const tx_pipeline_stats_t *stats, char *out, size_t capacity)
{
    int result;
    if (!stats || !stats->driver_valid) {
        result = snprintf(out, capacity, "{\"valid\":false}");
    } else {
        const struct smi_stream_tx_status *d = &stats->driver;
        result = snprintf(out, capacity, "{\"valid\":true,\"abi_version\":%" PRIu32 ","
            "\"struct_size\":%" PRIu32 ",\"state\":%" PRIu32 ",\"flags\":%" PRIu32 ","
            "\"bounded_fifo\":%s,\"dma_running\":%s,\"fifo_queued_bytes\":%" PRIu32 ","
            "\"fifo_capacity_bytes\":%" PRIu32 ",\"fifo_limit_bytes\":%" PRIu32 ","
            "\"period_bytes\":%" PRIu32 ",\"ring_bytes\":%" PRIu32 ",\"reserved\":%" PRIu32 ","
            "\"session_generation\":%" PRIu64 ",\"completed_periods\":%" PRIu64 ","
            "\"underrun_periods\":%" PRIu64 ",\"accepted_bytes\":%" PRIu64 ","
            "\"fifo_dequeued_bytes\":%" PRIu64 "}",
            (uint32_t)d->abi_version, (uint32_t)d->struct_size,
            (uint32_t)d->state, (uint32_t)d->flags,
            d->flags & SMI_STREAM_TX_STATUS_BOUNDED_FIFO ? "true" : "false",
            d->flags & SMI_STREAM_TX_STATUS_DMA_RUNNING ? "true" : "false",
            (uint32_t)d->fifo_queued_bytes, (uint32_t)d->fifo_capacity_bytes,
            (uint32_t)d->fifo_limit_bytes, (uint32_t)d->period_bytes,
            (uint32_t)d->ring_bytes, (uint32_t)d->reserved,
            (uint64_t)d->session_generation, (uint64_t)d->completed_periods,
            (uint64_t)d->underrun_periods, (uint64_t)d->accepted_bytes,
            (uint64_t)d->fifo_dequeued_bytes);
    }
    return result < 0 || (size_t)result >= capacity ? -1 : result;
}

static int tx_audio_buffer_json(const tx_pipeline_stats_t *stats, char *out, size_t capacity)
{
    int result;
    if (!stats || !stats->audio_buffer_valid) {
        result = snprintf(out, capacity, "{\"valid\":false}");
    } else {
        const tx_audio_buffer_stats_t *a = &stats->audio_buffer;
        result = snprintf(out, capacity, "{\"valid\":true,\"created\":%s,\"alive\":%s,"
            "\"running\":%s,\"cue\":%s,\"primed\":%s,\"clock_valid\":%s,\"last_error\":%d,"
            "\"queued_frames\":%zu,\"capacity_frames\":%zu,\"target_frames\":%zu,"
            "\"minimum_frames\":%zu,\"maximum_frames\":%zu,\"input_per_output\":%.9f,"
            "\"correction_ppm\":%.3f,\"occupancy_filtered_frames\":%.3f,"
            "\"captured_frames\":%" PRIu64 ",\"consumed_frames\":%" PRIu64 ","
            "\"output_frames\":%" PRIu64 ",\"capture_calls\":%" PRIu64 ","
            "\"again_calls\":%" PRIu64 ",\"read_calls\":%" PRIu64 ","
            "\"underflows\":%" PRIu64 ",\"overflows\":%" PRIu64 ","
            "\"overflow_frames\":%" PRIu64 ",\"cue_discard_frames\":%" PRIu64 ","
            "\"resets\":%" PRIu64 ",\"clock_limit_frames\":%" PRIu64 ","
            "\"clock_limit_faults\":%" PRIu64 "}",
            a->created ? "true" : "false", a->alive ? "true" : "false",
            a->running ? "true" : "false", a->cue ? "true" : "false",
            a->primed ? "true" : "false", a->clock_valid ? "true" : "false", a->last_error,
            a->queued_frames, a->capacity_frames, a->target_frames,
            a->minimum_frames, a->maximum_frames, a->input_per_output,
            a->correction_ppm, a->occupancy_filtered_frames,
            a->captured_frames, a->consumed_frames, a->output_frames,
            a->capture_calls, a->again_calls, a->read_calls, a->underflows,
            a->overflows, a->overflow_frames, a->cue_discard_frames, a->resets,
            a->clock_limit_frames, a->clock_limit_faults);
    }
    return result < 0 || (size_t)result >= capacity ? -1 : result;
}

static int tx_smi_timing_json(const tx_pipeline_stats_t *stats, char *out, size_t capacity)
{
    int result;
    if (!stats || !stats->smi_timing_valid) {
        result = snprintf(out, capacity, "{\"valid\":false}");
    } else {
        const struct smi_settings *s = &stats->smi_timing;
        result = snprintf(out, capacity, "{\"valid\":true,\"write_setup\":%d,"
            "\"write_strobe\":%d,\"write_hold\":%d,\"write_pace\":%d}",
            s->write_setup_time, s->write_strobe_time, s->write_hold_time, s->write_pace_time);
    }
    return result < 0 || (size_t)result >= capacity ? -1 : result;
}

static void profile_observation_number(char *out, size_t capacity, double value)
{
    if (radio_profile_finite(value) && value > 0) snprintf(out, capacity, "%.3f", value);
    else snprintf(out, capacity, "null");
}

/* Desired profiles describe the next stream start. Observations describe only
 * the cached register sample, including when another direction or idle setup
 * left a different profile in the modem. Never fill missing bytes from intent. */
static int profile_status_json(const options_t *o, const modem_snapshot *modem,
    char *desired, size_t desired_capacity, char *observed, size_t observed_capacity)
{
    radio_profile_t rx, tx;
    if (radio_profile_rx(o->rx.mode, (unsigned)o->rx.fs_rf, &rx) ||
        radio_profile_tx(o->tx.rf_fs, &tx)) return -1;
    int desired_size = snprintf(desired, desired_capacity,
        "{\"rx\":{\"sample_rate_hz\":%u,\"analog_bandwidth_hz\":%u,\"digital_cutoff_hz\":%u},"
        "\"tx\":{\"sample_rate_hz\":%u,\"analog_bandwidth_hz\":%u,\"digital_cutoff_hz\":%u,"
        "\"sample_gap\":%u}}",
        rx.sample_rate_hz, rx.analog_bandwidth_hz, rx.digital_cutoff_hz,
        tx.sample_rate_hz, tx.analog_bandwidth_hz, tx.digital_cutoff_hz, tx.tx_sample_gap);
    if (desired_size < 0 || (size_t)desired_size >= desired_capacity) return -1;

    /* Indices match the shared modem_snapshot_radio_names register schema. */
    const bool channel_valid = modem &&
        (modem->channel == at86rf215_rf_channel_900mhz ||
         modem->channel == at86rf215_rf_channel_2400mhz);
    const int rxbwc = channel_valid ? modem->radio[10] : -1;
    const int rxdfe = channel_valid ? modem->radio[11] : -1;
    const int txcutc = channel_valid ? modem->radio[19] : -1;
    const int txdfe = channel_valid ? modem->radio[20] : -1;
    char rx_rate[32], rx_analog[32], rx_digital[32];
    char tx_rate[32], tx_analog[32], tx_digital[32];
    profile_observation_number(rx_rate, sizeof(rx_rate), radio_profile_sample_rate_hz(rxdfe));
    profile_observation_number(rx_analog, sizeof(rx_analog), radio_profile_rx_bandwidth_hz(rxbwc));
    profile_observation_number(rx_digital, sizeof(rx_digital), radio_profile_digital_cutoff_hz(rxdfe));
    profile_observation_number(tx_rate, sizeof(tx_rate), radio_profile_sample_rate_hz(txdfe));
    profile_observation_number(tx_analog, sizeof(tx_analog), radio_profile_tx_bandwidth_hz(txcutc));
    profile_observation_number(tx_digital, sizeof(tx_digital), radio_profile_digital_cutoff_hz(txdfe));
    int observed_size = snprintf(observed, observed_capacity,
        "{\"rx\":{\"sample_rate_hz\":%s,\"analog_bandwidth_hz\":%s,\"digital_cutoff_hz\":%s},"
        "\"tx\":{\"sample_rate_hz\":%s,\"analog_bandwidth_hz\":%s,\"digital_cutoff_hz\":%s}}",
        rx_rate, rx_analog, rx_digital, tx_rate, tx_analog, tx_digital);
    return observed_size < 0 || (size_t)observed_size >= observed_capacity ? -1 : 0;
}

static int make_snapshot(char *out, size_t capacity, const options_t *o,
    const char *session, uint64_t sequence, const char *state, rx_pipeline_t *pipeline,
    const rx_pipeline_stats_t *stats,
    const modem_snapshot *modem, const status_publisher_t *publisher,
    const cariboulite_radio_state_st *radio, const tx_pipeline_stats_t *txstats)
{
    char hardware[2500], playback[1600], queue[600] = "null", rms[48] = "null";
    char audio_queue[600] = "null", playback_stats[700], reader[300] = "null";
    char reader_worker[1200], demod_worker[1200], audio_worker[1200];
    char tx_json[9000], tx_queue[600] = "null", tx_writer[700] = "null";
    char tx_capture[700] = "{\"valid\":false}", producer_worker[1200], writer_worker[1200];
    char tx_driver[1200], tx_audio_buffer[2000], capture_worker[1200], tx_smi_timing[180];
    char desired_profile[400], observed_profile[400];
    if (profile_status_json(o, modem, desired_profile, sizeof(desired_profile),
                            observed_profile, sizeof(observed_profile)) != 0) return -1;
    rx_worker_status_t producer = {0}, writer = {0}, capture = {0};
    if (txstats) {
        producer = (rx_worker_status_t){.created = txstats->producer_worker.created,
            .alive = txstats->producer_worker.alive, .last_error = txstats->producer_worker.last_error,
            .scheduling = txstats->producer_worker.scheduling};
        writer = (rx_worker_status_t){.created = txstats->writer_worker.created,
            .alive = txstats->writer_worker.alive, .last_error = txstats->writer_worker.last_error,
            .scheduling = txstats->writer_worker.scheduling};
        if (txstats->audio_buffer_valid) {
            const tx_audio_buffer_stats_t *a = &txstats->audio_buffer;
            capture = (rx_worker_status_t){.created = a->created, .alive = a->alive,
                .last_error = a->last_error, .scheduling = a->scheduling};
        }
        const rf10_stats_t *q = &txstats->txq;
        snprintf(tx_queue, sizeof(tx_queue), "{\"capacity\":%zu,\"depth\":%zu,"
            "\"puts\":%zu,\"gets\":%zu,\"drops\":%zu,\"timeouts_put\":%zu,"
            "\"timeouts_get\":%zu,\"min_depth\":%zu,\"max_depth\":%zu}",
            q->cap, q->count, q->puts, q->gets, q->drops, q->timeouts_put,
            q->timeouts_get, q->min_depth, q->max_depth);
        const tx_writer_stats_t *w = &txstats->writer;
        snprintf(tx_writer, sizeof(tx_writer), "{\"calls\":%" PRIu64 ",\"samples\":%" PRIu64 ","
            "\"partial_writes\":%" PRIu64 ",\"zero_writes\":%" PRIu64 ","
            "\"would_block\":%" PRIu64 ",\"errors\":%" PRIu64 ",\"last_error\":%d,"
            "\"written_sequence\":%" PRIu64 "}", w->calls, w->samples, w->partial_writes,
            w->zero_writes, w->would_block, w->errors, w->last_error, w->written_sequence);
        if (txstats->capture_valid) {
            const alsa_source_stats_t *a = &txstats->capture;
            snprintf(tx_capture, sizeof(tx_capture), "{\"valid\":true,\"read_calls\":%" PRIu64 ","
                "\"frames_read\":%" PRIu64 ",\"overruns\":%" PRIu64 ",\"recovery_attempts\":%" PRIu64 ","
                "\"recovery_successes\":%" PRIu64 ",\"recovery_failures\":%" PRIu64 ","
                "\"read_errors\":%" PRIu64 ",\"last_error\":%d}", a->read_calls, a->frames_read,
                a->overruns, a->recovery_attempts, a->recovery_successes,
                a->recovery_failures, a->read_errors, a->last_error);
        }
    }
    if (worker_json(txstats ? &producer : NULL, producer_worker, sizeof(producer_worker)) < 0
        || worker_json(txstats ? &writer : NULL, writer_worker, sizeof(writer_worker)) < 0
        || worker_json(txstats && txstats->audio_buffer_valid ? &capture : NULL,
                       capture_worker, sizeof(capture_worker)) < 0
        || tx_driver_json(txstats, tx_driver, sizeof(tx_driver)) < 0
        || tx_audio_buffer_json(txstats, tx_audio_buffer, sizeof(tx_audio_buffer)) < 0
        || tx_smi_timing_json(txstats, tx_smi_timing, sizeof(tx_smi_timing)) < 0) return -1;
    int tx_size = snprintf(tx_json, sizeof(tx_json), "{\"running\":%s,\"requested\":%s,"
        "\"initialized\":%s,\"generation\":%" PRIu64 ",\"queue\":%s,\"writer\":%s,"
        "\"capture\":%s,\"hardware_paced\":%s,\"driver\":%s,\"audio_buffer\":%s,\"smi_timing\":%s,"
        "\"workers\":{\"producer\":%s,\"writer\":%s,\"capture\":%s},\"fault\":%s%s%s}",
        o->dry_run ? "false" : o->transition ? "null" : o->tx_requested ? "true" : "false",
        o->tx_requested ? "true" : "false",
        o->dry_run ? "false" : o->transition ? "null" : o->tx_initialized ? "true" : "false",
        o->tx_pipeline_generation, tx_queue, tx_writer, tx_capture,
        txstats ? txstats->hardware_paced ? "true" : "false" : "null",
        tx_driver, tx_audio_buffer, tx_smi_timing, producer_worker, writer_worker, capture_worker,
        o->tx_error ? "\"" : "", o->tx_error ? o->tx_error : "null", o->tx_error ? "\"" : "");
    if (tx_size < 0 || (size_t)tx_size >= sizeof(tx_json)) return -1;
    if (modem_snapshot_hardware_json(modem, hardware, sizeof(hardware)) < 0
        || json_string(o->playback, playback, sizeof(playback))
        || playback_json(stats, playback_stats, sizeof(playback_stats)) < 0
        || worker_json(stats ? &stats->reader_worker : NULL, reader_worker, sizeof(reader_worker)) < 0
        || worker_json(stats ? &stats->demod_worker : NULL, demod_worker, sizeof(demod_worker)) < 0
        || worker_json(stats ? &stats->audio_worker : NULL, audio_worker, sizeof(audio_worker)) < 0)
        return -1;
    rx_noise_squelch_status_t noise = {.open_rms = o->rx.noise_squelch_open_rms,
        .close_rms = o->rx.noise_squelch_close_rms};
    const bool live = pipeline && pipeline->inited;
    if (live) {
        if (!stats) return -1;
        const rf10_stats_t *q = &stats->rxq;
        snprintf(queue, sizeof(queue), "{\"capacity\":%zu,\"depth\":%zu,"
            "\"puts\":%zu,\"gets\":%zu,\"drops\":%zu,\"timeouts_put\":%zu,"
            "\"timeouts_get\":%zu,\"min_depth\":%zu,\"max_depth\":%zu}",
            q->cap, q->count, q->puts, q->gets, q->drops, q->timeouts_put,
            q->timeouts_get, q->min_depth, q->max_depth);
        rx_pipeline_get_noise_squelch_status(pipeline, &noise);
        if (noise.valid) snprintf(rms, sizeof(rms), "%.6f", (double)noise.rms);
        const aud10_stats_t *a = &stats->audioq;
        snprintf(audio_queue, sizeof(audio_queue), "{\"capacity\":%zu,\"depth\":%zu,"
            "\"puts\":%zu,\"gets\":%zu,\"drops\":%zu,\"timeouts_put\":%zu,"
            "\"timeouts_get\":%zu,\"min_depth\":%zu,\"max_depth\":%zu,"
            "\"shutdown_puts\":%zu,\"shutdown_gets\":%zu}",
            a->cap, a->count, a->puts, a->gets, a->drops, a->timeouts_put,
            a->timeouts_get, a->min_depth, a->max_depth, a->shutdown_puts, a->shutdown_gets);
        const rx_reader_stats_t *r = &stats->reader;
        snprintf(reader, sizeof(reader), "{\"calls\":%" PRIu64 ",\"samples\":%" PRIu64 ","
            "\"short_reads\":%" PRIu64 ",\"zero_reads\":%" PRIu64 ","
            "\"errors\":%" PRIu64 ",\"last_error\":%d}",
            r->calls, r->samples, r->short_reads, r->zero_reads, r->errors, r->last_error);
    }
    const bool noise_enabled = o->rx.mode == AUDIO_DEMOD_NBFM && !o->rx.noise_squelch_disabled;
    radio_profile_t rx_profile;
    if (radio_profile_rx(o->rx.mode, (unsigned)o->rx.fs_rf, &rx_profile)) return -1;
    const double bandwidth = rx_profile.analog_bandwidth_hz;
    const double actual_freq = o->actual_rx_frequency ? o->actual_rx_frequency
        : radio ? radio->actual_rf_frequency : o->frequency;
    char tx_actual_freq[64] = "null", tx_actual_power[48] = "null";
    char tx_bandwidth[32] = "null";
    const int tx_analog_cutoff_hz = modem_snapshot_tx_analog_cutoff_hz(modem);
    if (tx_analog_cutoff_hz > 0)
        snprintf(tx_bandwidth, sizeof(tx_bandwidth), "%d", tx_analog_cutoff_hz);
    if (o->actual_tx_frequency > 0) snprintf(tx_actual_freq, sizeof(tx_actual_freq), "%.3f", o->actual_tx_frequency);
    if (o->actual_tx_power != INT_MIN) snprintf(tx_actual_power, sizeof(tx_actual_power), "%d", o->actual_tx_power);
    int result = snprintf(out, capacity,
        "{\"version\":1,\"session\":\"%s\",\"sequence\":%" PRIu64 ","
        "\"monotonic_s\":%.9f,\"realtime_s\":%.6f,\"hardware_monotonic_s\":%.9f,"
        "\"source\":{\"kind\":\"%s\",\"label\":\"C radio backend%s\"},"
        "\"owner_pid\":%ld,\"channel\":\"%s\",\"state\":\"%s\",\"rx_mode\":\"%s\","
        "\"config\":{\"sample_rate_hz\":%.0f,\"rx_frequency_hz\":%.3f,"
        "\"requested_frequency_hz\":%.3f,\"rx_bandwidth_hz\":%.0f,\"tx_bandwidth_hz\":%s,"
        "\"playback\":%s,\"pcm_gain\":%.3f,\"deemphasis_us\":%.3f,\"rx_deviation_hz\":%.3f,"
        "\"tx_frequency_hz\":%.3f,\"tx_actual_frequency_hz\":%s,\"tx_power_dbm\":%d,"
        "\"tx_actual_power_dbm\":%s,\"tx_sample_rate_hz\":%u,\"tx_source\":\"%s\","
        "\"tx_tone_hz\":%.3f,\"tx_tone_amp\":%.3f,\"tx_deviation_hz\":%.3f,"
        "\"profile\":%s,\"observed\":%s},"
        "\"rx\":{\"running\":%s,\"queue\":%s,\"noise_enabled\":%s,\"carrier_enabled\":%s,"
        "\"gate_open\":%s,\"noise\":{\"valid\":%s,\"detector_open\":%s,\"rms\":%s,"
        "\"open_rms\":%.3f,\"close_rms\":%.3f},"
        "\"audio_queue\":%s,\"playback\":%s,\"reader\":%s,"
        "\"workers\":{\"reader\":%s,\"demod\":%s,\"audio\":%s}},"
        "\"tx\":%s,\"hardware\":%s,"
        "\"loopback\":{\"armed\":false,\"active\":false,\"samples\":0,\"timeouts\":0},"
        "\"control\":{\"config_generation\":%" PRIu64 ",\"pipeline_generation\":%" PRIu64 ","
        "\"rx_requested\":%s,\"state\":\"%s\",\"tx_pipeline_generation\":%" PRIu64 ","
        "\"tx_requested\":%s,\"rx_resume\":%s,\"transition\":%s},"
        "\"publisher\":{\"status_hz\":%.3f,\"spi_blocks\":%u,\"spi_failed_blocks\":%u,"
        "\"spi_read_ms\":%.6f,\"spi_transactions\":%" PRIu64 ",\"spi_failures\":%" PRIu64 ","
        "\"clients\":%u,\"packets_sent\":%" PRIu64 ",\"packets_skipped\":%" PRIu64 ","
        "\"clients_rejected\":%" PRIu64 "}}",
        session, sequence, clock_seconds(CLOCK_MONOTONIC), clock_seconds(CLOCK_REALTIME),
        modem->monotonic_s, o->dry_run ? "simulated" : "live", o->dry_run ? " transport-only dry run" : "",
        (long)getpid(), o->channel, state, o->rx.mode == AUDIO_DEMOD_NBFM ? "NBFM" : "WBFM mono",
        (double)o->rx.fs_rf, actual_freq, o->frequency, bandwidth, tx_bandwidth, playback,
        (double)o->rx.pcm_gain, o->rx.deemph_tau_s * 1e6,
        (double)(o->rx.nbfm_deviation_hz != 0 ? o->rx.nbfm_deviation_hz : NBFM_DEFAULT_DEVIATION_HZ),
        o->tx.freq_hz, tx_actual_freq, o->tx.tx_power_dbm, tx_actual_power,
        o->tx.rf_fs, o->tx.tone_mode ? "tone" : "mic", (double)o->tx.tone_hz,
        (double)o->tx.tone_amp, (double)o->tx.f_dev_hz,
        desired_profile, observed_profile,
        live && rx_pipeline_running(pipeline) ? "true" : "false", queue,
        noise_enabled ? "true" : "false", o->rx.carrier_squelch_enabled ? "true" : "false",
        live ? (rx_pipeline_squelch_open(pipeline) ? "true" : "false") : "null",
        noise.valid ? "true" : "false", noise.valid ? (noise.detector_open ? "true" : "false") : "null",
        rms, (double)noise.open_rms, (double)noise.close_rms, audio_queue, playback_stats, reader,
        reader_worker, demod_worker, audio_worker,
        tx_json, hardware, o->config_generation, o->pipeline_generation,
        o->rx_requested ? "true" : "false", backend_control_state(o),
        o->tx_pipeline_generation, o->tx_requested ? "true" : "false",
        o->rx_resume ? "true" : "false", o->transition ? "true" : "false",
        o->status_hz, modem->read_blocks, modem->failed_blocks,
        modem->read_duration_s * 1e3, modem->spi_transactions, modem->spi_failures,
        publisher->clients_count, publisher->packets_sent, publisher->packets_skipped,
        publisher->clients_rejected);
    return result < 0 || (size_t)result >= capacity ? -1 : result;
}

int main(int argc, char **argv)
{
    options_t options;
    int parsed = parse_options(argc, argv, &options);
    if (parsed > 0) return 0;
    if (parsed < 0) { usage(stderr, argv[0]); return 2; }
    if (install_stop_signals()) { perror("signal setup"); return 1; }

    status_publisher_t publisher;
    if (status_publisher_init(&publisher, options.socket_path)) {
        perror("status socket"); return 1;
    }
    fprintf(stderr, "Status socket: %s\n", publisher.socket_path);
    char control_path[STATUS_PUBLISHER_SOCKET_PATH_BYTES];
    if (!options.control_socket_path) {
        const char *separator = strrchr(publisher.socket_path, '/');
        size_t prefix = separator ? (size_t)(separator - publisher.socket_path + 1) : 0;
        if (prefix + sizeof("control.sock") > sizeof(control_path)) {
            fprintf(stderr, "Default control socket path is too long; use --control-socket.\n");
            status_publisher_close(&publisher); return 1;
        }
        memcpy(control_path, publisher.socket_path, prefix);
        strcpy(control_path + prefix, "control.sock");
    }
    control_server_t control_server;
    if (control_server_init(&control_server, options.control_socket_path
        ? options.control_socket_path : control_path)) {
        perror("control socket"); status_publisher_close(&publisher); return 1;
    }
    fprintf(stderr, "Control socket: %s\n", control_server.listener_anchor.socket_path);
    CARIBOULITE_CONFIG_DEFAULT(sys);
    rx_pipeline_t pipeline = {0};
    tx_pipeline_t tx_pipeline = {0};
    cariboulite_radio_state_st *radio = NULL;
    backend_control_t control = {.options = &options, .sys = &sys,
        .pipeline = &pipeline, .tx_pipeline = &tx_pipeline, .server = &control_server};
    modem_snapshot modem;
    const at86rf215_rf_channel_en channel = !strcmp(options.channel, "RF09")
        ? at86rf215_rf_channel_900mhz : at86rf215_rf_channel_2400mhz;
    modem_snapshot_init(&modem, channel);
    int result = 1;
    bool driver_ready = false;
    sigset_t blocked, previous_mask;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT); sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGHUP); sigaddset(&blocked, SIGQUIT);

    if (!options.dry_run) {
        /* Driver init installs its own handlers. Defer routine stop signals
         * until our override is installed, then unwind through normal cleanup. */
        if (pthread_sigmask(SIG_BLOCK, &blocked, &previous_mask)) goto cleanup;
        sys.force_fpga_reprogramming = false;
        cariboulite_set_log_level(cariboulite_log_level_info);
        int rc = cariboulite_init_driver(&sys, NULL);
        cariboulite_setup_signal_handler(&sys, driver_signal, signal_handler_op_override, NULL);
        int mask_error = pthread_sigmask(SIG_SETMASK, &previous_mask, NULL);
        if (mask_error) {
            fprintf(stderr, "Cannot restore signal mask: %s\n", strerror(mask_error));
            driver_ready = rc == 0;
            goto cleanup;
        }
        if (rc != 0) { fprintf(stderr, "Radio initialization failed (%d).\n", rc); goto cleanup; }
        driver_ready = true;
        if (stop_requested) { result = 0; goto cleanup; }
        radio = channel == at86rf215_rf_channel_900mhz ? &sys.radio_low : &sys.radio_high;
        control.radio = radio;
        if (!control_frequency_valid(&options, &sys, options.frequency)) {
            fprintf(stderr, "Frequency is outside this board/channel's range.\n"); goto cleanup;
        }
    }
    if (options.start_stopped) {
        if (backend_tune(&control, options.frequency) != 0) {
            fprintf(stderr, "Initial tuning/idle verification failed.\n"); goto cleanup;
        }
    } else {
        if (backend_start(&control) != 0) {
            fprintf(stderr, "RX pipeline initialization/tuning/start verification failed.\n"); goto cleanup;
        }
        options.rx_requested = true;
    }

    const double started = clock_seconds(CLOCK_MONOTONIC);
    double next_update = started, last_put_progress = started, last_get_progress = started;
    size_t previous_puts = 0, previous_gets = 0;
    uint64_t progress_generation = options.pipeline_generation;
    uint64_t published_config_generation = options.config_generation;
    uint64_t sequence = 0;
    char session[80], packet[STATUS_PUBLISHER_MAX_PACKET_BYTES];
    snprintf(session, sizeof(session), "radio-%ld-%" PRIu64, (long)getpid(), mono_ns());
    fprintf(stderr, "%s: %s %.6f MHz, %.0f S/s, %s. Ctrl+C stops the backend.\n",
        options.dry_run ? "SIMULATED TRANSPORT ONLY" : options.start_stopped ? "RX idle" : "RX started", options.channel,
        options.frequency / 1e6, (double)options.rx.fs_rf,
        options.rx.mode == AUDIO_DEMOD_NBFM ? "NBFM" : "WBFM mono");
    result = 0;
    while (!stop_requested) {
        double now = clock_seconds(CLOCK_MONOTONIC);
        if (options.seconds && now - started >= options.seconds) break;
        backend_poll_lifecycle(&control);
        if (control_server_service(&control_server, backend_control_command, &control) != 0) {
            fprintf(stderr, "Control socket service failed.\n"); result = 1; break;
        }
        /* Only the PTT-owning controller's disconnect releases transmission.
         * A replacement controller cannot inherit another client's PTT. */
        if (control.tx_controller_generation
            && control_server.disconnected_generation >= control.tx_controller_generation) {
            if (control.lifecycle_active && control.lifecycle_action == 1)
                control.stop_after_start = true;
            else if (!control.lifecycle_active && options.tx_requested) {
                if (backend_launch_lifecycle(&control, 2) != 0) {
                    options.control_fault = true; options.tx_error = "STOP_FAILED";
                    result = 1; break;
                }
                ++options.config_generation;
            }
        }
        if (control.lifecycle_active && now - control.lifecycle_started > 15
            && atomic_load_explicit(&control.lifecycle_tx_ready, memory_order_acquire)) {
            atomic_store_explicit(&control.lifecycle_no_resume, true, memory_order_release);
            tx_pipeline_request_abort(&tx_pipeline);
            options.control_fault = true; options.tx_error = "TRANSITION_TIMEOUT";
        }
        if (!options.dry_run && options.tx_requested && !control.lifecycle_active
            && control.tx_state_deadline && now >= control.tx_state_next_check
            && backend_tx_recheck(&control, now) != 0) { result = 1; break; }
        if (options.config_generation != published_config_generation) next_update = now;
        if (now >= next_update) {
            const char *state = options.dry_run ? "DRY_RUN" : backend_control_state(&options);
            rx_pipeline_stats_t stats = {0};
            tx_pipeline_stats_t txstats = {0};
            bool txstats_valid = false;
            if (!options.dry_run) {
                if (!control.lifecycle_active) {
                    modem_snapshot_refresh(&modem, &sys.modem, channel, &g_hw_lock);
                    rx_pipeline_get_stats(&pipeline, &stats);
                    if (tx_pipeline.inited) {
                        tx_pipeline_get_stats(&tx_pipeline, &txstats);
                        txstats_valid = true;
                    }
                }
                if (!options.rx_requested || progress_generation != options.pipeline_generation) {
                    last_put_progress = last_get_progress = now;
                    progress_generation = options.pipeline_generation;
                }
                if (stats.rxq.puts != previous_puts) last_put_progress = now;
                if (stats.rxq.gets != previous_gets) last_get_progress = now;
                previous_puts = stats.rxq.puts; previous_gets = stats.rxq.gets;
                if (options.rx_requested && !control.lifecycle_active) {
                    if (now - last_put_progress > 2 || now - last_get_progress > 2) state = "RX_STALLED";
                    if (modem.radio[3] < 0) state = "RX_UNCONFIRMED";
                    else if ((modem.radio[3] & 7) != 5) state = "RX_STATE_ERROR";
                    if (!stats.reader_worker.alive || !stats.demod_worker.alive || !stats.audio_worker.alive)
                        state = "RX_WORKER_ERROR";
                    if (strcmp(state, "RX") && !options.control_fault) {
                        options.control_fault = true; ++options.config_generation;
                    }
                }
                if (options.tx_requested && !control.lifecycle_active && txstats_valid) {
                    if (backend_tx_watchdog(&control, modem.radio[3], &txstats, now) != 0)
                        { result = 1; break; }
                    state = backend_control_state(&options);
                }
            }
            published_config_generation = options.config_generation;
            int length = make_snapshot(packet, sizeof(packet), &options, session, ++sequence,
                state, options.dry_run || control.lifecycle_active ? NULL : &pipeline,
                options.dry_run || control.lifecycle_active ? NULL : &stats,
                &modem, &publisher, control.lifecycle_active ? NULL : radio,
                txstats_valid && !control.lifecycle_active ? &txstats : NULL);
            if (length < 0 || status_publisher_publish(&publisher, packet, (size_t)length)) {
                fprintf(stderr, "Status snapshot publication failed.\n"); result = 1; break;
            }
            next_update = clock_seconds(CLOCK_MONOTONIC) + 1 / options.status_hz;
        }
        double wait = fmin(0.05, fmax(0, next_update - clock_seconds(CLOCK_MONOTONIC)));
        if (options.tx_requested && !control.lifecycle_active && control.tx_state_deadline)
            wait = fmin(wait, fmax(0, control.tx_state_next_check - clock_seconds(CLOCK_MONOTONIC)));
        struct timespec delay = {.tv_nsec = (long)(wait * 1e9)};
        nanosleep(&delay, NULL);
    }
    /* Stop an in-flight opening cue first, then use the normal closing cue.
     * Routine shutdown never resumes RX and never leaves the carrier keyed. */
    control.shutdown = true;
    if (control.lifecycle_active)
        atomic_store_explicit(&control.lifecycle_no_resume, true, memory_order_release);
    while (control.lifecycle_active) {
        backend_poll_lifecycle(&control);
        if (control.lifecycle_active && clock_seconds(CLOCK_MONOTONIC) - control.lifecycle_started > 15
            && atomic_load_explicit(&control.lifecycle_tx_ready, memory_order_acquire))
            tx_pipeline_request_abort(&tx_pipeline);
        struct timespec delay = {.tv_nsec = 10000000};
        nanosleep(&delay, NULL);
    }
    if (options.tx_requested) {
        if (backend_tx_idle(&control, false) != 0) result = 1;
        options.tx_requested = false;
    }
    options.rx_resume = false;
    if (!options.dry_run) {
        rx_pipeline_stop(&pipeline);
        modem_snapshot_refresh(&modem, &sys.modem, channel, &g_hw_lock);
    }
    options.rx_requested = false;
    rx_pipeline_stats_t final_stats = {0};
    if (pipeline.inited) rx_pipeline_get_stats(&pipeline, &final_stats);
    int length = make_snapshot(packet, sizeof(packet), &options, session, ++sequence,
        "STOPPED", options.dry_run ? NULL : &pipeline, options.dry_run ? NULL : &final_stats,
        &modem, &publisher, radio, NULL);
    if (length > 0) status_publisher_publish(&publisher, packet, (size_t)length);
    if (pipeline.inited) {
        fprintf(stderr, "RX queue: puts=%zu gets=%zu drops=%zu; SPI transactions=%" PRIu64
            " failures=%" PRIu64 ".\n", final_stats.rxq.puts, final_stats.rxq.gets, final_stats.rxq.drops,
            modem.spi_transactions, modem.spi_failures);
    }

cleanup:
    tx_pipeline_destroy(&tx_pipeline);
    rx_pipeline_destroy(&pipeline);
    if (driver_ready || sys.ownership_claimed) cariboulite_release_driver(&sys);
    control_server_close(&control_server);
    status_publisher_close(&publisher);
    return result;
}
