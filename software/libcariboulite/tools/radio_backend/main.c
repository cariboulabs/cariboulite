/* One radio owner, shared instrumented RX workers, receive-only status clients. */
#include "cariboulite_setup.h"
#include "rx_pipeline.h"
#include "pipeline_runtime.h"
#include "noise_squelch.h"
#include "modem_snapshot.h"
#include "status_publisher.h"

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    const char *channel, *playback, *socket_path;
    double frequency, seconds, status_hz;
    rx_params_t rx;
    bool dry_run;
} options_t;

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
        "Separate RX-only radio owner; starts reception automatically.\n"
        "  --channel RF24|RF09       default RF24 (HiF)\n"
        "  --frequency HZ            default 430125000\n"
        "  --sample-rate HZ          1000000, 2000000 or 4000000 (default 1000000)\n"
        "  --mode nbfm|wbfm          default nbfm; WBFM is mono\n"
        "  --playback DEVICE         default plughw:Loopback,0,0\n"
        "  --socket PATH             default /tmp/cariboulite-monitor-<UID>/status.sock\n"
        "  --seconds SECONDS         stop after this RX duration (default until Ctrl+C)\n"
        "  --status-hz HZ            0.5..20, default 5\n"
        "  --no-noise-squelch         bypass noise squelch\n"
        "  --carrier-squelch          enable carrier squelch (default off)\n"
        "  --noise-open RMS          default %.2f\n"
        "  --noise-close RMS         default %.2f\n"
        "  --gain GAIN               PCM gain, default 8000\n"
        "  --deemphasis-us US         50 or 75, default 50\n"
        "  --dry-run                  exercise status IPC without hardware or audio\n"
        "  --help                     show this help\n",
        program, (double)NOISE_SQUELCH_OPEN_RMS, (double)NOISE_SQUELCH_CLOSE_RMS);
}

static int number(const char *text, double *result, double low, double high)
{
    char *end;
    errno = 0;
    double value = strtod(text, &end);
    if (errno || end == text || *end || !isfinite(value) || value < low || value > high)
        return -1;
    *result = value;
    return 0;
}

static int parse_options(int argc, char **argv, options_t *o)
{
    *o = (options_t){
        .channel = "RF24", .playback = "plughw:Loopback,0,0",
        .frequency = 430125000, .status_hz = 5,
        .rx = {.freq_hz = 430125000, .fs_rf = 1000000, .fs_audio = 48000,
            .deemph_tau_s = 50e-6f, .pcm_gain = 8000,
            .noise_squelch_open_rms = NOISE_SQUELCH_OPEN_RMS,
            .noise_squelch_close_rms = NOISE_SQUELCH_CLOSE_RMS,
            .mode = AUDIO_DEMOD_NBFM},
    };
    enum { CHANNEL = 1000, FREQ, RATE, MODE, PLAYBACK, SOCKET, SECONDS,
        STATUS_HZ, NO_NOISE, CARRIER, NOISE_OPEN, NOISE_CLOSE, GAIN, DEEMPH, DRY_RUN };
    static const struct option long_options[] = {
        {"channel", required_argument, NULL, CHANNEL},
        {"frequency", required_argument, NULL, FREQ},
        {"sample-rate", required_argument, NULL, RATE},
        {"mode", required_argument, NULL, MODE},
        {"playback", required_argument, NULL, PLAYBACK},
        {"socket", required_argument, NULL, SOCKET},
        {"seconds", required_argument, NULL, SECONDS},
        {"status-hz", required_argument, NULL, STATUS_HZ},
        {"no-noise-squelch", no_argument, NULL, NO_NOISE},
        {"carrier-squelch", no_argument, NULL, CARRIER},
        {"noise-open", required_argument, NULL, NOISE_OPEN},
        {"noise-close", required_argument, NULL, NOISE_CLOSE},
        {"gain", required_argument, NULL, GAIN},
        {"deemphasis-us", required_argument, NULL, DEEMPH},
        {"dry-run", no_argument, NULL, DRY_RUN},
        {"help", no_argument, NULL, 'h'}, {0},
    };
    int key;
    double value;
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
                || (value != 1e6 && value != 2e6 && value != 4e6)) return -1;
            o->rx.fs_rf = (float)value; break;
        case MODE:
            if (!strcmp(optarg, "nbfm")) o->rx.mode = AUDIO_DEMOD_NBFM;
            else if (!strcmp(optarg, "wbfm")) o->rx.mode = AUDIO_DEMOD_WBFM;
            else return -1;
            break;
        case PLAYBACK:
            if (!*optarg || strlen(optarg) > 255) return -1;
            o->playback = optarg; break;
        case SOCKET: o->socket_path = optarg; break;
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
    if (!strcmp(o->channel, "RF09")
        && !((o->frequency >= CARIBOULITE_S1G_MIN1 && o->frequency <= CARIBOULITE_S1G_MAX1)
        || (o->frequency >= CARIBOULITE_S1G_MIN2 && o->frequency <= CARIBOULITE_S1G_MAX2)))
        return -1;
    o->rx.freq_hz = o->frequency;
    o->rx.pcm_dev = o->playback;
    return 0;
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

