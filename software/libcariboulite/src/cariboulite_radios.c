#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Radios"
#include "zf_log/zf_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <linux/random.h>
#include <sys/ioctl.h>

#include "cariboulite_radios.h"
#include "cariboulite_events.h"
#include "cariboulite_setup.h"
#include "datatypes/entropy.h"

#define GET_RADIO_PTR(radio,chan)   ((chan)==cariboulite_channel_s1g?&((radio)->radio_sub1g):&((radio)->radio_6g))

//======================================================================
int cariboulite_init_radios(cariboulite_radios_st* radios, sys_st *sys)
{
    memset (radios, 0, sizeof(cariboulite_radios_st));

    // Sub-1GHz
	cariboulite_radio_init(&radios->radio_sub1g, sys, cariboulite_channel_s1g);

    // Wide band channel
	cariboulite_radio_init(&radios->radio_6g, sys, cariboulite_channel_6g);

    cariboulite_radio_sync_information(&radios->radio_sub1g);
	cariboulite_radio_sync_information(&radios->radio_6g);
}

//======================================================================
int cariboulite_dispose_radios(cariboulite_radios_st* radios)
{
	cariboulite_radio_dispose(&radios->radio_sub1g);
	cariboulite_radio_dispose(&radios->radio_6g);
}

//======================================================================
int cariboulite_sync_radio_information(cariboulite_radios_st* radios)
{
	cariboulite_radio_sync_information(&radios->radio_sub1g);
	cariboulite_radio_sync_information(&radios->radio_6g);
}

//======================================================================
int cariboulite_get_mod_state ( cariboulite_radios_st* radios, 
                                cariboulite_channel_en channel,
                                at86rf215_radio_state_cmd_en *state)
{
	return cariboulite_radio_get_mod_state (GET_RADIO_PTR(radios,channel), state);
}

//======================================================================
int cariboulite_get_mod_intertupts (cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    at86rf215_radio_irq_st **irq_table)
{
	return cariboulite_radio_get_mod_intertupts (GET_RADIO_PTR(radios,channel), irq_table);
}

//======================================================================
int cariboulite_set_rx_gain_control(cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    bool rx_agc_on,
                                    int rx_gain_value_db)
{
	return cariboulite_radio_set_rx_gain_control(GET_RADIO_PTR(radios,channel), 
                                    rx_agc_on,
                                    rx_gain_value_db);
}

//======================================================================
int cariboulite_get_rx_gain_control(cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    bool *rx_agc_on,
                                    int *rx_gain_value_db)
{
	return cariboulite_radio_get_rx_gain_control(GET_RADIO_PTR(radios,channel),
                                    rx_agc_on,
                                    rx_gain_value_db);
}

//======================================================================
int cariboulite_get_rx_gain_limits(cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    int *rx_min_gain_value_db,
                                    int *rx_max_gain_value_db,
                                    int *rx_gain_value_resolution_db)
{
	return cariboulite_radio_get_rx_gain_limits(NULL, 
                                    rx_min_gain_value_db,
                                    rx_max_gain_value_db,
                                    rx_gain_value_resolution_db);
}

//======================================================================
int cariboulite_set_rx_bandwidth(cariboulite_radios_st* radios, 
                                 cariboulite_channel_en channel,
                                 at86rf215_radio_rx_bw_en rx_bw)
{
	return cariboulite_radio_set_rx_bandwidth(GET_RADIO_PTR(radios,channel), rx_bw);

}

//======================================================================
int cariboulite_get_rx_bandwidth(cariboulite_radios_st* radios, 
                                 cariboulite_channel_en channel,
                                 at86rf215_radio_rx_bw_en *rx_bw)
{
	cariboulite_radio_get_rx_bandwidth(GET_RADIO_PTR(radios,channel), rx_bw);
}

//======================================================================
int cariboulite_set_rx_samp_cutoff(cariboulite_radios_st* radios, 
                                   cariboulite_channel_en channel,
                                   at86rf215_radio_sample_rate_en rx_sample_rate,
                                   at86rf215_radio_f_cut_en rx_cutoff)
{
	return cariboulite_radio_set_rx_samp_cutoff(GET_RADIO_PTR(radios,channel), 
                                   rx_sample_rate,
                                   rx_cutoff);
}

//======================================================================
int cariboulite_get_rx_samp_cutoff(cariboulite_radios_st* radios, 
                                   cariboulite_channel_en channel,
                                   at86rf215_radio_sample_rate_en *rx_sample_rate,
                                   at86rf215_radio_f_cut_en *rx_cutoff)
{
	return cariboulite_radio_get_rx_samp_cutoff(GET_RADIO_PTR(radios,channel), 
                                   rx_sample_rate,
                                   rx_cutoff);
}

//======================================================================
int cariboulite_set_tx_power(cariboulite_radios_st* radios, 
                             cariboulite_channel_en channel,
                             int tx_power_dbm)
{
	return cariboulite_radio_set_tx_power(GET_RADIO_PTR(radios,channel), tx_power_dbm);
}

