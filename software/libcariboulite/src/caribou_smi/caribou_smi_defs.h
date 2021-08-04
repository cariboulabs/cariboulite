#ifndef __CARIBOU_SMI_DEFS_H__
#define __CARIBOU_SMI_DEFS_H__

// This SMI definitions file was originally created by [Jeremy P Bentham]:
//
// ''' Test of parallel AD9226 ADC using Raspberry Pi SMI (Secondary Memory Interface)
// ''' For detailed description, see https://iosoft.blog
// ''' Copyright (c) 2020 Jeremy P Bentham
//
// It was modified to contain all possible register mappings and to adapt for easier usage
// DMA and Clock definitions have been unified in the file and it is going to undergo a 
// adaptation for the CaribouLite project.

#include <stdint.h>
#include "register_utils.h"

//==========================================================
// SMI
//==========================================================

// Register definitions
#define SMI_BASE    (0x600000)  /* + PHYS_REG_BASE */
#define SMI_CS      0x00    // Secondary Memory Interface Control / Status
#define SMI_L       0x04    // Secondary Memory Interface Length
#define SMI_A       0x08    // Secondary Memory Interface Address
#define SMI_D       0x0c    // Secondary Memory Interface Data
#define SMI_DSR0    0x10    // SMI Device Read Settings 0
#define SMI_DSW0    0x14    // SMI Device Write Settings 0
#define SMI_DSR1    0x18    // SMI Device Read Settings 1
#define SMI_DSW1    0x1c    // SMI Device Write Settings 1
#define SMI_DSR2    0x20    // SMI Device Read Settings 2
#define SMI_DSW2    0x24    // SMI Device Write Settings 2
#define SMI_DSR3    0x28    // SMI Device Read Settings 3
#define SMI_DSW3    0x2c    // SMI Device Write Settings 3
#define SMI_DMC     0x30    // SMI DMA Control
#define SMI_DCS     0x34    // SMI Direct Control / Status
#define SMI_DCA     0x38    // SMI Direct Address
#define SMI_DCD     0x3c    // SMI Direct Data
#define SMI_FD      0x40    // SMI FIFO Debug
#define SMI_REGLEN  (SMI_FD * 4)

//=====================================================================================
// Control and status register
#define SMI_CS_FIELDS   \
    enable:1,          /* Enable the SMI - This bit is OR‘d with the ENABLE bit in the SMI_DCS register. If either bit is set then the SMI is enabled.*/ \
    done:1,            /* Indicates the current transfer is complete. 1 = The transfer has finished.*/ \
    active:1,          /* Indicates the current Transfer Status 0, 1 = Transfer is taking place.*/ \
    start:1,           /* Start Transfer 0 (writing 1 = start a transfer if one is not already taking place) */ \
    clear:1,           /* Clear the FIFOs 0 (1 = writing 1 causes the FIFOs to be reset to the empty state.) */ \
    write:1,           /* Sets the Transfer Direction on the SMI bus - 1 = Transfers will write to external devices.*/ \
    pad:2,             /* Padding Words – This indicates the number of FIFO words to be discarded */ \
                       /* at the start of a transfer. For write transfers this indicates the number of words */ \
                       /* that will be taken from the TX FIFO but will not be transmitted. For read transfers */ \
                       /* this indicates the number of words that will be received from the peripheral (after packing) */ \
                       /* but will not be written into the RX FIFO.*/ \
    teen:1,            /* Tear Effect Mode Enable 0 - 1 = TE enabled - programmed transfers will wait for a TE trigger before starting.*/ \
    intd:1,            /* Interrupt on DONE - 1 = Generate interrupt while DONE = 1.*/ \
    intt:1,            /* Interrupt on TX - 1 = Generate interrupt while TXW = 1.*/ \
    intr:1,            /* Interrupt on RX - 1 = Generate interrupt while RXR = 1.*/ \
    pvmode:1,          /* Pixel Valve Mode - 1 = Pixel Valve enabled. Transmit data is taken from the pixel valve interface rather than the AXI input.*/ \
    seterr:1,          /* A Setup Error has occurred. 1 = Setup registers were written to when enabled. must be cleared by writing 1 to this bit.*/ \
    pxldat:1,          /* Pixel Data – enables pixel formatting modes - 1 = Pixel modes enabled. Data in the FIFO will be packed to suit the pixel format selected.*/ \
    edreq:1,           /* External DREQ received – indicates the status of the external devices - 1 = External DREQ received.*/ \
    _x2:8,             /* Reserved */ \
    prdy:1,            /* Force the SMI to appear not ready on the AXI bus if the appropriate FIFO Is not ready.*/ \
                       /* 0 = SMI appears ready all the time and all AXI transfers to it will complete.*/ \
                       /* 1 = SMI will stall the AXI bus when reading or writing data to SMI_D unless there is room in the FIFO for */ \
                       /* writes or there is data available for a read. Setting this bit may cause the AXI bus to become locked or may */ \
                       /* seriously impact system performance.*/ \
    aferr:1,           /* An AXI FIFO Error has occurred - 1 = A FIFO error has occurred – either a read of the RFIFO when empty or a */ \
                       /* write of the WFIFO when full. This is a latching error bit and it must be cleared by writing 1 to this bit.*/ \
    txw:1,             /* TX FIFO needs Writing - 1 = TX FIFO is less than ¼ full and the transfer direction is set to WRITE.*/ \
    rxr:1,             /* RX FIFO needs Reading - 1 = RX FIFO is at least ¾ full or the transfer has finished and the FIFO still needs reading. */ \
                       /* The transfer direction must be set to READ.*/ \
    txd:1,             /* TX FIFO can accept Data - 1 = TX FIFO can accept new at least 1 word of data and the transfer direction is set to WRITE.*/ \
    rxd:1,             /* RX FIFO contains Data (1 = RX FIFO contains at least 1 word of data that can be read and the transfer direction is set to READ.) */ \
    txe:1,             /* TX FIFO is Empty (1 = TX FIFO is empty, no further external transfers can take place.) */ \
    rxf:1              /* RX FIFO is Full (1 = RX FIFO is full, no further external transfers can take place.) */
