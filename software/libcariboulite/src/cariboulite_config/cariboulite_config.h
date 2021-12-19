#ifndef __CARIBOULITE_CONFIG_H__
#define __CARIBOULITE_CONFIG_H__


#ifdef __cplusplus
extern "C" {
#endif

#include <signal.h>
#include "latticeice40/latticeice40.h"
#include "caribou_fpga/caribou_fpga.h"
#include "at86rf215/at86rf215.h"
#include "rffc507x/rffc507x.h"
#include "caribou_smi/caribou_smi.h"
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"
#include "io_utils/io_utils_sys_info.h"
#include "ustimer/ustimer.h"

// GENERAL SETTINGS
#define MAX_PATH_LEN 512
#define INFO_MAX_LEN 64

struct cariboulite_st_t;

typedef void (*caribou_signal_handler)( struct cariboulite_st_t *sys,       // the current cariboulite low-level management struct
                                        void* context,                      // custom context - can be a higher level app class
                                        int signal_number,                  // the signal number
                                        siginfo_t *si);

typedef enum
{
    cariboulite_signal_handler_op_last = 0,             // The curtom sighandler operates (if present) after the default sig handler
    cariboulite_signal_handler_op_first = 1,            // The curtom sighandler operates (if present) before the default sig handler
    cariboulite_signal_handler_op_override = 2,         // The curtom sighandler operates (if present) instead of the default sig handler
} cariboulite_signal_handler_operation_en;

typedef enum
{
    cariboulite_system_type_unknown = 0,
    cariboulite_system_type_full = 1,
    cariboulite_system_type_ism = 2,
} cariboulite_system_type_en;

typedef struct
{
    char category_name[INFO_MAX_LEN];
    char product_name[INFO_MAX_LEN];
    char product_id[INFO_MAX_LEN];
    char product_version[INFO_MAX_LEN];
    char product_uuid[INFO_MAX_LEN];
    char product_vendor[INFO_MAX_LEN];

    uint32_t numeric_serial_number;
    uint32_t numeric_version;
    uint32_t numeric_product_id;

    cariboulite_system_type_en sys_type;
} cariboulite_board_info_st;


typedef enum
{
    cariboulite_ext_ref_src_modem = 0,
    cariboulite_ext_ref_src_connector = 1,
    cariboulite_ext_ref_src_txco = 2,
    cariboulite_ext_ref_src_na = 3,             // not applicable
} cariboulite_ext_ref_src_en;

typedef struct
{
    cariboulite_ext_ref_src_en src;
    double freq_hz;
} cariboulite_ext_ref_settings_st;

typedef struct cariboulite_st_t
{
    cariboulite_board_info_st board_info;

    // Chip level
    io_utils_spi_st spi_dev;
    caribou_smi_st smi;
    ustimer_t timer;

    // Peripheral chips
    latticeice40_st ice40;
    caribou_fpga_st fpga;
    at86rf215_st modem;
    rffc507x_st mixer;

    // Configuration
    int reset_fpga_on_startup;
    char firmware_path_operational[MAX_PATH_LEN];
    char firmware_path_testing[MAX_PATH_LEN];

    // signals
    caribou_signal_handler signal_cb;
    void* singal_cb_context;
    cariboulite_signal_handler_operation_en sig_op;

    // Management
    caribou_fpga_versions_st fpga_versions;
    cariboulite_ext_ref_settings_st ext_ref_settings;
    uint8_t fpga_error_status;
} cariboulite_st;

int cariboulite_config_detect_board(cariboulite_board_info_st *info);
void cariboulite_config_print_board_info(cariboulite_board_info_st *info);

#ifdef __cplusplus
}
#endif


#endif // __CARIBOULITE_CONFIG_H__