//======================================================================
int cariboulite_get_tx_power(cariboulite_radios_st* radios, 
                             cariboulite_channel_en channel,
                             int *tx_power_dbm)
{
	return cariboulite_radio_get_tx_power(GET_RADIO_PTR(radios,channel), tx_power_dbm);
}

//======================================================================
int cariboulite_set_tx_bandwidth(cariboulite_radios_st* radios, 
                                 cariboulite_channel_en channel,
                                 at86rf215_radio_tx_cut_off_en tx_bw)
{
	return cariboulite_radio_set_tx_bandwidth(GET_RADIO_PTR(radios,channel), tx_bw);
}

//======================================================================
int cariboulite_get_tx_bandwidth(cariboulite_radios_st* radios, 
                                 cariboulite_channel_en channel,
                                 at86rf215_radio_tx_cut_off_en *tx_bw)
{
	return cariboulite_radio_get_tx_bandwidth(GET_RADIO_PTR(radios,channel), tx_bw);
}

//======================================================================
int cariboulite_set_tx_samp_cutoff(cariboulite_radios_st* radios, 
                                   cariboulite_channel_en channel,
                                   at86rf215_radio_sample_rate_en tx_sample_rate,
                                   at86rf215_radio_f_cut_en tx_cutoff)
{
	return cariboulite_radio_set_tx_samp_cutoff(GET_RADIO_PTR(radios,channel), 
                                   tx_sample_rate,
                                   tx_cutoff);
}

//======================================================================
int cariboulite_get_tx_samp_cutoff(cariboulite_radios_st* radios, 
                                   cariboulite_channel_en channel,
                                   at86rf215_radio_sample_rate_en *tx_sample_rate,
                                   at86rf215_radio_f_cut_en *tx_cutoff)
{
	return cariboulite_radio_get_tx_samp_cutoff(GET_RADIO_PTR(radios,channel), 
                                   tx_sample_rate,
                                   tx_cutoff);
}

//======================================================================
int cariboulite_get_rssi(cariboulite_radios_st* radios, cariboulite_channel_en channel, float *rssi_dbm)
{
	return cariboulite_radio_get_rssi(GET_RADIO_PTR(radios,channel), rssi_dbm);
}

//======================================================================
int cariboulite_get_energy_det(cariboulite_radios_st* radios, cariboulite_channel_en channel, float *energy_det_val)
{
	return cariboulite_radio_get_energy_det(GET_RADIO_PTR(radios,channel), energy_det_val);
}

//======================================================================
int cariboulite_get_rand_val(cariboulite_radios_st* radios, cariboulite_channel_en channel, uint8_t *rnd)
{
	return cariboulite_radio_get_rand_val(GET_RADIO_PTR(radios,channel), rnd);
}

//=================================================
int cariboulite_set_frequency(  cariboulite_radios_st* radios, 
                                cariboulite_channel_en channel, 
                                bool break_before_make,
                                double *freq)
{
	return cariboulite_radio_set_frequency(GET_RADIO_PTR(radios,channel), 
									break_before_make,
									freq);
}

//======================================================================
int cariboulite_get_frequency(  cariboulite_radios_st* radios, 
                                cariboulite_channel_en channel, 
                                double *freq, double *lo, double* i_f)
{
	return cariboulite_radio_get_frequency(GET_RADIO_PTR(radios,channel), 
                                	freq, lo, i_f);
}

//======================================================================
int cariboulite_activate_channel(cariboulite_radios_st* radios, 
                                cariboulite_channel_en channel,
                                bool active)
{
	return cariboulite_radio_activate_channel(GET_RADIO_PTR(radios,channel), active);
}

//======================================================================
int cariboulite_set_cw_outputs(cariboulite_radios_st* radios, 
                               cariboulite_channel_en channel, bool lo_out, bool cw_out)
{
	return cariboulite_radio_set_cw_outputs(GET_RADIO_PTR(radios,channel), 
                               			lo_out, cw_out);
}

//======================================================================
int cariboulite_get_cw_outputs(cariboulite_radios_st* radios, 
                               cariboulite_channel_en channel, bool *lo_out, bool *cw_out)
{
	return cariboulite_radio_get_cw_outputs(GET_RADIO_PTR(radios,channel), 
                               			lo_out, cw_out);
}

//=================================================
int cariboulite_create_smi_stream(cariboulite_radios_st* radios, 
                               cariboulite_channel_en channel,
                               cariboulite_channel_dir_en dir,
                               void* context)
{
	return cariboulite_radio_create_smi_stream(GET_RADIO_PTR(radios,channel), 
										dir,
										context);
}

//=================================================
int cariboulite_destroy_smi_stream(cariboulite_radios_st* radios,
                               cariboulite_channel_en channel,
                               cariboulite_channel_dir_en dir)
{
	return cariboulite_radio_destroy_smi_stream(GET_RADIO_PTR(radios,channel), dir);
}

//=================================================
int cariboulite_run_pause_stream(cariboulite_radios_st* radios, 
                               cariboulite_channel_en channel,
                               cariboulite_channel_dir_en dir,
                               bool run)
{
	return cariboulite_radio_run_pause_stream(GET_RADIO_PTR(radios,channel), dir, run);
}