static int make_snapshot(char *out, size_t capacity, const options_t *o,
    const char *session, uint64_t sequence, const char *state, rx_pipeline_t *pipeline,
    const rx_pipeline_stats_t *stats,
    const modem_snapshot *modem, const status_publisher_t *publisher,
    const cariboulite_radio_state_st *radio)
{
    char hardware[2500], playback[1600], queue[600] = "null", rms[48] = "null";
    char audio_queue[600] = "null", playback_stats[700], reader[300] = "null";
    char reader_worker[1200], demod_worker[1200], audio_worker[1200];
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
    const double bandwidth = o->rx.mode == AUDIO_DEMOD_NBFM ? 160000 : 2000000;
    const double actual_freq = radio ? radio->actual_rf_frequency : o->frequency;
    int result = snprintf(out, capacity,
        "{\"version\":1,\"session\":\"%s\",\"sequence\":%" PRIu64 ","
        "\"monotonic_s\":%.9f,\"realtime_s\":%.6f,\"hardware_monotonic_s\":%.9f,"
        "\"source\":{\"kind\":\"%s\",\"label\":\"C RX backend%s\"},"
        "\"owner_pid\":%ld,\"channel\":\"%s\",\"state\":\"%s\",\"rx_mode\":\"%s\","
        "\"config\":{\"sample_rate_hz\":%.0f,\"rx_frequency_hz\":%.3f,"
        "\"requested_frequency_hz\":%.3f,\"rx_bandwidth_hz\":%.0f,"
        "\"playback\":%s,\"pcm_gain\":%.3f,\"deemphasis_us\":%.3f},"
        "\"rx\":{\"running\":%s,\"queue\":%s,\"noise_enabled\":%s,\"carrier_enabled\":%s,"
        "\"gate_open\":%s,\"noise\":{\"valid\":%s,\"detector_open\":%s,\"rms\":%s,"
        "\"open_rms\":%.3f,\"close_rms\":%.3f},"
        "\"audio_queue\":%s,\"playback\":%s,\"reader\":%s,"
        "\"workers\":{\"reader\":%s,\"demod\":%s,\"audio\":%s}},"
        "\"tx\":{\"running\":false},\"hardware\":%s,"
        "\"loopback\":{\"armed\":false,\"active\":false,\"samples\":0,\"timeouts\":0},"
        "\"publisher\":{\"status_hz\":%.3f,\"spi_blocks\":%u,\"spi_failed_blocks\":%u,"
        "\"spi_read_ms\":%.6f,\"spi_transactions\":%" PRIu64 ",\"spi_failures\":%" PRIu64 ","
        "\"clients\":%u,\"packets_sent\":%" PRIu64 ",\"packets_skipped\":%" PRIu64 ","
        "\"clients_rejected\":%" PRIu64 "}}",
        session, sequence, clock_seconds(CLOCK_MONOTONIC), clock_seconds(CLOCK_REALTIME),
        modem->monotonic_s, o->dry_run ? "simulated" : "live", o->dry_run ? " transport-only dry run" : "",
        (long)getpid(), o->channel, state, o->rx.mode == AUDIO_DEMOD_NBFM ? "NBFM" : "WBFM mono",
        (double)o->rx.fs_rf, actual_freq, o->frequency, bandwidth, playback,
        (double)o->rx.pcm_gain, o->rx.deemph_tau_s * 1e6,
        live && rx_pipeline_running(pipeline) ? "true" : "false", queue,
        noise_enabled ? "true" : "false", o->rx.carrier_squelch_enabled ? "true" : "false",
        live ? (rx_pipeline_squelch_open(pipeline) ? "true" : "false") : "null",
        noise.valid ? "true" : "false", noise.valid ? (noise.detector_open ? "true" : "false") : "null",
        rms, (double)noise.open_rms, (double)noise.close_rms, audio_queue, playback_stats, reader,
        reader_worker, demod_worker, audio_worker,
        hardware, o->status_hz, modem->read_blocks, modem->failed_blocks,
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
    CARIBOULITE_CONFIG_DEFAULT(sys);
    rx_pipeline_t pipeline = {0};
    cariboulite_radio_state_st *radio = NULL;
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
        if (rx_pipeline_init(&pipeline, &sys, radio, &options.rx) != 0
            || rx_pipeline_set_freq(&pipeline, options.frequency) != 0
            || rx_pipeline_start(&pipeline) != 0) {
            fprintf(stderr, "RX pipeline initialization/tuning/start failed.\n"); goto cleanup;
        }
        modem_snapshot_refresh(&modem, &sys.modem, channel, &g_hw_lock);
        /* STATE is radio[3] in the defined register inventory. */
        if (modem.radio[3] < 0 || (modem.radio[3] & 7) != 5) {
            fprintf(stderr, "Modem did not confirm RX state; stopping.\n"); goto cleanup;
        }
    }

    const double started = clock_seconds(CLOCK_MONOTONIC);
    double next_update = started, last_put_progress = started, last_get_progress = started;
    size_t previous_puts = 0, previous_gets = 0;
    uint64_t sequence = 0;
    char session[80], packet[8192];
    snprintf(session, sizeof(session), "rx-%ld-%" PRIu64, (long)getpid(), mono_ns());
    fprintf(stderr, "%s: %s %.6f MHz, %.0f S/s, %s. Ctrl+C stops the backend.\n",
        options.dry_run ? "SIMULATED TRANSPORT ONLY" : "RX started", options.channel,
        options.frequency / 1e6, (double)options.rx.fs_rf,
        options.rx.mode == AUDIO_DEMOD_NBFM ? "NBFM" : "WBFM mono");
    result = 0;
    while (!stop_requested) {
        double now = clock_seconds(CLOCK_MONOTONIC);
        if (options.seconds && now - started >= options.seconds) break;
        if (now >= next_update) {
            const char *state = options.dry_run ? "DRY_RUN" : "RX";
            rx_pipeline_stats_t stats = {0};
            if (!options.dry_run) {
                modem_snapshot_refresh(&modem, &sys.modem, channel, &g_hw_lock);
                rx_pipeline_get_stats(&pipeline, &stats);
                if (stats.rxq.puts != previous_puts) last_put_progress = now;
                if (stats.rxq.gets != previous_gets) last_get_progress = now;
                previous_puts = stats.rxq.puts; previous_gets = stats.rxq.gets;
                if (now - last_put_progress > 2 || now - last_get_progress > 2)
                    state = "RX_STALLED";
                if (modem.radio[3] < 0) state = "RX_UNCONFIRMED";
                else if ((modem.radio[3] & 7) != 5) state = "RX_STATE_ERROR";
            }
            int length = make_snapshot(packet, sizeof(packet), &options, session, ++sequence,
                state, options.dry_run ? NULL : &pipeline, options.dry_run ? NULL : &stats,
                &modem, &publisher, radio);
            if (length < 0 || status_publisher_publish(&publisher, packet, (size_t)length)) {
                fprintf(stderr, "Status snapshot publication failed.\n"); result = 1; break;
            }
            next_update = clock_seconds(CLOCK_MONOTONIC) + 1 / options.status_hz;
        }
        double wait = fmin(0.05, fmax(0, next_update - clock_seconds(CLOCK_MONOTONIC)));
        struct timespec delay = {.tv_nsec = (long)(wait * 1e9)};
        nanosleep(&delay, NULL);
    }
    if (!options.dry_run) {
        rx_pipeline_stop(&pipeline);
        modem_snapshot_refresh(&modem, &sys.modem, channel, &g_hw_lock);
    }
    rx_pipeline_stats_t final_stats = {0};
    if (pipeline.inited) rx_pipeline_get_stats(&pipeline, &final_stats);
    int length = make_snapshot(packet, sizeof(packet), &options, session, ++sequence,
        "STOPPED", options.dry_run ? NULL : &pipeline, options.dry_run ? NULL : &final_stats,
        &modem, &publisher, radio);
    if (length > 0) status_publisher_publish(&publisher, packet, (size_t)length);
    if (pipeline.inited) {
        fprintf(stderr, "RX queue: puts=%zu gets=%zu drops=%zu; SPI transactions=%" PRIu64
            " failures=%" PRIu64 ".\n", final_stats.rxq.puts, final_stats.rxq.gets, final_stats.rxq.drops,
            modem.spi_transactions, modem.spi_failures);
    }

cleanup:
    rx_pipeline_destroy(&pipeline);
    if (driver_ready || sys.ownership_claimed) cariboulite_release_driver(&sys);
    status_publisher_close(&publisher);
    return result;
}
