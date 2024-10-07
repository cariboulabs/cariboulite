/**
 * @file cariboulite_radio.h
 * @author David Michaeli
 * @date September 2023
 * @brief System level radio API
 *
 * Radio device implementation exposing TxRx API
 */
#ifndef __CARIBOULABS_RADIO_H__
#define __CARIBOULABS_RADIO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Radio channel direction
 */
typedef enum
{
    cariboulite_channel_dir_rx = 0,
    cariboulite_channel_dir_tx = 1,
} cariboulite_channel_dir_en;

typedef enum
{
    cariboulite_channel_s1g = 0,
    cariboulite_channel_hif = 1,
} cariboulite_channel_en;

typedef enum
{
    cariboulite_ext_ref_off     = 0,
    cariboulite_ext_ref_26mhz   = 26,
    cariboulite_ext_ref_32mhz   = 32,
} cariboulite_ext_ref_freq_en;

typedef enum
{
    cariboulite_radio_cmd_nop = 0x0,              // No operation (dummy)
    cariboulite_radio_cmd_sleep = 0x1,            // Go to sleep
    cariboulite_radio_state_cmd_trx_off = 0x2,    // Transceiver off, SPI active
    cariboulite_radio_state_cmd_tx_prep = 0x3,    // Transmit preparation
    cariboulite_radio_state_cmd_tx = 0x4,         // Transmit
    cariboulite_radio_state_cmd_rx = 0x5,         // Receive
    cariboulite_radio_state_transition = 0x6,     // State transition in progress
    cariboulite_radio_state_cmd_reset = 0x7,      // Transceiver is in state RESET or SLEEP
                                                  // if commanded reset, the transceiver state will
                                                  // automatically end up in state TRXOFF
} cariboulite_radio_state_cmd_en;

typedef enum
{
    cariboulite_radio_rx_bw_160KHz = 0x0,
    cariboulite_radio_rx_bw_200KHz = 0x1,
    cariboulite_radio_rx_bw_250KHz = 0x2,
    cariboulite_radio_rx_bw_320KHz = 0x3,
    cariboulite_radio_rx_bw_400KHz = 0x4,
    cariboulite_radio_rx_bw_500KHz = 0x5,
    cariboulite_radio_rx_bw_630KHz = 0x6,
    cariboulite_radio_rx_bw_800KHz = 0x7,
    cariboulite_radio_rx_bw_1000KHz = 0x8,
    cariboulite_radio_rx_bw_1250KHz = 0x9,
    cariboulite_radio_rx_bw_1600KHz = 0xA,
    cariboulite_radio_rx_bw_2000KHz = 0xB,
} cariboulite_radio_rx_bw_en;

typedef enum
{
    cariboulite_radio_rx_f_cut_0_25_half_fs = 0,      // whan 4MSPS => 500 KHz
    cariboulite_radio_rx_f_cut_0_375_half_fs = 1,     // whan 4MSPS => 750 KHz
    cariboulite_radio_rx_f_cut_0_5_half_fs = 2,       // whan 4MSPS => 1000 KHz
    cariboulite_radio_rx_f_cut_0_75_half_fs = 3,      // whan 4MSPS => 1500 KHz
    cariboulite_radio_rx_f_cut_half_fs = 4,           // whan 4MSPS => 2000 KHz
} cariboulite_radio_f_cut_en;

typedef enum
{
    cariboulite_radio_rx_sample_rate_4000khz = 0x1,
    cariboulite_radio_rx_sample_rate_2000khz = 0x2,
    cariboulite_radio_rx_sample_rate_1333khz = 0x3,
    cariboulite_radio_rx_sample_rate_1000khz = 0x4,
    cariboulite_radio_rx_sample_rate_800khz = 0x5,
    cariboulite_radio_rx_sample_rate_666khz = 0x6,
    cariboulite_radio_rx_sample_rate_500khz = 0x8,
    cariboulite_radio_rx_sample_rate_400khz = 0xA,
} cariboulite_radio_sample_rate_en;

typedef enum
{
    cariboulite_radio_tx_cut_off_80khz = 0x0,
    cariboulite_radio_tx_cut_off_100khz = 0x1,
    cariboulite_radio_tx_cut_off_125khz = 0x2,
    cariboulite_radio_tx_cut_off_160khz = 0x3,
    cariboulite_radio_tx_cut_off_200khz = 0x4,
    cariboulite_radio_tx_cut_off_250khz = 0x5,
    cariboulite_radio_tx_cut_off_315khz = 0x6,
    cariboulite_radio_tx_cut_off_400khz = 0x7,
    cariboulite_radio_tx_cut_off_500khz = 0x8,
    cariboulite_radio_tx_cut_off_625khz = 0x9,
    cariboulite_radio_tx_cut_off_800khz = 0xA,
    cariboulite_radio_tx_cut_off_1000khz = 0xB,
} cariboulite_radio_tx_cut_off_en;

