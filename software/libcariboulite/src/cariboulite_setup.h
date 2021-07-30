#ifndef __CARIBOULITE_SETUP_H__
#define __CARIBOULITE_SETUP_H__

#include "latticeice40/latticeice40.h"
#include "caribou_fpga/caribou_fpga.h"
#include "at86rf215/at86rf215.h"
#include "rffc507x/rffc507x.h"
#include "caribou_smi/caribou_smi.h"
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"
#include "io_utils/io_utils_sys_info.h"
#include "ustimer/ustimer.h"

// PINOUT SPI
#define CARIBOULITE_SPI_DEV 1
#define CARIBOULITE_MOSI 20
#define CARIBOULITE_SCK 21
#define CARIBOULITE_MISO 19

// PINOUT FPGA - ICE40
#define CARIBOULITE_FPGA_SPI_CHANNEL 0
#define CARIBOULITE_FPGA_SS 18
#define CARIBOULITE_FPGA_CDONE 27
#define CARIBOULITE_FPGA_CRESET 26

// PINOUT AT86 - AT86RF215
#define CARIBOULITE_MODEM_SPI_CHANNEL 1
#define CARIBOULITE_MODEM_SS 17
#define CARIBOULITE_MODEM_IRQ 22
#define CARIBOULITE_MODEM_RESET 23

// PINOUT MIXER - RFFC5072
#define CARIBOULITE_MIXER_SPI_CHANNEL 2
#define CARIBOULITE_MIXER_SS 16
#define CARIBOULITE_MIXER_RESET 5

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

    // Management
    caribou_fpga_versions_st fpga_versions;
    uint8_t fpga_error_status;
} cariboulite_st;

int cariboulite_setup_io (void);
int cariboulite_release_io (void);
int cariboulite_configure_fpga (char* fpga_bin_path);
int cariboulite_init_submodules (void);
int cariboulite_release_submodules(void);

#endif // __CARIBOULITE_SETUP_H__