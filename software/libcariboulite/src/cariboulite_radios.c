#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
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


#define GET_RADIO_PTR(radio,chan)   ((chan)==cariboulite_channel_s1g?&((radio)->radio_sub1g):&((radio)->radio_6g))
#define GET_CH(rad_ch)              ((rad_ch)==cariboulite_channel_s1g?at86rf215_rf_channel_900mhz:at86rf215_rf_channel_2400mhz)
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

//======================================================================
int cariboulite_init_radios(cariboulite_radios_st* radios, cariboulite_st *sys)
{
    memset (radios, 0, sizeof(cariboulite_radios_st));

    // Sub-1GHz
    radios->radio_sub1g.cariboulite_sys = sys;
    radios->radio_sub1g.active = true;
    radios->radio_sub1g.channel_direction = cariboulite_channel_dir_rx;
    radios->radio_sub1g.type = cariboulite_channel_s1g;
    radios->radio_sub1g.cw_output = false;
    radios->radio_sub1g.lo_output = false;
    radios->radio_sub1g.rx_stream_id = -1;
    radios->radio_sub1g.tx_stream_id = -1;

    // Wide band channel
    radios->radio_6g.cariboulite_sys = sys;
    radios->radio_6g.active = true;
    radios->radio_6g.channel_direction = cariboulite_channel_dir_rx;
    radios->radio_6g.type = cariboulite_channel_6g;
    radios->radio_6g.cw_output = false;
    radios->radio_6g.lo_output = false;
    radios->radio_6g.rx_stream_id = -1;
    radios->radio_6g.tx_stream_id = -1;

    cariboulite_sync_radio_information(radios);
}

//======================================================================
int cariboulite_dispose_radios(cariboulite_radios_st* radios)
{
    radios->radio_sub1g.active = false;
    radios->radio_6g.active = false;

    // If streams are active - destroy them
    if (radios->radio_sub1g.rx_stream_id != -1)
    {

    }

    if (radios->radio_sub1g.tx_stream_id != -1)
    {

    }

    if (radios->radio_6g.rx_stream_id != -1)
    {

    }

    if (radios->radio_6g.tx_stream_id != -1)
    {
        
    }

    cariboulite_radio_state_st* rad_s1g = GET_RADIO_PTR(radios,cariboulite_channel_s1g);
    cariboulite_radio_state_st* rad_6g = GET_RADIO_PTR(radios,cariboulite_channel_6g);

    at86rf215_radio_set_state( &rad_s1g->cariboulite_sys->modem, 
                                    GET_CH(cariboulite_channel_s1g), 
                                    at86rf215_radio_state_cmd_trx_off);
    rad_s1g->state = at86rf215_radio_state_cmd_trx_off;

    at86rf215_radio_set_state( &rad_6g->cariboulite_sys->modem, 
                                    GET_CH(cariboulite_channel_6g), 
                                    at86rf215_radio_state_cmd_trx_off);
    rad_6g->state = at86rf215_radio_state_cmd_trx_off;

    caribou_fpga_set_io_ctrl_mode (&rad_6g->cariboulite_sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
}

//======================================================================
int cariboulite_sync_radio_information(cariboulite_radios_st* radios)
{
    cariboulite_get_mod_state (radios, cariboulite_channel_s1g, NULL);
    cariboulite_get_mod_state (radios, cariboulite_channel_6g, NULL);

    cariboulite_get_rx_gain_control(radios, cariboulite_channel_s1g, NULL, NULL);
    cariboulite_get_rx_gain_control(radios, cariboulite_channel_6g, NULL, NULL);      

    cariboulite_get_rx_bandwidth(radios, cariboulite_channel_s1g, NULL);
    cariboulite_get_rx_bandwidth(radios, cariboulite_channel_6g, NULL);

    cariboulite_get_tx_power(radios, cariboulite_channel_s1g, NULL);
    cariboulite_get_tx_power(radios, cariboulite_channel_6g, NULL);

    cariboulite_get_rssi(radios, cariboulite_channel_s1g, NULL);
    cariboulite_get_rssi(radios, cariboulite_channel_6g, NULL);

    cariboulite_get_energy_det(radios, cariboulite_channel_s1g, NULL);
    cariboulite_get_energy_det(radios, cariboulite_channel_6g, NULL);
}

//======================================================================
int cariboulite_get_mod_state ( cariboulite_radios_st* radios, 
                                cariboulite_channel_en channel,
                                at86rf215_radio_state_cmd_en *state)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    rad->state  = at86rf215_radio_get_state(&rad->cariboulite_sys->modem, GET_CH(channel));

    if (state) *state = rad->state;
    return 0;
}


