#pragma once
#include "cariboulite.h"
#include "cariboulite_setup.h"
#include "pipeline_transport.h"
#include "demod_worker.h"
#include "audio_sink.h"

// Internal application handles. One control owner serializes lifecycle calls.
typedef struct {
    bool active;
    atomic_bool alive;
    atomic_int last_error;
    pipeline_thread_diagnostics_t scheduling;
    audio_sink_t* sink;
    aud10_fifo_t* fifo;     // source of 10 ms audio frames
    size_t xruns;
} audio_writer_ctrl_t;

typedef struct {
    bool active;
    atomic_bool alive;
    pipeline_thread_diagnostics_t scheduling;
    atomic_uint_fast64_t read_calls, read_samples, short_reads, zero_reads, read_errors;
    atomic_int last_error;
    const atomic_uint* squelch_flags;
    cariboulite_radio_state_st *radio;
    cariboulite_sample_complex_int16 *rx_buffer;
    size_t rx_buffer_size;
	
	// new
	rf10_fifo_t* rx_fifo;
} rx_reader_ctrl_st;

typedef struct {
    // Radio
    double freq_hz;             // e.g., 430.1e6

    // Demod/audio
    const char* pcm_dev;        // ALSA playback device ("plughw:3,0" etc.)
    float  deemph_tau_s;        // 50e-6 (EU) or 75e-6 (NA)
    float  pcm_gain;            // e.g., 8000.0f

    bool noise_squelch_disabled; // zero/default enables noise squelch
    bool carrier_squelch_enabled; // zero/default disables carrier squelch
    float noise_squelch_open_rms; // both zero select the default 0.20/0.30 pair
    float noise_squelch_close_rms;

    audio_demod_mode_t mode;       // zero/default is NBFM; selected at initialization

    // Supported rates
    float  fs_rf;               // 1e6, 2e6 or 4e6
    float  fs_audio;            // 48e3
    float  nbfm_deviation_hz;   // audio normalization; zero selects the default
} rx_params_t;

typedef struct {
    // Allocated/owned objects
    rf10_fifo_t          rxq;       // IQ at the selected RF rate → 10ms frames
    aud10_fifo_t         afifo;     // 10ms PCM for ALSA
    rx_reader_ctrl_st    rx_ctrl;
    audio_demod_ctrl_t    demod;
    audio_writer_ctrl_t  aw;

    // Threads
    pthread_t            rx_thread;
    pthread_t            demod_thread;
    pthread_t            aw_thread;
    bool rx_thread_created;
    bool demod_thread_created;
    bool aw_thread_created;

    // State
    bool inited;
    bool running;

    // Binding
    sys_st*                         sys;
    cariboulite_radio_state_st*     radio;
} rx_pipeline_t;

typedef struct {
    bool created, alive;
    int last_error;
    pipeline_thread_status_t scheduling;
} rx_worker_status_t;

typedef struct {
    uint64_t calls, samples, short_reads, zero_reads, errors;
    int last_error;
} rx_reader_stats_t;

typedef struct {
    rf10_stats_t rxq;
    aud10_stats_t audioq;
    bool playback_valid;
    audio_sink_stats_t playback;
    rx_reader_stats_t reader;
    rx_worker_status_t reader_worker, demod_worker, audio_worker;
} rx_pipeline_stats_t;

typedef struct {
    float rms, open_rms, close_rms;
    bool valid, detector_open; // valid only during NBFM RX after measurement
} rx_noise_squelch_status_t;

extern cariboulite_sample_complex_int16 latest_rx_sample;
int rx_pipeline_init(rx_pipeline_t*, sys_st*, cariboulite_radio_state_st*, const rx_params_t*);
// Each start verifies the modem profile: NBFM uses AGC and minimum RX analog
// bandwidth/digital cutoff; WBFM restores the wide receive filters.
int rx_pipeline_start(rx_pipeline_t*);
void rx_pipeline_stop(rx_pipeline_t*);
void rx_pipeline_destroy(rx_pipeline_t*);
bool rx_pipeline_running(const rx_pipeline_t*);
int rx_pipeline_set_freq(rx_pipeline_t*, double freq_hz);
int rx_pipeline_set_pcm_gain(rx_pipeline_t*, float gain);
int rx_pipeline_set_deemph(rx_pipeline_t*, float tau_s);
// Control-owner snapshot: serialize with init/start/stop/reset/destroy. Safe
// while workers run; independent counters may describe adjacent instants.
void rx_pipeline_get_stats(rx_pipeline_t*, rx_pipeline_stats_t*);
// Internal worker helpers reused by the modem self-test and lifecycle checks.
int write_audio_exact(audio_sink_t*, const int16_t*, size_t frames);
void* audio_writer_thread(void*);
void* rx_reader_thread_func(void*);

size_t rx_pipeline_frame_samples(const rx_pipeline_t*);
// Resets RF/audio FIFO counters only. Reader/playback counters last until destroy.
void rx_pipeline_reset_stats(rx_pipeline_t*);

// Thread-safe controls; configuration survives start/stop, reset on re-init.
void rx_pipeline_set_squelch(rx_pipeline_t*, bool noise_enabled, bool carrier_enabled);
bool rx_pipeline_squelch_open(const rx_pipeline_t*);
// Threshold pairs update atomically without stopping RX. Resolution 0.001 RMS;
// require 0 < open < close <= 8.0 after rounding. PCM volume is independent.
int rx_pipeline_set_noise_squelch_levels(rx_pipeline_t*, float open_rms, float close_rms);
int rx_pipeline_get_noise_squelch_status(const rx_pipeline_t*, rx_noise_squelch_status_t*);
