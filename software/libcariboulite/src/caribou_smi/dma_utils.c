#include "dma_utils.h"
#include "register_utils.h"
#include "mbox_utils.h"


// Control Blocks and data to be in uncached memory
typedef struct {
    DMA_CB cbs[NUM_CBS];
    uint32_t pindata, pwmdata;
} DMA_TEST_DATA;
 

// Updated DMA trigger test, using data structure
/*void dma_test_pwm_trigger(int pin)
{
    DMA_TEST_DATA *dp=virt_dma_mem;
    DMA_TEST_DATA dma_data = {
        .pindata=1<<pin, .pwmdata=PWM_RANGE/2,
        .cbs = {
          // TI      Srce addr          Dest addr        Len   Next CB
            {PWM_TI, MEM(&dp->pindata), GPIO(GPIO_CLR0), 4, 0, CBS(1), 0},  // 0
            {PWM_TI, MEM(&dp->pwmdata), PWM(PWM_FIF1),   4, 0, CBS(2), 0},  // 1
            {PWM_TI, MEM(&dp->pindata), GPIO(GPIO_SET0), 4, 0, CBS(3), 0},  // 2
            {PWM_TI, MEM(&dp->pwmdata), PWM(PWM_FIF1),   4, 0, CBS(0), 0},  // 3
        }
    };
    memcpy(dp, &dma_data, sizeof(dma_data));    // Copy data into uncached memory
    init_pwm(PWM_FREQ);                         // Enable PWM with DMA
    *VIRT_PWM_REG(PWM_DMAC) = PWM_DMAC_ENAB|1;
    start_dma(&dp->cbs[0]);                     // Start DMA
    start_pwm();                                // Start PWM
    sleep(4);                                   // Do nothing while LED flashing
}*/

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
    *REG32(dev->dma_regs, DMA_REG(chan, DMA_CS)) = 1 << 31;
}

//================================================================================
void dma_utils_start_dma(dma_utils_st *dev, MEM_MAP *mp, int chan, DMA_CB *cbp, uint32_t csval)
{
    *REG32(dev->dma_regs, DMA_REG(chan, DMA_CONBLK_AD)) = MEM_BUS_ADDR(mp, cbp);
    *REG32(dev->dma_regs, DMA_REG(chan, DMA_CS)) = 2;        // Clear 'end' flag
    *REG32(dev->dma_regs, DMA_REG(chan, DMA_DEBUG)) = 7;     // Clear error bits
    *REG32(dev->dma_regs, DMA_REG(chan, DMA_CS)) = 1|csval;  // Start DMA
}

//================================================================================
uint32_t dma_utils_dma_transfer_len(dma_utils_st *dev, int chan)
{
    return(*REG32(dev->dma_regs, DMA_REG(chan, DMA_TXFR_LEN)));
}

//================================================================================
uint32_t dma_utils_dma_active(dma_utils_st *dev, int chan)
{
    return((*REG32(dev->dma_regs, DMA_REG(chan, DMA_CS))) & 1);
}

//================================================================================
void dma_utils_stop_dma(dma_utils_st *dev, int chan)
{
    if (dev->dma_regs.virt)
    {
        *REG32(dev->dma_regs, DMA_REG(chan, DMA_CS)) = 1 << 31;
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