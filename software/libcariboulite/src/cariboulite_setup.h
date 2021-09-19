#ifndef __CARIBOULITE_SETUP_H__
#define __CARIBOULITE_SETUP_H__

#ifdef __cplusplus
extern "C" {
#endif


#include "latticeice40/latticeice40.h"
#include "caribou_fpga/caribou_fpga.h"
#include "at86rf215/at86rf215.h"
#include "rffc507x/rffc507x.h"
#include "caribou_smi/caribou_smi.h"
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"
#include "io_utils/io_utils_sys_info.h"
#include "ustimer/ustimer.h"
#include "cariboulite_config/cariboulite_config.h"

#define CARIBOULITE_MAJOR_VERSION 1
#define CARIBOULITE_MINOR_VERSION 0
#define CARIBOULITE_REVISION 1


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
    cariboulite_board_detection_failed          = -1,
    cariboulite_io_setup_failed                 = -2,
    cariboulite_fpga_configuration_failed       = -3,
    cariboulite_submodules_init_failed          = -4,
    cariboulite_self_test_failed                = -5,
} cariboulite_errors_en;


int cariboulite_init_driver(cariboulite_st *sys, void* signal_handler_cb, cariboulite_board_info_st *info);
void cariboulite_release_driver(cariboulite_st *sys);
void cariboulite_lib_version(cariboulite_lib_version_st* v);
int cariboulite_get_serial_number(cariboulite_st *sys, uint32_t* serial_number, int *count);
int cariboulite_setup_io (cariboulite_st* sys, void* sighandler);
int cariboulite_release_io (cariboulite_st* sys);
int cariboulite_configure_fpga (cariboulite_st* sys, cariboulite_firmware_source_en src, char* fpga_bin_path);
int cariboulite_init_submodules (cariboulite_st* sys);
int cariboulite_release_submodules(cariboulite_st* sys);
int cariboulite_self_test(cariboulite_st* sys);

#ifdef __cplusplus
}
#endif


#endif // __CARIBOULITE_SETUP_H__