typedef struct __attribute__((__packed__))
{
    uint8_t wake_up_por:1;
    uint8_t trx_ready:1;
    uint8_t energy_detection_complete:1;
    uint8_t battery_low:1;
    uint8_t trx_error:1;
    uint8_t IQ_if_sync_fail:1;
    uint8_t res :2;
} cariboulite_radio_irq_st;

typedef struct __attribute__((__packed__))
{
    int16_t i;                      // LSB
	int16_t q;                      // MSB
} cariboulite_sample_complex_int16;

typedef struct __attribute__((__packed__))
{
    uint8_t sync;
} cariboulite_sample_meta;


// Frequency Ranges
#define CARIBOULITE_6G_MIN      (1.0e6)
#define CARIBOULITE_6G_MAX      (6000.0e6)
#define CARIBOULITE_MIN_LO      (85.0e6)
#define CARIBOULITE_MAX_LO      (4200.0e6)
#define CARIBOULITE_2G4_MIN     (2385.0e6)
#define CARIBOULITE_2G4_MAX     (2495.0e6)
#define CARIBOULITE_S1G_MIN1    (377.0e6)
#define CARIBOULITE_S1G_MAX1    (530.0e6)
#define CARIBOULITE_S1G_MIN2    (779.0e6)
#define CARIBOULITE_S1G_MAX2    (1020.0e6)

typedef enum
{
    conversion_dir_none = 0,
    conversion_dir_up = 1,
    conversion_dir_down = 2,
} cariboulite_conversion_dir_en;

// Radio Struct
typedef struct
{
    struct sys_st_t*               		sys;
    cariboulite_channel_dir_en          channel_direction;
    cariboulite_channel_en              type;
    bool                                active;
    bool                                cw_output;
    bool                                lo_output;
 
    // MODEM STATES
    cariboulite_radio_state_cmd_en      state;
    cariboulite_radio_irq_st            interrupts;

    bool                                rx_agc_on;
    int                                 rx_gain_value_db;
    
    cariboulite_radio_rx_bw_en          rx_bw;
    cariboulite_radio_f_cut_en          rx_fcut;
    cariboulite_radio_sample_rate_en    rx_fs;

    int                                 tx_power;
    cariboulite_radio_tx_cut_off_en     tx_bw;
    cariboulite_radio_f_cut_en          tx_fcut;
    cariboulite_radio_sample_rate_en    tx_fs;
    
    bool                                tx_loopback_anabled;

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
    int                                 smi_channel_id;

    // OTHERS
    uint8_t                             random_value;
    float                               rx_thermal_noise_floor;
} cariboulite_radio_state_st;

