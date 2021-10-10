#ifndef __AT86RF215_RADIO_H__
#define __AT86RF215_RADIO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include "at86rf215_common.h"

struct at86rf215_radio_regs
{
    uint16_t RG_IRQS;
    uint16_t RG_IRQM;
    uint16_t RG_AUXS;
    uint16_t RG_STATE;
    uint16_t RG_CMD;
    uint16_t RG_CS;
    uint16_t RG_CCF0L;
    uint16_t RG_CCF0H;
    uint16_t RG_CNL;
    uint16_t RG_CNM;
    uint16_t RG_RXBWC;
    uint16_t RG_RXDFE;
    uint16_t RG_AGCC;
    uint16_t RG_AGCS;
    uint16_t RG_RSSI;
    uint16_t RG_EDC;
    uint16_t RG_EDD;
    uint16_t RG_EDV;
    uint16_t RG_RNDV;
    uint16_t RG_TXCUTC;
    uint16_t RG_TXDFE;
    uint16_t RG_PAC;
    uint16_t RG_PADFE;
    uint16_t RG_PLL;
    uint16_t RG_PLLCF;
    uint16_t RG_TXCI;
    uint16_t RG_TXCQ;
    uint16_t RG_TXDACI;
    uint16_t RG_TXDACQ;
};

typedef enum
{
    at86rf215_radio_cmd_nop = 0x0,              // No operation (dummy)
    at86rf215_radio_cmd_sleep = 0x1,            // Go to sleep
    at86rf215_radio_state_cmd_trx_off = 0x2,    // Transceiver off, SPI active
    at86rf215_radio_state_cmd_tx_prep = 0x3,    // Transmit preparation
    at86rf215_radio_state_cmd_tx = 0x4,         // Transmit
    at86rf215_radio_state_cmd_rx = 0x5,         // Receive
    at86rf215_radio_state_transition = 0x6,     // State transition in progress
    at86rf215_radio_state_cmd_reset = 0x7,      // Transceiver is in state RESET or SLEEP
                                                // if commanded reset, the transceiver state will
                                                // automatically end up in state TRXOFF
} at86rf215_radio_state_cmd_en;

typedef enum
{
    at86rf215_radio_channel_mode_ieee = 0x00,
    // IEEE compliant channel scheme; f=(CCF0+CN*CS)*25kHz+f offset ;
    // (f offset = 0Hz for sub-1GHz transceiver; f offset = 1.5GHz for 2.4GHz transceiver)

    at86rf215_radio_channel_mode_fine_low = 0x01,
    // Fine resolution (389.5-510.0)MHz with 99.182Hz channel stepping

    at86rf215_radio_channel_mode_fine_mid = 0x02,
    // Fine resolution (779-1020)MHz with 198.364Hz channel stepping

    at86rf215_radio_channel_mode_fine_high = 0x03,
    // Fine resolution (2400-2483.5)MHz with 396.728Hz channel stepping

} at86rf215_radio_channel_mode_en;

/** offset (in Hz) for CCF0 in 2.4 GHz mode */
#define CCF0_24G_OFFSET          1500000U

typedef enum
{
    at86rf215_radio_rx_bw_BW160KHZ_IF250KHZ = 0x0,
    at86rf215_radio_rx_bw_BW200KHZ_IF250KHZ = 0x1,
    at86rf215_radio_rx_bw_BW250KHZ_IF250KHZ = 0x2,
    at86rf215_radio_rx_bw_BW320KHZ_IF500KHZ = 0x3,
    at86rf215_radio_rx_bw_BW400KHZ_IF500KHZ = 0x4,
    at86rf215_radio_rx_bw_BW500KHZ_IF500KHZ = 0x5,
    at86rf215_radio_rx_bw_BW630KHZ_IF1000KHZ = 0x6,
    at86rf215_radio_rx_bw_BW800KHZ_IF1000KHZ = 0x7,
    at86rf215_radio_rx_bw_BW1000KHZ_IF1000KHZ = 0x8,
    at86rf215_radio_rx_bw_BW1250KHZ_IF2000KHZ = 0x9,
    at86rf215_radio_rx_bw_BW1600KHZ_IF2000KHZ = 0xA,
    at86rf215_radio_rx_bw_BW2000KHZ_IF2000KHZ = 0xB,
} at86rf215_radio_rx_bw_en;