//======================================================================
int cariboulite_get_mod_intertupts (cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    at86rf215_radio_irq_st **irq_table)
{
    at86rf215_irq_st irq = {0};
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    at86rf215_get_irqs(&rad->cariboulite_sys->modem, &irq, 0);

    if (channel == cariboulite_channel_s1g)
    {
        memcpy (&rad->interrupts, &irq.radio09, sizeof(at86rf215_radio_irq_st));
        if (irq_table) *irq_table = &irq.radio09;
    }
    else
    {
        memcpy (&rad->interrupts, &irq.radio24, sizeof(at86rf215_radio_irq_st));
        if (irq_table) *irq_table = &irq.radio24;
    }

    return 0;
}

//======================================================================
int cariboulite_set_rx_gain_control(cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    bool rx_agc_on,
                                    int rx_gain_value_db)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);

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

    at86rf215_radio_setup_agc(&rad->cariboulite_sys->modem, GET_CH(channel), &rx_gain_control);
    rad->rx_agc_on = rx_agc_on;
    rad->rx_gain_value_db = rx_gain_value_db;
    return 0;
}

//======================================================================
int cariboulite_get_rx_gain_control(cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    bool *rx_agc_on,
                                    int *rx_gain_value_db)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    at86rf215_radio_agc_ctrl_st agc_ctrl = {0};
    at86rf215_radio_get_agc(&rad->cariboulite_sys->modem, GET_CH(channel), &agc_ctrl);

    rad->rx_agc_on = agc_ctrl.enable_cmd;
    rad->rx_gain_value_db = agc_ctrl.gain_control_word * 3;
    if (rx_agc_on) *rx_agc_on = rad->rx_agc_on;
    if (rx_gain_value_db) *rx_gain_value_db = rad->rx_gain_value_db;
    return 0;
}

//======================================================================
int cariboulite_get_rx_gain_limits(cariboulite_radios_st* radios, 
                                    cariboulite_channel_en channel,
                                    int *rx_min_gain_value_db,
                                    int *rx_max_gain_value_db,
                                    int *rx_gain_value_resolution_db)
{
    if (rx_min_gain_value_db) *rx_min_gain_value_db = 0;
    if (rx_max_gain_value_db) *rx_max_gain_value_db = 23*3;
    if (rx_gain_value_resolution_db) *rx_gain_value_resolution_db = 3;
    return 0;
}

//======================================================================
int cariboulite_set_rx_bandwidth(cariboulite_radios_st* radios, 
                                 cariboulite_channel_en channel,
                                 at86rf215_radio_rx_bw_en rx_bw)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);

    at86rf215_radio_set_rx_bw_samp_st cfg = 
    {
        .inverter_sign_if = 0,
        .shift_if_freq = 0,                 // A value of one configures the receiver to shift the IF frequency
                                            // by factor of 1.25. This is useful to place the image frequency according
                                            // to channel scheme.
        .bw = rx_bw,
        .fcut = rad->rx_fcut,               // keep the same
        .fs = rad->rx_fs,                   // keep the same
    };
    at86rf215_radio_set_rx_bandwidth_sampling(&rad->cariboulite_sys->modem, GET_CH(channel), &cfg);
    rad->rx_bw = rx_bw;
    return 0;
}

