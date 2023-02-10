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

#include "cariboulite.h"
#include "cariboulite_radio.h"
#include "cariboulite_events.h"
#include "cariboulite_setup.h"

#define GET_MODEM_CH(rad_ch)	((rad_ch)==cariboulite_channel_s1g ? at86rf215_rf_channel_900mhz : at86rf215_rf_channel_2400mhz)
#define GET_SMI_CH(rad_ch)		((rad_ch)==cariboulite_channel_s1g ? caribou_smi_channel_900 : caribou_smi_channel_2400)

//=========================================================================
void cariboulite_radio_init(cariboulite_radio_state_st* radio, sys_st *sys, cariboulite_channel_en type)
{
	memset (radio, 0, sizeof(cariboulite_radio_state_st));

	radio->sys = sys;
    radio->active = true;
    radio->channel_direction = cariboulite_channel_dir_rx;
    radio->type = type;
    radio->cw_output = false;
    radio->lo_output = false;
    radio->smi_channel_id = GET_SMI_CH(type);
    
    // activation of the channel
    cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_rx, true);
    usleep(10000);
    cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_rx, false);
}

//=========================================================================
int cariboulite_radio_dispose(cariboulite_radio_state_st* radio)
{
	radio->active = false;

    at86rf215_radio_set_state( &radio->sys->modem, 
								GET_MODEM_CH(radio->type), 
								at86rf215_radio_state_cmd_trx_off);
    radio->state = at86rf215_radio_state_cmd_trx_off;

	// Type specific
	if (radio->type == cariboulite_channel_6g)
	{
    	caribou_fpga_set_io_ctrl_mode (&radio->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
	}
}

//=========================================================================
int cariboulite_radio_sync_information(cariboulite_radio_state_st* radio)
{
	cariboulite_radio_get_mod_state (radio, NULL);
    cariboulite_radio_get_rx_gain_control(radio, NULL, NULL);
    cariboulite_radio_get_rx_bandwidth(radio, NULL);
    cariboulite_radio_get_tx_power(radio, NULL);
    cariboulite_radio_get_rssi(radio, NULL);
    cariboulite_radio_get_energy_det(radio, NULL);
}

//=======================================================================================
int cariboulite_radio_ext_ref ( sys_st *sys, cariboulite_ext_ref_freq_en ref)
{
    switch(ref)
    {
        case cariboulite_ext_ref_26mhz:
            ZF_LOGD("Setting ext_ref = 26MHz");
            at86rf215_set_clock_output(&sys->modem, at86rf215_drive_current_2ma, at86rf215_clock_out_freq_26mhz);
            rffc507x_setup_reference_freq(&sys->mixer, 26e6);
            break;
        case cariboulite_ext_ref_32mhz:
            ZF_LOGD("Setting ext_ref = 32MHz");
            at86rf215_set_clock_output(&sys->modem, at86rf215_drive_current_2ma, at86rf215_clock_out_freq_32mhz);
            rffc507x_setup_reference_freq(&sys->mixer, 32e6);
            break;
        case cariboulite_ext_ref_off:
            ZF_LOGD("Setting ext_ref = OFF");
            at86rf215_set_clock_output(&sys->modem, at86rf215_drive_current_2ma, at86rf215_clock_out_freq_off);
        default:
            return -1;
        break;
    }
}

//=========================================================================
int cariboulite_radio_get_mod_state (cariboulite_radio_state_st* radio, at86rf215_radio_state_cmd_en *state)
{
    radio->state  = at86rf215_radio_get_state(&radio->sys->modem, GET_MODEM_CH(radio->type));

    if (state) *state = radio->state;
    return 0;
}

//=========================================================================
int cariboulite_radio_get_mod_intertupts (cariboulite_radio_state_st* radio, at86rf215_radio_irq_st **irq_table)
{
	at86rf215_irq_st irq = {0};
    at86rf215_get_irqs(&radio->sys->modem, &irq, 0);

	memcpy (&radio->interrupts, 
			(radio->type == cariboulite_channel_s1g) ? (&irq.radio09) : (&irq.radio24),
			sizeof(at86rf215_radio_irq_st));

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
                                 		at86rf215_radio_rx_bw_en rx_bw)
{
    at86rf215_radio_f_cut_en fcut = at86rf215_radio_rx_f_cut_half_fs;

    // Automatically calculate the digital f_cut
    if (rx_bw >= at86rf215_radio_rx_bw_BW160KHZ_IF250KHZ && rx_bw <= at86rf215_radio_rx_bw_BW500KHZ_IF500KHZ)
        fcut = at86rf215_radio_rx_f_cut_0_25_half_fs;
    else if (rx_bw >= at86rf215_radio_rx_bw_BW630KHZ_IF1000KHZ && rx_bw <= at86rf215_radio_rx_bw_BW630KHZ_IF1000KHZ)
        fcut = at86rf215_radio_rx_f_cut_0_375_half_fs;
    else if (rx_bw >= at86rf215_radio_rx_bw_BW800KHZ_IF1000KHZ && rx_bw <= at86rf215_radio_rx_bw_BW1000KHZ_IF1000KHZ)
        fcut = at86rf215_radio_rx_f_cut_0_5_half_fs;
    else if (rx_bw >= at86rf215_radio_rx_bw_BW1250KHZ_IF2000KHZ && rx_bw <= at86rf215_radio_rx_bw_BW1250KHZ_IF2000KHZ)
        fcut = at86rf215_radio_rx_f_cut_0_75_half_fs;
    else 
        fcut = at86rf215_radio_rx_f_cut_half_fs;

    radio->rx_fcut = fcut;

    at86rf215_radio_set_rx_bw_samp_st cfg = 
    {
        .inverter_sign_if = 0,
        .shift_if_freq = 1,                 // A value of one configures the receiver to shift the IF frequency
                                            // by factor of 1.25. This is useful to place the image frequency according
                                            // to channel scheme. This increases the IF frequency to max 2.5MHz
                                            // thus places the internal LO fasr away from the signal => lower noise
        .bw = rx_bw,
        .fcut = radio->rx_fcut,             // keep the same
        .fs = radio->rx_fs,                 // keep the same
    };
    at86rf215_radio_set_rx_bandwidth_sampling(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
    radio->rx_bw = rx_bw;
    return 0;
}

//=========================================================================
int cariboulite_radio_get_rx_bandwidth(cariboulite_radio_state_st* radio, 
                                 at86rf215_radio_rx_bw_en *rx_bw)
{
    at86rf215_radio_set_rx_bw_samp_st cfg = {0};
    at86rf215_radio_get_rx_bandwidth_sampling(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
    radio->rx_bw = cfg.bw;
    radio->rx_fcut = cfg.fcut;
    radio->rx_fs = cfg.fs;
    if (rx_bw) *rx_bw = radio->rx_bw;
    return 0;
}

//=========================================================================
int cariboulite_radio_set_rx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   at86rf215_radio_sample_rate_en rx_sample_rate,
                                   at86rf215_radio_f_cut_en rx_cutoff)
{
    at86rf215_radio_set_rx_bw_samp_st cfg = 
    {
        .inverter_sign_if = 0,              // A value of one configures the receiver to implement the inverted-sign
                                            // IF frequency. Use default setting for normal operation
        .shift_if_freq = 1,                 // A value of one configures the receiver to shift the IF frequency
                                            // by factor of 1.25. This is useful to place the image frequency according
                                            // to channel scheme.
        .bw = radio->rx_bw,                 // keep the same
        .fcut = rx_cutoff,
        .fs = rx_sample_rate,
    };
    at86rf215_radio_set_rx_bandwidth_sampling(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
    radio->rx_fs = rx_sample_rate;
    radio->rx_fcut = rx_cutoff;
    return 0;
}

//=========================================================================
int cariboulite_radio_get_rx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   at86rf215_radio_sample_rate_en *rx_sample_rate,
                                   at86rf215_radio_f_cut_en *rx_cutoff)
{
    cariboulite_radio_get_rx_bandwidth(radio, NULL);
    if (rx_sample_rate) *rx_sample_rate = radio->rx_fs;
    if (rx_cutoff) *rx_cutoff = radio->rx_fcut;
    return 0;
}

//=========================================================================
int cariboulite_radio_set_tx_power(cariboulite_radio_state_st* radio, 
                             int tx_power_dbm)
{
	float x = tx_power_dbm;
	float tx_power_ctrl_model;
	int tx_power_ctrl = 0;
	
	if (radio->type == cariboulite_channel_s1g)
	{
		if (tx_power_dbm < -14) tx_power_dbm = -14;
		if (tx_power_dbm > 12) tx_power_dbm = 12;

		x = tx_power_dbm;
		tx_power_ctrl_model = roundf(0.001502f*x*x*x + 0.020549f*x*x + 0.991045f*x + 13.727758f);
		tx_power_ctrl = (int)tx_power_ctrl_model;
		if (tx_power_ctrl < 0) tx_power_ctrl = 0;
		if (tx_power_ctrl > 31) tx_power_ctrl = 31;
	}
	else if (radio->type == cariboulite_channel_6g)
	{
		if (tx_power_dbm < -12) tx_power_dbm = -12;
		if (tx_power_dbm > 9) tx_power_dbm = 9;

		x = tx_power_dbm;
		tx_power_ctrl_model = roundf(0.000710f*x*x*x*x + 0.010521f*x*x*x + 0.015169f*x*x + 0.914333f*x + 12.254084f);
		tx_power_ctrl = (int)tx_power_ctrl_model;
		if (tx_power_ctrl < 0) tx_power_ctrl = 0;
		if (tx_power_ctrl > 31) tx_power_ctrl = 31;
	}
	
    at86rf215_radio_tx_ctrl_st cfg =
    {
        .pa_ramping_time = at86rf215_radio_tx_pa_ramp_16usec,
        .current_reduction = at86rf215_radio_pa_current_reduction_0ma,  // we can use this to gain some more
                                                                        // granularity with the tx gain control
        .tx_power = tx_power_ctrl,
        .analog_bw = radio->tx_bw,            // same as before
        .digital_bw = radio->tx_fcut,         // same as before
        .fs = radio->tx_fs,                   // same as before
        .direct_modulation = 0,
    };

    at86rf215_radio_setup_tx_ctrl(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
    radio->tx_power = tx_power_dbm;
	
    return 0;
}

//=========================================================================
int cariboulite_radio_get_tx_power(cariboulite_radio_state_st* radio, 
                             int *tx_power_dbm)
{
    at86rf215_radio_tx_ctrl_st cfg = {0};
    at86rf215_radio_get_tx_ctrl(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
	
	float x = cfg.tx_power;
	float actual_model;
	
	if (radio->type == cariboulite_channel_s1g)
	{
		actual_model = -0.000546f*x*x*x + 0.014352f*x*x + 0.902754f*x - 13.954753f;
	}
	else if (radio->type == cariboulite_channel_6g)
	{
		actual_model = 0.000031f*x*x*x*x - 0.002344f*x*x*x + 0.040478f*x*x + 0.712209f*x - 11.168502;
	}

    radio->tx_power = (int)(actual_model);
    radio->tx_bw = cfg.analog_bw;
    radio->tx_fcut = cfg.digital_bw;
    radio->tx_fs = cfg.fs;

    if (tx_power_dbm) *tx_power_dbm = radio->tx_power;
    return 0;
}

//=========================================================================
int cariboulite_radio_set_tx_bandwidth(cariboulite_radio_state_st* radio, 
                                 at86rf215_radio_tx_cut_off_en tx_bw)
{
    at86rf215_radio_tx_ctrl_st cfg = 
    {
        .pa_ramping_time = at86rf215_radio_tx_pa_ramp_16usec,
        .current_reduction = at86rf215_radio_pa_current_reduction_0ma,  // we can use this to gain some more
                                                                        // granularity with the tx gain control
        .tx_power = 18 + radio->tx_power,     // same as before
        .analog_bw = tx_bw,
        .digital_bw = radio->tx_fcut,         // same as before
        .fs = radio->tx_fs,                   // same as before
        .direct_modulation = 0,
    };

    at86rf215_radio_setup_tx_ctrl(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
    radio->tx_bw = tx_bw;

    return 0;
}

//=========================================================================
int cariboulite_radio_get_tx_bandwidth(cariboulite_radio_state_st* radio, 
                                 at86rf215_radio_tx_cut_off_en *tx_bw)
{
    cariboulite_radio_get_tx_power(radio, NULL);
    if (tx_bw) *tx_bw = radio->tx_bw;
    return 0;
}

//=========================================================================
int cariboulite_radio_set_tx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   at86rf215_radio_sample_rate_en tx_sample_rate,
                                   at86rf215_radio_f_cut_en tx_cutoff)
{
    at86rf215_radio_tx_ctrl_st cfg = 
    {
        .pa_ramping_time = at86rf215_radio_tx_pa_ramp_16usec,
        .current_reduction = at86rf215_radio_pa_current_reduction_0ma,  // we can use this to gain some more
                                                                        // granularity with the tx gain control
        .tx_power = 18 + radio->tx_power,     // same as before
        .analog_bw = radio->tx_bw,            // same as before
        .digital_bw = tx_cutoff,
        .fs = tx_sample_rate,
        .direct_modulation = 0,
    };

    at86rf215_radio_setup_tx_ctrl(&radio->sys->modem, GET_MODEM_CH(radio->type), &cfg);
    radio->tx_fcut = tx_cutoff;
    radio->tx_fs = tx_sample_rate;
    
    return 0;
}

//=========================================================================
int cariboulite_radio_get_tx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   at86rf215_radio_sample_rate_en *tx_sample_rate,
                                   at86rf215_radio_f_cut_en *tx_cutoff)
{
    cariboulite_radio_get_tx_power(radio, NULL);
    if (tx_sample_rate) *tx_sample_rate = radio->tx_fs;
    if (tx_cutoff) *tx_cutoff = radio->tx_fcut;
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
#define CARIBOULITE_MIN_MIX     (1.0e6)        // 30
#define CARIBOULITE_MAX_MIX     (6000.0e6)      // 6000
#define CARIBOULITE_MIN_LO      (85.0e6)
#define CARIBOULITE_MAX_LO      (4200.0e6)
#define CARIBOULITE_2G4_MIN     (2385.0e6)      // 2400
#define CARIBOULITE_2G4_MAX     (2495.0e6)      // 2483.5
#define CARIBOULITE_S1G_MIN1    (377.0e6)		// 389.5e6
#define CARIBOULITE_S1G_MAX1    (530.0e6)
#define CARIBOULITE_S1G_MIN2    (779.0e6)
#define CARIBOULITE_S1G_MAX2    (1020.0e6)

typedef enum
{
    conversion_dir_none = 0,
    conversion_dir_up = 1,
    conversion_dir_down = 2,
} conversion_dir_en;

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
	if (radio->type != cariboulite_channel_6g)
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
	if (radio->type == cariboulite_channel_6g && mix != NULL)
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
int cariboulite_radio_set_frequency(cariboulite_radio_state_st* radio, 
									bool break_before_make,
									double *freq)
{
    double f_rf = *freq;
    double modem_act_freq = 0.0;
    double lo_act_freq = 0.0;
    double act_freq = 0.0;
    int error = 0;
    cariboulite_ext_ref_freq_en ext_ref_choice = cariboulite_ext_ref_off;
    conversion_dir_en conversion_direction = conversion_dir_none;

    //--------------------------------------------------------------------------------
    // SUB 1GHZ CONFIGURATION
    //--------------------------------------------------------------------------------
    if (radio->type == cariboulite_channel_s1g)
    {
        if ( (f_rf >= CARIBOULITE_S1G_MIN1 && f_rf <= CARIBOULITE_S1G_MAX1) ||
             (f_rf >= CARIBOULITE_S1G_MIN2 && f_rf <= CARIBOULITE_S1G_MAX2)   )
        {
            // setup modem frequency <= f_rf
            if (break_before_make)
            {
                at86rf215_radio_set_state(&radio->sys->modem, 
                                        at86rf215_rf_channel_900mhz, 
                                        at86rf215_radio_state_cmd_trx_off);
                radio->state = at86rf215_radio_state_cmd_trx_off;
            }

            modem_act_freq = at86rf215_setup_channel (&radio->sys->modem, 
                                                    at86rf215_rf_channel_900mhz, 
                                                    (uint32_t)f_rf);

            radio->if_frequency = 0;
            radio->lo_pll_locked = 1;
            radio->modem_pll_locked = cariboulite_radio_wait_modem_lock(radio, 3);
            radio->if_frequency = modem_act_freq;
            radio->actual_rf_frequency = radio->if_frequency;
            radio->requested_rf_frequency = f_rf;
            radio->rf_frequency_error = radio->actual_rf_frequency - radio->requested_rf_frequency;   

            // return actual frequency
            *freq = radio->actual_rf_frequency;
        }
        else
        {
            ZF_LOGE("unsupported frequency for the S1G channel - %.2f Hz", f_rf);
            error = -1;
        }
    }
	//--------------------------------------------------------------------------------
    // ISM 2.4 GHZ CONFIGURATION
    //--------------------------------------------------------------------------------
	else if (radio->type == cariboulite_channel_6g && 
			 radio->sys->board_info.numeric_product_id == system_type_cariboulite_ism)
	{
		if (f_rf >= CARIBOULITE_2G4_MIN && f_rf <= CARIBOULITE_2G4_MAX)
        {
            // setup modem frequency <= f_rf
            if (break_before_make)
            {
                at86rf215_radio_set_state(&radio->sys->modem, 
                                        at86rf215_rf_channel_2400mhz, 
                                        at86rf215_radio_state_cmd_trx_off);
                radio->state = at86rf215_radio_state_cmd_trx_off;
            }

            modem_act_freq = at86rf215_setup_channel (&radio->sys->modem, 
                                                    at86rf215_rf_channel_2400mhz, 
                                                    (uint32_t)f_rf);

            radio->if_frequency = 0;
            radio->lo_pll_locked = true;
            radio->modem_pll_locked = cariboulite_radio_wait_modem_lock(radio, 3);
            radio->if_frequency = modem_act_freq;
            radio->actual_rf_frequency = radio->if_frequency;
            radio->requested_rf_frequency = f_rf;
            radio->rf_frequency_error = radio->actual_rf_frequency - radio->requested_rf_frequency;   

            // return actual frequency
            *freq = radio->actual_rf_frequency;
        }
        else
        {
            ZF_LOGE("unsupported frequency for the HiF channel - %.2f Hz", f_rf);
            error = -1;
        }
	}
    //--------------------------------------------------------------------------------
    // FULL 30-6GHz CONFIGURATION
    //--------------------------------------------------------------------------------
    else if (radio->type == cariboulite_channel_6g && 
			 radio->sys->board_info.numeric_product_id == system_type_cariboulite_full)
    {		
        // Changing the frequency may sometimes need to break RX / TX
        if (break_before_make)
        {
            // make sure that during the transition the modem is not transmitting and then
            // verify that the FE is in low power mode
            at86rf215_radio_set_state( &radio->sys->modem, 
                                    at86rf215_rf_channel_2400mhz, 
                                    at86rf215_radio_state_cmd_trx_off);
            radio->state = at86rf215_radio_state_cmd_trx_off;

            caribou_fpga_set_io_ctrl_mode (&radio->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
        }

        // Calculate the best ext_ref
        double f_rf_mod_32 = (f_rf / 32e6);
        double f_rf_mod_26 = (f_rf / 26e6);
        f_rf_mod_32 -= (uint64_t)(f_rf_mod_32);
        f_rf_mod_26 -= (uint64_t)(f_rf_mod_26);
		f_rf_mod_32 *= 32e6;
        f_rf_mod_26 *= 26e6;
        if (f_rf_mod_32 > 16e6) f_rf_mod_32 = 32e6 - f_rf_mod_32;
        if (f_rf_mod_26 > 13e6) f_rf_mod_26 = 26e6 - f_rf_mod_26;
        ext_ref_choice = f_rf_mod_32 > f_rf_mod_26 ? cariboulite_ext_ref_32mhz : cariboulite_ext_ref_26mhz;
		//ext_ref_choice = cariboulite_ext_ref_26mhz;
        cariboulite_radio_ext_ref (radio->sys, ext_ref_choice);

        // Decide the conversion direction and IF/RF/LO
        //-------------------------------------
        if (f_rf >= CARIBOULITE_MIN_MIX && 
            f_rf < (CARIBOULITE_2G4_MIN) )
        {
            // region #1 - UP CONVERSION
            uint32_t modem_freq = CARIBOULITE_2G4_MAX;
            modem_act_freq = (double)at86rf215_setup_channel (&radio->sys->modem, 
																at86rf215_rf_channel_2400mhz, 
																modem_freq);
            
            // setup mixer LO according to the actual modem frequency
			lo_act_freq = rffc507x_set_frequency(&radio->sys->mixer, modem_act_freq + f_rf);
			act_freq = lo_act_freq - modem_act_freq;

            // setup fpga RFFE <= upconvert (tx / rx)
            conversion_direction = conversion_dir_up;
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
        }
        //-------------------------------------
        else if ( f_rf >= (CARIBOULITE_2G4_MAX) && 
                f_rf < CARIBOULITE_MAX_MIX )
        {
            // region #3 - DOWN-CONVERSION
            // setup modem frequency <= CARIBOULITE_2G4_MIN
            modem_act_freq = (double)at86rf215_setup_channel (&radio->sys->modem, 
                                                        at86rf215_rf_channel_2400mhz, 
                                                        (uint32_t)(CARIBOULITE_2G4_MIN));

            // setup mixer LO to according to actual modem frequency
			lo_act_freq = rffc507x_set_frequency(&radio->sys->mixer, f_rf - modem_act_freq);
            act_freq = lo_act_freq + modem_act_freq;

            // setup fpga RFFE <= downconvert (tx / rx)
            conversion_direction = conversion_dir_down;
        }
        //-------------------------------------
        else
        {
            ZF_LOGE("unsupported frequency for 6GHz channel - %.2f Hz", f_rf);
            error = -1;
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
        at86rf215_radio_set_state( &radio->sys->modem, 
                                    GET_MODEM_CH(radio->type), 
                                    at86rf215_radio_state_cmd_tx_prep);
        radio->state = at86rf215_radio_state_cmd_tx_prep;

        if (!cariboulite_radio_wait_for_lock(radio, &radio->modem_pll_locked, 
                                            lo_act_freq > CARIBOULITE_MIN_LO ? &radio->lo_pll_locked : NULL, 
                                            100))
        {
            if (!radio->lo_pll_locked) ZF_LOGE("PLL MIXER failed to lock LO frequency (%.2f Hz)", lo_act_freq);
            if (!radio->modem_pll_locked) ZF_LOGE("PLL MODEM failed to lock IF frequency (%.2f Hz)", modem_act_freq);
            error = 1;
        }

        // Update the actual frequencies
        radio->lo_frequency = lo_act_freq;
        radio->if_frequency = modem_act_freq;
        radio->actual_rf_frequency = act_freq;
        radio->requested_rf_frequency = f_rf;
        radio->rf_frequency_error = radio->actual_rf_frequency - radio->requested_rf_frequency;
        if (freq) *freq = act_freq;

        // activate the channel according to the new configuration
        //cariboulite_radio_activate_channel(radio, 1);
    }

    if (error >= 0)
    {
        ZF_LOGD("Frequency setting CH: %d, Wanted: %.2f Hz, Set: %.2f Hz (MOD: %.2f, MIX: %.2f)", 
                        radio->type, f_rf, act_freq, modem_act_freq, lo_act_freq);
    }

    return -error;
}

//=========================================================================
int cariboulite_radio_get_frequency(cariboulite_radio_state_st* radio, 
                                	double *freq, double *lo, double* i_f)
{
    if (freq) *freq = radio->actual_rf_frequency;
    if (lo) *lo = radio->lo_frequency;
    if (i_f) *i_f = radio->if_frequency;
}

//=========================================================================
int cariboulite_radio_activate_channel(cariboulite_radio_state_st* radio,
                                        cariboulite_channel_dir_en dir,
                                        bool activate)
{  
    radio->channel_direction = dir;
    
    ZF_LOGD("Activating channel %d, dir = %s, activate = %d", radio->type, radio->channel_direction==cariboulite_channel_dir_rx?"RX":"TX", activate);

    // Deactivation first
    if (activate == false) 
    {
        caribou_fpga_set_smi_channel (&radio->sys->fpga, radio->type == cariboulite_channel_s1g? caribou_fpga_smi_channel_0 : caribou_fpga_smi_channel_1);
        caribou_fpga_set_io_ctrl_dig (&radio->sys->fpga, radio->type == cariboulite_channel_s1g?0:1, 0);
        
        // if we deactivate, first shut off the smi stream
        if (caribou_smi_set_driver_streaming_state(&radio->sys->smi, smi_stream_idle) != 0)
        {
            return -1;
        }
        usleep(100000);
        
        // then deactivate the modem's stream
        at86rf215_radio_set_state( &radio->sys->modem, 
                                    GET_MODEM_CH(radio->type), 
                                    at86rf215_radio_state_cmd_trx_off);
        radio->state = at86rf215_radio_state_cmd_trx_off;
        
        //caribou_smi_set_driver_streaming_state(&radio->sys->smi, smi_stream_idle);
        ZF_LOGD("Setup Modem state trx_off");
        return 0;
    }
    
    
    // if the channel state is active, turn it off before reactivating
    if (radio->state != at86rf215_radio_state_cmd_tx_prep)
    {
        at86rf215_radio_set_state( &radio->sys->modem, 
                                    GET_MODEM_CH(radio->type), 
                                    at86rf215_radio_state_cmd_tx_prep);
        radio->state = at86rf215_radio_state_cmd_tx_prep;
        ZF_LOGD("Setup Modem state tx_prep");
    }

	//===========================================================
	// ACTIVATE RX
	//===========================================================
    // Activate the channel according to the configurations
    // RX on both channels looks the same
    if (radio->channel_direction == cariboulite_channel_dir_rx)
    {
        at86rf215_radio_set_state( &radio->sys->modem, 
                                GET_MODEM_CH(radio->type),
                                at86rf215_radio_state_cmd_rx);
        radio->state = at86rf215_radio_state_cmd_rx;
        ZF_LOGD("Setup Modem state cmd_rx");
        usleep(100000);
        
        // after modem is activated turn on the the smi stream
        smi_stream_state_en smi_state = smi_stream_idle;
        if (radio->smi_channel_id == caribou_smi_channel_900)
            smi_state = smi_stream_rx_channel_0;
        else if (radio->smi_channel_id == caribou_smi_channel_2400)
            smi_state = smi_stream_rx_channel_1;
        
        caribou_fpga_set_smi_channel (&radio->sys->fpga, radio->type == cariboulite_channel_s1g? caribou_fpga_smi_channel_0 : caribou_fpga_smi_channel_1);
        caribou_fpga_set_io_ctrl_dig (&radio->sys->fpga, radio->type == cariboulite_channel_s1g?0:1, 0);
        
        // apply the state
        if (caribou_smi_set_driver_streaming_state(&radio->sys->smi, smi_state) != 0)
        {
            return -1;
        }
    }
    
	//===========================================================
	// ACTIVATE TX
	//===========================================================
    else if (radio->channel_direction == cariboulite_channel_dir_tx)
    {
        // if its an LO frequency output from the mixer - no need for modem output
        // LO applicable only to the channel with the mixer
        if (radio->lo_output && 
			radio->type == cariboulite_channel_6g &&
			radio->sys->board_info.numeric_product_id == system_type_cariboulite_full)
        {
            // here we need to configure lo bypass on the mixer
            rffc507x_output_lo(&radio->sys->mixer, 1);
        }
        // otherwise we need the modem
        else
        {
			if (radio->sys->board_info.numeric_product_id == system_type_cariboulite_full)
            {
				// make sure the mixer doesn't bypass the lo
				rffc507x_output_lo(&radio->sys->mixer, 0);
			}

            cariboulite_radio_set_tx_bandwidth(radio, radio->cw_output?at86rf215_radio_tx_cut_off_80khz:radio->tx_bw);

            // CW output - constant I/Q values override
            at86rf215_radio_set_tx_dac_input_iq(&radio->sys->modem, 
                                                GET_MODEM_CH(radio->type), 
                                                radio->cw_output, 0x7E, 
                                                radio->cw_output, 0x3F);

            // transition to state TX
            at86rf215_radio_set_state(&radio->sys->modem, 
                                        GET_MODEM_CH(radio->type),
                                        at86rf215_radio_state_cmd_tx);
            radio->state = at86rf215_radio_state_cmd_tx;
        }
    }

    return 0;
}

//=========================================================================
int cariboulite_radio_set_cw_outputs(cariboulite_radio_state_st* radio, 
                               			bool lo_out, bool cw_out)
{
    if (radio->lo_output && radio->type == cariboulite_channel_6g)
    {
        radio->lo_output = lo_out;
    }
    else
    {
        radio->lo_output = false;
    }
    radio->cw_output = cw_out;

    if (cw_out)
    {
        radio->channel_direction = cariboulite_channel_dir_tx;
    }
    else
    {
        radio->channel_direction = cariboulite_channel_dir_rx;
    }

    return 0;
}

//=========================================================================
int cariboulite_radio_get_cw_outputs(cariboulite_radio_state_st* radio, 
                               			bool *lo_out, bool *cw_out)
{
    if (lo_out) *lo_out = radio->lo_output;
    if (cw_out) *cw_out = radio->cw_output;

    return 0;
}

//=========================================================================
// I/O Functions
//=========================================================================
int cariboulite_radio_read_samples(cariboulite_radio_state_st* radio,
                            caribou_smi_sample_complex_int16* buffer,
                            caribou_smi_sample_meta* metadata,
                            size_t length)
{
    int ret = 0;
      
    // CaribouSMI read   
    ret = caribou_smi_read(&radio->sys->smi, radio->smi_channel_id, buffer, metadata, length);
    if (ret < 0)
    {
        // -2 reserved for debug mode
        if (ret == -1) ZF_LOGE("SMI reading operation failed");
    }
    else if (ret == 0)
    {
        ZF_LOGD("SMI reading operation returned timeout");
    }
    
    return ret;
}

//=========================================================================
int cariboulite_radio_write_samples(cariboulite_radio_state_st* radio,
                            caribou_smi_sample_complex_int16* buffer,
                            size_t length)                            
{
    // Modem configuration for TX on specified channel
    //cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_tx, true);
    //usleep(1000);
    
    // Caribou SMI write
    int ret = caribou_smi_write(&radio->sys->smi, radio->smi_channel_id, buffer, length);
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
