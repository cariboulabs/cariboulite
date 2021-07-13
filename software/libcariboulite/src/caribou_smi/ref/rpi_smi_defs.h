// Secondary Memory Interface definitions for Raspberry Pi
//
// v0.01 JPB 12/7/20 Adapted from rpi_smi_test v0.19

// Register definitions
#define SMI_BASE    (PHYS_REG_BASE + 0x600000)
#define SMI_CS      0x00    // Control & status
#define SMI_L       0x04    // Transfer length
#define SMI_A       0x08    // Address
#define SMI_D       0x0c    // Data
#define SMI_DSR0    0x10    // Read settings device 0
#define SMI_DSW0    0x14    // Write settings device 0
#define SMI_DSR1    0x18    // Read settings device 1
#define SMI_DSW1    0x1c    // Write settings device 1
#define SMI_DSR2    0x20    // Read settings device 2
#define SMI_DSW2    0x24    // Write settings device 2
#define SMI_DSR3    0x28    // Read settings device 3
#define SMI_DSW3    0x2c    // Write settings device 3
#define SMI_DMC     0x30    // DMA control
#define SMI_DCS     0x34    // Direct control/status
#define SMI_DCA     0x38    // Direct address
#define SMI_DCD     0x3c    // Direct data
#define SMI_FD      0x40    // FIFO debug
#define SMI_REGLEN  (SMI_FD * 4)

// Data widths
#define SMI_8_BITS  0
#define SMI_16_BITS 1
#define SMI_18_BITS 2
#define SMI_9_BITS  3

// DMA request
#define DMA_SMI_DREQ 4

// Union of 32-bit value with register bitfields
#define REG_DEF(name, fields) typedef union {struct {volatile uint32_t fields;}; volatile uint32_t value;} name

// Control and status register
#define SMI_CS_FIELDS \
    enable:1, done:1, active:1, start:1, clear:1, write:1, _x1:2,\
    teen:1, intd:1, intt:1, intr:1, pvmode:1, seterr:1, pxldat:1, edreq:1,\
    _x2:8, _x3:1, aferr:1, txw:1, rxr:1, txd:1, rxd:1, txe:1, rxf:1
REG_DEF(SMI_CS_REG, SMI_CS_FIELDS);

// Data length register
#define SMI_L_FIELDS \
    len:32
REG_DEF(SMI_L_REG, SMI_L_FIELDS);

// Address & device number
#define SMI_A_FIELDS \
    addr:6, _x1:2, dev:2
REG_DEF(SMI_A_REG, SMI_A_FIELDS);

// Data FIFO
#define SMI_D_FIELDS \
    data:32
REG_DEF(SMI_D_REG, SMI_D_FIELDS);

// DMA control register
#define SMI_DMC_FIELDS \
    reqw:6, reqr:6, panicw:6, panicr:6, dmap:1, _x1:3, dmaen:1
REG_DEF(SMI_DMC_REG, SMI_DMC_FIELDS);

// Device settings: read (1 of 4)
#define SMI_DSR_FIELDS \
    rstrobe:7, rdreq:1, rpace:7, rpaceall:1, \
    rhold:6, fsetup:1, mode68:1, rsetup:6, rwidth:2
REG_DEF(SMI_DSR_REG, SMI_DSR_FIELDS);

// Device settings: write (1 of 4)
#define SMI_DSW_FIELDS \
    wstrobe:7, wdreq:1, wpace:7, wpaceall:1, \
    whold:6, wswap:1, wformat:1, wsetup:6, wwidth:2
REG_DEF(SMI_DSW_REG, SMI_DSW_FIELDS);

// Direct control register
#define SMI_DCS_FIELDS \
    enable:1, start:1, done:1, write:1
REG_DEF(SMI_DCS_REG, SMI_DCS_FIELDS);

// Direct control address & device number
#define SMI_DCA_FIELDS \
    addr:6, _x1:2, dev:2
REG_DEF(SMI_DCA_REG, SMI_DCA_FIELDS);

// Direct control data
#define SMI_DCD_FIELDS \
    data:32
REG_DEF(SMI_DCD_REG, SMI_DCD_FIELDS);

// Debug register
#define SMI_FLVL_FIELDS \
    fcnt:6, _x1:2, flvl:6
REG_DEF(SMI_FLVL_REG, SMI_FLVL_FIELDS);

#define CLK_SMI_CTL     0xb0
#define CLK_SMI_DIV     0xb4

// EOF