typedef enum
{
    at86rf215_radio_rx_f_cut_0_25_half_fs = 0,
    at86rf215_radio_rx_f_cut_0_375_half_fs = 1,
    at86rf215_radio_rx_f_cut_0_5_half_fs = 2,
    at86rf215_radio_rx_f_cut_0_75_half_fs = 3,
    at86rf215_radio_rx_f_cut_half_fs = 4,
} at86rf215_radio_f_cut_en;

typedef enum
{
    at86rf215_radio_rx_sample_rate_4000khz = 0x1,
    at86rf215_radio_rx_sample_rate_2000khz = 0x2,
    at86rf215_radio_rx_sample_rate_1333khz = 0x3,
    at86rf215_radio_rx_sample_rate_1000khz = 0x4,
    at86rf215_radio_rx_sample_rate_800khz = 0x5,
    at86rf215_radio_rx_sample_rate_666khz = 0x6,
    at86rf215_radio_rx_sample_rate_500khz = 0x8,
    at86rf215_radio_rx_sample_rate_400khz = 0xA,
} at86rf215_radio_sample_rate_en;


typedef enum
{
    at86rf215_radio_agc_averaging_8 = 0,
    at86rf215_radio_agc_averaging_16 = 1,
    at86rf215_radio_agc_averaging_32 = 2,
    at86rf215_radio_agc_averaging_64 = 3,
} at86rf215_radio_agc_averaging_en;

typedef enum
{
    at86rf215_radio_agc_relative_atten_21_db = 0,
    at86rf215_radio_agc_relative_atten_24_db = 1,
    at86rf215_radio_agc_relative_atten_27_db = 2,
    at86rf215_radio_agc_relative_atten_30_db = 3,
    at86rf215_radio_agc_relative_atten_33_db = 4,
    at86rf215_radio_agc_relative_atten_36_db = 5,
    at86rf215_radio_agc_relative_atten_39_db = 6,
    at86rf215_radio_agc_relative_atten_41_db = 7,
} at86rf215_radio_agc_relative_atten_en;

typedef struct
{
    // commands
    int agc_measure_source_not_filtered;        // AGC Input (0 - filterred, 1 - unfiltered, faster operation)
    at86rf215_radio_agc_averaging_en avg;       // AGC Average Time in Number of Samples
    int reset_cmd;                              // AGC Reset - resets the AGC and sets the maximum receiver gain.
    int freeze_cmd;                             // AGC Freeze Control - A value of one forces the AGC to
                                                // freeze to its current value.
    int enable_cmd;                             // AGC Enable - a value of zero allows a manual setting of
                                                // the RX gain control by sub-register AGCS.GCW

    at86rf215_radio_agc_relative_atten_en att;  // AGC Target Level - sets the AGC target level relative to ADC full scale.
    int gain_control_word;                      // If AGCC_EN is set to 1, a read of bit AGCS.GCW indicates the current
                                                // receiver gain setting. If AGCC_EN is set to 0, a write access to GCW
                                                // manually sets the receiver gain. An integer value of 23 indicates
                                                // the maximum receiver gain; each integer step changes the gain by 3dB.

    // status
    int freeze_status;                          // AGC Freeze Status - A value of one indicates that the AGC is on hold.
} at86rf215_radio_agc_ctrl_st;

typedef struct
{
    int ext_lna_bypass_available;       // The bit activates an AGC scheme where the external LNA
                                        // can be bypassed using pins FEAnn/FEBnn.

    int agc_backoff;                    // 0x00 - no backoff - anyway we do not use this option
    int analog_voltage_external;        // The bit disables the internal analog supply voltage - requires external LDO
    int analog_voltage_enable_in_off;   // for fast turn-on from TRXOFF state - the voltage is kept going even when off
    int int_power_amplifier_voltage;    // 0: 2V, 1: 2.2V, 2: 2.4V;
    int fe_pad_configuration;           // 2 bits, 4 options pg.73 in datasheet. for Caribou this should be "1"
                                        // 0: no control - pads disabled (always '0')
                                        // 1: FEA|FEB: L|L: TXPREP (Frontend off); H|L: TX (Transmit); L|H: RX (Receive); H|H: RX with LNA Bypass
                                        // 2: FEA|FEB: L|L: TXPREP (Frontend off); H|H: TX (Transmit); L|H: RX (Receive); H|L: RX with LNA Bypass
                                        // 3: FEA|FEB|MCUPin: L|L|L: TXPREP (Frontend off); H|H|H: TX (Transmit); L|H|H: RX (Receive); L|L|H: RX with LNA Bypass

    // statuses
    int analog_voltage_status;
} at86rf215_radio_external_ctrl_st;


