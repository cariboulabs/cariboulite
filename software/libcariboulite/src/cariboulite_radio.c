#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Radio"
#include "zf_log/zf_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <linux/random.h>
#include <sys/ioctl.h>

#include "cariboulite_internal.h"
#include "cariboulite_radio.h"
#include "cariboulite_events.h"
#include "cariboulite_setup.h"

#define GET_MODEM_CH(rad_ch)	((rad_ch)==cariboulite_channel_s1g ? at86rf215_rf_channel_900mhz : at86rf215_rf_channel_2400mhz)
#define GET_SMI_CH(rad_ch)		((rad_ch)==cariboulite_channel_s1g ? caribou_smi_channel_900 : caribou_smi_channel_2400)

static float sample_rate_middles[] = {3000, 1666, 1166, 900, 733, 583, 450};
static float rx_bandwidth_middles[] = {225, 281, 356, 450, 562, 706, 893, 1125, 1406, 1781, 2250};
static float tx_bandwidth_middles[] = {90, 112, 142, 180, 225, 282, 357, 450, 562, 712, 900};

int cariboulite_radio_set_modem_state(cariboulite_radio_state_st* radio, cariboulite_radio_state_cmd_en state);

void cariboulite_radio_debug(cariboulite_radio_state_st* radio)
{
    at86rf215_radio_state_cmd_en state = at86rf215_radio_get_state(&radio->sys->modem, at86rf215_rf_channel_2400mhz);
    printf("rf state %u\n",state);
    
    at86rf215_iq_interface_config_st cfg;
    at86rf215_get_iq_if_cfg(&radio->sys->modem,&cfg,1);
    
    uint8_t debug_word;
    caribou_fpga_get_debug (&radio->sys->fpga, &debug_word);
    printf("debug word vale %02X\n",(int)debug_word);
}

//=========================================================================
int cariboulite_radio_init(cariboulite_radio_state_st* radio, sys_st *sys, cariboulite_channel_en type)
{
	memset (radio, 0, sizeof(cariboulite_radio_state_st));

	radio->sys = sys;
    radio->active = false;
    radio->channel_direction = cariboulite_channel_dir_rx;
    radio->type = type;
    radio->cw_output = false;
    radio->lo_output = false;
    radio->tx_loopback_anabled = false;
    radio->smi_channel_id = GET_SMI_CH(type);
    
    cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_reset);
    usleep(100000);
    
    return 0;
}

//=========================================================================
int cariboulite_radio_dispose(cariboulite_radio_state_st* radio)
{
	cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_rx, false);

    at86rf215_radio_set_state( &radio->sys->modem, 
								GET_MODEM_CH(radio->type), 
								at86rf215_radio_state_cmd_trx_off);
    radio->state = cariboulite_radio_state_cmd_trx_off;

	// Type specific
	if (radio->type == cariboulite_channel_hif)
	{
    	caribou_fpga_set_io_ctrl_mode (&radio->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
	}
    return 0;
}


//=======================================================================================
int cariboulite_radio_ext_ref ( sys_st *sys, cariboulite_ext_ref_freq_en ref)
{
    switch(ref)
    {
        case cariboulite_ext_ref_26mhz:
            ZF_LOGD("Setting ext_ref = 26MHz");
            at86rf215_set_clock_output(&sys->modem, at86rf215_drive_current_8ma, at86rf215_clock_out_freq_26mhz);
            rffc507x_setup_reference_freq(&sys->mixer, 26e6);
            break;
        case cariboulite_ext_ref_32mhz:
            ZF_LOGD("Setting ext_ref = 32MHz");
            at86rf215_set_clock_output(&sys->modem, at86rf215_drive_current_8ma, at86rf215_clock_out_freq_32mhz);
            rffc507x_setup_reference_freq(&sys->mixer, 32e6);
            break;
        case cariboulite_ext_ref_off:
            ZF_LOGD("Setting ext_ref = OFF");
            at86rf215_set_clock_output(&sys->modem, at86rf215_drive_current_8ma, at86rf215_clock_out_freq_off);
            break;
        default:
            return -1;
        break;
    }
    return 0;
}

//=========================================================================
int cariboulite_radio_sync_information(cariboulite_radio_state_st* radio)
{
	if (cariboulite_radio_get_mod_state (radio, NULL) != 0) { return -1; }
    if (cariboulite_radio_get_rx_gain_control(radio, NULL, NULL) != 0) { return -1; }
    if (cariboulite_radio_get_rx_bandwidth(radio, NULL) != 0) { return -1; }
    if (cariboulite_radio_get_tx_power(radio, NULL) != 0) { return -1; }
    if (cariboulite_radio_get_rssi(radio, NULL) != 0) { return -1; }
    if (cariboulite_radio_get_energy_det(radio, NULL) != 0) { return -1; }
    return 0;
}

//=========================================================================
int cariboulite_radio_get_mod_state (cariboulite_radio_state_st* radio, cariboulite_radio_state_cmd_en *state)
{
    radio->state = (cariboulite_radio_state_cmd_en)at86rf215_radio_get_state(&radio->sys->modem, GET_MODEM_CH(radio->type));

    if (state) *state = radio->state;
    return 0;
}

//=========================================================================
int cariboulite_radio_get_mod_intertupts (cariboulite_radio_state_st* radio, cariboulite_radio_irq_st **irq_table)
{
	at86rf215_irq_st irq = {0};
    at86rf215_get_irqs(&radio->sys->modem, &irq, 0);

	memcpy (&radio->interrupts, 
			(radio->type == cariboulite_channel_s1g) ? (&irq.radio09) : (&irq.radio24),
			sizeof(cariboulite_radio_irq_st));

	if (irq_table) *irq_table = &radio->interrupts;

    return 0;
}

//=========================================================================
int cariboulite_radio_set_rx_gain_control(cariboulite_radio_state_st* radio, 
                                    bool rx_agc_on,
                                    int rx_gain_value_db)
{
    int control_gain_val = (int)roundf((float)(rx_gain_value_db) / 3.0f);
    if (control_gain_val < 0) control_gain_val = 0;
    if (control_gain_val > 23) control_gain_val = 23;

    at86rf215_radio_agc_ctrl_st rx_gain_control = 
    {
        .agc_measure_source_not_filtered = 1,
        .avg = at86rf215_radio_agc_averaging_32,
        .reset_cmd = 0,
        .freeze_cmd = 0,
        .enable_cmd = rx_agc_on,
        .att = at86rf215_radio_agc_relative_atten_21_db,
        .gain_control_word = control_gain_val,
    };

    at86rf215_radio_setup_agc(&radio->sys->modem, GET_MODEM_CH(radio->type), &rx_gain_control);
    radio->rx_agc_on = rx_agc_on;
    radio->rx_gain_value_db = rx_gain_value_db;
    return 0;
}

//=========================================================================
int cariboulite_radio_get_rx_gain_control(cariboulite_radio_state_st* radio,
                                    bool *rx_agc_on,
                                    int *rx_gain_value_db)
{
    at86rf215_radio_agc_ctrl_st agc_ctrl = {0};
    at86rf215_radio_get_agc(&radio->sys->modem, GET_MODEM_CH(radio->type), &agc_ctrl);

    radio->rx_agc_on = agc_ctrl.enable_cmd;
    radio->rx_gain_value_db = agc_ctrl.gain_control_word * 3;
    if (rx_agc_on) *rx_agc_on = radio->rx_agc_on;
    if (rx_gain_value_db) *rx_gain_value_db = radio->rx_gain_value_db;
    return 0;
}

