#include "radio_profile.h"
#include "cariboulite_setup.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static void test_profiles(void)
{
    const unsigned rates[] = {1000000, 2000000, 4000000};
    const cariboulite_radio_sample_rate_en codes[] = {
        cariboulite_radio_rx_sample_rate_1000khz,
        cariboulite_radio_rx_sample_rate_2000khz,
        cariboulite_radio_rx_sample_rate_4000khz,
    };
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned mode = 0; mode < 2; ++mode) {
            radio_profile_t profile;
            assert(radio_profile_rx(mode ? AUDIO_DEMOD_WBFM : AUDIO_DEMOD_NBFM,
                                    rates[i], &profile) == 0);
            assert(profile.sample_rate_hz == rates[i] && profile.rate == codes[i]);
            assert(profile.analog_bandwidth_hz == (mode ? 250000u : 160000u));
            assert(profile.rx_bandwidth == (mode ? cariboulite_radio_rx_bw_250KHz
                                                 : cariboulite_radio_rx_bw_160KHz));
            assert(profile.cutoff == cariboulite_radio_rx_f_cut_0_25_half_fs);
            assert(profile.digital_cutoff_hz == rates[i] / 8);
        }
        radio_profile_t profile;
        assert(radio_profile_tx(rates[i], &profile) == 0);
        assert(profile.sample_rate_hz == rates[i] && profile.rate == codes[i]);
        assert(profile.analog_bandwidth_hz == 80000);
        assert(profile.tx_bandwidth == cariboulite_radio_tx_cut_off_80khz);
        assert(profile.cutoff == cariboulite_radio_rx_f_cut_0_25_half_fs);
        assert(profile.digital_cutoff_hz == rates[i] / 8);
        assert(profile.tx_sample_gap == 4000000 / rates[i] - 1);
    }
    radio_profile_t profile;
    const unsigned invalid_rates[] = {0, 400000, 500000, 1333333, 3000000, 8000000};
    for (unsigned i = 0; i < sizeof(invalid_rates) / sizeof(*invalid_rates); ++i) {
        assert(radio_profile_rx(AUDIO_DEMOD_NBFM, invalid_rates[i], &profile) < 0);
        assert(radio_profile_tx(invalid_rates[i], &profile) < 0);
    }
    assert(radio_profile_rx((audio_demod_mode_t)-1, 1000000, &profile) < 0);
    assert(radio_profile_rx((audio_demod_mode_t)99, 1000000, &profile) < 0);
    assert(radio_profile_rx(AUDIO_DEMOD_NBFM, 1000000, NULL) < 0);
    assert(radio_profile_tx(1000000, NULL) < 0);
}

static void test_frequency_and_power(void)
{
    const int products[] = {system_type_cariboulite_full, system_type_cariboulite_ism};
    for (unsigned i = 0; i < 2; ++i) {
        const double endpoints[] = {CARIBOULITE_S1G_MIN1, CARIBOULITE_S1G_MAX1,
                                    CARIBOULITE_S1G_MIN2, CARIBOULITE_S1G_MAX2};
        for (unsigned j = 0; j < 4; ++j)
            assert(radio_profile_frequency_valid(cariboulite_channel_s1g, products[i], endpoints[j]));
        const double rejected[] = {CARIBOULITE_S1G_MIN1 - 1, CARIBOULITE_S1G_MAX1 + 1,
                                    CARIBOULITE_S1G_MIN2 - 1, CARIBOULITE_S1G_MAX2 + 1};
        for (unsigned j = 0; j < 4; ++j)
            assert(!radio_profile_frequency_valid(cariboulite_channel_s1g, products[i], rejected[j]));
    }
    assert(radio_profile_frequency_valid(cariboulite_channel_hif,
        system_type_cariboulite_full, CARIBOULITE_6G_MIN));
    assert(radio_profile_frequency_valid(cariboulite_channel_hif,
        system_type_cariboulite_full, CARIBOULITE_6G_MAX - 1));
    assert(!radio_profile_frequency_valid(cariboulite_channel_hif,
        system_type_cariboulite_full, CARIBOULITE_6G_MIN - 1));
    assert(!radio_profile_frequency_valid(cariboulite_channel_hif,
        system_type_cariboulite_full, CARIBOULITE_6G_MAX));
    assert(radio_profile_frequency_valid(cariboulite_channel_hif,
        system_type_cariboulite_ism, CARIBOULITE_2G4_MIN));
    assert(radio_profile_frequency_valid(cariboulite_channel_hif,
        system_type_cariboulite_ism, CARIBOULITE_2G4_MAX));
    assert(!radio_profile_frequency_valid(cariboulite_channel_hif,
        system_type_cariboulite_ism, CARIBOULITE_2G4_MIN - 1));
    assert(!radio_profile_frequency_valid(cariboulite_channel_hif,
        system_type_cariboulite_ism, CARIBOULITE_2G4_MAX + 1));
    assert(!radio_profile_frequency_valid(cariboulite_channel_hif, -1, 430125000));
    assert(!radio_profile_frequency_valid(cariboulite_channel_s1g, -1, 430125000));
    assert(!radio_profile_frequency_valid((cariboulite_channel_en)99,
        system_type_cariboulite_full, 430125000));

    const double invalid[] = {NAN, INFINITY, -INFINITY};
    for (unsigned i = 0; i < 3; ++i) {
        assert(!radio_profile_finite(invalid[i]));
        for (unsigned channel = 0; channel < 2; ++channel)
            assert(!radio_profile_frequency_valid(channel ? cariboulite_channel_hif :
                cariboulite_channel_s1g, system_type_cariboulite_full, invalid[i]));
    }
    assert(radio_profile_finite(0) && radio_profile_finite(-1) && radio_profile_finite(6e9));
    for (unsigned channel = 0; channel < 2; ++channel) {
        cariboulite_channel_en type = channel ? cariboulite_channel_hif : cariboulite_channel_s1g;
        int minimum = channel ? -12 : -14;
        assert(!radio_profile_tx_power_valid(type, minimum - 1));
        assert(!radio_profile_tx_power_valid(type, 15));
        assert(radio_profile_tx_power_code(type, minimum - 1) < 0);
        assert(radio_profile_tx_power_code(type, 15) < 0);
        for (int power = minimum; power <= 14; ++power) {
            assert(radio_profile_tx_power_valid(type, power));
            int code = radio_profile_tx_power_code(type, power);
            assert(code >= 0 && code <= 31);
        }
        assert(radio_profile_tx_power_code(type, minimum) == 0);
        assert(radio_profile_tx_power_code(type, 14) == 31);
        assert(radio_profile_tx_power_code(type, -3) == (channel ? 9 : 11));
    }
    assert(!radio_profile_tx_power_valid((cariboulite_channel_en)99, 0));
    assert(radio_profile_tx_power_code((cariboulite_channel_en)99, 0) < 0);
}