/**
 * @brief Initialize a radio device
 *
 * Initialize a radio device allocated and given by the user. The radio device
 * is setup to a known state and the internal variables (in the struct) are initialized.
 * Note: This function shouldn't normally be used by the end-user as the radio module
 *       is initialized by the "driver initializations sequence" in "cariboulite_setup.h"
 *       and is ready to be used as the driver loading finishes
 *
 * @param radio a pre-allocated radio state structure to initialize
 * @param sys a pointer to the system environment containing the relevant
 *            low level drivers and init / close logic.
 * @param type the type of the channel (6g, or s1g)
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_init(cariboulite_radio_state_st* radio, struct sys_st_t *sys, cariboulite_channel_en type);
/**
 * @brief Disposing the radio device
 *
 * Remove and free the resources taken by the radio device. The system returns to
 * its known state.
 * Note: This function shouldn't normally be used by the end-user as the radio module
 *       is disposed by the "driver closing sequence" in "cariboulite_setup.h"
 *       and is supposed to be hidden
 *
 * @param radio a pre-allocated radio state structure
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_dispose(cariboulite_radio_state_st* radio);
/**
 * @brief Synchronizing radio struture contents to the hardware actual state
 *
 * Reading all the relevant modem internal (hardware) states to update and synchronize
 * the software state to them.
 *
 * @param radio a pre-allocated radio state structure
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_sync_information(cariboulite_radio_state_st* radio);
/**
 * @brief Set modem's external refernece ferquency
 *
 * This ext reference is needed for the mixer to lock
 *
 * @param sys a pointer to the system environment containing the relevant
 *            low level drivers and init / close logic.
 * @param ref the used reference source (none, 26MHz, 32MHz, etc.)
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_ext_ref (struct sys_st_t *sys, cariboulite_ext_ref_freq_en ref);

/**
 * @brief Get modem chip state
 *
 * Getting the modem current state
 *
 * @param radio a pre-allocated radio state structure
 * @param state the modem state (pre-allocated pointer), nullable if not needed
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_mod_state (cariboulite_radio_state_st* radio, cariboulite_radio_state_cmd_en *state);

/**
 * @brief Get modem irq table
 *
 * Reading the IRQ states table of the modem
 *
 * @param radio a pre-allocated radio state structure
 * @param irq_table a table of interrupts state of the modem (nullable if not needed)
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_mod_intertupts (cariboulite_radio_state_st* radio, cariboulite_radio_irq_st **irq_table);

/**
 * @brief Modem Rx gain control (write)
 *
 * Setting the Rx gain properties of the modem
 *
 * @param radio a pre-allocated radio state structure
 * @param rx_agc_on enable modem automatic gain control
 * @param rx_gain_value_db if AGC is disabled, here we can set the gain manually
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_set_rx_gain_control(cariboulite_radio_state_st* radio, 
                                    bool rx_agc_on,
                                    int rx_gain_value_db);

/**
 * @brief Modem Rx gain control (read)
 *
 * Getting the Rx gain properties of the modem
 *
 * @param radio a pre-allocated radio state structure
 * @param rx_agc_on modem automatic gain control enabled
 * @param rx_gain_value_db when AGC is disabled this is the actual gain control
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_rx_gain_control(cariboulite_radio_state_st* radio,
                                    bool *rx_agc_on,
                                    int *rx_gain_value_db);

/**
 * @brief Modem Rx gain capabilities
 *
 * Reading the modem possible gain limits on Rx
 *
 * @param radio a pre-allocated radio state structure
 * @param rx_min_gain_value_db modem gain limit low [dB]
 * @param rx_max_gain_value_db modem gain limit high [dB]
 * @param rx_gain_value_resolution_db modem gain steps [dB]
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_rx_gain_limits(cariboulite_radio_state_st* radio, 
                                    int *rx_min_gain_value_db,
                                    int *rx_max_gain_value_db,
                                    int *rx_gain_value_resolution_db);

/**
 * @brief Modem set RX analog bandwidth
 *
 * Setting the RX analog bandwidth
 *
 * @param radio a pre-allocated radio state structure
 * @param rx_bw bandwidth value
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_set_rx_bandwidth(cariboulite_radio_state_st* radio, 
                                 cariboulite_radio_rx_bw_en rx_bw);
int cariboulite_radio_set_rx_bandwidth_flt(cariboulite_radio_state_st* radio, float bw_hz);

/**
 * @brief Modem get RX analog bandwidth
 *
 * Getting the RX analog bandwidth
 *
 * @param radio a pre-allocated radio state structure
 * @param rx_bw bandwidth value (pointer - pre-allocated), nullable if not needed
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_rx_bandwidth(cariboulite_radio_state_st* radio, 
                                 cariboulite_radio_rx_bw_en *rx_bw);
int cariboulite_radio_get_rx_bandwidth_flt(cariboulite_radio_state_st* radio, float* bw_hz);

/**
 * @brief Modem set RX sample cut-off bandwidth
 *
 * Setting the RX channel sample cut-off bandwidth (digital)
 *
 * @param radio a pre-allocated radio state structure
 * @param rx_sample_rate the used rx sample rate
 * @param rx_cutoff digital bandwidth
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_set_rx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   cariboulite_radio_sample_rate_en rx_sample_rate,
                                   cariboulite_radio_f_cut_en rx_cutoff);
int cariboulite_radio_set_rx_sample_rate_flt(cariboulite_radio_state_st* radio, float sample_rate_hz);

/**
 * @brief Modem get RX sample cut-off bandwidth
 *
 * Getting the RX channel sample cut-off bandwidth (digital)
 *
 * @param radio a pre-allocated radio state structure
 * @param rx_sample_rate the used rx sample rate (pointer - pre-allocated), nullable if not needed
 * @param rx_cutoff digital bandwidth (pointer - pre-allocated), nullable if not needed
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_rx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   cariboulite_radio_sample_rate_en *rx_sample_rate,
                                   cariboulite_radio_f_cut_en *rx_cutoff);
int cariboulite_radio_get_rx_sample_rate_flt(cariboulite_radio_state_st* radio, float *sample_rate_hz);

/**
 * @brief Modem set TX power
 *
 * Setting the Modem's output TX power towards the RFFE
 *
 * @param radio a pre-allocated radio state structure
 * @param tx_power_dbm The TX power in dBm
 * @return 0 = success, -1 = failure
 */