//=========================================================================
int cariboulite_radio_get_rx_gain_limits(cariboulite_radio_state_st* radio, 
                                    int *rx_min_gain_value_db,
                                    int *rx_max_gain_value_db,
                                    int *rx_gain_value_resolution_db)
{
	if (rx_min_gain_value_db) *rx_min_gain_value_db = 0;
    if (rx_max_gain_value_db) *rx_max_gain_value_db = 23*3;
    if (rx_gain_value_resolution_db) *rx_gain_value_resolution_db = 3;
    return 0;
}

//=========================================================================
int cariboulite_radio_set_rx_bandwidth(cariboulite_radio_state_st* radio, 
                                 		cariboulite_radio_rx_bw_en rx_bw)
{
    cariboulite_radio_f_cut_en fcut = cariboulite_radio_rx_f_cut_half_fs;
    radio->rx_fcut = fcut;

    at86rf215_radio_set_rx_bw_samp_st cfg = 
    {
        .inverter_sign_if = 0,
        .shift_if_freq = 1,                 // A value of one configures the receiver to shift the IF frequency
                                            // by factor of 1.25. This is useful to place the image frequency according
                                            // to channel scheme. This increases the IF frequency to max 2.5MHz
                                            // thus places the internal LO fasr away from the signal => lower noise
        .bw = (at86rf215_radio_rx_bw_en)rx_bw,
        .fcut = (at86rf215_radio_f_cut_en)radio->rx_fcut,             // keep the same
        .fs = (at86rf215_radio_sample_rate_en)radio->rx_fs,           // keep the same
    };
    at86rf215_radio_set_rx_bandwidth_sampling(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
    radio->rx_bw = rx_bw;
    return 0;
}

//=========================================================================
int cariboulite_radio_set_rx_bandwidth_flt(cariboulite_radio_state_st* radio, float bw_hz)
{
    cariboulite_radio_rx_bw_en bw = cariboulite_radio_rx_bw_200KHz;
    
    if (bw_hz <= rx_bandwidth_middles[0]) bw = cariboulite_radio_rx_bw_200KHz;
    else if (bw_hz <= rx_bandwidth_middles[1]) bw = cariboulite_radio_rx_bw_250KHz;
    else if (bw_hz <= rx_bandwidth_middles[2]) bw = cariboulite_radio_rx_bw_312KHz;
    else if (bw_hz <= rx_bandwidth_middles[3]) bw = cariboulite_radio_rx_bw_400KHz;
    else if (bw_hz <= rx_bandwidth_middles[4]) bw = cariboulite_radio_rx_bw_500KHz;
    else if (bw_hz <= rx_bandwidth_middles[5]) bw = cariboulite_radio_rx_bw_625KHz;
    else if (bw_hz <= rx_bandwidth_middles[6]) bw = cariboulite_radio_rx_bw_787KHz;
    else if (bw_hz <= rx_bandwidth_middles[7]) bw = cariboulite_radio_rx_bw_1000KHz;
    else if (bw_hz <= rx_bandwidth_middles[8]) bw = cariboulite_radio_rx_bw_1250KHz;
    else if (bw_hz <= rx_bandwidth_middles[9]) bw = cariboulite_radio_rx_bw_1562KHz;
    else if (bw_hz <= rx_bandwidth_middles[10]) bw = cariboulite_radio_rx_bw_2000KHz;
    else bw = cariboulite_radio_rx_bw_2500KHz;

    return cariboulite_radio_set_rx_bandwidth(radio, bw);
}

//=========================================================================
int cariboulite_radio_get_rx_bandwidth(cariboulite_radio_state_st* radio, 
                                 cariboulite_radio_rx_bw_en *rx_bw)
{
    at86rf215_radio_set_rx_bw_samp_st cfg = {0};
    at86rf215_radio_get_rx_bandwidth_sampling(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
    radio->rx_bw = (cariboulite_radio_rx_bw_en)cfg.bw;
    radio->rx_fcut = (cariboulite_radio_f_cut_en)cfg.fcut;
    radio->rx_fs = (cariboulite_radio_sample_rate_en)cfg.fs;
    if (rx_bw) *rx_bw = radio->rx_bw;
    return 0;
}

//=========================================================================
int cariboulite_radio_get_rx_bandwidth_flt(cariboulite_radio_state_st* radio, float* bw_hz)
{
    cariboulite_radio_rx_bw_en bw;
    cariboulite_radio_get_rx_bandwidth(radio, &bw);
    
    if (bw_hz == NULL) return 0;
    
    switch(bw)
    {
        case cariboulite_radio_rx_bw_200KHz: *bw_hz = 200e5; break;
        case cariboulite_radio_rx_bw_250KHz: *bw_hz = 250e5; break;
        case cariboulite_radio_rx_bw_312KHz: *bw_hz = 312e5; break;
        case cariboulite_radio_rx_bw_400KHz: *bw_hz = 400e5; break;
        case cariboulite_radio_rx_bw_500KHz: *bw_hz = 500e5; break;
        case cariboulite_radio_rx_bw_625KHz: *bw_hz = 625e5; break;
        case cariboulite_radio_rx_bw_787KHz: *bw_hz = 787e5; break;
        case cariboulite_radio_rx_bw_1000KHz: *bw_hz = 1000e5; break;
        case cariboulite_radio_rx_bw_1250KHz: *bw_hz = 1250e5; break;
        case cariboulite_radio_rx_bw_1562KHz: *bw_hz = 1562e5; break;
        case cariboulite_radio_rx_bw_2000KHz: *bw_hz = 2000e5; break;
        case cariboulite_radio_rx_bw_2500KHz:
        default: *bw_hz = 2500e5; break;
    }
    return 0;
}

//=========================================================================
int cariboulite_radio_set_rx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   cariboulite_radio_sample_rate_en rx_sample_rate,
                                   cariboulite_radio_f_cut_en rx_cutoff)
{
    at86rf215_radio_set_rx_bw_samp_st cfg = 
    {
        .inverter_sign_if = 0,              // A value of one configures the receiver to implement the inverted-sign
                                            // IF frequency. Use default setting for normal operation
        .shift_if_freq = 1,                 // A value of one configures the receiver to shift the IF frequency
                                            // by factor of 1.25. This is useful to place the image frequency according
                                            // to channel scheme.
        .bw = (at86rf215_radio_rx_bw_en)radio->rx_bw,                 // keep the same
        .fcut = (at86rf215_radio_f_cut_en)rx_cutoff,
        .fs = (at86rf215_radio_sample_rate_en)rx_sample_rate,
    };
    at86rf215_radio_set_rx_bandwidth_sampling(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
    radio->rx_fs = rx_sample_rate;
    radio->rx_fcut = rx_cutoff;
    
    // setup the smi sample rate for timeout
    uint32_t sample_rate = 4000000;
    switch(rx_sample_rate)
    {
        case cariboulite_radio_rx_sample_rate_4000khz: sample_rate = 4000000; break;
        case cariboulite_radio_rx_sample_rate_2000khz: sample_rate = 2000000; break;
        case cariboulite_radio_rx_sample_rate_1333khz: sample_rate = 1333000; break;
        case cariboulite_radio_rx_sample_rate_1000khz: sample_rate = 1000000; break;
        case cariboulite_radio_rx_sample_rate_800khz: sample_rate = 800000; break;
        case cariboulite_radio_rx_sample_rate_666khz: sample_rate = 666000; break;
        case cariboulite_radio_rx_sample_rate_500khz: sample_rate = 500000; break;
        case cariboulite_radio_rx_sample_rate_400khz: sample_rate = 400000; break;
        default: sample_rate = 4000000; break;
    }
    
    caribou_smi_set_sample_rate(&radio->sys->smi, sample_rate);
    return 0;
}

//=========================================================================
static cariboulite_radio_sample_rate_en sample_rate_from_flt(float sample_rate_hz)
{
    cariboulite_radio_sample_rate_en sample_rate = cariboulite_radio_rx_sample_rate_4000khz;
    if (sample_rate_hz >= sample_rate_middles[0]) sample_rate = cariboulite_radio_rx_sample_rate_4000khz;
    else if (sample_rate_hz >= sample_rate_middles[1]) sample_rate = cariboulite_radio_rx_sample_rate_2000khz;
    else if (sample_rate_hz >= sample_rate_middles[2]) sample_rate = cariboulite_radio_rx_sample_rate_1333khz;
    else if (sample_rate_hz >= sample_rate_middles[3]) sample_rate = cariboulite_radio_rx_sample_rate_1000khz;
    else if (sample_rate_hz >= sample_rate_middles[4]) sample_rate = cariboulite_radio_rx_sample_rate_800khz;
    else if (sample_rate_hz >= sample_rate_middles[5]) sample_rate = cariboulite_radio_rx_sample_rate_666khz;
    else if (sample_rate_hz >= sample_rate_middles[6]) sample_rate = cariboulite_radio_rx_sample_rate_500khz;
    else sample_rate = cariboulite_radio_rx_sample_rate_400khz;
    return sample_rate;
}

//=========================================================================
static float sample_rate_to_flt(cariboulite_radio_sample_rate_en sample_rate)
{
    float sample_rate_hz = 4000000;
    switch(sample_rate)
    {
        case cariboulite_radio_rx_sample_rate_4000khz: sample_rate_hz = 4000000; break;
        case cariboulite_radio_rx_sample_rate_2000khz: sample_rate_hz = 2000000; break;
        case cariboulite_radio_rx_sample_rate_1333khz: sample_rate_hz = 1333000; break;
        case cariboulite_radio_rx_sample_rate_1000khz: sample_rate_hz = 1000000; break;
        case cariboulite_radio_rx_sample_rate_800khz: sample_rate_hz = 800000; break;
        case cariboulite_radio_rx_sample_rate_666khz: sample_rate_hz = 666000; break;
        case cariboulite_radio_rx_sample_rate_500khz: sample_rate_hz = 500000; break;
        case cariboulite_radio_rx_sample_rate_400khz: sample_rate_hz = 400000; break;
        default: sample_rate_hz = 4000000; break;
    }
    return sample_rate_hz;
}

//=========================================================================
int cariboulite_radio_set_rx_sample_rate_flt(cariboulite_radio_state_st* radio, float sample_rate_hz)
{
    cariboulite_radio_sample_rate_en rx_sample_rate = sample_rate_from_flt(sample_rate_hz);
    cariboulite_radio_f_cut_en rx_cutoff = radio->rx_fcut;
    return cariboulite_radio_set_rx_samp_cutoff(radio, rx_sample_rate, rx_cutoff);
}

//=========================================================================
int cariboulite_radio_get_rx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   cariboulite_radio_sample_rate_en *rx_sample_rate,
                                   cariboulite_radio_f_cut_en *rx_cutoff)
{
    cariboulite_radio_get_rx_bandwidth(radio, NULL);
    if (rx_sample_rate) *rx_sample_rate = (cariboulite_radio_sample_rate_en)radio->rx_fs;
    if (rx_cutoff) *rx_cutoff = (cariboulite_radio_f_cut_en)radio->rx_fcut;
    return 0;
}

//=========================================================================
int cariboulite_radio_get_rx_sample_rate_flt(cariboulite_radio_state_st* radio, float *sample_rate_hz)
{
    cariboulite_radio_sample_rate_en rx_sample_rate;
    cariboulite_radio_get_rx_samp_cutoff(radio, &rx_sample_rate, NULL);
    
    if (sample_rate_hz == NULL) return 0;
    
    *sample_rate_hz = sample_rate_to_flt(rx_sample_rate);
    return 0;
}

//=========================================================================
int cariboulite_radio_set_tx_power(cariboulite_radio_state_st* radio, int tx_power_dbm)
{
	float x = tx_power_dbm;
	float tx_power_ctrl_model;
	int tx_power_ctrl = 0;
	
	if (radio->type == cariboulite_channel_s1g)
	{
		if (tx_power_dbm < -14) tx_power_dbm = -14;
		if (tx_power_dbm > 14) tx_power_dbm = 14;

		x = tx_power_dbm;
		tx_power_ctrl_model = roundf(0.001502f*x*x*x + 0.020549f*x*x + 0.991045f*x + 13.727758f);
		tx_power_ctrl = (int)tx_power_ctrl_model;
		if (tx_power_ctrl < 0) tx_power_ctrl = 0;
		if (tx_power_ctrl > 31) tx_power_ctrl = 31;
	}
	else if (radio->type == cariboulite_channel_hif)
	{
		if (tx_power_dbm < -12) tx_power_dbm = -12;
		if (tx_power_dbm > 14) tx_power_dbm = 14;

		x = tx_power_dbm;
		tx_power_ctrl_model = roundf(0.000710f*x*x*x*x + 0.010521f*x*x*x + 0.015169f*x*x + 0.914333f*x + 12.254084f);
		tx_power_ctrl = (int)tx_power_ctrl_model;
		if (tx_power_ctrl < 0) tx_power_ctrl = 0;
		if (tx_power_ctrl > 31) tx_power_ctrl = 31;
	}
    
    /*tx_power_ctrl = tx_power_dbm + 17;
    if (tx_power_ctrl < 0) tx_power_ctrl = 0;
	if (tx_power_ctrl > 31) tx_power_ctrl = 31;*/
	
    at86rf215_radio_tx_ctrl_st cfg =
    {
        .pa_ramping_time = at86rf215_radio_tx_pa_ramp_16usec,
        .current_reduction = at86rf215_radio_pa_current_reduction_0ma,  // we can use this to gain some more
                                                                        // granularity with the tx gain control
        .tx_power = tx_power_ctrl,
        .analog_bw = (at86rf215_radio_tx_cut_off_en)radio->tx_bw,            // same as before
        .digital_bw = (at86rf215_radio_f_cut_en)radio->tx_fcut,         // same as before
        .fs = (at86rf215_radio_sample_rate_en)radio->tx_fs,                   // same as before
        .direct_modulation = 0,
    };

    at86rf215_radio_setup_tx_ctrl(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
    radio->tx_power = tx_power_dbm;
	
    return 0;
}

//=========================================================================
int cariboulite_radio_get_tx_power(cariboulite_radio_state_st* radio, int *tx_power_dbm)
{
    at86rf215_radio_tx_ctrl_st cfg = {0};
    at86rf215_radio_get_tx_ctrl(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
	
	float x = cfg.tx_power;
	float actual_model = 0;
	
	if (radio->type == cariboulite_channel_s1g)
	{
		actual_model = -0.000546f*x*x*x + 0.014352f*x*x + 0.902754f*x - 13.954753f;
	}
	else if (radio->type == cariboulite_channel_hif)
	{
		actual_model = 0.000031f*x*x*x*x - 0.002344f*x*x*x + 0.040478f*x*x + 0.712209f*x - 11.168502;
	}
    
    /*actual_model = x - 17;
    if (actual_model < -17) actual_model = -17;
	if (actual_model > 14) actual_model = 14;*/
    
    radio->tx_power = (int)(actual_model);
    radio->tx_bw = (cariboulite_radio_tx_cut_off_en)cfg.analog_bw;
    radio->tx_fcut = (cariboulite_radio_f_cut_en)cfg.digital_bw;
    radio->tx_fs = (cariboulite_radio_sample_rate_en)cfg.fs;

    if (tx_power_dbm) *tx_power_dbm = radio->tx_power;
    return 0;
}

//=========================================================================
int cariboulite_radio_set_tx_bandwidth(cariboulite_radio_state_st* radio, 
                                 cariboulite_radio_tx_cut_off_en tx_bw)
{
    at86rf215_radio_tx_ctrl_st cfg = 
    {
        .pa_ramping_time = at86rf215_radio_tx_pa_ramp_16usec,
        .current_reduction = at86rf215_radio_pa_current_reduction_0ma,  // we can use this to gain some more
                                                                        // granularity with the tx gain control
        .tx_power = 18 + radio->tx_power,     // same as before
        .analog_bw = (at86rf215_radio_tx_cut_off_en)tx_bw,
        .digital_bw = (at86rf215_radio_f_cut_en)radio->tx_fcut,         // same as before
        .fs = (at86rf215_radio_sample_rate_en)radio->tx_fs,                   // same as before
        .direct_modulation = 0,
    };

    at86rf215_radio_setup_tx_ctrl(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
    radio->tx_bw = tx_bw;

    return 0;
}

//=========================================================================
int cariboulite_radio_set_tx_bandwidth_flt(cariboulite_radio_state_st* radio, float tx_bw)
{
    cariboulite_radio_tx_cut_off_en bw = cariboulite_radio_tx_cut_off_80khz;
    
    if (tx_bw <= tx_bandwidth_middles[0]) bw = cariboulite_radio_tx_cut_off_80khz;
    else if (tx_bw <= tx_bandwidth_middles[1]) bw = cariboulite_radio_tx_cut_off_100khz;
    else if (tx_bw <= tx_bandwidth_middles[2]) bw = cariboulite_radio_tx_cut_off_125khz;
    else if (tx_bw <= tx_bandwidth_middles[3]) bw = cariboulite_radio_tx_cut_off_160khz;
    else if (tx_bw <= tx_bandwidth_middles[4]) bw = cariboulite_radio_tx_cut_off_200khz;
    else if (tx_bw <= tx_bandwidth_middles[5]) bw = cariboulite_radio_tx_cut_off_250khz;
    else if (tx_bw <= tx_bandwidth_middles[6]) bw = cariboulite_radio_tx_cut_off_315khz;
    else if (tx_bw <= tx_bandwidth_middles[7]) bw = cariboulite_radio_tx_cut_off_400khz;
    else if (tx_bw <= tx_bandwidth_middles[8]) bw = cariboulite_radio_tx_cut_off_500khz;
    else if (tx_bw <= tx_bandwidth_middles[9]) bw = cariboulite_radio_tx_cut_off_625khz;
    else if (tx_bw <= tx_bandwidth_middles[10]) bw = cariboulite_radio_tx_cut_off_800khz;
    else bw = cariboulite_radio_tx_cut_off_1000khz;

    return cariboulite_radio_set_tx_bandwidth(radio, bw);
}

//=========================================================================
int cariboulite_radio_get_tx_bandwidth(cariboulite_radio_state_st* radio, 
                                 cariboulite_radio_tx_cut_off_en *tx_bw)
{
    cariboulite_radio_get_tx_power(radio, NULL);
    if (tx_bw) *tx_bw = radio->tx_bw;
    return 0;
}

//=========================================================================
int cariboulite_radio_get_tx_bandwidth_flt(cariboulite_radio_state_st* radio, float *tx_bw)
{
    cariboulite_radio_tx_cut_off_en bw;
    cariboulite_radio_get_tx_bandwidth(radio, &bw);
    
    if (tx_bw == NULL) return 0;
    
    switch(bw)
    {
        case cariboulite_radio_tx_cut_off_80khz: *tx_bw = 80e5; break;
        case cariboulite_radio_tx_cut_off_100khz: *tx_bw = 100e5; break;
        case cariboulite_radio_tx_cut_off_125khz: *tx_bw = 125e5; break;
        case cariboulite_radio_tx_cut_off_160khz: *tx_bw = 160e5; break;
        case cariboulite_radio_tx_cut_off_200khz: *tx_bw = 200e5; break;
        case cariboulite_radio_tx_cut_off_250khz: *tx_bw = 250e5; break;
        case cariboulite_radio_tx_cut_off_315khz: *tx_bw = 315e5; break;
        case cariboulite_radio_tx_cut_off_400khz: *tx_bw = 400e5; break;
        case cariboulite_radio_tx_cut_off_500khz: *tx_bw = 500e5; break;
        case cariboulite_radio_tx_cut_off_625khz: *tx_bw = 625e5; break;
        case cariboulite_radio_tx_cut_off_800khz: *tx_bw = 800e5; break;
        case cariboulite_radio_tx_cut_off_1000khz:
        default: *tx_bw = 1000e5; break;
    }
    return 0;
}

//=========================================================================
int cariboulite_radio_set_tx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   cariboulite_radio_sample_rate_en tx_sample_rate,
                                   cariboulite_radio_f_cut_en tx_cutoff)
{
    uint8_t sample_gap = 0;
    at86rf215_radio_tx_ctrl_st cfg = 
    {
        .pa_ramping_time = at86rf215_radio_tx_pa_ramp_16usec,
        .current_reduction = at86rf215_radio_pa_current_reduction_0ma,  // we can use this to gain some more
                                                                        // granularity with the tx gain control
        .tx_power = 18 + radio->tx_power,     // same as before
        .analog_bw = (at86rf215_radio_tx_cut_off_en)radio->tx_bw,            // same as before
        .digital_bw = (at86rf215_radio_f_cut_en)tx_cutoff,
        .fs = (at86rf215_radio_sample_rate_en)tx_sample_rate,
        .direct_modulation = 0,
    };

    at86rf215_radio_setup_tx_ctrl(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
    radio->tx_fcut = (cariboulite_radio_f_cut_en)tx_cutoff;
    radio->tx_fs = tx_sample_rate;
    
    
    // setup the new sample rate in the FPGA
    switch (tx_sample_rate)
    {
        case cariboulite_radio_rx_sample_rate_4000khz: sample_gap = 0; break;
        case cariboulite_radio_rx_sample_rate_2000khz: sample_gap = 1; break;
        case cariboulite_radio_rx_sample_rate_1333khz: sample_gap = 2; break;
        case cariboulite_radio_rx_sample_rate_1000khz: sample_gap = 3; break;
        case cariboulite_radio_rx_sample_rate_800khz: sample_gap = 4; break;
        case cariboulite_radio_rx_sample_rate_666khz: sample_gap = 5; break;
        case cariboulite_radio_rx_sample_rate_500khz: sample_gap = 7; break;
        case cariboulite_radio_rx_sample_rate_400khz: sample_gap = 9; break;
        default: sample_gap = 0; break;
    }
    
    caribou_fpga_set_sys_ctrl_tx_sample_gap (&radio->sys->fpga, sample_gap);
    return 0;
}

//=========================================================================
int cariboulite_radio_set_tx_samp_cutoff_flt(cariboulite_radio_state_st* radio, float sample_rate_hz)
{
    cariboulite_radio_sample_rate_en tx_sample_rate = sample_rate_from_flt(sample_rate_hz);
    cariboulite_radio_f_cut_en tx_cutoff = radio->tx_fcut;
    return cariboulite_radio_set_tx_samp_cutoff(radio, tx_sample_rate, tx_cutoff);
}

//=========================================================================
int cariboulite_radio_get_tx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   cariboulite_radio_sample_rate_en *tx_sample_rate,
                                   cariboulite_radio_f_cut_en *tx_cutoff)
{
    uint8_t sample_gap = 0;
    cariboulite_radio_get_tx_power(radio, NULL);
    if (tx_sample_rate) *tx_sample_rate = radio->tx_fs;
    if (tx_cutoff) *tx_cutoff = radio->tx_fcut;
    
    // make sure that the sample rate matched the fpga gap   
    switch (radio->tx_fs)
    {
        case at86rf215_radio_rx_sample_rate_4000khz: sample_gap = 0; break;
        case at86rf215_radio_rx_sample_rate_2000khz: sample_gap = 1; break;
        case at86rf215_radio_rx_sample_rate_1333khz: sample_gap = 2; break;
        case at86rf215_radio_rx_sample_rate_1000khz: sample_gap = 3; break;
        case at86rf215_radio_rx_sample_rate_800khz: sample_gap = 4; break;
        case at86rf215_radio_rx_sample_rate_666khz: sample_gap = 5; break;
        case at86rf215_radio_rx_sample_rate_500khz: sample_gap = 7; break;
        case at86rf215_radio_rx_sample_rate_400khz: sample_gap = 9; break;
        default: sample_gap = 0; break;
    }
    
    caribou_fpga_set_sys_ctrl_tx_sample_gap (&radio->sys->fpga, sample_gap);
    
    return 0;
}

//=========================================================================
int cariboulite_radio_get_tx_samp_cutoff_flt(cariboulite_radio_state_st* radio, float *sample_rate_hz)
{
    cariboulite_radio_sample_rate_en tx_sample_rate;
    cariboulite_radio_get_tx_samp_cutoff(radio, &tx_sample_rate, NULL);
    
    if (sample_rate_hz == NULL) return 0;
    
    *sample_rate_hz = sample_rate_to_flt(tx_sample_rate);
    return 0;
}


//=========================================================================
int cariboulite_radio_get_rssi(cariboulite_radio_state_st* radio, float *rssi_dbm)
{
    float rssi = at86rf215_radio_get_rssi_dbm(&radio->sys->modem, GET_MODEM_CH(radio->type));
    if (rssi >= -127.0 && rssi <= 4)   // register only valid values
    {
        radio->rx_rssi = rssi;
        if (rssi_dbm) *rssi_dbm = rssi;
        return 0;
    }

	// if error maintain the older number and return error
    if (rssi_dbm) *rssi_dbm = radio->rx_rssi;
    
	return -1;
}

//=========================================================================
int cariboulite_radio_get_energy_det(cariboulite_radio_state_st* radio, float *energy_det_val)
{
    at86rf215_radio_energy_detection_st det = {0};
    at86rf215_radio_get_energy_detection(&radio->sys->modem, GET_MODEM_CH(radio->type), &det);
    
    if (det.energy_detection_value >= -127.0 && det.energy_detection_value <= 4)   // register only valid values
    {
        radio->rx_energy_detection_value = det.energy_detection_value;
        if (energy_det_val) *energy_det_val = radio->rx_energy_detection_value;
        return 0;
    }

    if (energy_det_val) *energy_det_val = radio->rx_energy_detection_value;

    return -1;
}

//======================================================================
typedef struct {
    int bit_count;               /* number of bits of entropy in data */
    int byte_count;              /* number of bytes of data in array */
    unsigned char buf[1];
} entropy_t;

static int add_entropy(uint8_t byte)
{
    int rand_fid = open("/dev/urandom", O_RDWR);
    if (rand_fid != 0)
    {
        // error opening device
        ZF_LOGE("Opening /dev/urandom device file failed");
        return -1;
    }

    entropy_t ent = {
        .bit_count = 8,
        .byte_count = 1,
        .buf = {byte},
    };

    if (ioctl(rand_fid, RNDADDENTROPY, &ent) != 0)
    {
        ZF_LOGE("IOCTL to /dev/urandom device file failed");
    }

    if (close(rand_fid) !=0 )
    {
        ZF_LOGE("Closing /dev/urandom device file failed");
        return -1;
    }

    return 0;
}

//=========================================================================
int cariboulite_radio_get_rand_val(cariboulite_radio_state_st* radio, uint8_t *rnd)
{
    radio->random_value = at86rf215_radio_get_random_value(&radio->sys->modem, GET_MODEM_CH(radio->type));
    if (rnd) *rnd = radio->random_value;

	// add the random number to the system entropy. why not :)
    add_entropy(radio->random_value);
    return 0;
}

//=================================================
// FREQUENCY CONVERSION LOGIC
//=================================================

//=================================================
bool cariboulite_radio_wait_mixer_lock(cariboulite_radio_state_st* radio, int retries)
{
	rffc507x_device_status_st stat = {0};
	
	// applicable only in 6G / FULL version
	if (radio->sys->board_info.numeric_product_id != system_type_cariboulite_full)
	{
		ZF_LOGW("Saved by the bell. We shouldn't be here!");
		return true;
	}

	// applicable only to the 6G channel
	if (radio->type != cariboulite_channel_hif)
	{
		return false;
	}

	int relock_retries = retries;
	do
	{
		rffc507x_readback_status(&radio->sys->mixer, NULL, &stat);
		rffc507x_print_stat(&stat);
		if (!stat.pll_lock) rffc507x_relock(&radio->sys->mixer);
	} while (!stat.pll_lock && relock_retries--);

	return stat.pll_lock;
}

//=========================================================================
int cariboulite_radio_set_modem_state(cariboulite_radio_state_st* radio, cariboulite_radio_state_cmd_en state)
{
    switch (state)
    {
        case cariboulite_radio_cmd_nop: ZF_LOGD("Setup Modem state to %d (nop)", state); break;
        case cariboulite_radio_cmd_sleep: ZF_LOGD("Setup Modem state to %d (sleep)", state); break;
        case cariboulite_radio_state_cmd_trx_off: ZF_LOGD("Setup Modem state to %d (trx off)", state); break;
        case cariboulite_radio_state_cmd_tx_prep: ZF_LOGD("Setup Modem state to %d (tx prep)", state); break;
        case cariboulite_radio_state_cmd_tx: ZF_LOGD("Setup Modem state to %d (tx)", state); break;
        case cariboulite_radio_state_cmd_rx: ZF_LOGD("Setup Modem state to %d (rx)", state); break;
        case cariboulite_radio_state_transition: ZF_LOGD("Setup Modem state to %d (transition)", state); break;
        case cariboulite_radio_state_cmd_reset: ZF_LOGD("Setup Modem state to %d (reset)", state); break;
        default:  ZF_LOGD("Setup Modem state to %d (unknown)", state); break;
    }
    at86rf215_radio_set_state( &radio->sys->modem,
                                GET_MODEM_CH(radio->type), 
                                (at86rf215_radio_state_cmd_en)state);
    radio->state = state;
    return 0;
}

//=================================================
bool cariboulite_radio_wait_modem_lock(cariboulite_radio_state_st* radio, int retries)
{
	at86rf215_radio_pll_ctrl_st cfg = {0};
	int relock_retries = retries;
	do
	{
		at86rf215_radio_get_pll_ctrl(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
	} while (!cfg.pll_locked && relock_retries--);

	return cfg.pll_locked;
}

//=================================================
bool cariboulite_radio_wait_for_lock( cariboulite_radio_state_st* radio, bool *mod, bool *mix, int retries)
{
	bool mix_lock = true, mod_lock = true;
	if (radio->type == cariboulite_channel_hif && mix != NULL)
	{
		mix_lock = cariboulite_radio_wait_mixer_lock(radio, retries);
		*mix = mix_lock;
	}

	if (mod != NULL)
	{
		mod_lock = cariboulite_radio_wait_modem_lock(radio, retries);
		if (mod) *mod = mod_lock;
	}

    return mix_lock && mod_lock;
}

//=========================================================================
cariboulite_ext_ref_freq_en cariboulite_radio_find_best_ref_freq(double f)
{
    return cariboulite_ext_ref_32mhz;
    double f_rf_mod_32 = (f / 32e6);
    double f_rf_mod_26 = (f / 26e6);
    f_rf_mod_32 -= (uint64_t)(f_rf_mod_32);
    f_rf_mod_26 -= (uint64_t)(f_rf_mod_26);
    f_rf_mod_32 *= 32e6;
    f_rf_mod_26 *= 26e6;
    if (f_rf_mod_32 > 16e6) f_rf_mod_32 = 32e6 - f_rf_mod_32;
    if (f_rf_mod_26 > 13e6) f_rf_mod_26 = 26e6 - f_rf_mod_26;
    return f_rf_mod_32 > f_rf_mod_26 ? cariboulite_ext_ref_32mhz : cariboulite_ext_ref_26mhz;
}

#define FREQ_IN_ISM_S1G_RANGE(f)  (((f)>=CARIBOULITE_S1G_MIN1&&(f)<=CARIBOULITE_S1G_MAX1)||((f)>=CARIBOULITE_S1G_MIN2&&(f)<=CARIBOULITE_S1G_MAX2))
#define FREQ_IN_ISM_24G_RANGE(f)  ((f)>=CARIBOULITE_2G4_MIN&&(f)<=CARIBOULITE_2G4_MAX)

//=========================================================================
int cariboulite_radio_set_frequency(cariboulite_radio_state_st* radio, 
									bool break_before_make,
									double *freq)
{
    double f_rf = *freq;
    double modem_act_freq = 0.0;
    double lo_act_freq = 0.0;
    double act_freq = 0.0;
    cariboulite_ext_ref_freq_en ext_ref_choice = cariboulite_radio_find_best_ref_freq(f_rf);
    cariboulite_conversion_dir_en conversion_direction = conversion_dir_none;

    //--------------------------------------------------------------------------------
    // SUB 1GHZ CONFIGURATION
    //--------------------------------------------------------------------------------
    if (radio->type == cariboulite_channel_s1g)
    {
        if (FREQ_IN_ISM_S1G_RANGE(f_rf))
        {
            if (break_before_make) cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_trx_off);

            modem_act_freq = at86rf215_setup_channel (&radio->sys->modem, 
                                                    at86rf215_rf_channel_900mhz, 
                                                    (uint32_t)f_rf);

            radio->if_frequency = 0;
            radio->lo_pll_locked = 1;
            radio->if_frequency = modem_act_freq;
            radio->actual_rf_frequency = radio->if_frequency;
            radio->requested_rf_frequency = f_rf;
            radio->rf_frequency_error = radio->actual_rf_frequency - radio->requested_rf_frequency;   

            // return actual frequency
            *freq = radio->actual_rf_frequency;
        }
        else
        {
            ZF_LOGE("Unsupported frequency for the ISM S1G channel - %.2f Hz, deactivating channel", f_rf);
            cariboulite_radio_activate_channel(radio, radio->channel_direction, false);
            return -1;
        }
    }
	//--------------------------------------------------------------------------------
    // ISM 2.4 GHZ CONFIGURATION
    //--------------------------------------------------------------------------------
	else if (radio->type == cariboulite_channel_hif && 
			 radio->sys->board_info.numeric_product_id == system_type_cariboulite_ism)
	{
		if (FREQ_IN_ISM_24G_RANGE(f_rf))
        {
            if (break_before_make) cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_trx_off);
            
            modem_act_freq = at86rf215_setup_channel (&radio->sys->modem, 
                                                    at86rf215_rf_channel_2400mhz, 
                                                    (uint32_t)f_rf);

            radio->if_frequency = 0;
            radio->lo_pll_locked = true;
            radio->if_frequency = modem_act_freq;
            radio->actual_rf_frequency = radio->if_frequency;
            radio->requested_rf_frequency = f_rf;
            radio->rf_frequency_error = radio->actual_rf_frequency - radio->requested_rf_frequency;   

            // return actual frequency
            *freq = radio->actual_rf_frequency;
        }
        else
        {
            ZF_LOGE("Unsupported frequency for the ISM HiF channel - %.2f Hz, deactivating channel", f_rf);
            cariboulite_radio_activate_channel(radio, radio->channel_direction, false);
            return -1;
        }
	}
    //--------------------------------------------------------------------------------
    // FULL 30-6GHz CONFIGURATION
    //--------------------------------------------------------------------------------
    else if (radio->type == cariboulite_channel_hif && 
			 radio->sys->board_info.numeric_product_id == system_type_cariboulite_full)
    {		
        // Changing the frequency may sometimes need to break RX / TX
        if (break_before_make)
        {
            // make sure that during the transition the modem is not transmitting and then
            // verify that the FE is in low power mode
            cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_trx_off);
            caribou_fpga_set_io_ctrl_mode (&radio->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
        }

        // Decide the conversion direction and IF/RF/LO
        //-------------------------------------
        if (f_rf >= CARIBOULITE_6G_MIN && 
            f_rf < (CARIBOULITE_2G4_MIN) )
        {
            // Setup the reference frequency as calculated before
            cariboulite_radio_ext_ref (radio->sys, ext_ref_choice);
            rffc507x_calibrate(&radio->sys->mixer);
            
            // region #1 - UP CONVERSION
            uint32_t modem_freq = CARIBOULITE_2G4_MAX;
            modem_act_freq = (double)at86rf215_setup_channel (&radio->sys->modem, 
																at86rf215_rf_channel_2400mhz, 
																modem_freq);
            
            // setup mixer LO according to the actual modem frequency          
			//lo_act_freq = rffc507x_set_frequency(&radio->sys->mixer, modem_act_freq - f_rf);
			//act_freq = modem_act_freq - lo_act_freq;
            
            lo_act_freq = rffc507x_set_frequency(&radio->sys->mixer, modem_act_freq + f_rf);
            act_freq = lo_act_freq - modem_act_freq;
            
            // setup fpga RFFE <= upconvert (tx / rx)
            conversion_direction = conversion_dir_up;
            caribou_smi_invert_iq(&radio->sys->smi, true);
        }
        //-------------------------------------
        else if ( f_rf >= CARIBOULITE_2G4_MIN && 
                f_rf < CARIBOULITE_2G4_MAX )
        {
			cariboulite_radio_ext_ref (radio->sys, cariboulite_ext_ref_off);
            // region #2 - bypass mode
            // setup modem frequency <= f_rf
            modem_act_freq = (double)at86rf215_setup_channel (&radio->sys->modem, 
                                                        at86rf215_rf_channel_2400mhz, 
                                                        (uint32_t)f_rf);
            lo_act_freq = 0;
            act_freq = modem_act_freq;
            conversion_direction = conversion_dir_none;
            caribou_smi_invert_iq(&radio->sys->smi, true);
        }
        //-------------------------------------
        else if ( f_rf >= (CARIBOULITE_2G4_MAX) && 
                f_rf < CARIBOULITE_6G_MAX )
        {
            // Setup the reference frequency as calculated before
            cariboulite_radio_ext_ref (radio->sys, ext_ref_choice);
            rffc507x_calibrate(&radio->sys->mixer);
            
            // region #3 - DOWN-CONVERSION
            uint32_t modem_freq = CARIBOULITE_2G4_MIN;
            modem_act_freq = (double)at86rf215_setup_channel (&radio->sys->modem, 
                                                                at86rf215_rf_channel_2400mhz, 
                                                                modem_freq);

            // setup mixer LO to according to actual modem frequency
			lo_act_freq = rffc507x_set_frequency(&radio->sys->mixer, f_rf - modem_act_freq);
            act_freq = lo_act_freq + modem_act_freq;

            // setup fpga RFFE <= downconvert (tx / rx)
            conversion_direction = conversion_dir_down;
            caribou_smi_invert_iq(&radio->sys->smi, true);
        }
        //-------------------------------------
        else
        {
            ZF_LOGE("Unsupported frequency for 6GHz channel - %.2f Hz, deactivating channel", f_rf);
            cariboulite_radio_activate_channel(radio, radio->channel_direction, false);
            return -1;
        }

        // Setup the frontend
        // This step takes the current radio direction of communication
        // and the down/up conversion decision made before to setup the RF front-end
        switch (conversion_direction)
        {
            case conversion_dir_up: 
                if (radio->channel_direction == cariboulite_channel_dir_rx) 
                {
                    caribou_fpga_set_io_ctrl_mode (&radio->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_rx_lowpass);
                }
                else if (radio->channel_direction == cariboulite_channel_dir_tx)
                {
                    caribou_fpga_set_io_ctrl_mode (&radio->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_tx_lowpass);
                }
                break;
            case conversion_dir_none: 
                caribou_fpga_set_io_ctrl_mode (&radio->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_bypass);
                break;
            case conversion_dir_down:
                if (radio->channel_direction == cariboulite_channel_dir_rx)
                {
                    caribou_fpga_set_io_ctrl_mode (&radio->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_rx_hipass);
                }
                else if (radio->channel_direction == cariboulite_channel_dir_tx)
                {
                    caribou_fpga_set_io_ctrl_mode (&radio->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_tx_hipass);
                }
                break;
            default: break;
        }

        // Make sure the LO and the IF PLLs are locked
        /*cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_tx_prep);
        if (!cariboulite_radio_wait_for_lock(radio, &radio->modem_pll_locked, 
                                            lo_act_freq > CARIBOULITE_MIN_LO ? &radio->lo_pll_locked : NULL, 
                                            100))
        {
            if (!radio->lo_pll_locked) ZF_LOGE("PLL MIXER failed to lock LO frequency (%.2f Hz), deactivating", lo_act_freq);
            if (!radio->modem_pll_locked) ZF_LOGE("PLL MODEM failed to lock IF frequency (%.2f Hz), deactivating", modem_act_freq);
            cariboulite_radio_activate_channel(radio, radio->channel_direction, false);
            return -1;
        }*/

        // Update the actual frequencies
        radio->lo_frequency = lo_act_freq;
        radio->if_frequency = modem_act_freq;
        radio->actual_rf_frequency = act_freq;
        radio->requested_rf_frequency = f_rf;
        radio->rf_frequency_error = radio->actual_rf_frequency - radio->requested_rf_frequency;
        if (freq) *freq = act_freq;
    }

    ZF_LOGD("Frequency setting CH: %d, Wanted: %.2f Hz, Set: %.2f Hz (MOD: %.2f, MIX: %.2f)", 
                    radio->type, f_rf, act_freq, modem_act_freq, lo_act_freq);
    
    // reactivate the channel if it was active before the frequency change request was issued
    return cariboulite_radio_activate_channel(radio, radio->channel_direction, radio->active);
}

//=========================================================================
int cariboulite_radio_get_frequency(cariboulite_radio_state_st* radio, 
                                	double *freq, double *lo, double* i_f)
{
    if (freq) *freq = radio->actual_rf_frequency;
    if (lo) *lo = radio->lo_frequency;
    if (i_f) *i_f = radio->if_frequency;
    return 0;
}

//=========================================================================


static int cariboulite_radio_tx_prep(cariboulite_radio_state_st* radio)
{
    int cal_i, cal_q;
            cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_tx_prep);  
            {
                // deactivate the channel and prep it for pll lock
                at86rf215_radio_get_tx_iq_calibration(&radio->sys->modem, GET_MODEM_CH(radio->type), &cal_i, &cal_q);

                ZF_LOGD("Setup Modem state tx_prep");
                radio->modem_pll_locked = cariboulite_radio_wait_modem_lock(radio, 5);
                if (!radio->modem_pll_locked)
                {
                    ZF_LOGE("PLL didn't lock");

                    // deactivate the channel if this happens
                    cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_trx_off);
                    return -1;
                }
            }
            //cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_tx); 

	return 0;
}