typedef struct
{
    int inverter_sign_if;                   // A value of one configures the receiver to implement the
                                            // inverted-sign IF frequency. Use default setting for normal operation.
    int shift_if_freq;                      // A value of one configures the receiver to shift the IF frequency
                                            // by factor of 1.25. This is useful to place the image frequency according
                                            // to channel scheme.
    at86rf215_radio_rx_bw_en bw;            // The sub-register controls the receiver filter bandwidth settings.
    at86rf215_radio_f_cut_en fcut;          // RX filter relative cut-off frequency
    at86rf215_radio_sample_rate_en fs;      // RX Sample Rate
} at86rf215_radio_set_rx_bw_samp_st;


typedef enum
{
    at86rf215_radio_energy_detection_mode_auto = 0,
    // Energy detection measurement is automatically triggered if the AGC is held by the internal
    // baseband or by setting bit FRZC.

    at86rf215_radio_energy_detection_mode_single = 1,
    // A single energy detection measurement is started.

    at86rf215_radio_energy_detection_mode_continous = 2,
    // A continuous energy detection measurements of configured interval defined in
    // register EDD is started.

    at86rf215_radio_energy_detection_mode_off = 3,
    // Energy detection measurement is disabled

} at86rf215_radio_energy_detection_mode_en;

typedef struct
{
    // cmd
    at86rf215_radio_energy_detection_mode_en mode;      // nergy Detection Mode
    float average_duration_us;                          // T[μs]=DF*DTB - the DTB will be calculated accordingly

    // status
    float energy_detection_value;                       // Receiver Energy Detection Value
} at86rf215_radio_energy_detection_st;

typedef enum
{
    at86rf215_radio_tx_pa_ramp_4usec = 0,
    at86rf215_radio_tx_pa_ramp_8usec = 1,
    at86rf215_radio_tx_pa_ramp_16usec = 2,
    at86rf215_radio_tx_pa_ramp_32usec = 3,
} at86rf215_radio_tx_pa_ramp_en;

typedef enum
{
    at86rf215_radio_tx_cut_off_80khz = 0x0,
    at86rf215_radio_tx_cut_off_100khz = 0x1,
    at86rf215_radio_tx_cut_off_125khz = 0x2,
    at86rf215_radio_tx_cut_off_160khz = 0x3,
    at86rf215_radio_tx_cut_off_200khz = 0x4,
    at86rf215_radio_tx_cut_off_250khz = 0x5,
    at86rf215_radio_tx_cut_off_315khz = 0x6,
    at86rf215_radio_tx_cut_off_400khz = 0x7,
    at86rf215_radio_tx_cut_off_500khz = 0x8,
    at86rf215_radio_tx_cut_off_625khz = 0x9,
    at86rf215_radio_tx_cut_off_800khz = 0xA,
    at86rf215_radio_tx_cut_off_1000khz = 0xB,
} at86rf215_radio_tx_cut_off_en;


typedef enum
{
    at86rf215_radio_pa_current_reduction_22ma = 0, // 3dB reduction of gain
    at86rf215_radio_pa_current_reduction_18ma = 1, // 2dB reduction of gain
    at86rf215_radio_pa_current_reduction_11ma = 2, // 1dB reduction of gain
    at86rf215_radio_pa_current_reduction_0ma = 3, // no reduction
} at86rf215_radio_pa_current_reduction_en;


typedef struct
{
    at86rf215_radio_tx_pa_ramp_en pa_ramping_time;
    at86rf215_radio_pa_current_reduction_en current_reduction;
    int tx_power;
    // Maximum output power is TXPWR=31, minimum output power is TXPWR=0.
    // The output power can be set by about 1dB step resolution.

    at86rf215_radio_tx_cut_off_en analog_bw;
    at86rf215_radio_f_cut_en digital_bw;
    at86rf215_radio_sample_rate_en fs;
    int direct_modulation;

} at86rf215_radio_tx_ctrl_st;


