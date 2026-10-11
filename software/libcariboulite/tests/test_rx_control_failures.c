/* Run the real backend command handler against injected radio/pipeline faults.
 * No driver initialization, socket creation, ALSA or hardware operations run.
 * Link with function sections + gc-sections to discard its executable entry.
 */
#define _GNU_SOURCE
#define main unused_backend_main
#include "main.c"
#undef main
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>

pthread_mutex_t g_hw_lock = PTHREAD_MUTEX_INITIALIZER;

static bool fail_init, fail_frequency, fail_start, fail_stream, wrong_rx_state;
static bool reader_error, reader_no_progress, dead_demod, fail_levels;
static bool tune_new_fails, tune_all_fails, fail_idle;
static unsigned init_calls, destroy_calls, stop_calls, tune_calls, squelch_calls;
static bool tx_fail_init, tx_fail_tune, tx_fail_start, tx_fail_stream;
static bool tx_wrong_state, tx_bad_pac, tx_writer_error, tx_no_progress, tx_dead_writer;
static bool tx_state_read_error;
static bool tx_fail_reset;
static int tx_forced_state = -1;
static unsigned tx_transient_state, tx_transient_reads, tx_state_reads;
static bool modem_loopback;
static unsigned tx_init_calls, tx_stop_calls, tx_abort_calls, tx_destroy_calls;
static unsigned tx_reset_calls, tx_start_attempts, tx_start_abort_baseline;
static unsigned tx_diagnostic_calls, tx_diagnostic_fail_mask;
static uint64_t tx_samples;
static tx_pipeline_t mock_tx_pipeline;
static rx_pipeline_t *mock_rx_pipeline;
static unsigned modem_state = cariboulite_radio_state_cmd_trx_off;
static const double accepted_frequency = 430125000;
static const double rejected_frequency = 430130000;

enum fpga_event {
    FPGA_RADIO_IDLE, FPGA_SMI_IDLE, FPGA_MODEM_OFF, FPGA_HARD_RESET,
    FPGA_CONFIGURE, FPGA_SOFT_RESET, FPGA_WAIT, FPGA_VERSIONS, FPGA_LOW_POWER,
    FPGA_SPI_VERSION_0, FPGA_SPI_VERSION_1, FPGA_SPI_VERSION_2,
    FPGA_SPI_VERSION_3, FPGA_SPI_VERSION_4, FPGA_SPI_MODE,
    FPGA_FINAL_RADIO_IDLE, FPGA_FINAL_SMI_IDLE, FPGA_FINAL_MODEM_OFF
};
static bool fpga_test_active;
static int fpga_failure_event = -1;
static bool fpga_bad_manufacturer, fpga_bad_mode;
static bool fpga_reset_breaks_modem, fpga_modem_unavailable;
static bool fpga_modem_state_error, fpga_modem_stays_unavailable;
static bool fpga_configure_seen, fpga_program_readback_done;
static unsigned fpga_modem_transient_reads;
static unsigned fpga_events[1024], fpga_event_count;
static atomic_bool fpga_hold_reset, fpga_reset_entered;

static int fpga_record(unsigned event)
{
    assert(fpga_event_count < sizeof(fpga_events) / sizeof(fpga_events[0]));
    fpga_events[fpga_event_count++] = event;
    return fpga_failure_event == (int)event ? -1 : 0;
}

int cariboulite_radio_set_frequency(cariboulite_radio_state_st *radio,
                                    bool break_before_make, double *frequency)
{
    assert(break_before_make);
    ++tune_calls;
    /* Simulate a partial register update before a failed tuner operation. */
    radio->actual_rf_frequency = *frequency + 7.8125;
    if (tune_all_fails || (tune_new_fails && *frequency == rejected_frequency)) return -1;
    *frequency = radio->actual_rf_frequency;
    return 0;
}

int cariboulite_radio_activate_channel(cariboulite_radio_state_st *radio,
                                       cariboulite_channel_dir_en direction, bool active)
{
    (void)radio;
    assert((direction == cariboulite_channel_dir_rx || direction == cariboulite_channel_dir_tx)
           && !active);
    if (fpga_test_active) {
        /* The production deactivation wrapper discards modem command errors.
         * Shared SPI can remain unavailable despite its successful return. */
        if (!fpga_modem_unavailable) modem_state = cariboulite_radio_state_cmd_trx_off;
        return fpga_record(fpga_program_readback_done ? FPGA_FINAL_RADIO_IDLE : FPGA_RADIO_IDLE);
    }
    modem_state = cariboulite_radio_state_cmd_trx_off;
    return fail_idle ? -1 : 0;
}

int caribou_smi_set_driver_streaming_state(caribou_smi_st *device, smi_stream_state_en state)
{
    (void)device;
    assert(state == smi_stream_idle || state == smi_stream_rx_channel_0
           || state == smi_stream_rx_channel_1 || state == smi_stream_tx_channel);
    if (fpga_test_active) {
        assert(state == smi_stream_idle);
        return fpga_record(fpga_program_readback_done ? FPGA_FINAL_SMI_IDLE : FPGA_SMI_IDLE);
    }
    return state == smi_stream_tx_channel ? (tx_fail_stream ? -1 : 0)
        : state != smi_stream_idle && fail_stream ? -1 : 0;
}

int caribou_fpga_set_io_ctrl_mode(caribou_fpga_st *device, uint8_t debug,
                                 caribou_fpga_io_ctrl_rfm_en mode)
{
    (void)device;
    assert(debug == 0 && mode == caribou_fpga_io_ctrl_rfm_low_power);
    if (fpga_test_active) return fpga_record(FPGA_LOW_POWER);
    return 0;
}

int caribou_fpga_soft_reset(caribou_fpga_st *device)
{
    (void)device;
    assert(mock_rx_pipeline && !mock_rx_pipeline->inited && !mock_rx_pipeline->running);
    assert(!mock_tx_pipeline.inited && !mock_tx_pipeline.running);
    ++tx_reset_calls;
    if (fpga_test_active) {
        atomic_store_explicit(&fpga_reset_entered, true, memory_order_release);
        while (atomic_load_explicit(&fpga_hold_reset, memory_order_acquire)) {
            struct timespec delay = {.tv_nsec=1000000};
            nanosleep(&delay, NULL);
        }
        return fpga_record(FPGA_SOFT_RESET);
    }
    return tx_fail_reset ? -1 : 0;
}

int caribou_fpga_hard_reset(caribou_fpga_st *device)
{
    (void)device;
    assert(fpga_test_active);
    assert(!mock_rx_pipeline->inited && !mock_rx_pipeline->running);
    assert(!mock_tx_pipeline.inited && !mock_tx_pipeline.running);
    atomic_store_explicit(&fpga_reset_entered, true, memory_order_release);
    while (atomic_load_explicit(&fpga_hold_reset, memory_order_acquire)) {
        struct timespec delay = {.tv_nsec=1000000};
        nanosleep(&delay, NULL);
    }
    if (fpga_reset_breaks_modem) fpga_modem_unavailable = true;
    return fpga_record(FPGA_HARD_RESET);
}

int cariboulite_configure_fpga(sys_st *sys, cariboulite_firmware_source_en source,
                              const char *path)
{
    assert(fpga_test_active && sys->force_fpga_reprogramming == 1);
    assert(source == cariboulite_firmware_source_file);
    assert(!strcmp(path, "firmware/top.bin"));
    int rc = fpga_record(FPGA_CONFIGURE);
    if (rc == 0) {
        fpga_configure_seen = true;
        if (!fpga_modem_stays_unavailable) fpga_modem_unavailable = false;
    }
    return rc;
}

void io_utils_usleep(int usec)
{
    assert(fpga_test_active && usec == 100000);
    assert(fpga_record(FPGA_WAIT) == 0);
}

int caribou_fpga_get_versions(caribou_fpga_st *device, caribou_fpga_versions_st *versions)
{
    assert(fpga_test_active && versions == NULL);
    device->versions = (caribou_fpga_versions_st){.sys_ver=2,
        .sys_manu_id=CARIBOU_SDR_MANU_CODE, .sys_ctrl_mod_ver=1,
        .io_ctrl_mod_ver=1, .smi_ctrl_mod_ver=1};
    return fpga_record(FPGA_VERSIONS);
}

int io_utils_spi_transmit(io_utils_spi_st *device, int handle,
                         const unsigned char *tx, unsigned char *rx, size_t length,
                         io_utils_spi_dir_en direction)
{
    (void)device; (void)handle;
    static const unsigned char opcodes[] = {0x01, 0x02, 0x00, 0x20, 0x40, 0x21};
    unsigned index = 0;
    assert(fpga_test_active && length == 2 && direction == io_utils_spi_read_write);
    assert(tx[1] == 0);
    while (index < sizeof(opcodes) && opcodes[index] != tx[0]) ++index;
    assert(index < sizeof(opcodes));
    rx[0] = 0;
    rx[1] = index == 0 ? 2 : index == 1 ? (fpga_bad_manufacturer ? 0 : CARIBOU_SDR_MANU_CODE)
        : index == 5 ? (fpga_bad_mode ? caribou_fpga_io_ctrl_rfm_tx_lowpass : 0) : 1;
    int rc = fpga_record(FPGA_SPI_VERSION_0 + index);
    if (rc == 0 && index == sizeof(opcodes) - 1 && fpga_configure_seen)
        fpga_program_readback_done = true;
    return rc;
}