//======================================================================
int cariboulite_get_rx_bandwidth(cariboulite_radios_st* radios, 
                                 cariboulite_channel_en channel,
                                 at86rf215_radio_rx_bw_en *rx_bw)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    at86rf215_radio_set_rx_bw_samp_st cfg = {0};
    at86rf215_radio_get_rx_bandwidth_sampling(&rad->cariboulite_sys->modem, GET_CH(channel), &cfg);
    rad->rx_bw = cfg.bw;
    rad->rx_fcut = cfg.fcut;
    rad->rx_fs = cfg.fs;
    if (rx_bw) *rx_bw = rad->rx_bw;
    return 0;
}

//======================================================================
int cariboulite_set_rx_samp_cutoff(cariboulite_radios_st* radios, 
                                   cariboulite_channel_en channel,
                                   at86rf215_radio_sample_rate_en rx_sample_rate,
                                   at86rf215_radio_f_cut_en rx_cutoff)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);

    at86rf215_radio_set_rx_bw_samp_st cfg = 
    {
        .inverter_sign_if = 0,
        .shift_if_freq = 0,                 // A value of one configures the receiver to shift the IF frequency
                                            // by factor of 1.25. This is useful to place the image frequency according
                                            // to channel scheme.
        .bw = rad->rx_bw,                   // keep the same
        .fcut = rx_cutoff,
        .fs = rx_sample_rate,
    };
    at86rf215_radio_set_rx_bandwidth_sampling(&rad->cariboulite_sys->modem, GET_CH(channel), &cfg);
    rad->rx_fs = rx_sample_rate;
    rad->rx_fcut = rx_cutoff;
    return 0;
}

//======================================================================
int cariboulite_get_rx_samp_cutoff(cariboulite_radios_st* radios, 
                                   cariboulite_channel_en channel,
                                   at86rf215_radio_sample_rate_en *rx_sample_rate,
                                   at86rf215_radio_f_cut_en *rx_cutoff)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    cariboulite_get_rx_bandwidth(radios, channel, NULL);
    if (rx_sample_rate) *rx_sample_rate = rad->rx_fs;
    if (rx_cutoff) *rx_cutoff = rad->rx_fcut;
    return 0;
}

//======================================================================
int cariboulite_set_tx_power(cariboulite_radios_st* radios, 
                             cariboulite_channel_en channel,
                             int tx_power_dbm)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    if (tx_power_dbm < -18) tx_power_dbm = -18;
    if (tx_power_dbm > 13) tx_power_dbm = 13;
    int tx_power_ctrl = 18 + tx_power_dbm;

    at86rf215_radio_tx_ctrl_st cfg = 
    {
        .pa_ramping_time = at86rf215_radio_tx_pa_ramp_16usec,
        .current_reduction = at86rf215_radio_pa_current_reduction_0ma,  // we can use this to gain some more
                                                                        // granularity with the tx gain control
        .tx_power = tx_power_ctrl,
        .analog_bw = rad->tx_bw,            // same as before
        .digital_bw = rad->tx_fcut,         // same as before
        .fs = rad->tx_fs,                   // same as before
        .direct_modulation = 0,
    };

    at86rf215_radio_setup_tx_ctrl(&rad->cariboulite_sys->modem, GET_CH(channel), &cfg);
    rad->tx_power = tx_power_dbm;

    return 0;
}

//======================================================================
int cariboulite_get_tx_power(cariboulite_radios_st* radios, 
                             cariboulite_channel_en channel,
                             int *tx_power_dbm)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    at86rf215_radio_tx_ctrl_st cfg = {0};
    at86rf215_radio_get_tx_ctrl(&rad->cariboulite_sys->modem, GET_CH(channel), &cfg);
    rad->tx_power = cfg.tx_power - 18;
    rad->tx_bw = cfg.analog_bw;
    rad->tx_fcut = cfg.digital_bw;
    rad->tx_fs = cfg.fs;

    if (tx_power_dbm) *tx_power_dbm = rad->tx_power;
    return 0;
}

