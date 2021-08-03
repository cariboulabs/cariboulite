#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "mbox_utils.h"

//=====================================================================
// Open mailbox interface, return file descriptor
int open_mbox(void)
{
    int fd;

    if ((fd = open("/dev/vcio", 0)) < 0)
    {
        printf("Error: can't open VC mailbox\n");
    }
    return(fd);
}

//=====================================================================
// Close mailbox interface
void close_mbox(int fd)
{
    if (fd >= 0)
    {
        close(fd);
    }
}

//=====================================================================
// Send message to mailbox, return first response int, 0 if error
uint32_t msg_mbox(int fd, VC_MSG *msgp)
{
    uint32_t ret=0, i;

    // TODO: consider using memset instead?
    for (i=msgp->dlen/4; i<=msgp->blen/4; i+=4)
    {
        msgp->uints[i++] = 0;
    }

    msgp->len = (msgp->blen + 6) * 4;
    msgp->req = 0;
    if (ioctl(fd, _IOWR(100, 0, void *), msgp) < 0)
    {
        printf("VC IOCTL failed\n");
    }
    else if ((msgp->req&0x80000000) == 0)
    {
        printf("VC IOCTL error\n");
    }
    else if (msgp->req == 0x80000001)
    {
        printf("VC IOCTL partial error\n");
    }
    else
    {
        ret = msgp->uints[0];
    }
    
    #if DEBUG
        caribou_smi_disp_vc_msg(msgp);
    #endif

    return (ret);
}

//=====================================================================
// Free memory
uint32_t free_vc_mem(int fd, int h)
{
    VC_MSG msg={.tag=0x3000f, .blen=4, .dlen=4, .uints={h}};
    return (h ? msg_mbox(fd, &msg) : 0);
}


//=====================================================================
// Allocate memory on PAGE_SIZE boundary, return handle
uint32_t alloc_vc_mem(int fd, uint32_t size, VC_ALLOC_FLAGS flags)
{
    VC_MSG msg={.tag=0x3000c, .blen=12, .dlen=12, .uints={PAGE_ROUNDUP(size), PAGE_SIZE, flags}};
    return (msg_mbox(fd, &msg));
}

//=====================================================================
// Lock allocated memory, return bus address
void *lock_vc_mem(int fd, int h)
{
    VC_MSG msg={.tag=0x3000d, .blen=4, .dlen=4, .uints={h}};
    return (h ? (void *)msg_mbox(fd, &msg) : 0);
}

//=====================================================================
// Unlock allocated memory
uint32_t unlock_vc_mem(int fd, int h)
{
    VC_MSG msg={.tag=0x3000e, .blen=4, .dlen=4, .uints={h}};
    return (h ? msg_mbox(fd, &msg) : 0);
}


//=====================================================================
// Display mailbox message
void disp_vc_msg(VC_MSG *msgp)
{
    int i;

    printf("VC msg len=%X, req=%X, tag=%X, blen=%x, dlen=%x, data ",
        msgp->len, msgp->req, msgp->tag, msgp->blen, msgp->dlen);
    for (i=0; i<msgp->blen/4; i++)
    {
        printf("%08X ", msgp->uints[i]);
    }
    printf("\n");
}