static int diagnostic_read(unsigned index)
{
    assert(tx_start_attempts && tx_abort_calls == tx_start_abort_baseline);
    ++tx_diagnostic_calls;
    return (tx_diagnostic_fail_mask & (1u << index)) ? -1 : 0;
}

int caribou_fpga_get_smi_ctrl_fifo_status(caribou_fpga_st *device,
                                         caribou_fpga_smi_fifo_status_st *status)
{
    (void)device;
    *status = (caribou_fpga_smi_fifo_status_st){.rx_fifo_empty=1, .tx_fifo_full=1,
        .smi_channel=1, .smi_direction=0};
    return diagnostic_read(1);
}

int caribou_fpga_get_io_ctrl_mode(caribou_fpga_st *device, uint8_t *debug,
                                 caribou_fpga_io_ctrl_rfm_en *mode)
{
    (void)device;
    *debug = 0;
    *mode = caribou_fpga_io_ctrl_rfm_tx_lowpass;
    return diagnostic_read(2);
}

int caribou_fpga_get_sys_ctrl_tx_sample_gap(caribou_fpga_st *device, uint8_t *gap)
{
    (void)device;
    *gap = 3;
    return diagnostic_read(3);
}

int caribou_fpga_get_io_ctrl_rf_state(caribou_fpga_st *device,
                                     caribou_fpga_rf_pin_st *pins)
{
    (void)device;
    *pins = (caribou_fpga_rf_pin_st){.lna_tx_shdn=1, .trvc1=1};
    return diagnostic_read(4);
}

int at86rf215_read_buffer(at86rf215_st *device, uint16_t reg, uint8_t *buffer, uint8_t size)
{
    (void)device;
    if (reg == REG_RF_IQIFC0 && size == 3) {
        buffer[0] = 0x33; buffer[1] = 0x12; buffer[2] = 0x0b;
        return diagnostic_read(0);
    }
    assert(size == 1);
    if (fpga_test_active && (reg == REG_RF24_STATE || reg == REG_RF09_STATE)) {
        int rc = fpga_record(fpga_program_readback_done ? FPGA_FINAL_MODEM_OFF : FPGA_MODEM_OFF);
        if (fpga_modem_unavailable) {
            *buffer = cariboulite_radio_state_cmd_reset;
            return rc ? rc : fpga_modem_state_error ? -1 : 0;
        }
        if (fpga_modem_transient_reads) {
            --fpga_modem_transient_reads;
            *buffer = cariboulite_radio_state_transition;
            return rc;
        }
        *buffer = cariboulite_radio_state_cmd_trx_off;
        return rc;
    }
    if (reg == REG_RF_IQIFC0) { *buffer = modem_loopback ? 0x80 : 0; return 0; }
    if (reg == REG_RF24_PAC || reg == REG_RF09_PAC) {
        *buffer = tx_bad_pac ? 1 : 0; return 0;
    }
    assert(reg == REG_RF24_STATE || reg == REG_RF09_STATE);
    if (modem_state == cariboulite_radio_state_cmd_tx) {
        ++tx_state_reads;
        if (tx_state_read_error) return -1;
        if (tx_transient_reads) {
            --tx_transient_reads;
            *buffer = tx_transient_state;
            return 0;
        }
        if (tx_forced_state >= 0) { *buffer = tx_forced_state; return 0; }
    }
    *buffer = wrong_rx_state && modem_state == cariboulite_radio_state_cmd_rx
        ? cariboulite_radio_state_cmd_trx_off
        : tx_wrong_state && modem_state == cariboulite_radio_state_cmd_tx
        ? cariboulite_radio_state_cmd_trx_off : modem_state;
    return 0;
}

int rx_pipeline_init(rx_pipeline_t *pipeline, sys_st *sys,
                     cariboulite_radio_state_st *radio, const rx_params_t *params)
{
    (void)params;
    ++init_calls;
    if (fail_init) return -1;
    pipeline->inited = true;
    pipeline->sys = sys;
    pipeline->radio = radio;
    return 0;
}

int rx_pipeline_set_freq(rx_pipeline_t *pipeline, double frequency)
{
    if (fail_frequency) return -1;
    return cariboulite_radio_set_frequency(pipeline->radio, true, &frequency);
}

int rx_pipeline_start(rx_pipeline_t *pipeline)
{
    if (fail_start) return -1;
    assert(pipeline->inited);
    pipeline->running = true;
    modem_state = cariboulite_radio_state_cmd_rx;
    return 0;
}

void rx_pipeline_stop(rx_pipeline_t *pipeline)
{
    ++stop_calls;
    pipeline->running = false;
}

void rx_pipeline_destroy(rx_pipeline_t *pipeline)
{
    ++destroy_calls;
    memset(pipeline, 0, sizeof(*pipeline));
}

void rx_pipeline_get_stats(rx_pipeline_t *pipeline, rx_pipeline_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->reader.samples = reader_no_progress ? 0 : 1;
    stats->reader.errors = reader_error ? 1 : 0;
    stats->reader_worker.alive = pipeline->running;
    stats->demod_worker.alive = pipeline->running && !dead_demod;
    stats->audio_worker.alive = pipeline->running;
}

int rx_pipeline_set_noise_squelch_levels(rx_pipeline_t *pipeline, float open, float close)
{
    (void)pipeline; (void)open; (void)close;
    return fail_levels ? -1 : 0;
}

void rx_pipeline_set_squelch(rx_pipeline_t *pipeline, bool noise, bool carrier)
{
    (void)pipeline; (void)noise; (void)carrier;
    ++squelch_calls;
}

int tx_pipeline_init(tx_pipeline_t *pipeline, sys_st *sys,
                     cariboulite_radio_state_st *radio, const tx_params_t *params)
{
    assert(tx_reset_calls && !tx_fail_reset);
    assert(!pipeline->inited && !mock_rx_pipeline->inited);
    ++tx_init_calls;
    assert(params->f_dev_hz == 2500 && params->tx_power_dbm == -12);
    if (tx_fail_init) return -1;
    atomic_store(&pipeline->tx_ctrl.last_error, 0);
    pipeline->inited = true;
    pipeline->sys = sys;
    pipeline->radio = radio;
    return 0;
}

int tx_pipeline_set_freq_power(tx_pipeline_t *pipeline, double frequency, int power)
{
    assert(power == -12); /* RF24 minimum maps to PAC code zero. */
    if (tx_fail_tune) return -1;
    return cariboulite_radio_set_frequency(pipeline->radio, true, &frequency);
}

int tx_pipeline_start(tx_pipeline_t *pipeline)
{
    assert(pipeline->inited);
    ++tx_start_attempts;
    tx_start_abort_baseline = tx_abort_calls;
    if (tx_fail_start || atomic_load(&pipeline->tx_ctrl.last_error)) return -1;
    pipeline->running = true;
    modem_state = cariboulite_radio_state_cmd_tx;
    return 0;
}

void tx_pipeline_stop(tx_pipeline_t *pipeline)
{
    ++tx_stop_calls;
    pipeline->running = false;
    modem_state = cariboulite_radio_state_cmd_trx_off;
}

int tx_pipeline_abort(tx_pipeline_t *pipeline)
{
    ++tx_abort_calls;
    pipeline->running = false;
    modem_state = cariboulite_radio_state_cmd_trx_off;
    return fail_idle ? -1 : 0;
}

void tx_pipeline_destroy(tx_pipeline_t *pipeline)
{
    ++tx_destroy_calls;
    memset(pipeline, 0, sizeof(*pipeline));
}

bool tx_pipeline_healthy(const tx_pipeline_t *pipeline)
{
    return pipeline->inited && !tx_writer_error && !tx_dead_writer
        && !atomic_load(&pipeline->tx_ctrl.last_error);
}

void tx_pipeline_get_stats(tx_pipeline_t *pipeline, tx_pipeline_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
    if (pipeline->running && !tx_no_progress) ++tx_samples;
    stats->writer.samples = tx_samples;
    stats->writer.errors = tx_writer_error ? 1 : 0;
    stats->writer.last_error = atomic_load(&pipeline->tx_ctrl.last_error);
    stats->producer_worker.alive = pipeline->running;
    stats->writer_worker.alive = pipeline->running && !tx_dead_writer;
}