//======================================================================
int cariboulite_set_tx_bandwidth(cariboulite_radios_st* radios, 
                                 cariboulite_channel_en channel,
                                 at86rf215_radio_tx_cut_off_en tx_bw)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    at86rf215_radio_tx_ctrl_st cfg = 
    {
        .pa_ramping_time = at86rf215_radio_tx_pa_ramp_16usec,
        .current_reduction = at86rf215_radio_pa_current_reduction_0ma,  // we can use this to gain some more
                                                                        // granularity with the tx gain control
        .tx_power = 18 + rad->tx_power,     // same as before
        .analog_bw = tx_bw,
        .digital_bw = rad->tx_fcut,         // same as before
        .fs = rad->tx_fs,                   // same as before
        .direct_modulation = 0,
    };

    at86rf215_radio_setup_tx_ctrl(&rad->cariboulite_sys->modem, GET_CH(channel), &cfg);
    rad->tx_bw = tx_bw;

    return 0;
}

//======================================================================
int cariboulite_get_tx_bandwidth(cariboulite_radios_st* radios, 
                                 cariboulite_channel_en channel,
                                 at86rf215_radio_tx_cut_off_en *tx_bw)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    cariboulite_get_tx_power(radios, channel, NULL);
    if (tx_bw) *tx_bw = rad->tx_bw;
    return 0;
}

//======================================================================
int cariboulite_set_tx_samp_cutoff(cariboulite_radios_st* radios, 
                                   cariboulite_channel_en channel,
                                   at86rf215_radio_sample_rate_en tx_sample_rate,
                                   at86rf215_radio_f_cut_en tx_cutoff)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    at86rf215_radio_tx_ctrl_st cfg = 
    {
        .pa_ramping_time = at86rf215_radio_tx_pa_ramp_16usec,
        .current_reduction = at86rf215_radio_pa_current_reduction_0ma,  // we can use this to gain some more
                                                                        // granularity with the tx gain control
        .tx_power = 18 + rad->tx_power,     // same as before
        .analog_bw = rad->tx_bw,            // same as before
        .digital_bw = tx_cutoff,
        .fs = tx_sample_rate,
        .direct_modulation = 0,
    };

    at86rf215_radio_setup_tx_ctrl(&rad->cariboulite_sys->modem, GET_CH(channel), &cfg);
    rad->tx_fcut = tx_cutoff;
    rad->tx_fs = tx_sample_rate;
    
    return 0;
}

//======================================================================
int cariboulite_get_tx_samp_cutoff(cariboulite_radios_st* radios, 
                                   cariboulite_channel_en channel,
                                   at86rf215_radio_sample_rate_en *tx_sample_rate,
                                   at86rf215_radio_f_cut_en *rx_cutoff)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    cariboulite_get_tx_power(radios, channel, NULL);
    if (tx_sample_rate) *tx_sample_rate = rad->tx_fs;
    if (rx_cutoff) *rx_cutoff = rad->tx_fcut;
    return 0;
}

//======================================================================
int cariboulite_get_rssi(cariboulite_radios_st* radios, cariboulite_channel_en channel, float *rssi_dbm)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    float rssi = at86rf215_radio_get_rssi_dbm(&rad->cariboulite_sys->modem, GET_CH(channel));
    if (rssi >= -127.0 && rssi <= -4)   // register only valid values
    {
        rad->rx_rssi = rssi;
        if (rssi_dbm) *rssi_dbm = rssi;
        return 0;
    }

    if (rssi_dbm) *rssi_dbm = rad->rx_rssi;
    return -1;
}

//======================================================================
int cariboulite_get_energy_det(cariboulite_radios_st* radios, cariboulite_channel_en channel, float *energy_det_val)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    at86rf215_radio_energy_detection_st det = {0};
    at86rf215_radio_get_energy_detection(&rad->cariboulite_sys->modem, GET_CH(channel), &det);
    
    if (det.energy_detection_value >= -127.0 && det.energy_detection_value <= -4)   // register only valid values
    {
        rad->rx_energy_detection_value = det.energy_detection_value;
        if (energy_det_val) *energy_det_val = rad->rx_energy_detection_value;
        return 0;
    }

    if (energy_det_val) *energy_det_val = rad->rx_energy_detection_value;
    return -1;
}

