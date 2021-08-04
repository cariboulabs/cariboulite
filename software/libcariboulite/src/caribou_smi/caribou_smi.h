#ifndef __CARIBOU_SMI_H__
#define __CARIBOU_SMI_H__

#include "caribou_smi_defs.h"
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_sys_info.h"
#include "dma_utils.h"

//==========================================================
// SAMPLING INFO
//==========================================================
#define SAMPLE_SIZE_BYTES  (4)

// step: in clock cycles of the specific platform (RPI0-3 or RPI4)
// setup, strobe, and hold: calculated in number of steps.
// 
// Thus, the total sample period (in nanosecs) is given by:
//    step * ( setup + strobe + hold ) * ns_per_clock_cycle
//
// For an RPI4 @ 1500 MHz clock => 0.667 nsec / clock cycle
// The configuration of: (step, setup, strobe, hold) = (4,  3,  8,  4)
// yields: 0.667nsec*4*(3+8+4) = 8/3*15 = 40 nanoseconds/sample => 25 MSPS
typedef struct
{
    int step_size;
    int setup_steps;
    int strobe_steps;
    int hold_steps;
} caribou_smi_timing_st;

typedef enum
{
    caribou_smi_transaction_size_8bits = 0,
    caribou_smi_transaction_size_16bits = 1,
    caribou_smi_transaction_size_18bits = 2,
    caribou_smi_transaction_size_9bits = 3,
} caribou_smi_transaction_size_bits_en;

typedef struct
{
    int data_0_pin;
    int num_data_pins;
    int soe_pin;
    int swe_pin;
    int addr0_pin;
    int num_addr_pins;
    int read_req_pin;
    int write_req_pin;

    uint32_t sample_buf_length;
    uint32_t num_sample_bufs;
    int dma_channel;

    // Internal varibles
    int initialized;
    io_utils_sys_info_st sys_info;
    uint32_t videocore_alloc_size;
    float actual_sample_buf_length_sec;

    dma_utils_st dma;
    MEM_MAP smi_regs;
    MEM_MAP vc_mem;
    MEM_MAP clk_regs;

    SMI_CS_REG  *smi_cs;
    SMI_L_REG   *smi_l;
    SMI_A_REG   *smi_a;
    SMI_D_REG   *smi_d;
    SMI_DMC_REG *smi_dmc;
    SMI_DSR_REG *smi_dsr0;
    SMI_DSW_REG *smi_dsw0;
    SMI_DSR_REG *smi_dsr1;
    SMI_DSW_REG *smi_dsw1;
    SMI_DSR_REG *smi_dsr2;
    SMI_DSW_REG *smi_dsw2;
    SMI_DSR_REG *smi_dsr3;
    SMI_DSW_REG *smi_dsw3;
    SMI_DCS_REG *smi_dcs;
    SMI_DCA_REG *smi_dca;
    SMI_DCD_REG *smi_dcd;
    
} caribou_smi_st;

int caribou_smi_init(caribou_smi_st* dev);
int caribou_smi_close(caribou_smi_st* dev);

#endif // __CARIBOU_SMI_H__