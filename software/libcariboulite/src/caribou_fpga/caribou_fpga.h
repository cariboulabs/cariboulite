#ifndef __CARIBOU_FPGA_H__
#define __CARIBOU_FPGA_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"
#include "caribou_programming/caribou_prog.h"

/**
 * @brief Tha manufacturer code - used to verify the FPGA's programming
 */
#define CARIBOU_SDR_MANU_CODE		0x1

#pragma pack(1)
/**
 * @brief Firmware versions and inner modules information
 */
typedef struct
{
    uint8_t sys_ver;
    uint8_t sys_manu_id;
    uint8_t sys_ctrl_mod_ver;
    uint8_t io_ctrl_mod_ver;
    uint8_t smi_ctrl_mod_ver;
} caribou_fpga_versions_st;

/**
 * @brief Firmware interface generic error codes
 */
typedef enum
{
    caribou_fpga_ec_okay                        = 0x00,
    caribou_fpga_ec_write_attempt_to_readonly   = 0x01,
} caribou_fpga_ec_en;   // error codes

/**
 * @brief RF front end modes of operations
 */
typedef enum
{
    caribou_fpga_io_ctrl_rfm_low_power = 0,
    caribou_fpga_io_ctrl_rfm_bypass = 1,
    caribou_fpga_io_ctrl_rfm_rx_lowpass = 2,
    caribou_fpga_io_ctrl_rfm_rx_hipass = 3,
    caribou_fpga_io_ctrl_rfm_tx_lowpass = 4,
    caribou_fpga_io_ctrl_rfm_tx_hipass = 5,
} caribou_fpga_io_ctrl_rfm_en;

/**
 * @brief FPGA status - either not programmed or programmed with
 *        a valid firmware
 */
typedef enum
{	
	caribou_fpga_status_not_programmed = 0,
	caribou_fpga_status_operational = 1,
} caribou_fpga_status_en;

/**
 * @brief RFFE controlling digital pins specifically controlled
 *        when in debug mode (read anytime)
 */
typedef struct
{
    uint8_t mixer_en : 1;           // LSB
    uint8_t lna_rx_shdn : 1;
    uint8_t lna_tx_shdn : 1;
    uint8_t trvc2 : 1;
    uint8_t trvc1_b : 1;
    uint8_t trvc1 : 1;
    uint8_t rx_h_tx_l_b : 1;
    uint8_t rx_h_tx_l : 1;          // MSB
} caribou_fpga_rf_pin_st;

/**
 * @brief SMI fifo status struct
 */
typedef struct
{
    uint8_t rx_fifo_empty : 1;       // LSB
    uint8_t tx_fifo_full : 1;
    uint8_t smi_channel: 1;
    uint8_t i_smi_test : 1;
    uint8_t reserved : 4;            // MSB
} caribou_fpga_smi_fifo_status_st;

/**
 * @brief SMI channel select
 */
typedef enum
{
    caribou_fpga_smi_channel_0 = 0,
    caribou_fpga_smi_channel_1 = 1,
} caribou_fpga_smi_channel_en;

#pragma pack()

/**
 * @brief Firmware control and programming device context
 */
typedef struct
{
    // pinout
    int reset_pin;
	int soft_reset_pin;
    int cs_pin;

    // spi device
    int spi_dev;
    int spi_channel;

	// programming
	caribou_prog_st prog_dev;
	caribou_fpga_status_en status;
	caribou_fpga_versions_st versions;

    // internal controls
    io_utils_spi_st* io_spi;
	int io_spi_handle;
    int initialized;
} caribou_fpga_st;

/**
 * @brief initialize FPGA device driver
 *
 * @param dev pointer to device context - should be preinitialized with pinout and spi info
 * @param io_spi spi device
 * @return int success (0) / failure (-1)
 */
int caribou_fpga_init(caribou_fpga_st* dev, io_utils_spi_st* io_spi);

int caribou_fpga_close(caribou_fpga_st* dev);
int caribou_fpga_soft_reset(caribou_fpga_st* dev);
int caribou_fpga_hard_reset(caribou_fpga_st* dev);
int caribou_fpga_hard_reset_keep(caribou_fpga_st* dev, bool reset);

// programming
int caribou_fpga_get_status(caribou_fpga_st* dev, caribou_fpga_status_en *stat);
int caribou_fpga_program_to_fpga(caribou_fpga_st* dev, unsigned char *buffer, size_t len, bool force_prog);
int caribou_fpga_program_to_fpga_from_file(caribou_fpga_st* dev, char *filename, bool force_prog);

// System Controller
int caribou_fpga_get_versions (caribou_fpga_st* dev, caribou_fpga_versions_st *vers);
void caribou_fpga_print_versions (caribou_fpga_st* dev);
int caribou_fpga_get_errors (caribou_fpga_st* dev, uint8_t *err_map);
char* caribou_fpga_get_mode_name (caribou_fpga_io_ctrl_rfm_en mode);
int caribou_fpga_set_debug_modes (caribou_fpga_st* dev, bool dbg_fifo_push, bool dbg_fifo_pull, bool dbg_smi);

int caribou_fpga_set_sys_ctrl_tx_sample_gap (caribou_fpga_st* dev, uint8_t gap);
int caribou_fpga_get_sys_ctrl_tx_sample_gap (caribou_fpga_st* dev, uint8_t *gap);
int caribou_fpga_set_sys_ctrl_tx_control_word (caribou_fpga_st* dev, uint8_t word);

// I/O Controller
int caribou_fpga_set_io_ctrl_mode (caribou_fpga_st* dev, uint8_t debug_mode, caribou_fpga_io_ctrl_rfm_en rfm);
int caribou_fpga_get_io_ctrl_mode (caribou_fpga_st* dev, uint8_t *debug_mode, caribou_fpga_io_ctrl_rfm_en *rfm);

int caribou_fpga_set_io_ctrl_dig (caribou_fpga_st* dev, int led0, int led1);
int caribou_fpga_get_io_ctrl_dig (caribou_fpga_st* dev, int *led0, int *led1, int *btn, int *cfg);

int caribou_fpga_set_io_ctrl_pmod_dir (caribou_fpga_st* dev, uint8_t dir);
int caribou_fpga_get_io_ctrl_pmod_dir (caribou_fpga_st* dev, uint8_t *dir);

int caribou_fpga_set_io_ctrl_pmod_val (caribou_fpga_st* dev, uint8_t val);
int caribou_fpga_get_io_ctrl_pmod_val (caribou_fpga_st* dev, uint8_t *val);

int caribou_fpga_set_io_ctrl_rf_state (caribou_fpga_st* dev, caribou_fpga_rf_pin_st *pins);
int caribou_fpga_get_io_ctrl_rf_state (caribou_fpga_st* dev, caribou_fpga_rf_pin_st *pins);

// SMI Controller
int caribou_fpga_get_smi_ctrl_fifo_status (caribou_fpga_st* dev, caribou_fpga_smi_fifo_status_st *status);
int caribou_fpga_set_smi_channel (caribou_fpga_st* dev, caribou_fpga_smi_channel_en channel);
int caribou_fpga_set_smi_ctrl_data_direction (caribou_fpga_st* dev, uint8_t dir);

int caribou_fpga_get_debug (caribou_fpga_st* dev, uint8_t *err_map);

#ifdef __cplusplus
}
#endif

#endif // __CARIBOU_FPGA_H__