//======================================================================
int cariboulite_get_rand_val(cariboulite_radios_st* radios, cariboulite_channel_en channel, uint8_t *rnd)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    rad->random_value = at86rf215_radio_get_random_value(&rad->cariboulite_sys->modem, GET_CH(channel));
    if (rnd) *rnd = rad->random_value;
    add_entropy(rad->random_value);
    return 0;
}


//=================================================
#define CARIBOULITE_MIN_MIX     (20.0e6)        // 30
#define CARIBOULITE_MAX_MIX     (6000.0e6)      // 6000
#define CARIBOULITE_MIN_LO      (85.0e6)
#define CARIBOULITE_MAX_LO      (4200.0e6)
#define CARIBOULITE_2G4_MIN     (2380.0e6)      // 2400
#define CARIBOULITE_2G4_MAX     (2495.0e6)      // 2483.5
#define CARIBOULITE_S1G_MIN1    (389.5e6)
#define CARIBOULITE_S1G_MAX1    (510.0e6)
#define CARIBOULITE_S1G_MIN2    (779.0e6)
#define CARIBOULITE_S1G_MAX2    (1020.0e6)

typedef enum
{
    conversion_dir_none = 0,
    conversion_dir_up = 1,
    conversion_dir_down = 2,
} conversion_dir_en;

//=================================================
bool cariboulite_wait_for_lock( cariboulite_radio_state_st* rad, bool *mod, bool *mix, int retries)
{
    bool mix_lock = true, mod_lock = true;
    if (mix)
    {
        rffc507x_device_status_st stat = {0};
        int relock_retries = retries;
        do
        {
            rffc507x_readback_status(&rad->cariboulite_sys->mixer, NULL, &stat);
            rffc507x_print_stat(&stat);
            if (!stat.pll_lock) rffc507x_relock(&rad->cariboulite_sys->mixer);
        } while (!stat.pll_lock && relock_retries--);

        *mix = stat.pll_lock;
        mix_lock = (bool)stat.pll_lock;
    }

    if (mod)
    {
        at86rf215_radio_pll_ctrl_st cfg = {0};
        int relock_retries = retries;
        do
        {
            at86rf215_rf_channel_en ch = rad->type == cariboulite_channel_s1g ? 
                        at86rf215_rf_channel_900mhz : at86rf215_rf_channel_2400mhz;
            at86rf215_radio_get_pll_ctrl(&rad->cariboulite_sys->modem, ch, &cfg);
        } while (!cfg.pll_locked && relock_retries--);

        *mod = cfg.pll_locked;
        mod_lock = (bool)cfg.pll_locked;
    }

    return mix_lock && mod_lock;
}

