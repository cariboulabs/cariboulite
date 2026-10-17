#pragma once
#include "cariboulite.h"
#include "cariboulite_setup.h"
#include "pipeline_transport.h"
#include "audio_source.h"
#include "nbfm_mod.h"
#include "alsa_source.h"
#include "pipeline_runtime.h"
#include "tx_audio_buffer.h"
#include <stdatomic.h>

// Internal application handles. One control owner serializes lifecycle calls.
typedef struct {
    volatile int   frames_left;   // number of 10ms frames to override
    volatile float hz;            // 0 => zeros, else tone frequency
    uint64_t last_sequence;       // Last injected frame successfully enqueued
    bool hold_silence;            // Keep silence after the stop cue
    bool fast_padding;            // Finite final silence drains via writer backpressure
} tone_injector_t;

typedef struct {
    atomic_bool active;
    atomic_bool alive;
    atomic_int last_error;
    pipeline_thread_diagnostics_t scheduling;
    atomic_uint_fast64_t write_calls, write_samples, partial_writes;
    atomic_uint_fast64_t zero_writes, would_block, write_errors;
    /* One condition-variable gate parks both workers while retained/stopped.
     * Owner changes its state; busy flags acknowledge one-reader quiescence. */
    pthread_mutex_t state_mutex;
    pthread_cond_t state_changed;
    bool gate_initialized, gate_enabled, writer_busy, producer_busy;
    bool producer_resumed;
    bool verify_start_profile; // Opt-in checked modem profile before any cue IQ
    bool hardware_paced;
    struct smi_stream_tx_status driver_config;
    bool smi_write_timing_saved;
    int saved_write_setup, saved_write_strobe, saved_write_hold;
    atomic_bool abort_requested;
    cariboulite_radio_state_st *radio;
    cariboulite_sample_complex_int16 *tx_buffer;
    size_t tx_buffer_size;

	// (live path)
    bool    live_from_mic;     // set true to enable live generation
    audio_source_t* mic;     // ALSA handle
    tx_audio_buffer_t* audio_buffer; // Independent capture and clock adaptation
    nbfm_mod_t*     fm;      // 48 kHz audio -> configured RF rate NBFM
    float*            a48k;    // 480-float scratch
    iq16_t*           iq_rf;    // One RF frame of IQ scratch
	
	// test tone generator for the FM modulator
    bool     tone_mode;        // true => synthesize 600 Hz audio
    audio_source_t* tone;      // worker-owned oscillator, shared by tone and injection
    float    tone_hz;          // default 600.0f
    float    tone_amp;         // audio amplitude (0..1), e.g. 0.8f
	
    size_t frame_samples;     // Immutable while pipeline threads exist
    unsigned tail_padding_frames; // Silence covering kernel/DMA buffering
    uint64_t next_sequence;   // Producer-owned, independent of diagnostic stats
    atomic_uint_fast64_t written_sequence; // Last complete frame accepted by SMI
    rf10_fifo_t* fifo;         // FIFO for 10 ms frames
	
    // new
	tone_injector_t inj;       // tone injector parameters

} tx_writer_ctrl_st;

typedef struct {
    atomic_bool         active;
    atomic_bool alive;
    atomic_int last_error;
    pipeline_thread_diagnostics_t scheduling;
    tx_writer_ctrl_st*  tx;      // reuse your modulator/mic/tone fields
    rf10_fifo_t*        fifo;
} dsp_producer_ctrl_t;

typedef enum {
    TX_PACING_AUTO = 0, // Use demand ABI when present; ENOTTY permits legacy
    TX_PACING_HARDWARE_REQUIRED,
    TX_PACING_LEGACY,
} tx_pacing_mode_t;

typedef struct {
    // Radio
    double freq_hz;             // e.g., 430.1e6
    int    tx_power_dbm;        // e.g., -3
    unsigned rf_fs;            // 1, 2 or 4 MS/s; 0 defaults to 4 MS/s

    // Baseband source for NBFM mod
    bool   tone_mode;           // true => synth audio
    float  tone_hz;             // default 600.0f
    float  tone_amp;            // 0..1 (e.g., 0.4f)
    const char* mic_dev;        // ALSA capture device or NULL for no mic

    // NBFM modulator config (kept same as your current)
    float  out_scale;           // e.g., 4000.0f
    float  f_dev_hz;            // nominal deviation at unit audio, in Hz
    // Backend-only checked pre-cue profile; preserves owner cancellation until
    // rebuild. Default false retains the legacy start/retry behavior.
    bool verify_start_profile;
    tx_pacing_mode_t pacing_mode;
} tx_params_t;

typedef struct {
    // Allocated/owned objects
    rf10_fifo_t         txq;
    tx_writer_ctrl_st   tx_ctrl;
    dsp_producer_ctrl_t dsp_ctrl;

    // Threads
    pthread_t           dsp_thread;
    pthread_t           tx_thread;
    bool dsp_thread_created;
    bool tx_thread_created;

    // State
    bool inited;
    atomic_bool running;

    // Binding
    sys_st*                         sys;
    cariboulite_radio_state_st*     radio;
} tx_pipeline_t;

typedef struct {
    uint64_t calls, samples, partial_writes, zero_writes, would_block, errors;
    int last_error;
    uint64_t written_sequence;
} tx_writer_stats_t;

typedef struct {
    bool created, alive;
    int last_error;
    pipeline_thread_status_t scheduling;
} tx_worker_status_t;

typedef struct {
    rf10_stats_t txq;
    tx_writer_stats_t writer;
    tx_worker_status_t producer_worker, writer_worker;
    bool capture_valid;
    alsa_source_stats_t capture;
    bool hardware_paced, driver_valid, audio_buffer_valid;
    bool smi_timing_valid;
    struct smi_settings smi_timing;
    struct smi_stream_tx_status driver;
    tx_audio_buffer_stats_t audio_buffer;
} tx_pipeline_stats_t;

extern cariboulite_sample_complex_int16 latest_tx_sample;
int tx_pipeline_init(tx_pipeline_t*, sys_st*, cariboulite_radio_state_st*, const tx_params_t*);
int tx_pipeline_start(tx_pipeline_t*);
void tx_pipeline_stop(tx_pipeline_t*);
void tx_pipeline_destroy(tx_pipeline_t*);
bool tx_pipeline_running(const tx_pipeline_t*);
bool tx_pipeline_healthy(const tx_pipeline_t*);
/* Fault/shutdown path only: request is thread-safe and does not touch hardware;
 * abort is serialized by the lifecycle owner and skips the normal Quindar tail. */
void tx_pipeline_request_abort(tx_pipeline_t*);
int tx_pipeline_abort(tx_pipeline_t*);
int tx_pipeline_set_freq_power(tx_pipeline_t*, double freq_hz, int tx_power_dbm);
void tx_pipeline_get_stats(tx_pipeline_t*, tx_pipeline_stats_t*);

size_t tx_pipeline_frame_samples(const tx_pipeline_t*);
void tx_pipeline_reset_stats(tx_pipeline_t*);
/* Internal worker gate: each successful begin requires one end, including
 * error paths. NULL/uninitialized gates support standalone worker fixtures. */
bool tx_worker_begin(tx_writer_ctrl_st*, bool producer);
void tx_worker_end(tx_writer_ctrl_st*, bool producer);
void tx_worker_fail(tx_writer_ctrl_st*, int error);
/* One parked lifecycle owner or the producer owns the modulator at a time.
 * Uses a48k input, validates exact audio/RF counts and packs one sequenced frame. */
int tx_generate_frame(tx_writer_ctrl_st*, rf10_frame_t*);
