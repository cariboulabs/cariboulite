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
    ...
    2 PCM TX
    3 PCM RX
    4 SMI                    <---====
    5 PWM0
    6 SPI0 TX
    7 SPI0 RX
    ...
*/

#define DMA_SMI_DREQ    4
#define DMA_PWM_DREQ    5       // In cases where the dara request is provided by clock ticks


// DMA register addresses offset by 0x100 * chan_num
#define DMA_BASE        (0x007000)  /* + PHYS_REG_BASE */
#define DMA_CS          0x00        // Control and Status
#define DMA_CONBLK_AD   0x04        // Control Block (CB) Address
#define DMA_TI          0x08        // Transfer Information
#define DMA_SRCE_AD     0x0c        // Source Address
#define DMA_DEST_AD     0x10        // Destination Address
#define DMA_TXFR_LEN    0x14        // Transfer Length
#define DMA_STRIDE      0x18        // 2D Stride
#define DMA_NEXTCONBK   0x1c        // Next CB (Control Block) Address
#define DMA_DEBUG       0x20        // Debug

#define DMA_REG(ch, r)  ((r)==DMA_ENABLE ? DMA_ENABLE : (ch)*0x100+(r))
#define DMA_ENABLE      0xff0
#define REG_DEF(name,fields) typedef union {struct {volatile uint32_t fields;}; volatile uint32_t value;} name

//=====================================================================================
// Control and status register
#define DMA_CS_FIELDS_0_10   \
    activate:1,             /* LSB. Activate the DMA This bit enables the DMA. The DMA will start if this bit is set */ \
                            /* and the CB_ADDR is non zero. The DMA transfer can be paused and resumed by clearing, then setting it again. */ \
    end:1,                  /* DMA End flag- clear by writing '1' */ \
    ints:1,                 /* Interrupt Status - if the INTEN is enabled, then clearing by writing '1'.*/ \
    dreq:1,                 /* DREQ State, READ ONLY*/ \
    paused:1,               /* DMA Paused State. READ ONLY*/ \
    dreq_stops_dma:1,       /* DMA Paused by DREQ State, READ ONLY*/ \
    waiting_outstanding_w:1,/* DMA is Waiting for the Last Write to be Received, READ ONLY*/ \
    res3:1,                                                         \
    error:1,                /* DMA Error, Read only*/ \
    res2:7,                                                         \
    priority:2,             /* AXI Priority Level, Zero is the lowest priority.*/ \
    panic_priority:4,       /* AXI Panic Priority Level, Zero is the lowest priority.*/ \
    res1:4,                                                     \
    wait_outstanding_w:1,   /* Wait for outstanding writes */ \
    dis_debug:1,            /* Disable debug pause signal*/ \
    abort:1,                /* Writing a 1 to this bit will abort the current DMA CB, DMA will load the next CB and attempt to continue*/ \
    reset:1                 /* MSB. Writing a 1 to this bit will reset the DMA. The bit cannot be read, and will self clear*/   
REG_DEF(DMA_CS_REG_0_10, DMA_CS_FIELDS_0_10);

#define DMA_CS_FIELDS_11_14   \
    activate:1,             /* LSB. Activate the DMA4 This bit enables the DMA. The DMA will start if this bit is set */ \
                            /* and the CB_ADDR is non zero. The DMA transfer can be paused and resumed by clearing, then setting it again. */ \
    end:1,                  /* DMA End flag- clear by writing '1' */ \
    ints:1,                 /* Interrupt Status - if the INTEN is enabled, then clearing by writing '1'.*/ \
    dreq:1,                 /* DREQ State, READ ONLY*/ \
    read_paused:1,           /*DMA read Paused State*/ \
    write_paused:1,         /* DMA Write Paused State*/ \
    dreq_stops_dma:1,       /* DMA Paused by DREQ State, READ ONLY*/ \
    waiting_outstanding_w:1,/* DMA is Waiting for the Last Write to be Received, READ ONLY*/ \
    res3:2,                                                         \
    error:1,                /* DMA Error, Read only*/ \
    res2:5,                                                         \
    priority:2,             /* AXI Priority Level, Zero is the lowest priority.*/ \
    panic_priority:4,       /* AXI Panic Priority Level, Zero is the lowest priority.*/ \
    dma_busy:1,             /* Indicates the DMA4 is BUSY.*/ \
    outstanding_resanc:1,   /* Indicates that there are outstanding AXI transfers*/ \
    res1:2,                                                     \
    wait_outstanding_w:1,   /* Wait for outstanding writes */ \
    dis_debug:1,            /* Disable debug pause signal*/ \
    abort:1,                /* Writing a 1 to this bit will abort the current DMA CB, DMA will load the next CB and attempt to continue*/ \
    halt:1                  /* MSB. Writing a 1 to this bit will cleanly halt the current DMA transfer.*/   