int cariboulite_radio_activate_channel(cariboulite_radio_state_st* radio,
                                        cariboulite_channel_dir_en dir,
                                        bool activate)
{
    int ret = 0;
    radio->channel_direction = dir;
    radio->active = activate;
    
	
    ZF_LOGD("Activating channel %d, dir = %s, activate = %d", radio->type, radio->channel_direction==cariboulite_channel_dir_rx?"RX":"TX", activate);

    
    // then deactivate the modem's stream
    cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_trx_off);
   
    // DEACTIVATION
    if (!activate) 
    {
        // if we deactivate, first shut off the smi stream
        ret = caribou_smi_set_driver_streaming_state(&radio->sys->smi, smi_stream_idle);
        return ret;
    }
    


	//===========================================================
	// ACTIVATE RX
	//===========================================================
    // Activate the channel according to the configurations
    // RX on both channels looks the same
    if (radio->channel_direction == cariboulite_channel_dir_rx)
    {   
        caribou_fpga_set_smi_ctrl_data_direction (&radio->sys->fpga, 1);
        // Setup the IQ stream properties
        smi_stream_state_en smi_state = smi_stream_idle;
        if (radio->smi_channel_id == caribou_smi_channel_900)
            smi_state = smi_stream_rx_channel_0;
        else if (radio->smi_channel_id == caribou_smi_channel_2400)
            smi_state = smi_stream_rx_channel_1;
        
        at86rf215_iq_interface_config_st modem_iq_config = {
            .loopback_enable = radio->tx_loopback_anabled,
            .drv_strength = at86rf215_iq_drive_current_4ma,
            .common_mode_voltage = at86rf215_iq_common_mode_v_ieee1596_1v2,
            .tx_control_with_iq_if = false,
            .radio09_mode = at86rf215_iq_if_mode,
            .radio24_mode = at86rf215_iq_if_mode,
            .clock_skew = at86rf215_iq_clock_data_skew_4_906ns,
        };
        at86rf215_setup_iq_if(&radio->sys->modem, &modem_iq_config);	
                
        // configure FPGA with the correct rx channel
		caribou_fpga_set_smi_channel (&radio->sys->fpga, radio->type == cariboulite_channel_s1g? caribou_fpga_smi_channel_0 : caribou_fpga_smi_channel_1);
        //caribou_fpga_set_io_ctrl_dig (&radio->sys->fpga, radio->type == cariboulite_channel_s1g? 0 : 1, 0);
	    if(cariboulite_radio_tx_prep(radio))
	    {

		return -1;
	    }
        
        // turn on the modem RX
        cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_rx);
        
        // turn on the SMI stream
        if (caribou_smi_set_driver_streaming_state(&radio->sys->smi, smi_state) != 0)
        {
            ZF_LOGD("Failed to configure modem with cmd_rx");
            return -1;
        }
    }
    
	//===========================================================
	// ACTIVATE TX
	//===========================================================
    else if (radio->channel_direction == cariboulite_channel_dir_tx)
    {
        caribou_fpga_set_smi_ctrl_data_direction (&radio->sys->fpga, 0);
        at86rf215_iq_interface_config_st modem_iq_config = 
        {
            .loopback_enable = radio->tx_loopback_anabled,
            .drv_strength = at86rf215_iq_drive_current_4ma,
            .common_mode_voltage = at86rf215_iq_common_mode_v_ieee1596_1v2,
            .tx_control_with_iq_if = !radio->cw_output,
            .radio09_mode = at86rf215_iq_if_mode,
            .radio24_mode = at86rf215_iq_if_mode,
            .clock_skew = at86rf215_iq_clock_data_skew_4_906ns,
        };
        at86rf215_setup_iq_if(&radio->sys->modem, &modem_iq_config);	

        
        // if its an LO frequency output from the mixer - no need for modem output
        // LO applicable only to the channel with the mixer
        if (radio->lo_output && 
			radio->type == cariboulite_channel_hif &&
			radio->sys->board_info.numeric_product_id == system_type_cariboulite_full)
        {
            // here we need to configure lo bypass on the mixer
            rffc507x_output_lo(&radio->sys->mixer, 1);
        }
        // otherwise we need the modem
        else if (radio->cw_output)
        {        
            ZF_LOGD("Transmitting with cw_output");
            cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_tx_prep);    
            
			if (radio->sys->board_info.numeric_product_id == system_type_cariboulite_full)
            {
				// make sure the mixer doesn't bypass the lo
				rffc507x_output_lo(&radio->sys->mixer, 0);
			}

            cariboulite_radio_set_tx_bandwidth(radio, cariboulite_radio_tx_cut_off_80khz);

            // CW output - constant I/Q values override
            at86rf215_radio_set_tx_dac_input_iq(&radio->sys->modem, 
                                                GET_MODEM_CH(radio->type), 
                                                1, 0x7E, 
                                                1, 0x3F);

            // transition to state TX
            //cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_tx); 
        }
		else
        {
            ZF_LOGD("Transmitting with iq");
            //cariboulite_radio_set_modem_state(radio, cariboulite_radio_state_cmd_tx_prep); 
            
            cariboulite_radio_set_tx_bandwidth(radio, radio->tx_bw);
            
            // CW output - disable
            at86rf215_radio_set_tx_dac_input_iq(&radio->sys->modem, 
                                                GET_MODEM_CH(radio->type), 
                                                0, 0x7E, 
                                                0, 0x3F);
            
            // apply the state
            caribou_smi_set_driver_streaming_state(&radio->sys->smi, smi_stream_tx_channel);            
            
                // ACTIVATION STEPS
            
            //caribou_fpga_set_sys_ctrl_tx_control_word(&radio->sys->fpga, 0x0); // send zero frames to the radio
	    if(cariboulite_radio_tx_prep(radio))
	    {

		return -1;
	    }

 
            usleep(100); // wait at least tx_start_delay
            caribou_fpga_set_sys_ctrl_tx_control_word(&radio->sys->fpga, 0x01);
            }

    }

    return 0;
}

