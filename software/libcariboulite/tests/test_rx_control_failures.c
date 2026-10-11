/* Run the real backend command handler against injected radio/pipeline faults.
 * No driver initialization, socket creation, ALSA or hardware operations run.
 * Link with function sections + gc-sections to discard its executable entry.
 */
#define _GNU_SOURCE
#define main unused_backend_main
#include "main.c"
#undef main
#include <assert.h>

pthread_mutex_t g_hw_lock = PTHREAD_MUTEX_INITIALIZER;

static bool fail_init, fail_frequency, fail_start, fail_stream, wrong_rx_state;
static bool reader_error, reader_no_progress, dead_demod, fail_levels;
static bool tune_new_fails, tune_all_fails, fail_idle;
static unsigned init_calls, destroy_calls, stop_calls, tune_calls, squelch_calls;
static unsigned modem_state = cariboulite_radio_state_cmd_trx_off;
static const double accepted_frequency = 430125000;
static const double rejected_frequency = 430130000;

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
    assert(direction == cariboulite_channel_dir_rx && !active);
    modem_state = cariboulite_radio_state_cmd_trx_off;
    return fail_idle ? -1 : 0;
}

int caribou_smi_set_driver_streaming_state(caribou_smi_st *device, smi_stream_state_en state)
{
    (void)device;
    assert(state == smi_stream_idle || state == smi_stream_rx_channel_0
           || state == smi_stream_rx_channel_1);
    return state != smi_stream_idle && fail_stream ? -1 : 0;
}

int caribou_fpga_set_io_ctrl_mode(caribou_fpga_st *device, uint8_t debug,
                                 caribou_fpga_io_ctrl_rfm_en mode)
{
    (void)device;
    assert(debug == 0 && mode == caribou_fpga_io_ctrl_rfm_low_power);
    return 0;
}

int at86rf215_read_buffer(at86rf215_st *device, uint16_t reg, uint8_t *buffer, uint8_t size)
{
    (void)device;
    assert((reg == REG_RF24_STATE || reg == REG_RF09_STATE) && size == 1);
    *buffer = wrong_rx_state && modem_state == cariboulite_radio_state_cmd_rx
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

static void reset_faults(void)
{
    fail_init = fail_frequency = fail_start = fail_stream = wrong_rx_state = false;
    reader_error = reader_no_progress = dead_demod = fail_levels = false;
    tune_new_fails = tune_all_fails = fail_idle = false;
    init_calls = destroy_calls = stop_calls = tune_calls = squelch_calls = 0;
    modem_state = cariboulite_radio_state_cmd_trx_off;
    stop_requested = 0;
}

static backend_control_t context(options_t *options, sys_st *sys,
                                 cariboulite_radio_state_st *radio, rx_pipeline_t *pipeline)
{
    reset_faults();
    *options = (options_t){.channel="RF24", .frequency=accepted_frequency,
        .config_generation=7, .pipeline_generation=3,
        .rx={.freq_hz=accepted_frequency, .fs_rf=1000000, .mode=AUDIO_DEMOD_NBFM,
             .noise_squelch_open_rms=.20f, .noise_squelch_close_rms=.30f}};
    memset(sys, 0, sizeof(*sys));
    sys->board_info.numeric_product_id = system_type_cariboulite_full;
    memset(radio, 0, sizeof(*radio));
    radio->actual_rf_frequency = accepted_frequency + 7.8125;
    memset(pipeline, 0, sizeof(*pipeline));
    return (backend_control_t){.options=options, .sys=sys, .radio=radio, .pipeline=pipeline};
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
    puts("RX control fault injection passed");
    return 0;
}
