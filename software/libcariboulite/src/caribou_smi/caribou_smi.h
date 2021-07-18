#ifndef __CARIBOU_SMI_H__
#define __CARIBOU_SMI_H__

#include "caribou_smi_defs.h"


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

    extern caribou_smi_mem_map_st gpio_regs, dma_regs;
    caribou_smi_mem_map_st vc_mem, clk_regs, smi_regs;

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