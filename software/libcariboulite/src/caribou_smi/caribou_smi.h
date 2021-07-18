#ifndef __CARIBOU_SMI_H__
#define __CARIBOU_SMI_H__

#include "caribou_smi_defs.h"
#include "io_utils/io_utils.h"

typedef struct
{
    int fd;         // File descriptor
    int h;          // Memory handle
    int size;       // Memory size
    void *bus;      // Bus address
    void *virt;     // Virtual address
    void *phys;     // Physical address
} caribou_smi_mem_map_st;

#define REG32(m, x) ((volatile uint32_t *)((uint32_t)(m.virt)+(uint32_t)(x)))


typedef struct
{
    int initialized;

    int data_0_pin;
    int num_data_pins;
    int soe_pin;
    int swe_pin;
    int addr0_pin;
    int num_addr_pins;
    int read_req_pin;
    int write_req_pin;

    caribou_smi_mem_map_st dma_regs, vc_mem, clk_regs, smi_regs;

    volatile SMI_CS_REG  *smi_cs;
    volatile SMI_L_REG   *smi_l;
    volatile SMI_A_REG   *smi_a;
    volatile SMI_D_REG   *smi_d;
    volatile SMI_DMC_REG *smi_dmc;
    volatile SMI_DSR_REG *smi_dsr;
    volatile SMI_DSW_REG *smi_dsw;
    volatile SMI_DCS_REG *smi_dcs;
    volatile SMI_DCA_REG *smi_dca;
    volatile SMI_DCD_REG *smi_dcd;
    
} caribou_smi_st;

int caribou_smi_init(caribou_smi_st* dev);
int caribou_smi_close(caribou_smi_st* dev);

#endif // __CARIBOU_SMI_H__