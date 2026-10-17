#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "nbfm_defaults.h"
// Single-owner, allocation-free detector for unfiltered 48 kHz discriminator
// audio, normalized to nominal +/-1 at NBFM_DEFAULT_DEVIATION_HZ. No PCM gain.
// Two cascaded 6 kHz high-pass biquads, 20 ms power averaging.
// Measured starting point for the improved HiF/RF24 receiver at 1 MS/s,
// admitting the captured weak intelligible transmission while closing on idle
// noise. These adjustable defaults are not a calibrated sensitivity limit.
#define NOISE_SQUELCH_OPEN_RMS (0.20f * (2500.0f / NBFM_DEFAULT_DEVIATION_HZ))
#define NOISE_SQUELCH_CLOSE_RMS (0.30f * (2500.0f / NBFM_DEFAULT_DEVIATION_HZ))
#define NOISE_SQUELCH_MAX_RMS 8.0f
typedef struct {
    float z1[2], z2[2], power;
    float open_power, close_power; // squared RMS levels; zero initialization uses defaults
    unsigned qualify;
    bool open;
} noise_squelch_t;
void noise_squelch_reset(noise_squelch_t* s);
// Runtime thresholds preserve detector history and the gate. A changed pair
// restarts transition qualification. Reject invalid levels without changing s.
bool noise_squelch_set_thresholds(noise_squelch_t* s, float open_rms, float close_rms);
// One atomic control word: open RMS milli in bits 31:16, close in bits 15:0.
// Zero resolves to defaults for existing/offline callers. Quantization is 0.001.
bool noise_squelch_pack_levels(float open_rms, float close_rms, uint32_t* levels);
bool noise_squelch_unpack_levels(uint32_t levels, float* open_rms, float* close_rms);
uint32_t noise_squelch_default_levels(void);
// Initially closed; 30 ms quiet opens, 120 ms noisy closes; hysteresis.
// Invalid samples reset closed. Detector observes audio even while muted.
bool noise_squelch_process(noise_squelch_t* s, float audio);
