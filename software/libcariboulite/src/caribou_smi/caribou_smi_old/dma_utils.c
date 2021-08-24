#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "DMA_Utils"
#include "zf_log/zf_log.h"

#include "dma_utils.h"
#include "register_utils.h"
#include "mbox_utils.h"

//================================================================================
void dma_utils_init (dma_utils_st *dev)
{
    // Get the RPI information
    io_utils_get_rpi_info(&dev->sys_info);
    map_periph(&dev->dma_regs, DMA_BASE, PAGE_SIZE);
}

//================================================================================
void dma_utils_close (dma_utils_st *dev)
{
    unmap_periph_mem(&dev->dma_regs);
}

//================================================================================
void dma_utils_enable_dma(dma_utils_st *dev, int chan)
{
    *REG32(dev->dma_regs, DMA_ENABLE) |= (1 << chan);
    *REG32(dev->dma_regs, DMA_REG(chan, DMA_CS)) = 1 << 31; // Resetting / halting the channel
}

//================================================================================
void dma_utils_start_dma(dma_utils_st *dev, MEM_MAP *mp, int chan, DMA_CB *cbp, uint32_t csval)
{
    *REG32(dev->dma_regs, DMA_REG(chan, DMA_CONBLK_AD)) = MEM_BUS_ADDR(mp, cbp);
    *REG32(dev->dma_regs, DMA_REG(chan, DMA_CS)) = 0x2;         // End flag- clear by writing '1' bit 1
    *REG32(dev->dma_regs, DMA_REG(chan, DMA_DEBUG)) = 0x7;      // Clear error bits
    *REG32(dev->dma_regs, DMA_REG(chan, DMA_CS)) = 0x1 | csval; // Start DMA (send '1' to "activate" bit)
}

//================================================================================
uint32_t dma_utils_dma_get_transfer_len(dma_utils_st *dev, int chan)
{
    return(*REG32(dev->dma_regs, DMA_REG(chan, DMA_TXFR_LEN)));
}

//================================================================================
uint32_t dma_utils_dma_is_active(dma_utils_st *dev, int chan)
{
    return((*REG32(dev->dma_regs, DMA_REG(chan, DMA_CS))) & 1);
}

//================================================================================
void dma_utils_stop_dma(dma_utils_st *dev, int chan)
{
    if (dev->dma_regs.virt)
    {
        *REG32(dev->dma_regs, DMA_REG(chan, DMA_CS)) = 1 << 31; // reset / halt
    }
}

//================================================================================
char *dma_regstrs[] = {"DMA CS", "CB_AD", "TI", "SRCE_AD", "DEST_AD",
    "TFR_LEN", "STRIDE", "NEXT_CB", "DEBUG", ""};


void dma_utils_disp_dma(dma_utils_st *dev, int chan)
{
    volatile uint32_t *p=REG32(dev->dma_regs, DMA_REG(chan, DMA_CS));
    int i=0;

    while (dma_regstrs[i][0])
    {
        printf("%-7s %08X ", dma_regstrs[i++], *p++);
        if (i%5==0 || dma_regstrs[i][0]==0)
        {
            printf("\n");
        }
    }
}