int cariboulite_radio_set_tx_power(cariboulite_radio_state_st* radio, 
                             int tx_power_dbm);

/**
 * @brief Modem get TX power
 *
 * Getting the Modem's output TX power towards the RFFE
 *
 * @param radio a pre-allocated radio state structure
 * @param tx_power_dbm The TX power in dBm (pointer - pre-allocated), nullable if not needed
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_tx_power(cariboulite_radio_state_st* radio, 
                             int *tx_power_dbm);

/**
 * @brief Modem set Tx analog Bandwidth
 *
 * Setting the Modem's Tx analog bandwidth
 *
 * @param radio a pre-allocated radio state structure
 * @param tx_bw bandwidth (according to enumeration)
 * @return 0 = success, -1 = failure
 */

int cariboulite_radio_set_tx_bandwidth(cariboulite_radio_state_st* radio, 
                                 cariboulite_radio_tx_cut_off_en tx_bw);
int cariboulite_radio_set_tx_bandwidth_flt(cariboulite_radio_state_st* radio, float tx_bw);

/**
 * @brief Modem get Tx analog Bandwidth
 *
 * Getting the current modem's Tx analog bandwidth
 *
 * @param radio a pre-allocated radio state structure
 * @param tx_bw bandwidth (according to enumeration) - pointer, pre-allocated or nullable if not needed
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_tx_bandwidth(cariboulite_radio_state_st* radio, 
                                 cariboulite_radio_tx_cut_off_en *tx_bw);
int cariboulite_radio_get_tx_bandwidth_flt(cariboulite_radio_state_st* radio, float *tx_bw);

/**
 * @brief Modem set Tx digital bandwidth
 *
 * Setting the modem's Tx digital cut-off bandwidth
 *
 * @param radio a pre-allocated radio state structure
 * @param tx_sample_rate Tx sample rate
 * @param tx_cutoff digital cut-off bandwidth
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_set_tx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   cariboulite_radio_sample_rate_en tx_sample_rate,
                                   cariboulite_radio_f_cut_en tx_cutoff);
int cariboulite_radio_set_tx_samp_cutoff_flt(cariboulite_radio_state_st* radio, float sample_rate_hz);

/**
 * @brief Modem get Tx digital bandwidth
 *
 * Getting the current modem's Tx digital cut-off bandwidth
 *
 * @param radio a pre-allocated radio state structure
 * @param tx_sample_rate Tx sample rate, pointer, pre-allocated or nullable if not needed
 * @param tx_cutoff digital cut-off bandwidth, pointer, pre-allocated or nullable if not needed
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_tx_samp_cutoff(cariboulite_radio_state_st* radio, 
                                   cariboulite_radio_sample_rate_en *tx_sample_rate,
                                   cariboulite_radio_f_cut_en *tx_cutoff);
int cariboulite_radio_get_tx_samp_cutoff_flt(cariboulite_radio_state_st* radio, float *sample_rate_hz);

/**
 * @brief Modem get current RSSI
 *
 * Getting the current receive signal strength indication
 *
 * @param radio a pre-allocated radio state structure
 * @param rssi_dbm the RSSI measurement in dBm
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_rssi(cariboulite_radio_state_st* radio, float *rssi_dbm);
/**
 * @brief Modem get current Energy Detection Value
 *
 * Getting the Rx energy detection value
 *
 * @param radio a pre-allocated radio state structure
 * @param energy_det_val the energy value measurement in dBm
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_energy_det(cariboulite_radio_state_st* radio, float *energy_det_val);
/**
 * @brief Modem get a random value
 *
 * The modem has an internal true random noise generation capability. This function
 * returnes the current stored value
 *
 * @param radio a pre-allocated radio state structure
 * @param uint8_t random value, pointer, pre-allocated or nullable if not needed
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_rand_val(cariboulite_radio_state_st* radio, uint8_t *rnd);

/**
 * @brief Wait for all PLLs to lock
 *
 * In the modem this function waits for a safe completion of frequency switch. Until
 * the modem indicates that the frequency was correctly set, the frequency switch should
 * not be takes as granted.
 *
 * @param radio a pre-allocated radio state structure
 * @param retries number of retries (typically up ot 5)
 * @return "0" = didn't lock during the period or "1" - locked successfully
 */
bool cariboulite_radio_wait_modem_lock(cariboulite_radio_state_st* radio, int retries);
bool cariboulite_radio_wait_mixer_lock(cariboulite_radio_state_st* radio, int retries);

