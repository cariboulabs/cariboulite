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
#include "cariboulite_config/cariboulite_config.h"

int cariboulite_setup_io (cariboulite_st* sys, void* sighandler);
int cariboulite_release_io (cariboulite_st* sys);
int cariboulite_configure_fpga (cariboulite_st* sys, char* fpga_bin_path);
int cariboulite_init_submodules (cariboulite_st* sys);
int cariboulite_release_submodules(cariboulite_st* sys);
int cariboulite_self_test(cariboulite_st* sys);

#endif // __CARIBOULITE_SETUP_H__