static void reset_faults(void)
{
    fail_init = fail_frequency = fail_start = fail_stream = wrong_rx_state = false;
    reader_error = reader_no_progress = dead_demod = fail_levels = false;
    tune_new_fails = tune_all_fails = fail_idle = false;
    tx_fail_init = tx_fail_tune = tx_fail_start = tx_fail_stream = false;
    tx_wrong_state = tx_bad_pac = tx_writer_error = tx_no_progress = tx_dead_writer = false;
    tx_state_read_error = false;
    tx_fail_reset = false;
    tx_forced_state = -1;
    tx_transient_state = tx_transient_reads = tx_state_reads = 0;
    modem_loopback = false;
    init_calls = destroy_calls = stop_calls = tune_calls = squelch_calls = 0;
    tx_init_calls = tx_stop_calls = tx_abort_calls = tx_destroy_calls = 0;
    tx_reset_calls = tx_start_attempts = tx_start_abort_baseline = 0;
    tx_diagnostic_calls = tx_diagnostic_fail_mask = 0;
    tx_samples = 0;
    memset(&mock_tx_pipeline, 0, sizeof(mock_tx_pipeline));
    modem_state = cariboulite_radio_state_cmd_trx_off;
    stop_requested = 0;
    fpga_test_active = fpga_bad_manufacturer = fpga_bad_mode = false;
    fpga_reset_breaks_modem = fpga_modem_unavailable = false;
    fpga_modem_state_error = fpga_modem_stays_unavailable = false;
    fpga_configure_seen = fpga_program_readback_done = false;
    fpga_modem_transient_reads = 0;
    fpga_failure_event = -1;
    fpga_event_count = 0;
    atomic_init(&fpga_hold_reset, false);
    atomic_init(&fpga_reset_entered, false);
}

static backend_control_t context(options_t *options, sys_st *sys,
                                 cariboulite_radio_state_st *radio, rx_pipeline_t *pipeline)
{
    reset_faults();
    *options = (options_t){.channel="RF24", .frequency=accepted_frequency,
        .config_generation=7, .pipeline_generation=3,
        .microphone="unused-fixture-microphone",
        .tx={.freq_hz=accepted_frequency, .tx_power_dbm=-12, .rf_fs=1000000,
             .tone_hz=600, .tone_amp=.4f, .f_dev_hz=2500},
        .rx={.freq_hz=accepted_frequency, .fs_rf=1000000, .mode=AUDIO_DEMOD_NBFM,
             .noise_squelch_open_rms=.20f, .noise_squelch_close_rms=.30f}};
    memset(sys, 0, sizeof(*sys));
    sys->board_info.numeric_product_id = system_type_cariboulite_full;
    memset(radio, 0, sizeof(*radio));
    radio->actual_rf_frequency = accepted_frequency + 7.8125;
    memset(pipeline, 0, sizeof(*pipeline));
    mock_rx_pipeline = pipeline;
    return (backend_control_t){.options=options, .sys=sys, .radio=radio,
        .pipeline=pipeline, .tx_pipeline=&mock_tx_pipeline};
}

static control_reply_t tx_command(backend_control_t *owner, control_command_t requested,
                                  const char *code, const char *state)
{
    control_reply_t reply;
    control_request_t request = {.id=43, .protocol_version=2, .command=requested};
    if (!owner->lifecycle_active && !strcmp(code, "ACCEPTED")
        && (requested == CONTROL_FPGA_PROGRAM || requested == CONTROL_FPGA_SOFT_RESET
            || requested == CONTROL_FPGA_HARD_RESET)) {
        /* Instrumentation phases start afresh; the modeled lost modem access
         * persists across jobs until the bitstream is successfully loaded. */
        fpga_configure_seen = fpga_program_readback_done = false;
    }
    backend_control_command(owner, &request, &reply);
    assert(reply.id == request.id && !strcmp(reply.code, code) && !strcmp(reply.state, state));
    assert(reply.ok == (!strcmp(code, "OK") || !strcmp(code, "ACCEPTED")));
    assert(!reply.simulated && reply.tx_requested_frequency_hz == accepted_frequency);
    assert(reply.tx_requested_power_dbm == -12 && reply.tx_sample_rate_hz == 1000000);
    assert(reply.tx_deviation_hz == 2500 && !reply.tx_tone_mode);
    return reply;
}

static void finish_lifecycle(backend_control_t *owner)
{
    const double deadline = clock_seconds(CLOCK_MONOTONIC) + 2;
    while (owner->lifecycle_active && clock_seconds(CLOCK_MONOTONIC) < deadline) {
        backend_poll_lifecycle(owner);
        struct timespec delay = {.tv_nsec=1000000};
        nanosleep(&delay, NULL);
    }
    assert(!owner->lifecycle_active);
}

static control_reply_t command(backend_control_t *owner, control_request_t request,
                               const char *code, const char *state)
{
    control_reply_t reply;
    request.id = 42;
    backend_control_command(owner, &request, &reply);
    assert(reply.id == request.id && !strcmp(reply.code, code) && !strcmp(reply.state, state));
    assert(reply.ok == !strcmp(code, "OK") && !reply.simulated);
    assert(reply.requested_frequency_hz == accepted_frequency);
    assert(reply.sample_rate_hz == 1000000 && !reply.wbfm);
    assert(reply.noise_enabled && !reply.carrier_enabled);
    assert(reply.noise_open_rms == .20f && reply.noise_close_rms == .30f);
    return reply;
}

static void write_test_firmware(void)
{
    FILE *file = fopen("firmware/top.bin", "wb");
    assert(file && fwrite("mock-only-bitstream", 1, 19, file) == 19);
    assert(fclose(file) == 0);
}

static void assert_fpga_sequence(bool recovery, bool final_idle)
{
    unsigned offset = 0;
    if (recovery) assert(fpga_events[offset++] == FPGA_SMI_IDLE);
    else for (unsigned i = FPGA_RADIO_IDLE; i <= FPGA_MODEM_OFF; ++i)
        assert(fpga_events[offset++] == i);
    for (unsigned i = FPGA_HARD_RESET; i <= FPGA_SPI_MODE; ++i)
        assert(fpga_events[offset++] == i);
    if (final_idle) for (unsigned i = FPGA_FINAL_RADIO_IDLE; i <= FPGA_FINAL_MODEM_OFF; ++i)
        assert(fpga_events[offset++] == i);
    assert(fpga_event_count == offset);
}

static void assert_fpga_reset_sequence(bool hard, bool recovery)
{
    static const unsigned soft[] = {FPGA_RADIO_IDLE, FPGA_SMI_IDLE, FPGA_MODEM_OFF,
        FPGA_SOFT_RESET, FPGA_LOW_POWER, FPGA_SPI_VERSION_0, FPGA_SPI_VERSION_1,
        FPGA_SPI_VERSION_2, FPGA_SPI_VERSION_3, FPGA_SPI_VERSION_4, FPGA_SPI_MODE};
    static const unsigned hard_events[] = {FPGA_RADIO_IDLE, FPGA_SMI_IDLE,
        FPGA_MODEM_OFF, FPGA_HARD_RESET};
    const unsigned *events = hard ? hard_events : soft;
    unsigned count = hard ? sizeof(hard_events) / sizeof(hard_events[0])
        : sizeof(soft) / sizeof(soft[0]);
    unsigned offset = 0;
    if (recovery) {
        assert(hard && fpga_events[offset++] == FPGA_SMI_IDLE);
        assert(fpga_events[offset++] == FPGA_HARD_RESET);
    } else for (unsigned i = 0; i < count; ++i) assert(fpga_events[offset++] == events[i]);
    assert(fpga_event_count == offset);
}