//=================================================
int cariboulite_set_frequency(  cariboulite_radios_st* radios, 
                                cariboulite_channel_en channel, 
                                bool break_before_make,
                                double *freq)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);

    double f_rf = *freq;
    double modem_act_freq = 0.0;
    double lo_act_freq = 0.0;
    double act_freq = 0.0;
    int error = 0;
    conversion_dir_en conversion_direction = conversion_dir_none;

    //--------------------------------------------------------------------------------
    // SUB 1GHZ CONFIGURATION
    //--------------------------------------------------------------------------------
    if (channel == cariboulite_channel_s1g)
    {
        if ( (f_rf >= CARIBOULITE_S1G_MIN1 && f_rf <= CARIBOULITE_S1G_MAX1) ||
             (f_rf >= CARIBOULITE_S1G_MIN2 && f_rf <= CARIBOULITE_S1G_MAX2)   )
        {
            // setup modem frequency <= f_rf
            if (break_before_make)
            {
                at86rf215_radio_set_state(  &rad->cariboulite_sys->modem, 
                                        at86rf215_rf_channel_900mhz, 
                                        at86rf215_radio_state_cmd_trx_off);
                rad->state = at86rf215_radio_state_cmd_trx_off;
            }

            modem_act_freq = at86rf215_setup_channel (&rad->cariboulite_sys->modem, 
                                                    at86rf215_rf_channel_900mhz, 
                                                    (uint32_t)f_rf);

            at86rf215_radio_pll_ctrl_st cfg = {0};
            at86rf215_radio_get_pll_ctrl(&rad->cariboulite_sys->modem, at86rf215_rf_channel_900mhz, &cfg);

            rad->if_frequency = 0;
            rad->lo_pll_locked = 1;
            rad->modem_pll_locked = cfg.pll_locked;
            rad->if_frequency = modem_act_freq;
            rad->actual_rf_frequency = rad->if_frequency;
            rad->requested_rf_frequency = f_rf;
            rad->rf_frequency_error = rad->actual_rf_frequency - rad->requested_rf_frequency;   

            // return actual frequency
            *freq = rad->actual_rf_frequency;
        }
        else
        {
            // error - unsupported frequency for S1G channel
            ZF_LOGE("unsupported frequency for S1G channel - %.2f Hz", f_rf);
            error = -1;
        }
    }
    //--------------------------------------------------------------------------------
    // 30-6GHz CONFIGURATION
    //--------------------------------------------------------------------------------
    else if (channel == cariboulite_channel_6g)
    {
        // Changing the frequency may sometimes need to break RX / TX
        if (break_before_make)
        {
            // make sure that during the transition the modem is not transmitting and then
            // verify that the FE is in low power mode
            at86rf215_radio_set_state( &rad->cariboulite_sys->modem, 
                                    at86rf215_rf_channel_2400mhz, 
                                    at86rf215_radio_state_cmd_trx_off);
            rad->state = at86rf215_radio_state_cmd_trx_off;

            caribou_fpga_set_io_ctrl_mode (&rad->cariboulite_sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
        }

        // Decide the conversion direction and IF/RF/LO
        //-------------------------------------
        if (f_rf >= CARIBOULITE_MIN_MIX && 
            f_rf <= (CARIBOULITE_2G4_MIN) )
        {
            // region #1 - UP CONVERSION
            // setup modem frequency <= CARIBOULITE_2G4_MAX
            modem_act_freq = (double)at86rf215_setup_channel (&rad->cariboulite_sys->modem, 
                                                        at86rf215_rf_channel_2400mhz, 
                                                        (uint32_t)(CARIBOULITE_2G4_MAX));
            
            // setup mixer LO according to the actual modem frequency
            lo_act_freq = rffc507x_set_frequency(&rad->cariboulite_sys->mixer, modem_act_freq + f_rf);
            act_freq = lo_act_freq - modem_act_freq;

            // setup fpga RFFE <= upconvert (tx / rx)
            conversion_direction = conversion_dir_up;
        }
        //-------------------------------------
        else if ( f_rf >= CARIBOULITE_2G4_MIN && 
                f_rf <= CARIBOULITE_2G4_MAX )
        {
            // region #2 - bypass mode
            // setup modem frequency <= f_rf
            modem_act_freq = (double)at86rf215_setup_channel (&rad->cariboulite_sys->modem, 
                                                        at86rf215_rf_channel_2400mhz, 
                                                        (uint32_t)f_rf);
            lo_act_freq = 0;
            act_freq = modem_act_freq;
            conversion_direction = conversion_dir_none;
        }
        //-------------------------------------
        else if ( f_rf > (CARIBOULITE_2G4_MAX) && 
                f_rf <= CARIBOULITE_MAX_MIX )
        {
            // region #3 - DOWN-CONVERSION
            // setup modem frequency <= CARIBOULITE_2G4_MIN
            modem_act_freq = (double)at86rf215_setup_channel (&rad->cariboulite_sys->modem, 
                                                        at86rf215_rf_channel_2400mhz, 
                                                        (uint32_t)(CARIBOULITE_2G4_MIN));

            // setup mixer LO to according to actual modem frequency
            lo_act_freq = rffc507x_set_frequency(&rad->cariboulite_sys->mixer, f_rf + modem_act_freq);
            act_freq = modem_act_freq + lo_act_freq;

            // setup fpga RFFE <= downconvert (tx / rx)
            conversion_direction = conversion_dir_down;
        }
        //-------------------------------------
        else
        {
            // error - unsupported frequency for 6G channel
            ZF_LOGE("unsupported frequency for 6GHz channel - %.2f Hz", f_rf);
            error = -1;
        }

        // Setup the frontend
        // This step takes the current radio direction of communication
        // and the down/up conversion decision made before to setup the RF front-end
        switch (conversion_direction)
        {
            case conversion_dir_up: 
                if (rad->channel_direction == cariboulite_channel_dir_rx) 
                {
                    caribou_fpga_set_io_ctrl_mode (&rad->cariboulite_sys->fpga, 0, caribou_fpga_io_ctrl_rfm_rx_lowpass);
                }
                else if (rad->channel_direction == cariboulite_channel_dir_tx)
                {
                    caribou_fpga_set_io_ctrl_mode (&rad->cariboulite_sys->fpga, 0, caribou_fpga_io_ctrl_rfm_tx_lowpass);
                }
                break;
            case conversion_dir_none: 
                caribou_fpga_set_io_ctrl_mode (&rad->cariboulite_sys->fpga, 0, caribou_fpga_io_ctrl_rfm_bypass);
                break;
            case conversion_dir_down:
                if (rad->channel_direction == cariboulite_channel_dir_rx)
                {
                    caribou_fpga_set_io_ctrl_mode (&rad->cariboulite_sys->fpga, 0, caribou_fpga_io_ctrl_rfm_rx_hipass);
                }
                else if (rad->channel_direction == cariboulite_channel_dir_tx)
                {
                    caribou_fpga_set_io_ctrl_mode (&rad->cariboulite_sys->fpga, 0, caribou_fpga_io_ctrl_rfm_tx_hipass);
                }
                break;
            default: break;
        }

        // Make sure the LO and the IF PLLs are locked
        at86rf215_radio_set_state( &rad->cariboulite_sys->modem, 
                                    GET_CH(channel), 
                                    at86rf215_radio_state_cmd_tx_prep);
        rad->state = at86rf215_radio_state_cmd_tx_prep;

        if (!cariboulite_wait_for_lock(rad, &rad->modem_pll_locked, &rad->lo_pll_locked, 100))
        {
            if (!rad->lo_pll_locked) ZF_LOGE("PLL MIXER failed to lock LO frequency (%.2f Hz)", lo_act_freq);
            if (!rad->modem_pll_locked) ZF_LOGE("PLL MODEM failed to lock IF frequency (%.2f Hz)", modem_act_freq);
            error = 1;
        }

        // Update the actual frequencies
        rad->lo_frequency = lo_act_freq;
        rad->if_frequency = modem_act_freq;
        rad->actual_rf_frequency = act_freq;
        rad->requested_rf_frequency = f_rf;
        rad->rf_frequency_error = rad->actual_rf_frequency - rad->requested_rf_frequency;
        if (freq) *freq = act_freq;

        // activate the channel according to the new configuration
        //cariboulite_activate_channel(radios, channel);
    }

    if (error >= 0)
    {
        ZF_LOGD("Frequency setting CH: %d, Wanted: %.2f Hz, Set: %.2f Hz (MOD: %.2f, MIX: %.2f)", 
                        channel, f_rf, act_freq, modem_act_freq, lo_act_freq);
    }

    return -error;
}