REG_DEF(DMA_CS_REG_11_14, DMA_CS_FIELDS_11_14);


#define DMA_TI_FIELDS_0_6   \
    inten:1,                /* */ \
    two_dim_mode:1,         /* */ \
    res1:1,                 /* */ \
    wait_resp:1,            /* */ \
    dest_increment:1,       /* */ \
    dest_width_128bit:1,    /* */ \
    dest_dreq:1,            /* */ \
    dest_ignore:1,          /* */ \
    src_increment:1,        /* */ \
    src_width:1,            /* */ \
    src_dreq:1,             /* */ \
    src_ignore:1,           /* */ \
    burst_length:4,         /* */ \
    periph_map:5,           /* */ \
    waits_to_add:5,         /* */ \
    no_wide_bursts:1,       /* */ \
    res2:5                  /* */
REG_DEF(DMA_TI_REG_0_6, DMA_TI_FIELDS_0_6);

#define DMA_TI_FIELDS_7_10   \
    inten:1,                /* */ \
    res1:2,                 /* */ \
    wait_resp:1,            /* */ \
    dest_increment:1,       /* */ \
    dest_width_128bit:1,    /* */ \
    dest_dreq:1,            /* */ \
    res2:1,                 /* */ \
    src_increment:1,        /* */ \
    src_width:1,            /* */ \
    src_dreq:1,             /* */ \
    res3:1,                 /* */ \
    burst_length:4,         /* */ \
    periph_map:5,           /* */ \
    waits_to_add:5,         /* */ \
    res4:6                  /* */
REG_DEF(DMA_TI_REG_7_10, DMA_TI_FIELDS_7_10);

#define DMA_TI_FIELDS_11_14   \
    inten:1,                /* */ \
    two_dim_mode:1,         /* */ \
    wait_to_response:1,     /* */ \
    wait_read_resp:1,       /* */ \
    res1:5,                 /* */ \
    periph_map:5,           /* */ \
    src_read_dreq:1,        /* */ \
    dest_writes_dreq:1,     /* */ \
    src_read_waits:8,       /* */ \
    dest_write_waits:8      /* */
REG_DEF(DMA_TI_REG_11_14, DMA_TI_FIELDS_11_14);

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


// DMA control block macros
#define MAX_NUM_CBS     (PAGE_SIZE / sizeof(DMA_CB))
#define GPIO(r)         BUS_GPIO_REG(r)
#define PWM(r)          BUS_PWM_REG(r)
#define MEM(m)          BUS_DMA_MEM(m)
#define CBS(n)          BUS_DMA_MEM(&dp->cbs[(n)])
//#define PWM_TI          ((1 << 6) | (DMA_PWM_DREQ << 16))
#define SMI_TI          ((1 << 6) | (DMA_SMI_DREQ << 16))


typedef struct
{
    MEM_MAP dma_regs;

    io_utils_sys_info_st sys_info;
} dma_utils_st;


void dma_utils_init (dma_utils_st *dev);
void dma_utils_close (dma_utils_st *dev);
void dma_utils_enable_dma(dma_utils_st *dev, int chan);
void dma_utils_start_dma(dma_utils_st *dev, MEM_MAP *mp, int chan, DMA_CB *cbp, uint32_t csval);
uint32_t dma_utils_dma_get_transfer_len(dma_utils_st *dev, int chan);
uint32_t dma_utils_dma_is_active(dma_utils_st *dev, int chan);
void dma_utils_stop_dma(dma_utils_st *dev, int chan);
void dma_utils_disp_dma(dma_utils_st *dev, int chan);

#endif // __DMA_UTILS_H__