//=========================================================================
int cariboulite_radio_set_cw_outputs(cariboulite_radio_state_st* radio, bool lo_out, bool cw_out)
{
    if (radio->lo_output && radio->type == cariboulite_channel_hif)
    {
        radio->lo_output = lo_out;
    }
    else
    {
        radio->lo_output = false;
    }
    radio->cw_output = cw_out;

    return 0;
}

//=========================================================================
int cariboulite_radio_get_cw_outputs(cariboulite_radio_state_st* radio, bool *lo_out, bool *cw_out)
{
    if (lo_out) *lo_out = radio->lo_output;
    if (cw_out) *cw_out = radio->cw_output;

    return 0;
}

//=========================================================================
// I/O Functions
//=========================================================================
int cariboulite_radio_read_samples(cariboulite_radio_state_st* radio,
                            cariboulite_sample_complex_int16* buffer,
                            cariboulite_sample_meta* metadata,
                            size_t length)
{
    int ret = 0;
      
    // CaribouSMI read   
    ret = caribou_smi_read( &radio->sys->smi, 
                            radio->smi_channel_id, 
                            (caribou_smi_sample_complex_int16*)buffer, 
                            (caribou_smi_sample_meta*)metadata, 
                            length);
    if (ret < 0)
    {
        // -2 reserved for debug mode
        if (ret == -1) {ZF_LOGE("SMI reading operation failed");}
        else if (ret == -2) {}
        else if (ret == -3) {ZF_LOGE("SMI data synchronization failed");}
        
    }
    else if (ret == 0)
    {
        ZF_LOGD("SMI reading operation returned timeout");
    }
    
    return ret;
}

//=========================================================================
int cariboulite_radio_write_samples(cariboulite_radio_state_st* radio,
                            cariboulite_sample_complex_int16* buffer,
                            size_t length)                            
{   
    // Caribou SMI write
    int ret = caribou_smi_write(&radio->sys->smi, 
                                radio->smi_channel_id, 
                                (caribou_smi_sample_complex_int16*)buffer, 
                                length);
    if (ret < 0)
    {
        ZF_LOGE("SMI writing operation failed");
    }
    else if (ret == 0)
    {
        ZF_LOGD("SMI writing operation returned timeout");
    }
    
    return ret;
}

//=========================================================================
size_t cariboulite_radio_get_native_mtu_size_samples(cariboulite_radio_state_st* radio)
{
    size_t num_samples = caribou_smi_get_native_batch_samples(&radio->sys->smi);
    //printf("DEBUG: native num samples: %lu\n", num_samples);
    return num_samples;
}