/**
 * @brief Set modem frequency
 *
 * Setting the current frequency of the modem. The resulting frequency
 * may be slightly different according to the internal PLL granularity
 * and the resulting frequency is written back to the given pointer.
 * The user may set the "break before make" to stich off the activation
 * bit (reset Rx or Tx) and change the frequency in a cold state
 *
 * @param radio a pre-allocated radio state structure
 * @param break_before_make break the current activity and only then set the frequency
 * @param freq the frequency in Hz, a pointer that carries the frequency to set and 
 *             returns back the actual set frequency after the operation is done
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_set_frequency(cariboulite_radio_state_st* radio, 
									bool break_before_make,
									double *freq);

/**
 * @brief Get current actual frequency
 *
 * Returns the current modem frequecny (which can be slightly different
 * for the one set by the "set frequency" function)
 *
 * @param radio a pre-allocated radio state structure
 * @param freq the frequnecy in Hz, pointer, pre-allocated or nullable if not needed
 * @param lo in case of the high band channel, the LO of the mixer (otherwise unused)
 * @param i_f in case of the high band channel, this is the IF frequency (otherwise unused)
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_frequency(cariboulite_radio_state_st* radio, 
                                	double *freq, double *lo, double* i_f);

/**
 * @brief Activate the channel in a certain state
 *
 * Activates the Tx / Rx channel according to the "dir" parameter (either Tx or Rx). 
 * If we want to de-activate the channel, "dir" doesn't matter
 *
 * @param radio a pre-allocated radio state structure
 * @param dir The channel activation direction
 * @param active either true for activation or false for deactivation
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_activate_channel(cariboulite_radio_state_st* radio,
                                            cariboulite_channel_dir_en dir,
                                			bool active);

/**
 * @brief Set up a CW output upon activation
 *
 * When we set the CW output, whenever the channel will be activated in Tx
 * direction, the modem shall output a CW signal instead of the regular sample
 * output. The CW frequency will be according to the set frequnecy
 *
 * @param radio a pre-allocated radio state structure
 * @param lo_out setting the LO frequency output directly from the mixer (if 6G)
 * @param cw_out activate (true) or deactivate (false) CW - the source is the modem
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_set_cw_outputs(cariboulite_radio_state_st* radio, 
                               			bool lo_out, bool cw_out);

/**
 * @brief Get the current CW output state
 *
 * Checks whether the CW signal output is on
 *
 * @param radio a pre-allocated radio state structure
 * @param lo_out the LO frequency existence from the mixer
 * @param cw_out activate (true) or deactivate (false), pointer, pre-allocated, 
 *               nullable if not needed
 * @return 0 = success, -1 = failure
 */
int cariboulite_radio_get_cw_outputs(cariboulite_radio_state_st* radio, 
                               			bool *lo_out, bool *cw_out);

/**
 * @brief Read samples
 *
 * Read the SMI driver and push the samples into the specified buffer of samples.
 * The number of samples to be read is "length". If needed, a pre-allocated metadata buffer
 * can be transfered to the function. It will hold the sample-matching metadata such as sync
 * bits.
 *
 * @param radio a pre-allocated radio state structure
 * @param buffer a pre-allocated buffer of native samples (complex i/q int16)
 * @param metadata a pre-allocated metadata buffer
 * @param length the number of I/Q samples to read
 * @return the number of samples read
 */
int cariboulite_radio_read_samples(cariboulite_radio_state_st* radio,
                            cariboulite_sample_complex_int16* buffer,
                            cariboulite_sample_meta* metadata,
                            size_t length);
                            
/**
 * @brief Write samples
 *
 * Write to the SMI driver a certain number of samples.
 *
 * @param radio a pre-allocated radio state structure
 * @param buffer a pre-allocated buffer of native samples (complex i/q int16)
 * @param length the number of I/Q samples to write
 * @return the number of samples written
 */
int cariboulite_radio_write_samples(cariboulite_radio_state_st* radio,
                            cariboulite_sample_complex_int16* buffer,
                            size_t length);  

/**
 * @brief Get Native Chunk (MTU)
 *
 * Gets the SMI IO native chunk size (MTU) in units of samples (4 bytes each)
 *
 * @param radio a pre-allocated radio state structure
 * @return the number of samples in an native sized chunk
 */
size_t cariboulite_radio_get_native_mtu_size_samples(cariboulite_radio_state_st* radio);


#ifdef __cplusplus
}
#endif

#endif // __CARIBOULABS_RADIO_H__
