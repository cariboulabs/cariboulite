#ifndef __MBOX_UTILS_H__
#define __MBOX_UTILS_H__

#include <stdio.h>
#include <string.h>
#include <stdint.h>

// Videocore mailbox memory allocation flags, see:
//     https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
typedef enum 
{
    MEM_FLAG_DISCARDABLE    = 1<<0, // can be resized to 0 at any time. Use for cached data
    MEM_FLAG_NORMAL         = 0<<2, // normal allocating alias. Don't use from ARM
    MEM_FLAG_DIRECT         = 1<<2, // 0xC alias uncached
    MEM_FLAG_COHERENT       = 2<<2, // 0x8 alias. Non-allocating in L2 but coherent
    MEM_FLAG_ZERO           = 1<<4, // initialise buffer to all zeros
    MEM_FLAG_NO_INIT        = 1<<5, // don't initialise (default is initialise to all ones)
    MEM_FLAG_HINT_PERMALOCK = 1<<6, // Likely to be locked for long periods of time
    MEM_FLAG_L1_NONALLOCATING=(MEM_FLAG_DIRECT | MEM_FLAG_COHERENT) // Allocating in L2
} VC_ALLOC_FLAGS;

#define PAGE_ROUNDUP(n) ((n)%PAGE_SIZE==0 ? (n) : ((n)+PAGE_SIZE)&~(PAGE_SIZE-1))
#define PAGE_SIZE       0x1000

// Mailbox command/response structure
typedef struct 
{
    uint32_t len,               // Overall length (bytes)
        req,                    // Zero for request, 1<<31 for response
        tag,                    // Command number
        blen,                   // Buffer length (bytes)
        dlen;                   // Data length (bytes)
        uint32_t uints[32-5];   // Data (108 bytes maximum)
} VC_MSG __attribute__ ((aligned (16)));


int open_mbox(void);
void close_mbox(int fd);
uint32_t msg_mbox(int fd, VC_MSG *msgp);
uint32_t free_vc_mem(int fd, int h);
uint32_t alloc_vc_mem(int fd, uint32_t size, VC_ALLOC_FLAGS flags);
void* lock_vc_mem(int fd, int h);
uint32_t unlock_vc_mem(int fd, int h);
void disp_vc_msg(VC_MSG *msgp);


#endif // __MBOX_UTILS_H__