
#ifndef __SMI_STREAM_IOCTL_H__
#define __SMI_STREAM_IOCTL_H__

#ifdef __KERNEL__
    #include <linux/types.h>   // u32, etc.
#else
    #include <stdint.h>        // uint32_t
#endif

// Unique magic number for our ioctl calls (arbitrary, must be unique)
#define SMI_STREAM_IOCTL_MAGIC 's'

// IOCTL command to read all key SMI registers
#define SMI_IOCTL_READ_REGS _IOR(SMI_STREAM_IOCTL_MAGIC, 0x01, struct smi_registers_t)

// Struct to carry SMI register values between kernel and user space
struct smi_registers_t {
    uint32_t smics;    // Control and Status
    uint32_t smil;     // Length
    uint32_t smids;    // DMA status
    uint32_t smisw0;   // Write register 0
};

#endif // __SMI_STREAM_IOCTL_H__