typedef enum
{
    at86rf215_radio_pll_loop_bw_default = 0,
    at86rf215_radio_pll_loop_bw_dec_15perc = 1,
    at86rf215_radio_pll_loop_bw_inc_15perc = 2,
} at86rf215_radio_pll_loop_bw_en;

typedef struct
{
    at86rf215_radio_pll_loop_bw_en loop_bw;
    // This sub-register controls the PLL loop bandwidth.The sub-register is applicable for the sub-1GHz transceiver only
    // (RF09). The TX modulation quality (i.e. FSK eye diagram) can be adjusted when direct modulation is used.

    int pll_center_freq;
    // Center frequency calibration is performed from state TRXOFF to state TXPREP and at channel change. The register
    // displays the center frequency value of the current channel.

    // statuses
    int pll_locked;
} at86rf215_radio_pll_ctrl_st;


void at86rf215_radio_setup_interrupt_mask(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                            at86rf215_radio_irq_st* mask);

void at86rf215_radio_setup_external_settings(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                at86rf215_radio_external_ctrl_st* cfg);

void at86rf215_radio_get_external_settings(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                at86rf215_radio_external_ctrl_st* cfg);

at86rf215_radio_state_cmd_en at86rf215_radio_get_state(at86rf215_st* dev, at86rf215_rf_channel_en ch);

void at86rf215_radio_set_state(at86rf215_st* dev, at86rf215_rf_channel_en ch, at86rf215_radio_state_cmd_en cmd);

double at86rf215_radio_get_frequency( /*IN*/ at86rf215_radio_channel_mode_en mode,
                                     /*IN*/ int channel_spacing_25khz_res,
                                     /*IN*/ double wanted_frequency_hz,
                                     /*OUT*/ int *center_freq_25khz_res,
                                     /*OUT*/ int *channel_number);

void at86rf215_radio_setup_channel(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                        int channel_spacing_25khz_res,
                                        int center_freq_25khz_res,
                                        int channel_number,
                                        at86rf215_radio_channel_mode_en mode);

void at86rf215_radio_set_rx_bandwidth_sampling(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                at86rf215_radio_set_rx_bw_samp_st* cfg);

void at86rf215_radio_get_rx_bandwidth_sampling(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                at86rf215_radio_set_rx_bw_samp_st* cfg);

void at86rf215_radio_setup_agc(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                    at86rf215_radio_agc_ctrl_st *agc_ctrl);

void at86rf215_radio_get_agc(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                at86rf215_radio_agc_ctrl_st *agc_ctrl);

float at86rf215_radio_get_rssi_dbm(at86rf215_st* dev, at86rf215_rf_channel_en ch);

void at86rf215_radio_setup_energy_detection(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                            at86rf215_radio_energy_detection_st* ed);

void at86rf215_radio_get_energy_detection(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                        at86rf215_radio_energy_detection_st* ed);

uint8_t at86rf215_radio_get_random_value(at86rf215_st* dev, at86rf215_rf_channel_en ch);

void at86rf215_radio_setup_tx_ctrl(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                at86rf215_radio_tx_ctrl_st* cfg);

void at86rf215_radio_get_tx_ctrl(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                at86rf215_radio_tx_ctrl_st* cfg);

void at86rf215_radio_setup_pll_ctrl(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                        at86rf215_radio_pll_ctrl_st* cfg);

void at86rf215_radio_get_pll_ctrl(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                        at86rf215_radio_pll_ctrl_st* cfg);

void at86rf215_radio_set_tx_iq_calibration(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                int cal_i, int cal_q);

void at86rf215_radio_get_tx_iq_calibration(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                int *cal_i, int *cal_q);

void at86rf215_radio_set_tx_dac_input_iq(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                int enable_dac_i_dc, int dac_i_val,
                                                int enable_dac_q_dc, int dac_q_val);

void at86rf215_radio_get_tx_dac_input_iq(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                int *enable_dac_i_dc, int *dac_i_val,
                                                int *enable_dac_q_dc, int *dac_q_val);
int at86rf215_radio_get_good_channel(double wanted_frequency_hz, at86rf215_radio_channel_mode_en *mode,
                                                                at86rf215_rf_channel_en *ch);

#ifdef __cplusplus
}
#endif

#endif // __AT86RF215_RADIO_H__