#pragma once

#include "cariboulite_radio.h"
#include "audio_demod.h"

/* Application radio policy, shared by the menu, backend and pipelines.
 * These are programmed filter cutoffs, not measured occupied RF bandwidth.
 * Builders/validators/decoders perform no hardware access or allocation.
 */
typedef struct {
    unsigned sample_rate_hz;
    unsigned analog_bandwidth_hz;
    unsigned digital_cutoff_hz;
    unsigned tx_sample_gap;
    cariboulite_radio_sample_rate_en rate;
    cariboulite_radio_f_cut_en cutoff;
    cariboulite_radio_rx_bw_en rx_bandwidth;
    cariboulite_radio_tx_cut_off_en tx_bandwidth;
} radio_profile_t;

int radio_profile_rx(audio_demod_mode_t mode, unsigned rate_hz, radio_profile_t* out);
int radio_profile_tx(unsigned rate_hz, radio_profile_t* out);
/* Bit-based finiteness checks also work in callers built with -ffast-math. */
bool radio_profile_finite(double value);
bool radio_profile_frequency_valid(cariboulite_channel_en channel, int product_id,
                                   double frequency_hz);
bool radio_profile_tx_power_valid(cariboulite_channel_en channel, int power_dbm);
/* Same calibrated driver mapping; -1 for an unsupported request. */
int radio_profile_tx_power_code(cariboulite_channel_en channel, int power_dbm);

/* Decode complete raw register bytes. Zero means unavailable/reserved.
 * These functions never update radio state or the FPGA sample gap.
 */
unsigned radio_profile_rx_bandwidth_hz(int rxbwc);
unsigned radio_profile_tx_bandwidth_hz(int txcutc);
double radio_profile_sample_rate_hz(int dfe);
double radio_profile_digital_cutoff_hz(int dfe);