//======================================================================
int cariboulite_get_frequency(  cariboulite_radios_st* radios, 
                                cariboulite_channel_en channel, 
                                double *freq, double *lo, double* i_f)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    if (freq) *freq = rad->actual_rf_frequency;
    if (lo) *lo = rad->lo_frequency;
    if (i_f) *i_f = rad->if_frequency;
}

//======================================================================
int cariboulite_activate_channel(cariboulite_radios_st* radios, 
                                cariboulite_channel_en channel)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);

    // if the channel state is active, turn it off before reactivating
    if (rad->state != at86rf215_radio_state_cmd_tx_prep)
    {
        at86rf215_radio_set_state( &rad->cariboulite_sys->modem, 
                                    GET_CH(channel), 
                                    at86rf215_radio_state_cmd_tx_prep);
        rad->state = at86rf215_radio_state_cmd_tx_prep;
    }

    // Activate the channel according to the configurations
    // RX on both channels looks the same
    if (rad->channel_direction == cariboulite_channel_dir_rx)
    {
        at86rf215_radio_set_state( &rad->cariboulite_sys->modem, 
                                GET_CH(channel),
                                at86rf215_radio_state_cmd_rx);
    }
    else if (rad->channel_direction == cariboulite_channel_dir_tx)
    {
        // if its an LO frequency output from the mixer - no need for modem output
        // LO applicable only to the channel with the mixer
        if (rad->lo_output && channel == cariboulite_channel_6g)
        {
            // here we need to configure lo bypass on the mixer
            rffc507x_output_lo(&rad->cariboulite_sys->mixer, 1);
        }
        // otherwise we need the modem
        else
        {
            // make sure the mixer doesn't bypass the lo
            rffc507x_output_lo(&rad->cariboulite_sys->mixer, 0);

            cariboulite_set_tx_bandwidth(radios, channel,
                        rad->cw_output?at86rf215_radio_tx_cut_off_80khz:rad->tx_bw);

            // CW output - constant I/Q values override
            at86rf215_radio_set_tx_dac_input_iq(&rad->cariboulite_sys->modem, 
                                                GET_CH(channel), 
                                                rad->cw_output, 0x7E, 
                                                rad->cw_output, 0x3F);

            // transition to state TX
            at86rf215_radio_set_state(&rad->cariboulite_sys->modem, 
                                        GET_CH(channel),
                                        at86rf215_radio_state_cmd_tx);

        }
    }

    return 0;
}

