#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "radio_profile.h"
#include "cariboulite_setup.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

bool radio_profile_finite(double value)
{
    uint64_t bits;
    _Static_assert(sizeof(bits) == sizeof(value), "64-bit double required");
    memcpy(&bits, &value, sizeof(bits));
    return (bits & UINT64_C(0x7ff0000000000000)) != UINT64_C(0x7ff0000000000000);
}

static int profile_rate(unsigned hz, radio_profile_t* out)
{
    if (!out) return -1;
    cariboulite_radio_sample_rate_en rate;
    switch (hz) {
        case 1000000: rate = cariboulite_radio_rx_sample_rate_1000khz; break;
        case 2000000: rate = cariboulite_radio_rx_sample_rate_2000khz; break;
        case 4000000: rate = cariboulite_radio_rx_sample_rate_4000khz; break;
        default: return -1;
    }
    *out = (radio_profile_t){.sample_rate_hz = hz, .rate = rate,
        .cutoff = cariboulite_radio_rx_f_cut_0_25_half_fs,
        .digital_cutoff_hz = hz / 8, .tx_sample_gap = 4000000 / hz - 1};
    return 0;
}

int radio_profile_rx(audio_demod_mode_t mode, unsigned hz, radio_profile_t* out)
{
    if (mode != AUDIO_DEMOD_NBFM && mode != AUDIO_DEMOD_WBFM) return -1;
    if (profile_rate(hz, out)) return -1;
    out->rx_bandwidth = mode == AUDIO_DEMOD_NBFM
        ? cariboulite_radio_rx_bw_160KHz : cariboulite_radio_rx_bw_250KHz;
    out->analog_bandwidth_hz = mode == AUDIO_DEMOD_NBFM ? 160000 : 250000;
    return 0;
}

int radio_profile_tx(unsigned hz, radio_profile_t* out)
{
    if (profile_rate(hz, out)) return -1;
    out->tx_bandwidth = cariboulite_radio_tx_cut_off_80khz;
    out->analog_bandwidth_hz = 80000;
    return 0;
}

bool radio_profile_frequency_valid(cariboulite_channel_en channel, int product_id, double hz)
{
    if (!radio_profile_finite(hz)) return false;
    if (product_id != system_type_cariboulite_full &&
        product_id != system_type_cariboulite_ism) return false;
    if (channel == cariboulite_channel_s1g)
        return (hz >= CARIBOULITE_S1G_MIN1 && hz <= CARIBOULITE_S1G_MAX1)
            || (hz >= CARIBOULITE_S1G_MIN2 && hz <= CARIBOULITE_S1G_MAX2);
    if (channel != cariboulite_channel_hif) return false;
    if (product_id == system_type_cariboulite_full)
        return hz >= CARIBOULITE_6G_MIN && hz < CARIBOULITE_6G_MAX;
    if (product_id == system_type_cariboulite_ism)
        return hz >= CARIBOULITE_2G4_MIN && hz <= CARIBOULITE_2G4_MAX;
    return false;
}

bool radio_profile_tx_power_valid(cariboulite_channel_en channel, int dbm)
{
    if (channel != cariboulite_channel_s1g && channel != cariboulite_channel_hif) return false;
    return dbm >= (channel == cariboulite_channel_s1g ? -14 : -12) && dbm <= 14;
}

int radio_profile_tx_power_code(cariboulite_channel_en channel, int dbm)
{
    if (!radio_profile_tx_power_valid(channel, dbm)) return -1;
    float x = dbm;
    float model = channel == cariboulite_channel_s1g
        ? .001502f*x*x*x + .020549f*x*x + .991045f*x + 13.727758f
        : .000710f*x*x*x*x + .010521f*x*x*x + .015169f*x*x + .914333f*x + 12.254084f;
    int code = (int)roundf(model);
    return code < 0 ? 0 : code > 31 ? 31 : code;
}

static bool byte_valid(int value) { return value >= 0 && value <= 255; }

unsigned radio_profile_rx_bandwidth_hz(int value)
{
    static const unsigned hz[] = {160000, 200000, 250000, 320000, 400000, 500000,
        630000, 800000, 1000000, 1250000, 1600000, 2000000};
    if (!byte_valid(value)) return 0;
    unsigned index = (unsigned)value & 15;
    return index < sizeof(hz)/sizeof(hz[0]) ? hz[index] : 0;
}

unsigned radio_profile_tx_bandwidth_hz(int value)
{
    static const unsigned hz[] = {80000, 100000, 125000, 160000, 200000, 250000,
        315000, 400000, 500000, 625000, 800000, 1000000};
    if (!byte_valid(value)) return 0;
    unsigned index = (unsigned)value & 15;
    return index < sizeof(hz)/sizeof(hz[0]) ? hz[index] : 0;
}

double radio_profile_sample_rate_hz(int value)
{
    if (!byte_valid(value)) return 0;
    unsigned code = (unsigned)value & 15;
    switch (code) {
        case 1: case 2: case 3: case 4: case 5: case 6: case 8: case 10:
            return 4000000.0 / code;
        default: return 0;
    }
}

double radio_profile_digital_cutoff_hz(int value)
{
    static const double fraction[] = {.125, .1875, .25, .375, .5};
    double rate = radio_profile_sample_rate_hz(value);
    if (!rate) return 0;
    unsigned code = ((unsigned)value >> 5) & 7;
    return code < sizeof(fraction)/sizeof(fraction[0]) ? rate * fraction[code] : 0;
}
