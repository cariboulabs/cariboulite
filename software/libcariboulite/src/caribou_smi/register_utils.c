#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "register_utils.h"
#include "mbox_utils.h"
#include "../io_utils/io_utils_sys_info.h"

//=====================================================================
// Get virtual memory segment for peripheral regs or physical mem
void* map_segment(void *addr, int size)
{
    int fd;
    void *mem;

    size = PAGE_ROUNDUP(size);
    if ((fd = open ("/dev/mem", O_RDWR|O_SYNC|O_CLOEXEC)) < 0)
    {
        printf("Error: can't open /dev/mem, run using sudo\n");
    }
    
    mem = mmap(0, size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, (uint32_t)addr);
    close(fd);

    #if DEBUG
        printf("Map %p -> %p\n", (void *)addr, mem);
    #endif

    if (mem == MAP_FAILED)
    {
        printf("Error: can't map memory\n");
    }
    return(mem);
}


//=====================================================================
// Free mapped memory
void unmap_segment(void *mem, int size)
{
    if (mem) 
    {
        munmap(mem, PAGE_ROUNDUP(size));
    }
}

//=====================================================================
// Use mmap to obtain virtual address, given physical
void* map_periph(MEM_MAP *mp, uint32_t base_addr, int size)
{
    io_utils_sys_info_st info = {0};
    if (io_utils_get_rpi_info(&info) != 0) return NULL;

    mp->phys = (void *)(base_addr + info.phys_reg_base);
    mp->size = PAGE_ROUNDUP(size);
    mp->bus = (void *)(base_addr + info.bus_reg_base);
    mp->virt = map_segment(mp->phys, mp->size);
    return(mp->virt);
}

//=====================================================================
// Free mapped peripheral or memory
void unmap_periph_mem(MEM_MAP *mp)
{
    if (mp)
    {
        unmap_segment(mp->virt, mp->size);

        if (mp->fd)
        {
            unlock_vc_mem(mp->fd, mp->h);
            free_vc_mem(mp->fd, mp->h);
            close_mbox(mp->fd);
        }
    }
}

//=====================================================================
// Allocate uncached memory, get bus & phys addresses
void* map_uncached_mem(MEM_MAP *mp, int size)
{
    void *ret;
    mp->size = PAGE_ROUNDUP(size);
    mp->fd = open_mbox();
    ret = (mp->h = alloc_vc_mem(mp->fd, mp->size, DMA_MEM_FLAGS)) > 0 &&
        (mp->bus = lock_vc_mem(mp->fd, mp->h)) != 0 &&
        (mp->virt = map_segment(BUS_PHYS_ADDR(mp->bus), mp->size)) != 0
        ? mp->virt : 0;
    printf("VC mem handle %u, phys %p, virt %p\n", mp->h, mp->bus, mp->virt);
    return ret;
}