//======================================================================
int cariboulite_set_cw_outputs(cariboulite_radios_st* radios, 
                               cariboulite_channel_en channel, bool lo_out, bool cw_out)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);

    if (rad->lo_output && channel == cariboulite_channel_6g)
    {
        rad->lo_output = lo_out;
    }
    else
    {
        rad->lo_output = false;
    }
    rad->cw_output = cw_out;

    return 0;
}

//======================================================================
int cariboulite_get_cw_outputs(cariboulite_radios_st* radios, 
                               cariboulite_channel_en channel, bool *lo_out, bool *cw_out)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);
    if (lo_out) *lo_out = rad->lo_output;
    if (cw_out) *cw_out = rad->cw_output;

    return 0;
}

//=================================================
int cariboulite_create_smi_stream(cariboulite_radios_st* radios, 
                               cariboulite_channel_en channel,
                               cariboulite_channel_dir_en dir,
                               int buffer_length,
                               void* context)
{
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);

    caribou_smi_channel_en ch = (channel == cariboulite_channel_s1g) ? 
                                    caribou_smi_channel_900 : caribou_smi_channel_2400;
    caribou_smi_stream_type_en type = (dir == cariboulite_channel_dir_rx) ? 
                                    caribou_smi_stream_type_read : caribou_smi_stream_type_write;

    int stream_id = caribou_smi_setup_stream(&rad->cariboulite_sys->smi,
                                                type, ch,
                                                buffer_length, 2,
                                                caribou_smi_data_event,
                                                context);
    
    // keep the stream ids
    if (type == caribou_smi_stream_type_read)
    {
        rad->rx_stream_id = stream_id;
    }
    else if (type == caribou_smi_stream_type_write)
    {
        rad->tx_stream_id = stream_id;
    }
    return stream_id;
}

//=================================================
int cariboulite_destroy_smi_stream(cariboulite_radios_st* radios,
                               cariboulite_channel_en channel,
                               cariboulite_channel_dir_en dir)
{
    // fetch the stream id
    cariboulite_radio_state_st* rad = GET_RADIO_PTR(radios,channel);

    int stream_id = (dir == cariboulite_channel_dir_rx) ? rad->rx_stream_id : rad->tx_stream_id;
    if (stream_id == -1)
    {
        ZF_LOGE("The specified channel (%d) doesn't have open stream of type %d", channel, dir);
        return -1;
    }

    caribou_smi_destroy_stream(&rad->cariboulite_sys->smi, stream_id);
}
    