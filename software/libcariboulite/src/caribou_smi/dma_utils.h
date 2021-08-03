#ifndef __DMA_UTILS_H__
#define __DMA_UTILS_H__

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "register_utils.h"
#include "../io_utils/io_utils_sys_info.h"

//==========================================================
// DEFINITIONS
//==========================================================

// DMA
//==========================================================
// DMA channels and data requests
#define DMA_CHAN_A      10
#define DMA_CHAN_B      11

/*
    0 DREQ = 1 This is always on so use this channel if no DREQ is required
    1 DSI0 / PWM1 **
    2 PCM TX
    3 PCM RX
    4 SMI
    5 PWM0
    6 SPI0 TX
    7 SPI0 RX
    8 BSC/SPI Slave TX
    9 BSC/SPI Slave RX
    10 HDMI0
    11 eMMC
    12 UART0 TX
    13 SD HOST
    14 UART0 RX
    15 DSI1
    16 SPI1 TX
    17 HDMI1
    18 SPI1 RX
    19 UART3 TX / SPI4 TX **
    20 UART3 RX / SPI4 RX **
    21 UART5 TX / SPI5 TX **
    22 UART5 RX / SPI5 RX **
    23 SPI6 TX
    24 Scaler FIFO 0 & SMI *
    25 Scaler FIFO 1 & SMI *
    26 Scaler FIFO 2 & SMI *
    27 SPI6 RX
    28 UART2 TX
    29 UART2 RX
    30 UART4 TX
    31 UART4 RX

    *)  The SMI element of the Scaler FIFO 0 & SMI DREQs can be disabled by setting the SMI_DISABLE bit in the
        DMA_DREQ_CONTROL register in the system arbiter control block.
*/

#define DMA_SMI_DREQ    XXX
#define DMA_PWM_DREQ    5
#define DMA_BASE        (0x007000)

// DMA register addresses offset by 0x100 * chan_num
#define DMA_CS          0x00
#define DMA_CONBLK_AD   0x04
#define DMA_TI          0x08
#define DMA_SRCE_AD     0x0c
#define DMA_DEST_AD     0x10
#define DMA_TXFR_LEN    0x14
#define DMA_STRIDE      0x18
#define DMA_NEXTCONBK   0x1c
#define DMA_DEBUG       0x20
#define DMA_REG(ch, r)  ((r)==DMA_ENABLE ? DMA_ENABLE : (ch)*0x100+(r))
#define DMA_ENABLE      0xff0

//===================================================================
// DMA TI register values
#define DMA_INTEN       (1 << 0)
/*
    Interrupt Enable
    1 = Generate an interrupt when the transfer described by the current Control Block completes.
    0 = Do not generate an interrupt.
*/

#define DMA_WAIT_RESP   (1 << 3)
/*
    Wait for a Write Response - When set this makes the DMA wait until it receives the AXI
    write response for each write. This ensures that multiple writes cannot get stacked in the AXI bus pipeline.
    1= Wait for the write response to be received before proceeding.
    0 = Don’t wait; continue as soon as the write data is sent.
*/

#define DMA_CB_DEST_INC (1 << 4)
/*
Destination Address Increment
1 = Destination address increments after each write. The address will increment by 4, if DEST_WIDTH=0 else by 32.
0 = Destination address does not change.
*/

#define DMA_DEST_DREQ   (1 << 6)
/*
    Control Destination Writes with DREQ
    1 = The DREQ selected by PERMAP will gate the destination writes.
    0 = DREQ has no effect.
*/

#define DMA_DEST_IGNORE (1<<7)
/*
    Ignore Writes
    1 = Do not perform destination writes.
    0 = Write data to destination.
*/

#define DMA_CB_SRCE_INC (1 << 8)
/*
    Source Address Increment
    1 = Source address increments after each read. The address will increment by 4, if SRC_WIDTH=0 else by 32.
    0 = Source address does not change.
*/

#define DMA_SRCE_DREQ   (1 << 10)
/*
    Control Source Reads with DREQ
    1 = The DREQ selected by PERMAP will gate the source reads.
    0 = DREQ has no effect.
*/

#define DMA_PRIORITY(n) ((n) << 16)
// END: DMA TI register values
//===================================================================


#define VIRT_GPIO_REG(a) ((uint32_t *)((uint32_t)virt_gpio_regs + (a)))
#define BUS_GPIO_REG(a) (GPIO_BASE-PHYS_REG_BASE+BUS_REG_BASE+(uint32_t)(a))
#define BUS_PWM_REG(a)  (PWM_BASE-PHYS_REG_BASE+BUS_REG_BASE+(uint32_t)(a))
#define BUS_DMA_MEM(a)  ((uint32_t)a-(uint32_t)virt_dma_mem+(uint32_t)bus_dma_mem)

// DMA control block macros
#define NUM_CBS         4
#define GPIO(r)         BUS_GPIO_REG(r)
#define PWM(r)          BUS_PWM_REG(r)
#define MEM(m)          BUS_DMA_MEM(m)
#define CBS(n)          BUS_DMA_MEM(&dp->cbs[(n)])
#define PWM_TI          ((1 << 6) | (DMA_PWM_DREQ << 16))

// DMA control block (must be 32-byte aligned)
typedef struct 
{
    uint32_t ti;            // Transfer info
    uint32_t srce_ad;       // Source address
    uint32_t dest_ad;       // Destination address
    uint32_t tfr_len;       // Transfer length
    uint32_t stride;        // Transfer stride
    uint32_t next_cb;       // Next control block
    uint32_t debug;         // Debug register, zero in control block
    uint32_t unused;
} DMA_CB __attribute__ ((aligned(32)));

typedef struct
{
    MEM_MAP dma_regs;

    io_utils_sys_info_st sys_info;
} dma_utils_st;


void dma_utils_init (dma_utils_st *dev);
void dma_utils_close (dma_utils_st *dev);
void dma_utils_enable_dma(dma_utils_st *dev, int chan);
void dma_utils_start_dma(dma_utils_st *dev, MEM_MAP *mp, int chan, DMA_CB *cbp, uint32_t csval);
uint32_t dma_utils_dma_transfer_len(dma_utils_st *dev, int chan);
uint32_t dma_utils_dma_active(dma_utils_st *dev, int chan);
void dma_utils_stop_dma(dma_utils_st *dev, int chan);
void dma_utils_disp_dma(dma_utils_st *dev, int chan);

#endif // __DMA_UTILS_H__