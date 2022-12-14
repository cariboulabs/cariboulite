#ifndef __CARIBOULABS_RADIO_H__
#define __CARIBOULABS_RADIO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "config/config.h"
#include "at86rf215/at86rf215.h"

typedef enum
{
    cariboulite_channel_dir_rx = 0,
    cariboulite_channel_dir_tx = 1,
} cariboulite_channel_dir_en;

typedef enum
{
    cariboulite_channel_s1g = 0,
    cariboulite_channel_6g = 1,
} cariboulite_channel_en;

typedef enum
{
    cariboulite_ext_ref_off     = 0,
    cariboulite_ext_ref_26mhz   = 26,
    cariboulite_ext_ref_32mhz   = 32,
} cariboulite_ext_ref_freq_en;


typedef struct
{
    sys_st*                     		sys;
    cariboulite_channel_dir_en          channel_direction;
    cariboulite_channel_en              type;
    bool                                active;
    bool                                cw_output;
    bool                                lo_output;
 
    // MODEM STATES
    at86rf215_radio_state_cmd_en        state;
    at86rf215_radio_irq_st              interrupts;

    bool                                rx_agc_on;
    int                                 rx_gain_value_db;
    
    at86rf215_radio_rx_bw_en            rx_bw;
    at86rf215_radio_f_cut_en            rx_fcut;
    at86rf215_radio_sample_rate_en      rx_fs;

    int                                 tx_power;
    at86rf215_radio_tx_cut_off_en       tx_bw;
    at86rf215_radio_f_cut_en            tx_fcut;
    at86rf215_radio_sample_rate_en      tx_fs;

    // at86rf215_radio_energy_detection_st rx_energy_detection;
    float                               rx_energy_detection_value;
    float                               rx_rssi;

    // FREQUENCY
    bool                                modem_pll_locked;
    bool                                lo_pll_locked;
    double                              lo_frequency;
    double                              if_frequency;
    double                              actual_rf_frequency;
    double                              requested_rf_frequency;
    double                              rf_frequency_error;

    // SMI STREAMS
    caribou_smi_channel_en              smi_channel_id;

    // OTHERS
    uint8_t                             random_value;
    float                               rx_thermal_noise_floor;

    // CALIBRATION
    int                                 num_of_rx_cal_points;
    int                                 num_of_tx_cal_points;
    float                               rx_power_gain_calibration[6001];
    float                               tx_power_gain_calibration[6001];
} cariboulite_radio_state_st;

void cariboulite_radio_init(cariboulite_radio_state_st* radio, sys_st *sys, cariboulite_channel_en type);
int cariboulite_radio_dispose(cariboulite_radio_state_st* radio);
int cariboulite_radio_sync_information(cariboulite_radio_state_st* radio);
int cariboulite_radio_ext_ref (sys_st *sys, cariboulite_ext_ref_freq_en ref);

int cariboulite_radio_get_mod_state (cariboulite_radio_state_st* radio, at86rf215_radio_state_cmd_en *state);

int cariboulite_radio_get_mod_intertupts (cariboulite_radio_state_st* radio, at86rf215_radio_irq_st **irq_table);

int cariboulite_radio_set_rx_gain_control(cariboulite_radio_state_st* radio, 
                                    bool rx_agc_on,
                                    int rx_gain_value_db);

int cariboulite_radio_get_rx_gain_control(cariboulite_radio_state_st* radio,
                                    bool *rx_agc_on,
                                    int *rx_gain_value_db);

int cariboulite_radio_get_rx_gain_limits(cariboulite_radio_state_st* radio, 
                                    int *rx_min_gain_value_db,
                                    int *rx_max_gain_value_db,
                                    int *rx_gain_value_resolution_db);

int cariboulite_radio_set_rx_bandwidth(cariboulite_radio_state_st* radio, 
                                 at86rf215_radio_rx_bw_en rx_bw);

int cariboulite_radio_get_rx_bandwidth(cariboulite_radio_state_st* radio, 
                                 at86rf215_radio_rx_bw_en *rx_bw);

int cariboulite_radio_set_rx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   at86rf215_radio_sample_rate_en rx_sample_rate,
                                   at86rf215_radio_f_cut_en rx_cutoff);

int cariboulite_radio_get_rx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   at86rf215_radio_sample_rate_en *rx_sample_rate,
                                   at86rf215_radio_f_cut_en *rx_cutoff);


int cariboulite_radio_set_tx_power(cariboulite_radio_state_st* radio, 
                             int tx_power_dbm);

int cariboulite_radio_get_tx_power(cariboulite_radio_state_st* radio, 
                             int *tx_power_dbm);


int cariboulite_radio_set_tx_bandwidth(cariboulite_radio_state_st* radio, 
                                 at86rf215_radio_tx_cut_off_en tx_bw);

int cariboulite_radio_get_tx_bandwidth(cariboulite_radio_state_st* radio, 
                                 at86rf215_radio_tx_cut_off_en *tx_bw);

int cariboulite_radio_set_tx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   at86rf215_radio_sample_rate_en tx_sample_rate,
                                   at86rf215_radio_f_cut_en tx_cutoff);

int cariboulite_radio_get_tx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   at86rf215_radio_sample_rate_en *tx_sample_rate,
                                   at86rf215_radio_f_cut_en *tx_cutoff);


int cariboulite_radio_get_rssi(cariboulite_radio_state_st* radio, float *rssi_dbm);
int cariboulite_radio_get_energy_det(cariboulite_radio_state_st* radio, float *energy_det_val);
int cariboulite_radio_get_rand_val(cariboulite_radio_state_st* radio, uint8_t *rnd);

bool cariboulite_radio_wait_mixer_lock(cariboulite_radio_state_st* radio, int retries);
bool cariboulite_radio_wait_modem_lock(cariboulite_radio_state_st* radio, int retries);
int cariboulite_radio_set_frequency(cariboulite_radio_state_st* radio, 
									bool break_before_make,
									double *freq);

int cariboulite_radio_get_frequency(cariboulite_radio_state_st* radio, 
                                	double *freq, double *lo, double* i_f);

int cariboulite_radio_activate_channel(cariboulite_radio_state_st* radio, 
                                			bool active);

int cariboulite_radio_set_cw_outputs(cariboulite_radio_state_st* radio, 
                               			bool lo_out, bool cw_out);

int cariboulite_radio_get_cw_outputs(cariboulite_radio_state_st* radio, 
                               			bool *lo_out, bool *cw_out);

int cariboulite_radio_create_smi_stream(cariboulite_radio_state_st* radio, 
										cariboulite_channel_dir_en dir,
										void* context);
                               
int cariboulite_radio_destroy_smi_stream(cariboulite_radio_state_st* radio, 
                               			cariboulite_channel_dir_en dir);

int cariboulite_radio_run_pause_stream(cariboulite_radio_state_st* radio, 
										cariboulite_channel_dir_en dir,
										bool run);

#ifdef __cplusplus
}
#endif

#endif // __CARIBOULABS_RADIO_H__