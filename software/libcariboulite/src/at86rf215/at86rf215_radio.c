#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "AT86RF215_Radio"

#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "zf_log/zf_log.h"
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"
#include "at86rf215_radio.h"
#include "at86rf215_regs.h"

static const struct at86rf215_radio_regs RF09_regs = {
    .RG_IRQS   = 0x00,
    .RG_IRQM   = 0x100,
    .RG_AUXS   = 0x101,
    .RG_STATE  = 0x102,
    .RG_CMD    = 0x103,
    .RG_CS     = 0x104,
    .RG_CCF0L  = 0x105,
    .RG_CCF0H  = 0x106,
    .RG_CNL    = 0x107,
    .RG_CNM    = 0x108,
    .RG_RXBWC  = 0x109,
    .RG_RXDFE  = 0x10A,
    .RG_AGCC   = 0x10B,
    .RG_AGCS   = 0x10C,
    .RG_RSSI   = 0x10D,
    .RG_EDC    = 0x10E,
    .RG_EDD    = 0x10F,
    .RG_EDV    = 0x110,
    .RG_RNDV   = 0x111,
    .RG_TXCUTC = 0x112,
    .RG_TXDFE  = 0x113,
    .RG_PAC    = 0x114,
    .RG_PADFE  = 0x116,
    .RG_PLL    = 0x121,
    .RG_PLLCF  = 0x122,
    .RG_TXCI   = 0x125,
    .RG_TXCQ   = 0x126,
    .RG_TXDACI = 0x127,
    .RG_TXDACQ = 0x128,
};

static const struct at86rf215_radio_regs RF24_regs = {
    .RG_IRQS   = 0x01,
    .RG_IRQM   = 0x200,
    .RG_AUXS   = 0x201,
    .RG_STATE  = 0x202,
    .RG_CMD    = 0x203,
    .RG_CS     = 0x204,
    .RG_CCF0L  = 0x205,
    .RG_CCF0H  = 0x206,
    .RG_CNL    = 0x207,
    .RG_CNM    = 0x208,
    .RG_RXBWC  = 0x209,
    .RG_RXDFE  = 0x20A,
    .RG_AGCC   = 0x20B,
    .RG_AGCS   = 0x20C,
    .RG_RSSI   = 0x20D,
    .RG_EDC    = 0x20E,
    .RG_EDD    = 0x20F,
    .RG_EDV    = 0x210,
    .RG_RNDV   = 0x211,
    .RG_TXCUTC = 0x212,
    .RG_TXDFE  = 0x213,
    .RG_PAC    = 0x214,
    .RG_PADFE  = 0x216,
    .RG_PLL    = 0x221,
    .RG_PLLCF  = 0x222,
    .RG_TXCI   = 0x225,
    .RG_TXCQ   = 0x226,
    .RG_TXDACI = 0x227,
    .RG_TXDACQ = 0x228,
};

