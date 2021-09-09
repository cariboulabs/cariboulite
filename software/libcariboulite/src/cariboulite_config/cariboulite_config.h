#ifndef __CARIBOULITE_CONFIG_H__
#define __CARIBOULITE_CONFIG_H__

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

typedef struct
{
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

    // Management
    caribou_fpga_versions_st fpga_versions;
    uint8_t fpga_error_status;
} cariboulite_st;


#endif // __CARIBOULITE_CONFIG_H__