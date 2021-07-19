#ifndef __CARIBOU_SMI_H__
#define __CARIBOU_SMI_H__

#include "caribou_smi_defs.h"
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_sys_info.h"

typedef struct
{
    int fd;         // File descriptor
    int h;          // Memory handle
    int size;       // Memory size
    void *bus;      // Bus address
    void *virt;     // Virtual address
    void *phys;     // Physical address
} caribou_smi_mem_map_st;

typedef enum
{
    caribou_smi_processor_BCM2835 = 0,
    caribou_smi_processor_BCM2836 = 1,
    caribou_smi_processor_BCM2837 = 2,
    caribou_smi_processor_BCM2711 = 3,
    caribou_smi_processor_UNKNOWN = 4,
} caribou_smi_processor_type_en;

#define REG32(m, x) ((volatile uint32_t *)((uint32_t)(m.virt)+(uint32_t)(x)))

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

    int initialized;

    uint32_t ram_size_mbytes;
    caribou_smi_processor_type_en processor_type;
    uint32_t phys_reg_base;
    uint32_t sys_clock_hz;
    uint32_t bus_reg_base;
    uint8_t use_video_core_clock;
    caribou_smi_mem_map_st dma_regs, vc_mem, clk_regs, smi_regs;

    SMI_CS_REG  *smi_cs;
    SMI_L_REG   *smi_l;
    SMI_A_REG   *smi_a;
    SMI_D_REG   *smi_d;
    SMI_DMC_REG *smi_dmc;
    SMI_DSR_REG *smi_dsr;
    SMI_DSW_REG *smi_dsw;
    SMI_DCS_REG *smi_dcs;
    SMI_DCA_REG *smi_dca;
    SMI_DCD_REG *smi_dcd;
    
} caribou_smi_st;

int caribou_smi_init(caribou_smi_st* dev);
int caribou_smi_close(caribou_smi_st* dev);

#endif // __CARIBOU_SMI_H__