static void test_fpga_resets(void)
{
    options_t options;
    sys_st sys;
    cariboulite_radio_state_st radio;
    rx_pipeline_t pipeline;
    int previous_directory = open(".", O_RDONLY | O_DIRECTORY);
    char directory[] = "/tmp/cl-fpga-resets-XXXXXX";
    assert(previous_directory >= 0 && mkdtemp(directory));
    assert(chdir(directory) == 0); /* Neither reset depends on a firmware file. */

    for (unsigned hard = 0; hard < 2; ++hard) {
        control_command_t reset = hard ? CONTROL_FPGA_HARD_RESET : CONTROL_FPGA_SOFT_RESET;
        backend_control_t owner = context(&options, &sys, &radio, &pipeline);
        fpga_test_active = true;
        options.rx_requested = true;
        tx_command(&owner, reset, "RX_RUNNING", "RX");
        options.rx_requested = false;
        pipeline.running = true;
        tx_command(&owner, reset, "RX_RUNNING", "IDLE");
        pipeline.running = false;
        options.tx_requested = true;
        tx_command(&owner, reset, "TX_RUNNING", "TX");
        options.tx_requested = false;
        mock_tx_pipeline.running = true;
        tx_command(&owner, reset, "TX_RUNNING", "IDLE");
        mock_tx_pipeline.running = false;
        assert(fpga_event_count == 0 && !owner.lifecycle_active);

        /* Parked pipelines must be destroyed before either hardware reset. */
        pipeline.inited = mock_tx_pipeline.inited = true;
        options.tx_initialized = true;
        sys.fpga.versions = (caribou_fpga_versions_st){.sys_ver=9,
            .sys_manu_id=CARIBOU_SDR_MANU_CODE, .sys_ctrl_mod_ver=9,
            .io_ctrl_mod_ver=9, .smi_ctrl_mod_ver=9};
        sys.fpga.status = caribou_fpga_status_operational;
        tx_command(&owner, reset, "ACCEPTED", "FPGA_RESETTING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", hard ? "FPGA_NEEDS_PROGRAM" : "IDLE");
        assert_fpga_reset_sequence(hard, false);
        assert(!options.control_fault && options.fpga_needs_programming == (bool)hard);
        assert(!options.rx_requested && !options.tx_requested && !options.rx_resume);
        assert(!options.tx_initialized && !pipeline.inited && !mock_tx_pipeline.inited);
        assert(options.pipeline_generation == 3 && options.tx_pipeline_generation == 0);
        assert(stop_calls == 1 && destroy_calls == 1 && tx_destroy_calls == 1);
        assert(sys.force_fpga_reprogramming == 0 && !options.tx_error);
        if (hard) {
            caribou_fpga_versions_st zero = {0};
            assert(!memcmp(&sys.fpga.versions, &zero, sizeof(zero)));
            assert(sys.fpga.status == caribou_fpga_status_not_programmed);
        } else {
            assert(sys.fpga.versions.sys_ver == 2);
            assert(sys.fpga.versions.sys_manu_id == CARIBOU_SDR_MANU_CODE);
        }
    }

    /* Every soft-reset/verification failure reports FAULT; an explicitly
     * retried soft reset recovers without reading or loading any file. */
    static const unsigned soft_failures[] = {FPGA_RADIO_IDLE, FPGA_SMI_IDLE,
        FPGA_MODEM_OFF, FPGA_SOFT_RESET, FPGA_LOW_POWER, FPGA_SPI_VERSION_0,
        FPGA_SPI_VERSION_1, FPGA_SPI_VERSION_2, FPGA_SPI_VERSION_3,
        FPGA_SPI_VERSION_4, FPGA_SPI_MODE};
    for (unsigned i = 0; i < sizeof(soft_failures) / sizeof(soft_failures[0]); ++i) {
        backend_control_t owner = context(&options, &sys, &radio, &pipeline);
        fpga_test_active = true;
        fpga_failure_event = (int)soft_failures[i];
        tx_command(&owner, CONTROL_FPGA_SOFT_RESET, "ACCEPTED", "FPGA_RESETTING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "FAULT");
        assert(!options.fpga_needs_programming && !options.rx_requested && !options.tx_requested);
        assert(fpga_event_count == (i < 3 ? 3 : i + 1));
        fpga_failure_event = -1;
        fpga_event_count = 0;
        tx_command(&owner, CONTROL_FPGA_SOFT_RESET, "ACCEPTED", "FPGA_RESETTING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "IDLE");
        assert_fpga_reset_sequence(false, false);
        assert(!options.control_fault);
    }
    for (unsigned bad = 0; bad < 2; ++bad) {
        backend_control_t owner = context(&options, &sys, &radio, &pipeline);
        fpga_test_active = true;
        fpga_bad_manufacturer = bad == 0;
        fpga_bad_mode = bad == 1;
        tx_command(&owner, CONTROL_FPGA_SOFT_RESET, "ACCEPTED", "FPGA_RESETTING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "FAULT");
        assert_fpga_reset_sequence(false, false);
    }

    /* Hard reset only attempts the hardware reset after idle verification.
     * Once attempted, its uncertain configuration cannot be recovered by STOP. */
    for (unsigned failure = FPGA_RADIO_IDLE; failure <= FPGA_HARD_RESET; ++failure) {
        backend_control_t owner = context(&options, &sys, &radio, &pipeline);
        fpga_test_active = true;
        fpga_failure_event = (int)failure;
        tx_command(&owner, CONTROL_FPGA_HARD_RESET, "ACCEPTED", "FPGA_RESETTING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "FAULT");
        assert(options.fpga_needs_programming == (failure == FPGA_HARD_RESET));
        assert(fpga_event_count == (failure < FPGA_HARD_RESET ? 3u : 4u));
        fpga_failure_event = -1;
        fpga_event_count = 0;
        if (failure == FPGA_HARD_RESET) {
            unsigned generation = options.config_generation;
            tx_command(&owner, CONTROL_STOP, "OK", "FAULT");
            tx_command(&owner, CONTROL_TX_STOP, "OK", "FAULT");
            assert(options.config_generation == generation && fpga_event_count == 0);
        }
        tx_command(&owner, CONTROL_FPGA_HARD_RESET, "ACCEPTED", "FPGA_RESETTING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "FPGA_NEEDS_PROGRAM");
        assert_fpga_reset_sequence(true, failure == FPGA_HARD_RESET);
    }

    /* Both reset jobs block every edit while held inside the actual reset
     * mock. Controller loss or shutdown cannot make them start or abort TX. */
    for (unsigned hard = 0; hard < 2; ++hard) {
        backend_control_t owner = context(&options, &sys, &radio, &pipeline);
        fpga_test_active = true;
        atomic_store(&fpga_hold_reset, true);
        tx_command(&owner, hard ? CONTROL_FPGA_HARD_RESET : CONTROL_FPGA_SOFT_RESET,
            "ACCEPTED", "FPGA_RESETTING");
        const double deadline = clock_seconds(CLOCK_MONOTONIC) + 2;
        while (!atomic_load_explicit(&fpga_reset_entered, memory_order_acquire)
            && clock_seconds(CLOCK_MONOTONIC) < deadline) {
            struct timespec delay = {.tv_nsec=1000000};
            nanosleep(&delay, NULL);
        }
        assert(atomic_load(&fpga_reset_entered));
        static const control_command_t edits[] = {CONTROL_FPGA_PROGRAM,
            CONTROL_FPGA_SOFT_RESET, CONTROL_FPGA_HARD_RESET, CONTROL_START,
            CONTROL_STOP, CONTROL_FREQUENCY, CONTROL_MODE, CONTROL_SAMPLE_RATE,
            CONTROL_SQUELCH, CONTROL_TX_START, CONTROL_TX_STOP, CONTROL_TX_POWER};
        for (unsigned i = 0; i < sizeof(edits) / sizeof(edits[0]); ++i)
            tx_command(&owner, edits[i], "TRANSITION", "FPGA_RESETTING");
        tx_command(&owner, CONTROL_GET, "OK", "FPGA_RESETTING");
        assert(!owner.stop_after_start && !atomic_load(&owner.lifecycle_tx_ready));
        owner.shutdown = true;
        atomic_store(&owner.lifecycle_no_resume, true);
        atomic_store_explicit(&fpga_hold_reset, false, memory_order_release);
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", hard ? "FPGA_NEEDS_PROGRAM" : "IDLE");
        assert_fpga_reset_sequence(hard, false);
        assert(tx_abort_calls == 0 && !options.rx_requested && !options.tx_requested);
    }

    /* A hard reset conservatively requires the approved bitstream, even if
     * the board's flash automatically supplies an operational older image. */
    backend_control_t owner = context(&options, &sys, &radio, &pipeline);
    fpga_test_active = true;
    tx_command(&owner, CONTROL_FPGA_HARD_RESET, "ACCEPTED", "FPGA_RESETTING");
    finish_lifecycle(&owner);
    unsigned event_count = fpga_event_count;
    unsigned generation = options.config_generation;
    static const control_command_t blocked[] = {CONTROL_FPGA_SOFT_RESET, CONTROL_START,
        CONTROL_FREQUENCY, CONTROL_MODE, CONTROL_SAMPLE_RATE, CONTROL_SQUELCH,
        CONTROL_TX_START, CONTROL_TX_FREQUENCY, CONTROL_TX_POWER, CONTROL_TX_SAMPLE_RATE,
        CONTROL_TX_SOURCE, CONTROL_TX_TONE};
    for (unsigned i = 0; i < sizeof(blocked) / sizeof(blocked[0]); ++i)
        tx_command(&owner, blocked[i], "FPGA_REQUIRED", "FPGA_NEEDS_PROGRAM");
    tx_command(&owner, CONTROL_STOP, "OK", "FPGA_NEEDS_PROGRAM");
    tx_command(&owner, CONTROL_TX_STOP, "OK", "FPGA_NEEDS_PROGRAM");
    tx_command(&owner, CONTROL_FPGA_PROGRAM, "FIRMWARE_FILE", "FPGA_NEEDS_PROGRAM");
    assert(fpga_event_count == event_count && options.config_generation == generation);
    assert(options.fpga_needs_programming && !options.control_fault);
    assert(mkdir("firmware", 0700) == 0);
    write_test_firmware();
    fpga_failure_event = FPGA_CONFIGURE;
    tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
    finish_lifecycle(&owner);
    tx_command(&owner, CONTROL_GET, "OK", "FAULT");
    assert(options.fpga_needs_programming);
    fpga_failure_event = -1;
    fpga_event_count = 0;
    tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
    finish_lifecycle(&owner);
    tx_command(&owner, CONTROL_GET, "OK", "IDLE");
    assert_fpga_sequence(true, true);
    assert(!options.fpga_needs_programming && !options.control_fault);
    fpga_test_active = false;
    tx_command(&owner, CONTROL_START, "OK", "RX");
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    finish_lifecycle(&owner);
    tx_command(&owner, CONTROL_GET, "OK", "TX");
    tx_command(&owner, CONTROL_TX_STOP, "ACCEPTED", "TX_STOPPING");
    finish_lifecycle(&owner);
    tx_command(&owner, CONTROL_GET, "OK", "RX");
    tx_command(&owner, CONTROL_STOP, "OK", "IDLE");

    assert(unlink("firmware/top.bin") == 0 && rmdir("firmware") == 0);
    assert(fchdir(previous_directory) == 0 && close(previous_directory) == 0);
    assert(rmdir(directory) == 0);
    puts("FPGA reset sequences, guards, stage faults and programming recovery passed");
}

static void test_fpga_programming(void)
{
    options_t options;
    sys_st sys;
    cariboulite_radio_state_st radio;
    rx_pipeline_t pipeline;
    int previous_directory = open(".", O_RDONLY | O_DIRECTORY);
    char directory[] = "/tmp/cl-fpga-faults-XXXXXX";
    assert(previous_directory >= 0 && mkdtemp(directory));
    assert(chdir(directory) == 0 && mkdir("firmware", 0700) == 0);
    backend_control_t owner = context(&options, &sys, &radio, &pipeline);
    fpga_test_active = true;

    /* Missing, empty and nonregular firmware must leave hardware untouched. */
    assert(backend_fpga_firmware_ready() == -1);
    tx_command(&owner, CONTROL_FPGA_PROGRAM, "FIRMWARE_FILE", "IDLE");
    assert(options.config_generation == 7 && !owner.lifecycle_active);
    assert(backend_fpga_program(&owner) == -1 && fpga_event_count == 0);
    assert(destroy_calls == 0 && tx_destroy_calls == 0 && stop_calls == 0);
    FILE *file = fopen("firmware/top.bin", "wb");
    assert(file && fclose(file) == 0);
    assert(backend_fpga_firmware_ready() == -1);
    assert(unlink("firmware/top.bin") == 0 && mkdir("firmware/top.bin", 0700) == 0);
    assert(backend_fpga_firmware_ready() == -1);
    assert(rmdir("firmware/top.bin") == 0 && mkfifo("firmware/top.bin", 0600) == 0);
    /* A FIFO with no writer must be rejected immediately, without blocking. */
    assert(backend_fpga_firmware_ready() == -1);
    assert(unlink("firmware/top.bin") == 0);
    write_test_firmware();
    assert(backend_fpga_firmware_ready() == 0);

    /* Both requested intent and actual live pipelines forbid reset. */
    options.rx_requested = true;
    tx_command(&owner, CONTROL_FPGA_PROGRAM, "RX_RUNNING", "RX");
    options.rx_requested = false;
    pipeline.running = true;
    tx_command(&owner, CONTROL_FPGA_PROGRAM, "RX_RUNNING", "IDLE");
    pipeline.running = false;
    options.tx_requested = true;
    tx_command(&owner, CONTROL_FPGA_PROGRAM, "TX_RUNNING", "TX");
    options.tx_requested = false;
    mock_tx_pipeline.running = true;
    tx_command(&owner, CONTROL_FPGA_PROGRAM, "TX_RUNNING", "IDLE");
    mock_tx_pipeline.running = false;
    assert(fpga_event_count == 0 && !owner.lifecycle_active);

    /* Parked workers are joined before the exact menu-3 firmware sequence. */
    pipeline.inited = mock_tx_pipeline.inited = true;
    options.tx_initialized = true;
    assert(backend_fpga_program(&owner) == 0);
    assert_fpga_sequence(false, true);
    assert(sys.force_fpga_reprogramming == 1 && !options.tx_initialized);
    assert(!pipeline.inited && !mock_tx_pipeline.inited);
    assert(stop_calls == 1 && destroy_calls == 1 && tx_destroy_calls == 1);
    assert(sys.fpga.versions.sys_ver == 2 && sys.fpga.versions.sys_manu_id == CARIBOU_SDR_MANU_CODE);

    /* Every checked stage fails visibly and permits an explicit fresh retry. */
    for (unsigned failure = FPGA_RADIO_IDLE; failure <= FPGA_SPI_MODE; ++failure) {
        if (failure == FPGA_WAIT) continue; /* The menu's delay returns void. */
        owner = context(&options, &sys, &radio, &pipeline);
        fpga_test_active = true;
        fpga_failure_event = (int)failure;
        pipeline.inited = mock_tx_pipeline.inited = true;
        options.tx_initialized = true;
        tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "FAULT");
        assert(!options.rx_requested && !options.tx_requested && !options.rx_resume);
        assert(!options.tx_initialized && !pipeline.inited && !mock_tx_pipeline.inited);
        assert(!options.tx_error && owner.tx_configuration_dirty);
        assert(options.fpga_needs_programming == (failure >= FPGA_HARD_RESET));
        assert(stop_calls == 1 && destroy_calls == 1 && tx_destroy_calls == 1);
        if (failure < FPGA_HARD_RESET) assert(fpga_event_count == FPGA_HARD_RESET);
        else assert(fpga_event_count == failure + 1);
        fpga_failure_event = -1;
        fpga_event_count = 0;
        tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "IDLE");
        assert_fpga_sequence(failure >= FPGA_HARD_RESET, true);
        assert(!options.control_fault && !options.rx_requested && !options.tx_requested);
        assert(!options.fpga_needs_programming);
        assert(options.pipeline_generation == 3 && options.tx_pipeline_generation == 0);
    }

    /* Successful legacy getters cannot hide bad checked completion data. */
    for (unsigned corrupt = 0; corrupt < 2; ++corrupt) {
        owner = context(&options, &sys, &radio, &pipeline);
        fpga_test_active = true;
        fpga_bad_manufacturer = corrupt == 0;
        fpga_bad_mode = corrupt == 1;
        tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "FAULT");
        assert_fpga_sequence(false, false);
    }

    /* Hold the actual reset mock to prove all edits are blocked during work. */
    owner = context(&options, &sys, &radio, &pipeline);
    fpga_test_active = true;
    options.control_fault = true; /* Programming itself is an explicit recovery. */
    atomic_store(&fpga_hold_reset, true);
    tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
    const double deadline = clock_seconds(CLOCK_MONOTONIC) + 2;
    while (!atomic_load_explicit(&fpga_reset_entered, memory_order_acquire)
        && clock_seconds(CLOCK_MONOTONIC) < deadline) {
        struct timespec delay = {.tv_nsec=1000000};
        nanosleep(&delay, NULL);
    }
    assert(atomic_load(&fpga_reset_entered));
    static const control_command_t edits[] = {CONTROL_FPGA_PROGRAM, CONTROL_START,
        CONTROL_STOP, CONTROL_FREQUENCY, CONTROL_SQUELCH, CONTROL_TX_START,
        CONTROL_TX_STOP, CONTROL_TX_POWER};
    for (unsigned i = 0; i < sizeof(edits) / sizeof(edits[0]); ++i)
        tx_command(&owner, edits[i], "TRANSITION", "FPGA_PROGRAMMING");
    tx_command(&owner, CONTROL_GET, "OK", "FPGA_PROGRAMMING");
    assert(!owner.stop_after_start && !atomic_load(&owner.lifecycle_tx_ready));
    atomic_store_explicit(&fpga_hold_reset, false, memory_order_release);
    finish_lifecycle(&owner);
    tx_command(&owner, CONTROL_GET, "OK", "IDLE");
    assert_fpga_sequence(false, true);
    assert(!options.control_fault && !options.rx_requested && !options.tx_requested);

    assert(unlink("firmware/top.bin") == 0 && rmdir("firmware") == 0);
    assert(fchdir(previous_directory) == 0 && close(previous_directory) == 0);
    assert(rmdir(directory) == 0);
    fpga_test_active = false;
    puts("FPGA programming sequence, preflight, stage faults and recovery passed");
}

static void test_fpga_modem_recovery(void)
{
    options_t options;
    sys_st sys;
    cariboulite_radio_state_st radio;
    rx_pipeline_t pipeline;
    int previous_directory = open(".", O_RDONLY | O_DIRECTORY);
    char directory[] = "/tmp/cl-fpga-modem-XXXXXX";
    assert(previous_directory >= 0 && mkdtemp(directory));
    assert(chdir(directory) == 0 && mkdir("firmware", 0700) == 0);
    write_test_firmware();

    /* Match the observed soft -> hard -> program sequence. After CRESET the
     * shared bus can yield a non-TRXOFF STATE or fail the read entirely. The
     * deactivation wrapper still reports success in both cases. */
    for (unsigned read_error = 0; read_error < 2; ++read_error) {
        backend_control_t owner = context(&options, &sys, &radio, &pipeline);
        fpga_test_active = fpga_reset_breaks_modem = true;
        fpga_modem_state_error = read_error;
        tx_command(&owner, CONTROL_FPGA_SOFT_RESET, "ACCEPTED", "FPGA_RESETTING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "IDLE");
        assert_fpga_reset_sequence(false, false);
        fpga_event_count = 0;
        tx_command(&owner, CONTROL_FPGA_HARD_RESET, "ACCEPTED", "FPGA_RESETTING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "FPGA_NEEDS_PROGRAM");
        assert_fpga_reset_sequence(true, false);
        assert(fpga_modem_unavailable && options.fpga_needs_programming);
        assert(backend_modem_state(&owner, cariboulite_radio_state_cmd_trx_off) == -1);

        /* A repeated hard reset checks SMI, without relying on modem reads
         * while the configuration and shared bus are unavailable. */
        fpga_event_count = 0;
        tx_command(&owner, CONTROL_FPGA_HARD_RESET, "ACCEPTED", "FPGA_RESETTING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "FPGA_NEEDS_PROGRAM");
        assert_fpga_reset_sequence(true, true);
        fpga_event_count = 0;
        tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "IDLE");
        assert_fpga_sequence(true, true);
        assert(!fpga_modem_unavailable && !options.fpga_needs_programming);
        assert(!options.rx_requested && !options.tx_requested);
    }

    /* The recovery exemption only covers inaccessible modem preconditions;
     * a genuine SMI idle failure must still block every destructive action. */
    for (unsigned hard = 0; hard < 2; ++hard) {
        backend_control_t owner = context(&options, &sys, &radio, &pipeline);
        fpga_test_active = fpga_reset_breaks_modem = true;
        tx_command(&owner, CONTROL_FPGA_HARD_RESET, "ACCEPTED", "FPGA_RESETTING");
        finish_lifecycle(&owner);
        fpga_event_count = 0;
        fpga_failure_event = FPGA_SMI_IDLE;
        tx_command(&owner, hard ? CONTROL_FPGA_HARD_RESET : CONTROL_FPGA_PROGRAM,
            "ACCEPTED", hard ? "FPGA_RESETTING" : "FPGA_PROGRAMMING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "FAULT");
        assert(options.fpga_needs_programming && fpga_modem_unavailable);
        assert(fpga_event_count == 1 && fpga_events[0] == FPGA_SMI_IDLE);
        fpga_failure_event = -1;
        fpga_event_count = 0;
        tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "IDLE");
        assert_fpga_sequence(true, true);
    }

    /* Checked FPGA readback alone cannot release the programming requirement
     * if modem SPI is still broken or the modem has not reached TRXOFF. */
    for (unsigned read_error = 0; read_error < 2; ++read_error) {
        backend_control_t owner = context(&options, &sys, &radio, &pipeline);
        fpga_test_active = fpga_reset_breaks_modem = true;
        fpga_modem_state_error = read_error;
        fpga_modem_stays_unavailable = true;
        tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "FAULT");
        assert(options.fpga_needs_programming && fpga_modem_unavailable);
        assert(fpga_program_readback_done);
        assert(fpga_events[FPGA_SPI_MODE + 1] == FPGA_FINAL_RADIO_IDLE);
        assert(fpga_events[FPGA_SPI_MODE + 2] == FPGA_FINAL_SMI_IDLE);
        assert(fpga_event_count > FPGA_SPI_MODE + 3);
        for (unsigned i = FPGA_SPI_MODE + 3; i < fpga_event_count; ++i)
            assert(fpga_events[i] == FPGA_FINAL_MODEM_OFF);
        tx_command(&owner, CONTROL_START, "FPGA_REQUIRED", "FAULT");
        tx_command(&owner, CONTROL_TX_START, "FPGA_REQUIRED", "FAULT");
        fpga_modem_stays_unavailable = false;
        fpga_event_count = 0;
        tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "IDLE");
        assert_fpga_sequence(true, true);
        assert(!options.fpga_needs_programming);
    }

    /* Inject each final-idle component failure independently, preserving
     * the programming requirement until a later complete verified attempt. */
    for (unsigned failure = FPGA_FINAL_RADIO_IDLE; failure <= FPGA_FINAL_MODEM_OFF; ++failure) {
        backend_control_t owner = context(&options, &sys, &radio, &pipeline);
        fpga_test_active = true;
        fpga_failure_event = (int)failure;
        tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "FAULT");
        assert(options.fpga_needs_programming && fpga_program_readback_done);
        assert(!options.rx_requested && !options.tx_requested);
        assert(fpga_event_count >= FPGA_FINAL_MODEM_OFF + 1);
        fpga_failure_event = -1;
        fpga_event_count = 0;
        tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
        finish_lifecycle(&owner);
        tx_command(&owner, CONTROL_GET, "OK", "IDLE");
        assert_fpga_sequence(true, true);
    }

    /* Allow a short modem transition after restored configuration, while
     * leaving persistent failures bounded by the real helper deadline. */
    backend_control_t owner = context(&options, &sys, &radio, &pipeline);
    fpga_test_active = fpga_reset_breaks_modem = true;
    tx_command(&owner, CONTROL_FPGA_HARD_RESET, "ACCEPTED", "FPGA_RESETTING");
    finish_lifecycle(&owner);
    fpga_event_count = 0;
    fpga_modem_transient_reads = 3;
    tx_command(&owner, CONTROL_FPGA_PROGRAM, "ACCEPTED", "FPGA_PROGRAMMING");
    finish_lifecycle(&owner);
    tx_command(&owner, CONTROL_GET, "OK", "IDLE");
    unsigned final_begin = 1 + FPGA_SPI_MODE - FPGA_HARD_RESET + 1;
    assert(fpga_events[0] == FPGA_SMI_IDLE);
    for (unsigned i = FPGA_HARD_RESET; i <= FPGA_SPI_MODE; ++i)
        assert(fpga_events[1 + i - FPGA_HARD_RESET] == i);
    assert(fpga_events[final_begin] == FPGA_FINAL_RADIO_IDLE);
    assert(fpga_events[final_begin + 1] == FPGA_FINAL_SMI_IDLE);
    assert(fpga_event_count == final_begin + 6 && fpga_modem_transient_reads == 0);
    for (unsigned i = final_begin + 2; i < fpga_event_count; ++i)
        assert(fpga_events[i] == FPGA_FINAL_MODEM_OFF);
    assert(!options.fpga_needs_programming && !options.control_fault);

    assert(unlink("firmware/top.bin") == 0 && rmdir("firmware") == 0);
    assert(fchdir(previous_directory) == 0 && close(previous_directory) == 0);
    assert(rmdir(directory) == 0);
    fpga_test_active = false;
    puts("FPGA recovery from unavailable modem SPI and final idle verification passed");
}

int main(void)
{
    options_t options;
    sys_st sys;
    cariboulite_radio_state_st radio;
    rx_pipeline_t pipeline;
    backend_control_t owner = context(&options, &sys, &radio, &pipeline);

    /* Failed partial tune restores the accepted setting and reports failure. */
    tune_new_fails = true;
    control_reply_t reply = command(&owner, (control_request_t){
        .command=CONTROL_FREQUENCY, .frequency_hz=rejected_frequency}, "HARDWARE", "IDLE");
    assert(tune_calls == 2 && reply.config_generation == 7);
    assert(reply.actual_frequency_hz == accepted_frequency + 7.8125);
    assert(!options.control_fault && !options.rx_requested && !pipeline.inited);

    /* Failure of the rollback is explicit FAULT; STOP can recover it. */
    owner = context(&options, &sys, &radio, &pipeline);
    tune_all_fails = true;
    reply = command(&owner, (control_request_t){
        .command=CONTROL_FREQUENCY, .frequency_hz=rejected_frequency}, "HARDWARE", "FAULT");
    assert(reply.config_generation == 8 && options.frequency == accepted_frequency);
    command(&owner, (control_request_t){.command=CONTROL_START}, "FAULT", "FAULT");
    tune_all_fails = false;
    reply = command(&owner, (control_request_t){.command=CONTROL_STOP}, "OK", "IDLE");
    assert(reply.config_generation == 9 && !options.control_fault);

    /* Every start failure releases resources and rejects the start request. */
    for (unsigned failure = 0; failure < 8; ++failure) {
        owner = context(&options, &sys, &radio, &pipeline);
        switch (failure) {
        case 0: fail_init = true; break;
        case 1: fail_frequency = true; break;
        case 2: fail_start = true; break;
        case 3: wrong_rx_state = true; break;
        case 4: fail_stream = true; break;
        case 5: reader_error = true; break;
        case 6: reader_no_progress = true; break;
        case 7: dead_demod = true; break;
        }
        reply = command(&owner, (control_request_t){.command=CONTROL_START}, "HARDWARE", "FAULT");
        assert(!options.rx_requested && !pipeline.inited && !pipeline.running);
        assert(options.frequency == accepted_frequency && options.rx.freq_hz == accepted_frequency);
        assert(reply.config_generation == 8 && reply.pipeline_generation == (failure == 0 ? 3 : 4));
        assert(init_calls == 1 && destroy_calls >= 2 && stop_calls >= 2);
        fail_init = fail_frequency = fail_start = fail_stream = wrong_rx_state = false;
        reader_error = reader_no_progress = dead_demod = false;
        command(&owner, (control_request_t){.command=CONTROL_STOP}, "OK", "IDLE");
        reply = command(&owner, (control_request_t){.command=CONTROL_START}, "OK", "RX");
        assert(options.rx_requested && pipeline.inited && pipeline.running);
        assert(reply.pipeline_generation == (failure == 0 ? 4 : 5));
        command(&owner, (control_request_t){.command=CONTROL_STOP}, "OK", "IDLE");
        assert(!pipeline.inited && !pipeline.running);
    }

    /* Hardware stop failure cannot be acknowledged as a successful stop. */
    owner = context(&options, &sys, &radio, &pipeline);
    command(&owner, (control_request_t){.command=CONTROL_START}, "OK", "RX");
    fail_idle = true;
    command(&owner, (control_request_t){.command=CONTROL_STOP}, "HARDWARE", "FAULT");
    assert(!pipeline.inited && !options.rx_requested);
    fail_idle = false;
    command(&owner, (control_request_t){.command=CONTROL_STOP}, "OK", "IDLE");

    /* An atomic squelch application failure preserves the saved pair/flags. */
    owner = context(&options, &sys, &radio, &pipeline);
    pipeline.inited = true;
    options.rx_requested = true;
    fail_levels = true;
    reply = command(&owner, (control_request_t){.command=CONTROL_SQUELCH,
        .noise_enabled=false, .carrier_enabled=true, .noise_open_rms=.18f,
        .noise_close_rms=.28f}, "HARDWARE", "RX");
    assert(reply.config_generation == 7 && squelch_calls == 0);

    /* ISM board endpoints differ from the full-board translated RF24 path. */
    owner = context(&options, &sys, &radio, &pipeline);
    sys.board_info.numeric_product_id = system_type_cariboulite_ism;
    assert(control_frequency_valid(&options, &sys, 2385000000));
    assert(control_frequency_valid(&options, &sys, 2495000000));
    assert(!control_frequency_valid(&options, &sys, 2384999999));
    assert(!control_frequency_valid(&options, &sys, 2495000001));
    assert(!control_frequency_valid(&options, &sys, accepted_frequency));

    /* A modem loopback request is rejected before disturbing active RX. */
    owner = context(&options, &sys, &radio, &pipeline);
    options.rx_requested = true;
    pipeline.inited = pipeline.running = true;
    modem_loopback = true;
    tx_command(&owner, CONTROL_TX_START, "LOOPBACK", "RX");
    assert(options.config_generation == 7 && options.rx_requested);
    assert(init_calls == 0 && tx_init_calls == 0 && destroy_calls == 0);

    /* Live TX verification must fail closed after every injected error. */
    atomic_bool tx_ready = ATOMIC_VAR_INIT(false);
    for (unsigned failure = 0; failure < 10; ++failure) {
        owner = context(&options, &sys, &radio, &pipeline);
        switch (failure) {
        case 0: tx_fail_init = true; break;
        case 1: tx_fail_tune = true; break;
        case 2: tx_bad_pac = true; break;
        case 3: tx_fail_start = true; break;
        case 4: tx_wrong_state = true; break;
        case 5: tx_fail_stream = true; break;
        case 6: tx_writer_error = true; break;
        case 7: tx_no_progress = true; break;
        case 8: tx_dead_writer = true; break;
        case 9: tx_fail_reset = true; break;
        }
        atomic_store(&tx_ready, false);
        assert(backend_tx_start(&owner, false, &tx_ready) != 0);
        assert(!mock_tx_pipeline.running && modem_state == cariboulite_radio_state_cmd_trx_off);
        assert(!options.tx_requested && options.tx.freq_hz == accepted_frequency);
        assert(options.tx.tx_power_dbm == -12 && options.tx.rf_fs == 1000000);
        assert(options.tx.f_dev_hz == 2500
            && options.tx_pipeline_generation == (failure == 0 || failure == 9 ? 0 : 1));
        assert(tx_abort_calls >= 1);
        assert(tx_reset_calls == 1 && tx_init_calls == (failure == 9 ? 0 : 1));
        assert(tx_diagnostic_calls == (failure >= 3 && failure < 9 ? 5 : 0));
        if (failure == 9) assert(!strcmp(options.tx_error, "TX_FPGA_RESET"));
    }

    /* Embedded control may report TXPREP/TRANSITION before actual TX. */
    for (unsigned transient = 0; transient < 2; ++transient) {
        owner = context(&options, &sys, &radio, &pipeline);
        tx_transient_state = transient ? cariboulite_radio_state_transition
            : cariboulite_radio_state_cmd_tx_prep;
        tx_transient_reads = 3;
        tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
        finish_lifecycle(&owner);
        assert(options.tx_requested && !options.control_fault && tx_state_reads >= 4);
        assert(tx_samples > 0 && !options.tx_error && tx_abort_calls == 0);
        assert(tx_reset_calls == 1 && tx_diagnostic_calls == 0);
        tx_command(&owner, CONTROL_TX_STOP, "ACCEPTED", "TX_STOPPING");
        finish_lifecycle(&owner);
    }

    /* Positive writes never excuse permanent unconfirmed or unreadable TX. */
    for (unsigned failure = 0; failure < 4; ++failure) {
        owner = context(&options, &sys, &radio, &pipeline);
        switch (failure) {
        case 0: tx_forced_state = cariboulite_radio_state_cmd_tx_prep; break;
        case 1: tx_forced_state = cariboulite_radio_state_transition; break;
        case 2: tx_state_read_error = true; break;
        case 3: tx_forced_state = cariboulite_radio_state_cmd_trx_off; break;
        }
        tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
        finish_lifecycle(&owner);
        assert(options.control_fault && !options.tx_requested && !mock_tx_pipeline.running);
        assert(modem_state == cariboulite_radio_state_cmd_trx_off && tx_abort_calls >= 1);
        assert(tx_samples > 0);
        assert(!strcmp(options.tx_error, failure < 2 ? "TX_STATE_TIMEOUT"
            : failure == 2 ? "TX_STATE_READ" : "TX_STATE"));
        assert(tx_state_reads >= (failure < 2 ? 2u : 1u));
        assert(tx_diagnostic_calls == 5);
    }

    /* Failure-only diagnostics precede unkeying; read errors cannot skip cleanup. */
    for (unsigned diagnostic = 0; diagnostic < 5; ++diagnostic) {
        owner = context(&options, &sys, &radio, &pipeline);
        tx_forced_state = cariboulite_radio_state_cmd_trx_off;
        tx_diagnostic_fail_mask = 1u << diagnostic;
        tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
        finish_lifecycle(&owner);
        assert(options.control_fault && !options.tx_requested && !mock_tx_pipeline.running);
        assert(modem_state == cariboulite_radio_state_cmd_trx_off && tx_abort_calls == 1);
        assert(tx_diagnostic_calls == 5 && !strcmp(options.tx_error, "TX_STATE"));
    }

    /* Runtime transient states trigger bounded fast rechecks, then clear on TX. */
    owner = context(&options, &sys, &radio, &pipeline);
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    finish_lifecycle(&owner);
    tx_pipeline_stats_t txstats;
    tx_pipeline_get_stats(&mock_tx_pipeline, &txstats);
    double watchdog_now = clock_seconds(CLOCK_MONOTONIC);
    assert(backend_tx_watchdog(&owner, cariboulite_radio_state_cmd_tx_prep,
        &txstats, watchdog_now) == 0);
    assert(owner.tx_state_deadline == watchdog_now + .5);
    assert(owner.tx_state_next_check == watchdog_now + .007);
    tx_forced_state = cariboulite_radio_state_transition;
    assert(backend_tx_recheck(&owner, watchdog_now + .007) == 0);
    assert(!options.control_fault && !owner.lifecycle_active);
    assert(owner.tx_state_deadline == watchdog_now + .5); /* No deadline extension. */
    tx_forced_state = -1;
    assert(backend_tx_recheck(&owner, watchdog_now + .014) == 0);
    assert(!owner.tx_state_deadline && !owner.tx_state_next_check);
    assert(!options.control_fault && options.tx_requested && tx_abort_calls == 0);
    tx_command(&owner, CONTROL_TX_STOP, "ACCEPTED", "TX_STOPPING");
    finish_lifecycle(&owner);

    /* The real runtime watchdog unkeys faults even while writes keep advancing. */
    for (unsigned failure = 0; failure < 6; ++failure) {
        owner = context(&options, &sys, &radio, &pipeline);
        tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
        finish_lifecycle(&owner);
        watchdog_now = clock_seconds(CLOCK_MONOTONIC);
        tx_pipeline_get_stats(&mock_tx_pipeline, &txstats);
        assert(backend_tx_watchdog(&owner, cariboulite_radio_state_cmd_tx,
            &txstats, watchdog_now) == 0);
        switch (failure) {
        case 0: tx_forced_state = cariboulite_radio_state_cmd_tx_prep; break;
        case 1: tx_forced_state = cariboulite_radio_state_transition; break;
        case 2: tx_state_read_error = true; break;
        case 3: tx_forced_state = cariboulite_radio_state_cmd_trx_off; break;
        case 4: tx_writer_error = true; break;
        case 5: tx_no_progress = true; break;
        }
        assert(backend_tx_recheck(&owner, watchdog_now + .007) == 0);
        if (failure < 2) {
            assert(!owner.lifecycle_active && !options.control_fault);
            assert(backend_tx_recheck(&owner, watchdog_now + .506) == 0);
            assert(!owner.lifecycle_active && !options.control_fault);
            assert(backend_tx_recheck(&owner, watchdog_now + .508) == 0);
        } else if (failure == 5) {
            assert(!owner.lifecycle_active && !options.control_fault);
            assert(backend_tx_recheck(&owner, watchdog_now + 2.001) == 0);
        }
        assert(owner.lifecycle_active && owner.lifecycle_action == 3);
        finish_lifecycle(&owner);
        assert(options.control_fault && !options.tx_requested && !mock_tx_pipeline.running);
        assert(modem_state == cariboulite_radio_state_cmd_trx_off && tx_abort_calls == 1);
        assert(!strcmp(options.tx_error, failure < 2 ? "TX_STATE_TIMEOUT"
            : failure == 2 ? "TX_STATE_READ" : failure == 3 ? "TX_STATE"
            : failure == 4 ? "TX_WORKER" : "TX_PROGRESS_TIMEOUT"));
        assert(!owner.tx_state_deadline && !owner.tx_state_next_check);
    }

    /* The real asynchronous owner handoff restores RX only after normal stop. */
    owner = context(&options, &sys, &radio, &pipeline);
    options.rx_requested = true;
    pipeline.inited = pipeline.running = true;
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    finish_lifecycle(&owner);
    tx_command(&owner, CONTROL_GET, "OK", "TX");
    assert(!pipeline.inited && options.rx_resume && mock_tx_pipeline.running);
    assert(options.tx_pipeline_generation == 1);
    assert(tx_reset_calls == 1 && tx_diagnostic_calls == 0);
    tx_command(&owner, CONTROL_TX_STOP, "ACCEPTED", "TX_STOPPING");
    finish_lifecycle(&owner);
    tx_command(&owner, CONTROL_GET, "OK", "RX");
    assert(tx_stop_calls == 1 && tx_abort_calls == 0);
    assert(pipeline.inited && pipeline.running && options.pipeline_generation == 4);
    assert(mock_tx_pipeline.inited && !mock_tx_pipeline.running);

    /* Each TX start joins parked workers and resets FPGA before fresh allocation. */
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    finish_lifecycle(&owner);
    assert(options.tx_pipeline_generation == 2 && tx_init_calls == 2);
    assert(tx_reset_calls == 2 && tx_destroy_calls == 1 && tx_diagnostic_calls == 0);
    tx_command(&owner, CONTROL_TX_STOP, "ACCEPTED", "TX_STOPPING");
    finish_lifecycle(&owner);
    assert(options.rx_requested && options.pipeline_generation == 5);

    /* A reset failure destroys retained TX, unkeys and forces a fresh retry. */
    owner = context(&options, &sys, &radio, &pipeline);
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    finish_lifecycle(&owner);
    tx_command(&owner, CONTROL_TX_STOP, "ACCEPTED", "TX_STOPPING");
    finish_lifecycle(&owner);
    assert(mock_tx_pipeline.inited && !mock_tx_pipeline.running);
    tx_fail_reset = true;
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    finish_lifecycle(&owner);
    assert(options.control_fault && !options.tx_requested && !mock_tx_pipeline.inited);
    assert(modem_state == cariboulite_radio_state_cmd_trx_off && tx_abort_calls == 1);
    assert(!strcmp(options.tx_error, "TX_FPGA_RESET") && tx_diagnostic_calls == 0);
    assert(tx_destroy_calls == 1 && tx_reset_calls == 2 && tx_init_calls == 1);
    assert(options.tx_pipeline_generation == 1);
    tx_fail_reset = false;
    command(&owner, (control_request_t){.command=CONTROL_STOP}, "OK", "IDLE");
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    finish_lifecycle(&owner);
    assert(options.tx_requested && !options.control_fault && mock_tx_pipeline.running);
    assert(tx_reset_calls == 3 && tx_init_calls == 2 && options.tx_pipeline_generation == 2);
    tx_command(&owner, CONTROL_TX_STOP, "ACCEPTED", "TX_STOPPING");
    finish_lifecycle(&owner);

    /* A failed TX job becomes explicit FAULT without falsely restoring RX. */
    owner = context(&options, &sys, &radio, &pipeline);
    options.rx_requested = true;
    pipeline.inited = pipeline.running = true;
    tx_fail_tune = true;
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    finish_lifecycle(&owner);
    tx_command(&owner, CONTROL_GET, "OK", "FAULT");
    assert(!options.rx_requested && !options.tx_requested && !mock_tx_pipeline.running);
    assert(options.tx_error && !strcmp(options.tx_error, "TX_TUNE"));
    tx_fail_tune = false;
    command(&owner, (control_request_t){.command=CONTROL_STOP}, "OK", "IDLE");

    /* Cancellation after worker completion cannot be accepted as live TX. */
    owner = context(&options, &sys, &radio, &pipeline);
    options.rx_requested = true;
    pipeline.inited = pipeline.running = true;
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    while (!atomic_load_explicit(&owner.lifecycle_done, memory_order_acquire)) {
        struct timespec delay = {.tv_nsec=1000000};
        nanosleep(&delay, NULL);
    }
    assert(owner.lifecycle_result == 0 && mock_tx_pipeline.running);
    options.control_fault = true;
    options.tx_error = "TRANSITION_TIMEOUT";
    finish_lifecycle(&owner);
    assert(options.control_fault && !options.rx_requested && !options.tx_requested);
    assert(owner.tx_configuration_dirty && !mock_tx_pipeline.running);
    assert(modem_state == cariboulite_radio_state_cmd_trx_off && tx_abort_calls == 1);

    /* A latched fault in retained workers must be rebuilt after recovery. */
    owner = context(&options, &sys, &radio, &pipeline);
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    finish_lifecycle(&owner);
    atomic_store(&mock_tx_pipeline.tx_ctrl.last_error, -EIO);
    assert(backend_launch_lifecycle(&owner, 3) == 0);
    finish_lifecycle(&owner);
    assert(options.control_fault && owner.tx_configuration_dirty);
    command(&owner, (control_request_t){.command=CONTROL_STOP}, "OK", "IDLE");
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    finish_lifecycle(&owner);
    assert(options.tx_requested && !options.control_fault);
    assert(tx_init_calls == 2 && tx_destroy_calls == 1);
    assert(options.tx_pipeline_generation == 2);
    tx_command(&owner, CONTROL_TX_STOP, "ACCEPTED", "TX_STOPPING");
    finish_lifecycle(&owner);

    /* A failed closing cue unkeys, records a fault and avoids RX restoration. */
    owner = context(&options, &sys, &radio, &pipeline);
    options.rx_requested = true;
    pipeline.inited = pipeline.running = true;
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    finish_lifecycle(&owner);
    atomic_store(&mock_tx_pipeline.tx_ctrl.last_error, -ETIMEDOUT);
    tx_command(&owner, CONTROL_TX_STOP, "ACCEPTED", "TX_STOPPING");
    finish_lifecycle(&owner);
    assert(options.control_fault && !options.rx_requested && !options.tx_requested);
    assert(modem_state == cariboulite_radio_state_cmd_trx_off);
    assert(!strcmp(options.tx_error, "TX_FAILED"));

    /* Failed RX restoration after a successful TX stop remains visible. */
    owner = context(&options, &sys, &radio, &pipeline);
    options.rx_requested = true;
    pipeline.inited = pipeline.running = true;
    tx_command(&owner, CONTROL_TX_START, "ACCEPTED", "TX_STARTING");
    finish_lifecycle(&owner);
    fail_init = true;
    tx_command(&owner, CONTROL_TX_STOP, "ACCEPTED", "TX_STOPPING");
    finish_lifecycle(&owner);
    tx_command(&owner, CONTROL_GET, "OK", "FAULT");
    assert(!options.rx_requested && !options.tx_requested && !mock_tx_pipeline.running);
    assert(options.tx_error && !strcmp(options.tx_error, "RX_RESUME_FAILED"));
    test_fpga_programming();
    test_fpga_resets();
    test_fpga_modem_recovery();
    puts("TX control fault injection passed");
    puts("RX control fault injection passed");
    return 0;
}