REG_DEF(SMI_CS_REG, SMI_CS_FIELDS);

//=====================================================================================
// Data length register
/*
    Write:  This sets the number of words to transfer over the external bus. The
            words can be either of 8,9,16 or 18 bits wide depending upon the output
            width that has been set in the SMI_DSx registers.
    Read:   Contains the value written to the register unless a transfer is active,
            in which case it contains the number of words transferred so far.
*/
#define SMI_L_FIELDS \
    len:32
REG_DEF(SMI_L_REG, SMI_L_FIELDS);

//=====================================================================================
// Address & device number
#define SMI_A_FIELDS \
    addr:6,         /* Address to be used for transfers and presented on the SMI Bus. */ \
    _x1:2,          /* Unused */ \
    dev:2           /* Indicates which set of device settings should be used for the transfer. (00 - dev setting 0, ... 11 - dev setting 3) */
REG_DEF(SMI_A_REG, SMI_A_FIELDS);

//=====================================================================================
// Data FIFO
/* Reading returns data that has been read from external devices. 
   Data written here is written out to external devices. */
#define SMI_D_FIELDS \
    data:32
REG_DEF(SMI_D_REG, SMI_D_FIELDS);

//=====================================================================================
// DMA control register
/*  Synopsis The SMI DMA Control register is used to specify the behaviour for the DMA DREQ and Panic signals
    on the AXI bus. The SMI can generate a TX and a RX DREQ to control an external AXI DMA. It
    can also generate a PANIC signal to indicate that it is running out of FIFO space. */
#define SMI_DMC_FIELDS \
    reqw:6,     /* TX DREQ Threshold Level. A TX DREQ will be generated when the TX FIFO drops below this threshold level. */ \
                /* This will instruct an external AXI TX DMA to write more data to the TX FIFO.*/ \
    reqr:6,     /* RX DREQ Threshold Level. A RX DREQ will be generated when the RX FIFO exceeds this threshold level. */ \
                /* This will instruct an external AXI RX DMA to read the RX FIFO. If the DMA is set to perform burst reads, */ \
                /* the threshold must ensure that there is sufficient data in the FIFO to satisfy the burst.*/ \
    panicw:6,   /* TX Panic threshold level. A TX Panic will be generated when the TX FIFO drops below this threshold */ \
                /* level. This will instruct the AXI TX DMA to increase the priority of its bus requests.*/ \
    panicr:6,   /* RX Panic Threshold level. A RX Panic will be generated when the RX FIFO exceeds this threshold */ \
                /* level. This will instruct the AXI RX DMA to increase the priority of its bus requests.*/ \
    dmap:1,     /* Enable external DREQ Mode. In this mode the top 2 bits of the SMI data are used as DREQ and DREQ_ACK signals */ \
                /*  and can be used to pace the flow of data on the external SMI bus. This is separate to the normal AXI */ \
                /* DMA behaviour. This must be used in conjunction with the RDREQ or WRREQ bits in the device settings registers.*/ \
    _x1:3,      /* Unused */ \
    dmaen:1     /* DMA Enable – enables the generation of DREQ and Panic signals to control the AXI bus DMA transfers. */ \
                /* 0 = No DREQ or Panic will be issued. */ \
                /* 1 = DREQ and Panic will be generated when the FIFO levels reach the programmed levels.*/
REG_DEF(SMI_DMC_REG, SMI_DMC_FIELDS);