#define AT86RF215_REG_ADDR(c,r)  \
            (((c)==at86rf215_rf_channel_900mhz)?(RF09_regs.RG_##r):(RF24_regs.RG_##r))


//==================================================================================
void at86rf215_radio_setup_interrupt_mask(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                            at86rf215_radio_irq_st* mask)
{
    uint16_t reg_address = AT86RF215_REG_ADDR(ch, IRQM);
    at86rf215_write_byte(dev, reg_address, *((uint8_t*)mask));
}

//==================================================================================
void at86rf215_radio_setup_external_settings(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                at86rf215_radio_external_ctrl_st* cfg)
{
// "RG_AUXS" RFn_AUXS – Transceiver Auxiliary Settings
    uint16_t reg_address_aux = AT86RF215_REG_ADDR(ch, AUXS);
    uint16_t reg_address_pad = AT86RF215_REG_ADDR(ch, PADFE);

    uint8_t aux = 0;
    aux |= (cfg->ext_lna_bypass_available&0x1) << 7;
    aux |= (cfg->agc_backoff&0x3) << 5;
    aux |= (cfg->analog_voltage_external&0x1) << 4;
    aux |= (cfg->analog_voltage_enable_in_off&0x1) << 3;
    aux |= (cfg->int_power_amplifier_voltage&0x3) << 0;

    at86rf215_write_byte(dev, reg_address_aux, aux);

    uint8_t pad = (cfg->fe_pad_configuration&0x3) << 6;
    at86rf215_write_byte(dev, reg_address_pad, pad);
}

//==================================================================================
void at86rf215_radio_get_external_settings(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                at86rf215_radio_external_ctrl_st* cfg)
{
    uint16_t reg_address_aux = AT86RF215_REG_ADDR(ch, AUXS);
    uint16_t reg_address_pad = AT86RF215_REG_ADDR(ch, PADFE);

    uint8_t aux = at86rf215_read_byte(dev, reg_address_aux);
    uint8_t pad = at86rf215_read_byte(dev, reg_address_pad);

    cfg->fe_pad_configuration = pad>>6;
    cfg->ext_lna_bypass_available = (aux>>7) & 0x1;
    cfg->agc_backoff = (aux>>5) & 0x3;
    cfg->analog_voltage_external = (aux>>4) & 0x1;
    cfg->analog_voltage_enable_in_off = (aux>>3) & 0x1;
    cfg->analog_voltage_status = (aux>>2) & 0x1;
    cfg->int_power_amplifier_voltage = (aux>>0) & 0x2;
}

//==================================================================================
at86rf215_radio_state_cmd_en at86rf215_radio_get_state(at86rf215_st* dev, at86rf215_rf_channel_en ch)
{
    // "RG_STATE" RFn_STATE – Transceiver State
    uint16_t reg_address = AT86RF215_REG_ADDR(ch, STATE);
    uint8_t state = at86rf215_read_byte(dev, reg_address);
    return state & 0x7;
}

//==================================================================================
void at86rf215_radio_set_state(at86rf215_st* dev, at86rf215_rf_channel_en ch, at86rf215_radio_state_cmd_en cmd)
{
    // "RG_CMD" RFn_CMD – Transceiver Command

    uint16_t reg_address = AT86RF215_REG_ADDR(ch, CMD);
    at86rf215_write_byte(dev, reg_address, cmd & 0x7);

    /*Errata #6:    State Machine Command RFn_CMD=TRXOFF may not be succeeded
                    Description: If the current state is different from SLEEP, the execution of the command TRXOFF may fail.
                    Software workaround: Check state by reading register RFn_STATE Repeat the command RFn_CMD=TRXOFF
                    if the target state was not reached.
    */
    if (cmd == at86rf215_radio_state_cmd_trx_off)
    {
        int retries = 5;
        while (at86rf215_radio_get_state(dev, ch) != cmd && retries--)
        {
            io_utils_usleep(100);
            at86rf215_write_byte(dev, reg_address, cmd & 0x7);
        }
    }
}

static double _fine_freq_starts[] = {0, 377e6, 754e6, 2366e6, 2550e6};
static double _fine_freq_pll_src[] = {0, 6.5e6, 13e6, 26e6};
//static int _fine_freq_ccf_min[] = {0, 126030, 126030, 85700};
//static int _fine_freq_ccf_max[] = {0, 1340967, 1340967, 296172};

//==================================================================================
int at86rf215_radio_get_good_channel(double wanted_frequency_hz, at86rf215_radio_channel_mode_en *mode,
                                                                at86rf215_rf_channel_en *ch)
{
    if (   wanted_frequency_hz > _fine_freq_starts[at86rf215_radio_channel_mode_fine_high]
        && wanted_frequency_hz < _fine_freq_starts[at86rf215_radio_channel_mode_fine_high+1])
    {
        *ch = at86rf215_rf_channel_2400mhz;
        *mode = at86rf215_radio_channel_mode_fine_high;
    }
    else if (  wanted_frequency_hz > _fine_freq_starts[at86rf215_radio_channel_mode_fine_mid]
            && wanted_frequency_hz < _fine_freq_starts[at86rf215_radio_channel_mode_fine_high] )
    {
        *ch = at86rf215_rf_channel_900mhz;
        *mode = at86rf215_radio_channel_mode_fine_mid;
    }
    else if (  wanted_frequency_hz > _fine_freq_starts[at86rf215_radio_channel_mode_fine_low]
            && wanted_frequency_hz < _fine_freq_starts[at86rf215_radio_channel_mode_fine_mid] )
    {
        *ch = at86rf215_rf_channel_900mhz;
        *mode = at86rf215_radio_channel_mode_fine_low;
    }
    else
    {
        ZF_LOGE("the requested frequency %.1f Hz not supported", wanted_frequency_hz);
        return -1;
    }
    return 0;
}

//==================================================================================
double at86rf215_radio_get_frequency( /*IN*/ at86rf215_radio_channel_mode_en mode,
                                     /*IN*/ int channel_spacing_25khz_res,
                                     /*IN*/ double wanted_frequency_hz,
                                     /*OUT*/ int *center_freq_25khz_res,
                                     /*OUT*/ int *channel_number)
{
    // "RG_CS" RFn_CS – Channel Spacing
    /*
        The register configures the channel spacing with a resolution of 25kHz. The register CNM must be written to take over
        the CS value. The CNM register must always be written last.
        Example: A value of 0x08 needs to be set (8x25kHz) for a channel spacing of 200kHz.
    */
    // "RG_CCF0L, RG_CCF0H" RFn_CCF0(L/H) – Channel Center Frequency F0 Low/High Byte
    /*
        The register configures the base value of the channel center frequency F0 (low / high byte) with a resolution of 25kHz.
        The register CNM must be written to latch the CCFOL/H value. The CNM register must always be written last.

        VALUES: The reset value (0xf8) of the channel register results in 902.2MHz for the sub-1GHz transceiver (RF09)
                and 2402.2MHz for the 2.4GHz transceiver (RF24): CCF0=0x8cf8=36088; CS=200KHz, CN=0.

        HOW:
                F_rf09 = CCF0*25khz
                F_rf24 = 1500000 + CCF0 * 25
                F_rf09(default) = 36088 * 25 = 902,200 kHz
                F_rf24(default) = 1500000 + 36088 * 25 = 2,402,200 kHz
    */
    // "RG_CNL" RFn_CNL – Channel Number Low Byte
    // "RG_CNM" RFn_CNM – Channel Mode and Channel Number High Bit
    // in total the CNL/M contains 9 bits of channel and the LATCH (Mode)
    /*
    CNL - The register contains the transceiver channel number low byte CN[7:0]. The register CNM must be written to latch the
          CNL value. The CNM register must always be written last.

    CNM - The register configures the channel mode and channel number bit 9 CN[8]. A write operation to this register applies the
          channel register CS, CCFOL, CCFOH and CNL values.

          0x0 IEEE compliant channel scheme; f=(CCF0+CN*CS)*25kHz+f_offset ; (f_offset = 0Hz/1.5GHz)
          0x1 Fine resolution (389.5-510.0)MHz with 99.182Hz channel stepping
          0x2 Fine resolution (779-1020)MHz with 198.364Hz channel stepping
          0x3 Fine resolution (2400-2483.5)MHz with 396.728Hz channel stepping
    */


    if (mode == at86rf215_radio_channel_mode_ieee)
    {
        float f_offset = (wanted_frequency_hz>1500e6)?1500e6:0.0f;
        *center_freq_25khz_res = (int)((wanted_frequency_hz - f_offset) / 25e3);
        *channel_number = 0;
        return f_offset + (*center_freq_25khz_res)*25e3;
    }

    int nchannel = (int)((wanted_frequency_hz - _fine_freq_starts[mode]) * ((float)(1<<16)) / _fine_freq_pll_src[mode]);
    float actual_freq = (nchannel*_fine_freq_pll_src[mode]) / ((float)(1<<16)) + _fine_freq_starts[mode];

    *center_freq_25khz_res = (nchannel >> 8) & 0xFFFF;
    *channel_number = (nchannel >> 0) & 0xFF;
    return actual_freq;
}

//==================================================================================
void at86rf215_radio_setup_channel(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                        int channel_spacing_25khz_res,
                                        int center_freq_25khz_res,
                                        int channel_number,
                                        at86rf215_radio_channel_mode_en mode)
{

    uint16_t reg_address_spacing = AT86RF215_REG_ADDR(ch, CS);
    uint8_t buf[5] = {0};
    buf[0] = channel_spacing_25khz_res;
    buf[1] = /*LOW*/ center_freq_25khz_res & 0xFF;
    buf[2] = /*HIGH*/ (center_freq_25khz_res >> 8) & 0xFF;
    buf[3] = /*LOW*/ channel_number & 0xFF;
    buf[4] = /*HIGH + MODE*/ ((channel_number>>8)&0x01) | ((mode & 0x3)<<6);
    at86rf215_write_buffer(dev, reg_address_spacing, buf, 5);
}

//==================================================================================
void at86rf215_radio_set_rx_bandwidth_sampling(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                at86rf215_radio_set_rx_bw_samp_st* cfg)
{
    // "RG_RXBWC" RFn_RXBWC – Receiver Filter Bandwidth Control
    /*
    The register configures the receiver filter settings.
    - Bit 5 – RXBWC.IFI: IF Inversion
            A value of one configures the receiver to implement the inverted-sign IF frequency.
            Use default setting for normal operation.
    - Bit 4 – RXBWC.IFS: IF Shift
            A value of one configures the receiver to shift the IF frequency by factor of 1.25.
            This is useful to place the image frequency according to channel scheme.
    - Bit 3:0 – RXBWC.BW: Receiver Bandwidth
            The sub-register controls the receiver filter bandwidth settings.
    */
    // "RG_RXDFE" RFn_RXDFE – Receiver Digital Frontend
    /*
    The register configures the receiver digital frontend
        Bit 7:5 – RXDFE.RCUT: RX filter relative cut-off frequency
        Bit 3:0 – RXDFE.SR: RX Sample Rate
    */

    uint16_t reg_address_bw = AT86RF215_REG_ADDR(ch, RXBWC);

    uint8_t buf[2] = {0};
    buf[0] |= (cfg->inverter_sign_if & 0x1)<<5;
    buf[0] |= (cfg->shift_if_freq & 0x1)<<4;
    buf[0] |= (cfg->bw & 0xF);
    buf[1] |= (cfg->fcut & 0x7) << 5;
    buf[1] |= (cfg->fs & 0xF);
    at86rf215_write_buffer(dev, reg_address_bw, buf, 2);
}

//==================================================================================
void at86rf215_radio_get_rx_bandwidth_sampling(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                at86rf215_radio_set_rx_bw_samp_st* cfg)
{
    uint16_t reg_address_bw = AT86RF215_REG_ADDR(ch, RXBWC);
    uint8_t buf[2] = {0};
    at86rf215_read_buffer(dev, reg_address_bw, buf, 2);

    cfg->inverter_sign_if = (buf[0]>>5) & 0x1;
    cfg->shift_if_freq = (buf[0]>>4) & 0x1;
    cfg->bw = (buf[0]>>0) & 0xF;
    cfg->fcut = (buf[1]>>5) & 0x7;
    cfg->fs = (buf[1]>>0) & 0xF;
}

//==================================================================================
void at86rf215_radio_setup_agc(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                    at86rf215_radio_agc_ctrl_st *agc_ctrl)
{
    // "RG_AGCC, RG_AGCS" RFn_AGCC – Receiver AGC Control 0 / RFn_AGCS – Receiver AGCG
    /*
    RFn_AGCC – Receiver AGC Control 0:
    The register controls the transceiver automatic gain control (AGC).
        Bit 6 – AGCC.AGCI: AGC Input (0 - filterred, 1 - unfiltered, faster operation)
        Bit 5:4 – AGCC.AVGS: AGC Average Time in Number of Samples
            0x0 8 samples
            0x1 16 samples
            0x2 32 samples
            0x3 64 samples
        Bit 3 – AGCC.RST: AGC Reset - resets the AGC and sets the maximum receiver gain.
        Bit 2 – AGCC.FRZS: AGC Freeze Status - A value of one indicates that the AGC is on hold.
        Bit 1 – AGCC.FRZC: AGC Freeze Control - A value of one forces the AGC to freeze to its current value.
        Bit 0 – AGCC.EN: AGC Enable - a value of zero allows a manual setting of the RX gain control by sub-register AGCS.GCW

    RFn_AGCS – Receiver AGCG:
    The register controls the AGC and the receiver gain.
        Bit 7:5 – AGCS.TGT: AGC Target Level - sets the AGC target level relative to ADC full scale.
            0x0 Target=-21dB
            0x1 Target=-24dB
            0x2 Target=-27dB
            0x3 Target=-30dB
            0x4 Target=-33dB
            0x5 Target=-36dB
            0x6 Target=-39dB
            0x7 Target=-42dB
        Bit 4:0 – AGCS.GCW: RX Gain Control Word
        If AGCC_EN is set to 1, a read of bit AGCS.GCW indicates the current receiver gain setting. If AGCC_EN is set to 0, a
        write access to GCW manually sets the receiver gain. An integer value of 23 indicates the maximum receiver gain; each
        integer step changes the gain by 3dB.
    */

    uint16_t reg_address_agc = AT86RF215_REG_ADDR(ch, AGCC);
    uint8_t buf[2] = {0};

    buf[0] |= (agc_ctrl->agc_measure_source_not_filtered & 0x1)<<6;
    buf[0] |= (agc_ctrl->avg & 0x3)<<4;
    buf[0] |= (agc_ctrl->reset_cmd & 0x1) << 3;
    buf[0] |= (agc_ctrl->freeze_cmd & 0x1) << 1;
    buf[0] |= (agc_ctrl->enable_cmd & 0x1);
    buf[1] |= (agc_ctrl->att & 0x7) << 5;
    buf[1] |= (agc_ctrl->gain_control_word & 0x1F);
    at86rf215_write_buffer(dev, reg_address_agc, buf, 2);
}

//==================================================================================
void at86rf215_radio_get_agc(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                at86rf215_radio_agc_ctrl_st *agc_ctrl)
{
    uint16_t reg_address_agc = AT86RF215_REG_ADDR(ch, AGCC);
    uint8_t buf[2] = {0};

    at86rf215_read_buffer(dev, reg_address_agc, buf, 2);

    agc_ctrl->agc_measure_source_not_filtered = (buf[0] >> 6);
    agc_ctrl->avg = (buf[0] >> 4) & 0x1;
    agc_ctrl->reset_cmd = (buf[0] >> 3)  & 0x3;
    agc_ctrl->freeze_status = (buf[0] >> 2)  & 0x1;
    agc_ctrl->freeze_cmd = (buf[0] >> 1) & 0x1;
    agc_ctrl->enable_cmd = (buf[0] >> 0) & 0x1;
    agc_ctrl->att = (buf[1] >> 5) & 0x7;
    agc_ctrl->gain_control_word = (buf[1] >> 0) & 0x1F;
}

//==================================================================================
float at86rf215_radio_get_rssi_dbm(at86rf215_st* dev, at86rf215_rf_channel_en ch)
{
    // "RG_RSSI" RFn_RSSI – Received Signal Strength Indicator
    /*
    Bit 7:0 – RSSI.RSSI: Received Signal Strength Indicator
    The value reflects the received signal power in dBm. A value of 127 indicates that the RSSI value is invalid. The register value presents a signed
    integer value (value range -127...+4) using twos complement representation.
    */
    uint16_t reg_address = AT86RF215_REG_ADDR(ch, RSSI);
    uint8_t v = at86rf215_read_byte(dev, reg_address);
    int8_t *sv = (int8_t *)&v;
    return (float)(*sv);
}

//==================================================================================
void at86rf215_radio_setup_energy_detection(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                            at86rf215_radio_energy_detection_st* ed)
{
    /*
    "RG_EDC" "RG_EDD" "RG_EDV"
    RFn_EDC – Energy Detection Configuration
    Bit 1:0 – EDC.EDM: Energy Detection Mode (at86rf215_radio_energy_detection_mode_en)
    RFn_EDD – Receiver Energy Detection Averaging Duration
    Bit 7:2 – EDD.DF: Receiver energy detection duration factor - T[μs]=DF*DTB
    Bit 1:0 – EDD.DTB: Receiver energy detection average duration time basis (RSSI averaging basis)
        0x0 2μs
        0x1 8μs
        0x2 32μs
        0x3 128μs
    */
    uint16_t reg_address_edc = AT86RF215_REG_ADDR(ch, EDC);
    uint8_t buf[2] = {0};

    buf[0] |= (ed->mode & 0x3)<<0;

    // find the closest usec number
    int df = ed->average_duration_us / 2;
    int dtb = 0;
    if (df > 63)
    {
        df = ed->average_duration_us / 8;
        dtb = 1;
        if (df > 63)
        {
            df = ed->average_duration_us / 32;
            dtb = 2;
            if (df > 63)
            {
                df = ed->average_duration_us / 128;
                dtb = 3;
                if (df > 63)
                {
                    df = 63;
                }
            }
        }
    }
    buf[1] |= (df & 0x3F) << 2;
    buf[1] |= (dtb & 0x3);

    at86rf215_write_buffer(dev, reg_address_edc, buf, 2);
}

//==================================================================================
void at86rf215_radio_get_energy_detection(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                        at86rf215_radio_energy_detection_st* ed)
{
    uint16_t reg_address_edc = AT86RF215_REG_ADDR(ch, EDC);
    uint8_t buf[3] = {0};

    at86rf215_read_buffer(dev, reg_address_edc, buf, 3);

    ed->mode = (buf[0]) & 0x3;
    buf[0] |= (ed->mode & 0x3)<<0;

    float dtb = 0.0f;
    switch (buf[1] & 0x3)
    {
        case 0: dtb = 2.0f; break;
        case 1: dtb = 8.0f; break;
        case 2: dtb = 32.0f; break;
        case 3: dtb = 64.0f; break;
        default: dtb = 2.0f; break;
    }
    float df = (buf[1] >> 2) & 0x3;
    ed->average_duration_us = df * dtb;
    ed->energy_detection_value = (float)(*(int8_t*)(&buf[2]));
}

//==================================================================================
uint8_t at86rf215_radio_get_random_value(at86rf215_st* dev, at86rf215_rf_channel_en ch)
{
    // "RG_RNDV" RFn_RNDV – Random Value - contains 8bit random value
    uint16_t reg_address = AT86RF215_REG_ADDR(ch, RNDV);
    return at86rf215_read_byte(dev, reg_address);
}

//==================================================================================
void at86rf215_radio_setup_tx_ctrl(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                at86rf215_radio_tx_ctrl_st* cfg)
{
    /*
    RFn_TXCUTC – Transmitter Filter Cutoff Control and PA Ramp Time
        Bit 7:6 – TXCUTC.PARAMP: Power Amplifier Ramp Time
            RF_PARAMP4U 0x0 tpa_ramp = 4μs
            RF_PARAMP8U 0x1 tpa_ramp = 8μs
            RF_PARAMP16U 0x2 tpa_ramp = 16μs
            RF_PARAMP32U 0x3 tpa_ramp = 32μs
        Bit 3:0 – TXCUTC.LPFCUT: Transmitter cut-off frequency
            RF_FLC80KHZ 0x0 fLPFCUT = 80kHz
            RF_FLC100KHZ 0x1 fLPFCUT = 100kHz
            RF_FLC125KHZ 0x2 fLPFCUT = 125kHz
            RF_FLC160KHZ 0x3 fLPFCUT = 160kHz
            RF_FLC200KHZ 0x4 fLPFCUT = 200kHz
            RF_FLC250KHZ 0x5 fLPFCUT = 250kHz
            RF_FLC315KHZ 0x6 fLPFCUT = 315kHz
            RF_FLC400KHZ 0x7 fLPFCUT = 400kHz
            RF_FLC500KHZ 0x8 fLPFCUT = 500kHz
            RF_FLC625KHZ 0x9 fLPFCUT = 625kHz
            RF_FLC800KHZ 0xA fLPFCUT = 800kHz
            RF_FLC1000KHZ 0xB fLPFCUT = 1000kHz
    */

    /*
    RFn_TXDFE – Transmitter TX Digital Frontend
        Bit 7:5 – TXDFE.RCUT: TX filter relative to the cut-off frequency
            0x0 fCUT=0.25 *fS/2
            0x1 fCUT=0.375*fS/2
            0x2 fCUT=0.5 *fS/2
            0x3 fCUT=0.75 *fS/2
            0x4 fCUT=1.00 *fS/2
        Bit 4 – TXDFE.DM: Direct Modulation
            If enabled (1) the transmitter direct modulation is enabled - available for FSK and
            OQPSK (OQPSKC0.FCHIP=0;1). Direct modulation must also be enabled at the BBCx registers (FSKDM.EN and OQPSKC0.DM).
        Bit 3:0 – TXDFE.SR: TX Sample Rate
            0x1 fS=4000kHz
            0x2 fS=2000kHz
            0x3 fS=(4000/3)kHz
            0x4 fS=1000kHz
            0x5 fS=800kHz
            0x6 fS=(2000/3)kHz
            0x8 fS=500kHz
            0xA fS=400kHz
    */

    /*
    RFn_PAC – Transmitter Power Amplifier Control
        Bit 6:5 – PAC.PACUR: Power Amplifier Current Control - PACUR configures the power amplifier DC current (useful for low power settings).
            0x0 Power amplifier current reduction by about 22mA (3dB reduction of max. small signal gain)
            0x1 Power amplifier current reduction by about 18mA (2dB reduction of max. small signal gain)
            0x2 Power amplifier current reduction by about 11mA (1dB reduction of max. small signal gain)
            0x3 No power amplifier current reduction (max. transmit small signal gain)
        Bit 4:0 – PAC.TXPWR: Transmitter Output Power
            Maximum output power is TXPWR=31, minimum output power is TXPWR=0. The output power can be set by about 1dB step resolution.
    */

    uint16_t reg_address_txcut = AT86RF215_REG_ADDR(ch, TXCUTC);
    uint8_t buf[3] = {0};

    buf[0] |= (cfg->pa_ramping_time & 0x3) << 6;
    buf[0] |= (cfg->analog_bw & 0xF);
    buf[1] |= (cfg->digital_bw & 0x7) << 5;
    buf[1] |= (cfg->direct_modulation & 0x1) << 4;
    buf[1] |= (cfg->fs & 0xF);
    buf[2] |= (cfg->current_reduction & 0x3) << 5;
    buf[2] |= (cfg->tx_power & 0x1F);

    at86rf215_write_buffer(dev, reg_address_txcut, buf, 3);
}

//==================================================================================
void at86rf215_radio_get_tx_ctrl(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                at86rf215_radio_tx_ctrl_st* cfg)
{
    uint16_t reg_address_txcut = AT86RF215_REG_ADDR(ch, TXCUTC);
    uint8_t buf[3] = {0};
    at86rf215_read_buffer(dev, reg_address_txcut, buf, 3);

    cfg->pa_ramping_time = (buf[0] >> 6) & 0x3;
    cfg->analog_bw = (buf[0] >> 0) & 0xF;
    cfg->digital_bw = (buf[1] >> 5) & 0x7;
    cfg->direct_modulation = (buf[1] >> 4) & 0x1;
    cfg->fs = (buf[1] >> 0) & 0xF;
    cfg->current_reduction = (buf[2] >> 5) & 0x3;
    cfg->tx_power = (buf[2] >> 0) & 0x1F;
}

//==================================================================================
void at86rf215_radio_setup_pll_ctrl(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                        at86rf215_radio_pll_ctrl_st* cfg)
{
    uint16_t reg_address_pll = AT86RF215_REG_ADDR(ch, PLL);
    uint8_t buf[2] = {0};

    buf[0] |= (cfg->loop_bw & 0x3) << 4;
    //buf[0] |= (cfg->pll_locked & 0x1) << 1;   <- status
    buf[1] |= (cfg->pll_center_freq & 0x3F) << 0;

    at86rf215_write_buffer(dev, reg_address_pll, buf, 2);
}

//==================================================================================
void at86rf215_radio_get_pll_ctrl(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                        at86rf215_radio_pll_ctrl_st* cfg)
{
    uint16_t reg_address_pll = AT86RF215_REG_ADDR(ch, PLL);
    uint8_t buf[2] = {0};

    at86rf215_read_buffer(dev, reg_address_pll, buf, 2);

    cfg->loop_bw = (buf[0] >> 4) & 0x3;
    cfg->pll_locked = (buf[0] >> 1) & 0x1;
    cfg->pll_center_freq = (buf[1] >> 0) & 0x3F;
}

//==================================================================================
void at86rf215_radio_set_tx_iq_calibration(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                int cal_i, int cal_q)
{
    uint16_t reg_address_cal_i = AT86RF215_REG_ADDR(ch, TXCI);

    uint8_t buf[2] = {0};
    buf[0] = cal_i & 0x3F;
    buf[1] = cal_q & 0x3F;
    at86rf215_write_buffer(dev, reg_address_cal_i, buf, 2);

    // RFn_TXCI – Transmit calibration I path
    //  The register contains information about the TX LO leakage calibration value of the transmit I path. At the transition
    //  process from state TRXOFF to TX the calibration is started automatically

    // RFn_TXCQ – Transmit calibration Q path
    //  The register contains information about the TX LO leakage calibration value of the transmit Q path. At the transition
    //  process from state TRXOFF to TX the calibration is started automatically.
}

//==================================================================================
void at86rf215_radio_get_tx_iq_calibration(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                int *cal_i, int *cal_q)
{
    uint16_t reg_address_cal_i = AT86RF215_REG_ADDR(ch, TXCI);
    uint16_t reg_address_cal_q = AT86RF215_REG_ADDR(ch, TXCQ);
    uint8_t buf[2] = {0};

    at86rf215_read_buffer(dev, reg_address_cal_i, buf, 2);

    *cal_i = buf[0] & 0x3F;
    *cal_q = buf[1] & 0x3F;
}

//==================================================================================
void at86rf215_radio_set_tx_dac_input_iq(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                int enable_dac_i_dc, int dac_i_val,
                                                int enable_dac_q_dc, int dac_q_val)
{
    uint16_t reg_address_dac_i = AT86RF215_REG_ADDR(ch, TXDACI);

    uint8_t buf[2] = {0};
    buf[0] = ((enable_dac_i_dc & 0x1)<<7) | (dac_i_val & 0x7F);
    buf[1] = ((enable_dac_q_dc & 0x1)<<7) | (dac_q_val & 0x7F);
    at86rf215_write_buffer(dev, reg_address_dac_i, buf, 2);

    // RFn_TXDACI – In-phase input value for TXDAC - Input I value can be applied at TXDAC (DC measurement)
    //  Bit 7 – TXDACI.ENTXDACID: Enable input to TXDAC
    //  Bit 6:0 – TXDACI.TXDACID: Input to TXDAC data

    // RFn_TXDACQ – Quadrature-phase input value for TXDAC - Input Q value can be applied at TXDAC (DC measurement)
    //  Bit 7 – TXDACQ.ENTXDACQD: Enable input to TXDAC
    //  Bit 6:0 – TXDACQ.TXDACQD: Input to TXDAC data

    /*
    PAGE 221 in datasheet!
    The AT86RF215 comprises a DAC (digital to analog converter) value overwrite functionality. Each transceiver contains
    two transmitter DACs (for the in-phase and quadrature-phase signal) in order to transmit IQ signals. Both DAC values
    can be overwritten separately by register settings. This feature is useful to transmit an LO carrier which is necessary for
    certain certifications.
    */
}

//==================================================================================
void at86rf215_radio_get_tx_dac_input_iq(at86rf215_st* dev, at86rf215_rf_channel_en ch,
                                                int *enable_dac_i_dc, int *dac_i_val,
                                                int *enable_dac_q_dc, int *dac_q_val)
{
    uint16_t reg_address_dac_i = AT86RF215_REG_ADDR(ch, TXDACI);

    uint8_t buf[2] = {0};
    at86rf215_read_buffer(dev, reg_address_dac_i, buf, 2);

    *enable_dac_i_dc = (buf[0] >> 7) & 0x1;
    *enable_dac_q_dc = (buf[1] >> 7) & 0x1;

    *dac_i_val = (buf[0]) & 0x3F;
    *dac_q_val = (buf[1]) & 0x3F;
}