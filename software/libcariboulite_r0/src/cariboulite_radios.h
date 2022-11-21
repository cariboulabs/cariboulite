#ifndef __CARIBOULABS_RADIOS_H__
#define __CARIBOULABS_RADIOS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "cariboulite_config/cariboulite_config.h"
#include "cariboulite_radio.h"
#include "at86rf215/at86rf215.h"


typedef struct
{
    cariboulite_radio_state_st radio_sub1g;
    cariboulite_radio_state_st radio_6g;
} cariboulite_radios_st;

int cariboulite_init_radios(cariboulite_radios_st* radios, cariboulite_st *sys);
int cariboulite_dispose_radios(cariboulite_radios_st* radios);
int cariboulite_sync_radio_information(cariboulite_radios_st* radios);

int cariboulite_get_mod_state (cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    at86rf215_radio_state_cmd_en *state);

int cariboulite_get_mod_intertupts (cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    at86rf215_radio_irq_st **irq_table);

int cariboulite_set_rx_gain_control(cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    bool rx_agc_on,
                                    int rx_gain_value_db);

int cariboulite_get_rx_gain_control(cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    bool *rx_agc_on,
                                    int *rx_gain_value_db);

int cariboulite_get_rx_gain_limits(cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    int *rx_min_gain_value_db,
                                    int *rx_max_gain_value_db,
                                    int *rx_gain_value_resolution_db);

int cariboulite_set_rx_bandwidth(cariboulite_radios_st* radios, 
                                 cariboulite_channel_en channel,
                                 at86rf215_radio_rx_bw_en rx_bw);

int cariboulite_get_rx_bandwidth(cariboulite_radios_st* radios, 
                                 cariboulite_channel_en channel,
                                 at86rf215_radio_rx_bw_en *rx_bw);

int cariboulite_set_rx_samp_cutoff(cariboulite_radios_st* radios, 
                                   cariboulite_channel_en channel,
                                   at86rf215_radio_sample_rate_en rx_sample_rate,
                                   at86rf215_radio_f_cut_en rx_cutoff);

int cariboulite_get_rx_samp_cutoff(cariboulite_radios_st* radios, 
                                   cariboulite_channel_en channel,
                                   at86rf215_radio_sample_rate_en *rx_sample_rate,
                                   at86rf215_radio_f_cut_en *rx_cutoff);


int cariboulite_set_tx_power(cariboulite_radios_st* radios, 
                             cariboulite_channel_en channel,
                             int tx_power_dbm);

int cariboulite_get_tx_power(cariboulite_radios_st* radios, 
                             cariboulite_channel_en channel,
                             int *tx_power_dbm);


int cariboulite_set_tx_bandwidth(cariboulite_radios_st* radios, 
                                 cariboulite_channel_en channel,
                                 at86rf215_radio_tx_cut_off_en tx_bw);

int cariboulite_get_tx_bandwidth(cariboulite_radios_st* radios, 
                                 cariboulite_channel_en channel,
                                 at86rf215_radio_tx_cut_off_en *tx_bw);

int cariboulite_set_tx_samp_cutoff(cariboulite_radios_st* radios, 
                                   cariboulite_channel_en channel,
                                   at86rf215_radio_sample_rate_en tx_sample_rate,
                                   at86rf215_radio_f_cut_en tx_cutoff);

int cariboulite_get_tx_samp_cutoff(cariboulite_radios_st* radios, 
                                   cariboulite_channel_en channel,
                                   at86rf215_radio_sample_rate_en *tx_sample_rate,
                                   at86rf215_radio_f_cut_en *tx_cutoff);


int cariboulite_get_rssi(cariboulite_radios_st* radios, cariboulite_channel_en channel, float *rssi_dbm);
int cariboulite_get_energy_det(cariboulite_radios_st* radios, cariboulite_channel_en channel, float *energy_det_val);
int cariboulite_get_rand_val(cariboulite_radios_st* radios, cariboulite_channel_en channel, uint8_t *rnd);

int cariboulite_set_frequency(  cariboulite_radios_st* radios, 
                                cariboulite_channel_en channel, 
                                bool break_before_make,
                                double *freq);

int cariboulite_get_frequency(  cariboulite_radios_st* radios, 
                                cariboulite_channel_en channel, 
                                double *freq, double *lo, double* i_f);

int cariboulite_activate_channel(cariboulite_radios_st* radios, 
                                cariboulite_channel_en channel,
                                bool active);

int cariboulite_set_cw_outputs(cariboulite_radios_st* radios, 
                               cariboulite_channel_en channel, bool lo_out, bool cw_out);

int cariboulite_get_cw_outputs(cariboulite_radios_st* radios, 
                               cariboulite_channel_en channel, bool *lo_out, bool *cw_out);

int cariboulite_create_smi_stream(cariboulite_radios_st* radios, 
                               cariboulite_channel_en channel,
                               cariboulite_channel_dir_en dir,
                               void* context);
                               
int cariboulite_destroy_smi_stream(cariboulite_radios_st* radios, 
                               cariboulite_channel_en channel,
                               cariboulite_channel_dir_en dir);

int cariboulite_run_pause_stream(cariboulite_radios_st* radios, 
                               cariboulite_channel_en channel,
                               cariboulite_channel_dir_en dir,
                               bool run);

#ifdef __cplusplus
}
#endif

#endif // __CARIBOULABS_RADIOS_H__