#ifndef __CARIBOULITE_SETUP_H__
#define __CARIBOULITE_SETUP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "cariboulite.h"
#include "cariboulite_radios.h"
#include "caribou_fpga/caribou_fpga.h"
#include "at86rf215/at86rf215.h"
#include "rffc507x/rffc507x.h"
#include "caribou_smi/caribou_smi.h"
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"
#include "io_utils/io_utils_sys_info.h"
#include "ustimer/ustimer.h"
#include "cariboulite_config_default.h"

#define CARIBOULITE_MAJOR_VERSION 1
#define CARIBOULITE_MINOR_VERSION 0
#define CARIBOULITE_REVISION 1

typedef struct
{
    int fpga_fail;
    int modem_fail;
    int mixer_fail;
    int smi_fail;
} cariboulite_self_test_result_st;

typedef struct
{
    int major_version;
    int minor_version;
    int revision;
} cariboulite_lib_version_st;

typedef enum
{
    cariboulite_firmware_source_file = 0,
    cariboulite_firmware_source_blob = 1,
} cariboulite_firmware_source_en;

typedef enum
{
    cariboulite_ok                              = 0,
    cariboulite_board_detection_failed          = 1,
    cariboulite_io_setup_failed                 = 2,
    cariboulite_fpga_configuration_failed       = 3,
    cariboulite_submodules_init_failed          = 4,
    cariboulite_self_test_failed                = 5,
    cariboulite_board_dependent_config_failed   = 6,
    cariboulite_signal_registration_failed      = 7,
} cariboulite_errors_en;

int cariboulite_detect_board(sys_st *sys);
void cariboulite_print_board_info(sys_st *sys, bool log);
int cariboulite_init_driver(sys_st *sys, hat_board_info_st *info);
int cariboulite_init_driver_minimal(sys_st *sys, hat_board_info_st *info, bool production);
int cariboulite_init_system_production(sys_st *sys);
int cariboulite_deinit_system_production(sys_st *sys);
int cariboulite_setup_signal_handler (sys_st *sys,
                                        signal_handler handler,
                                        signal_handler_operation_en op,
                                        void *context);
int cariboulite_configure_fpga (sys_st* sys, cariboulite_firmware_source_en src, char* fpga_bin_path);
void cariboulite_release_driver(sys_st *sys);
void cariboulite_lib_version(cariboulite_lib_version_st* v);
int cariboulite_get_serial_number(sys_st *sys, uint32_t* serial_number, int *count);
int cariboulite_setup_io (sys_st* sys);
int cariboulite_release_io (sys_st* sys);
int cariboulite_init_submodules (sys_st* sys);
int cariboulite_release_submodules(sys_st* sys);
int cariboulite_self_test(sys_st* sys, cariboulite_self_test_result_st* res);
                                    
#ifdef __cplusplus
}
#endif


#endif // __CARIBOULITE_SETUP_H__