//=====================================================================================
// Device settings: read (1 of 4)
#define SMI_DSR_FIELDS \
    rstrobe:7,  /* Duration that the read strobe should be active. Specified in SMI bus clock cycles, min 1 cycle, max 128 cycles.*/ \
    rdreq:1,    /* External Read DREQ - The top 2 SMI data bits can be used as DREQ (SD[16] - GPIO24) and DREQ_ACK (SD[17] - GPIO25). */ \
                /* These can be used used to pace the reads. This must be used in conjunction with the DMAP bit in SMI_CS.*/ \
                /* 0 = Don‘t use external DMA request. Reads always happen. */ \
                /* 1 = Use external DMA request to pace device reads. A read will only happen for each DREQ, DREQ_ACK cycle. */ \
    rpace:7,    /* Duration between chip select de-asserting and any other transfer being allowed on the bus. Specified in SMI */ \
                /* bus clock cycles, min 1 cycles, max 128 cycles.*/ \
    rpaceall:1, /* Selects if RPACE applies to all bus accesses - 1 = RPACE applies to the next access regardless of the device settings used.*/ \
                /*                                                0 = RPACE only applies to accesses through the same device settings. */ \
    rhold:6,    /* Duration between read strobe going inactive and chip select de-asserting. Specified in SMI bus clock cycles, min 1 cycle, max 64 cycles.*/ \
    fsetup:1,   /* 0 = always apply the setup time.*/ \
                /* 1 = a setup time is only applied to the first transfer after de-assertion of chip select. */ \
    mode68:1,   /* 0 = the external bus will operate in a System-80 compliant way. */ \
                /* 1 = the external bus will operate in a System-68 compliant way.*/ \
    rsetup:6,   /* Duration between chip select being asserted and read strobe going active. Specified in SMI bus clock cycles, min 1 cycle, max 64 cycles.*/ \
    rwidth:2    /* WIDTH – Read Transfer Width - 00 = 8bit, 01 = 16bit, 10 = 18bit, 11 = 9bit */
REG_DEF(SMI_DSR_REG, SMI_DSR_FIELDS);

//=====================================================================================
// Device settings: write (1 of 4)
#define SMI_DSW_FIELDS \
    wstrobe:7,  /* Duration that the write strobe should be active. Specified in SMI bus clock cycles, min 1 cycle, max 128 cycles.*/ \
    wdreq:1,    /* External Write DREQ - The top 2 SMI data bits can be used as DREQ (SD[16] GPIO24) and DREQ_ACK (SD[17] GPIO25). */ \
                /* These can be used used to pace the writes. This must be used in conjunction with the DMAP bit in SMI_CS. */ \
                /* 0 = Don‘t use external DMA request. Writes always happen. */ \
                /* 1 = Use external DMA request to pace device Writes. A write will only happen for each DREQ, DREQ_ACK cycle.*/ \
    wpace:7,    /* Duration between chip select de-asserting and any other transfer being allowed on the bus. */ \
                /*  Specified in SMI bus clock cycles, min 1 cycles, max 128 cycles.*/ \
    wpaceall:1, /* Selects if WPACE applies to all bus accesses :*/ \
                /* 0 = WPACE only applies to accesses through the same device settings. */ \
                /* 1 = WPACE applies to the next access regardless of the device settings used. */ \
    whold:6,    /* Duration between write strobe going inactive and chip select de-asserting. Specified in SMI bus clock cycles, min 1 cycle, max 64 cycles.*/ \
    wswap:1,    /* SWAP – Swap Modes Enabled - 1 = Pixel data bits swapped */ \
    wformat:1,  /* FORMAT – Input Pixel Format (0 = 16-bit RGB565, 1 = 32-bit RGBX8888)*/ \
    wsetup:6,   /* Duration between chip select being asserted and write strobe going active. Specified in SMI bus clock cycles, min 1 cycle, max 64 cycles.*/ \
    wwidth:2    /* WIDTH – Read Transfer Width - 00 = 8bit, 01 = 16bit, 10 = 18bit, 11 = 9bit */ 
REG_DEF(SMI_DSW_REG, SMI_DSW_FIELDS);

//=====================================================================================
// Direct control register
#define SMI_DCS_FIELDS \
    enable:1, start:1, done:1, write:1
REG_DEF(SMI_DCS_REG, SMI_DCS_FIELDS);

//=====================================================================================
// Direct control address & device number
#define SMI_DCA_FIELDS \
    addr:6, _x1:2, dev:2
REG_DEF(SMI_DCA_REG, SMI_DCA_FIELDS);

//=====================================================================================
// Direct control data
#define SMI_DCD_FIELDS \
    data:32
REG_DEF(SMI_DCD_REG, SMI_DCD_FIELDS);

//=====================================================================================
// Debug register
#define SMI_FLVL_FIELDS \
    fcnt:6, _x1:2, flvl:6
REG_DEF(SMI_FLVL_REG, SMI_FLVL_FIELDS);

#define CLK_SMI_CTL     0xb0
#define CLK_SMI_DIV     0xb4

#endif // __CARIBOU_SMI_DEFS_H__