static void test_register_decoders(void)
{
    const unsigned rx[] = {160000, 200000, 250000, 320000, 400000, 500000,
                            630000, 800000, 1000000, 1250000, 1600000, 2000000};
    const unsigned tx[] = {80000, 100000, 125000, 160000, 200000, 250000,
                            315000, 400000, 500000, 625000, 800000, 1000000};
    for (unsigned i = 0; i < 12; ++i) {
        assert(radio_profile_rx_bandwidth_hz(0x10 | i) == rx[i]);
        assert(radio_profile_tx_bandwidth_hz(0x80 | i) == tx[i]);
        assert(radio_profile_tx_bandwidth_hz(0x40 | i) == tx[i]);
    }
    for (int i = 12; i < 16; ++i) {
        assert(radio_profile_rx_bandwidth_hz(0x10 | i) == 0);
        assert(radio_profile_tx_bandwidth_hz(0x80 | i) == 0);
    }
    const int unavailable[] = {-1, -256, 256, 512};
    for (unsigned i = 0; i < sizeof(unavailable) / sizeof(*unavailable); ++i) {
        assert(radio_profile_rx_bandwidth_hz(unavailable[i]) == 0);
        assert(radio_profile_tx_bandwidth_hz(unavailable[i]) == 0);
        assert(radio_profile_sample_rate_hz(unavailable[i]) == 0);
        assert(radio_profile_digital_cutoff_hz(unavailable[i]) == 0);
    }
    assert(radio_profile_sample_rate_hz(0x04) == 1000000);
    assert(radio_profile_sample_rate_hz(0x82) == 2000000);
    assert(radio_profile_sample_rate_hz(0x01) == 4000000);
    assert(radio_profile_digital_cutoff_hz(0x04) == 125000);
    assert(radio_profile_digital_cutoff_hz(0x22) == 375000);
    assert(radio_profile_digital_cutoff_hz(0x41) == 1000000);
    assert(radio_profile_digital_cutoff_hz(0x64) == 375000);
    assert(radio_profile_digital_cutoff_hz(0x84) == 500000);
    const int reserved_rates[] = {0, 7, 9, 11, 12, 13, 14, 15};
    for (unsigned i = 0; i < sizeof(reserved_rates) / sizeof(*reserved_rates); ++i) {
        assert(radio_profile_sample_rate_hz(reserved_rates[i]) == 0);
        assert(radio_profile_digital_cutoff_hz(reserved_rates[i]) == 0);
    }
    assert(radio_profile_digital_cutoff_hz(0xa4) == 0);
    assert(radio_profile_digital_cutoff_hz(0xc4) == 0);
    assert(radio_profile_digital_cutoff_hz(0xe4) == 0);
}

int main(void)
{
    test_profiles();
    test_frequency_and_power();
    test_register_decoders();
    puts("PASS: shared radio profiles, board limits, power and unavailable register decoding");
    return 0;
}
