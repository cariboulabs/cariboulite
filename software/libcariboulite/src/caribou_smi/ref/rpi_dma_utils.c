// Raspberry Pi DMA utilities; see https://iosoft.blog for details
//
// Copyright (c) 2020 Jeremy P Bentham
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "rpi_dma_utils.h"

// If non-zero, print debug information
#define DEBUG           0
// If non-zero, enable PWM hardware output
#define PWM_OUT         0

char *dma_regstrs[] = {"DMA CS", "CB_AD", "TI", "SRCE_AD", "DEST_AD",
    "TFR_LEN", "STRIDE", "NEXT_CB", "DEBUG", ""};
char *gpio_mode_strs[] = {GPIO_MODE_STRS};

// Virtual memory pointers to acceess GPIO, DMA and PWM from user space
MEM_MAP pwm_regs, gpio_regs, dma_regs, clk_regs;

// Use mmap to obtain virtual address, given physical
void *map_periph(MEM_MAP *mp, void *phys, int size)
{
    mp->phys = phys;
    mp->size = PAGE_ROUNDUP(size);
    mp->bus = (void *)((uint32_t)phys - PHYS_REG_BASE + BUS_REG_BASE);
    mp->virt = map_segment(phys, mp->size);
    return(mp->virt);
}

// Allocate uncached memory, get bus & phys addresses
void *map_uncached_mem(MEM_MAP *mp, int size)
{
    void *ret;
    mp->size = PAGE_ROUNDUP(size);
    mp->fd = open_mbox();
    ret = (mp->h = alloc_vc_mem(mp->fd, mp->size, DMA_MEM_FLAGS)) > 0 &&
        (mp->bus = lock_vc_mem(mp->fd, mp->h)) != 0 &&
        (mp->virt = map_segment(BUS_PHYS_ADDR(mp->bus), mp->size)) != 0
        ? mp->virt : 0;
    printf("VC mem handle %u, phys %p, virt %p\n", mp->h, mp->bus, mp->virt);
    return(ret);
}

// Free mapped peripheral or memory
void unmap_periph_mem(MEM_MAP *mp)
{
    if (mp)
    {
        if (mp->fd)
        {
            unmap_segment(mp->virt, mp->size);
            unlock_vc_mem(mp->fd, mp->h);
            free_vc_mem(mp->fd, mp->h);
            close_mbox(mp->fd);
        }
        else
            unmap_segment(mp->virt, mp->size);
    }
}

// ----- GPIO -----

// Set input or output with pullups
void gpio_set(int pin, int mode, int pull)
{
    gpio_mode(pin, mode);
    gpio_pull(pin, pull);
}
// Set I/P pullup or pulldown
void gpio_pull(int pin, int pull)
{
    volatile uint32_t *reg = REG32(gpio_regs, GPIO_GPPUDCLK0) + pin / 32;

    *REG32(gpio_regs, GPIO_GPPUD) = pull;
    usleep(2);
    *reg = pin << (pin % 32);
    usleep(2);
    *REG32(gpio_regs, GPIO_GPPUD) = 0;
    *reg = 0;
}

// Set input or output
void gpio_mode(int pin, int mode)
{
    volatile uint32_t *reg = REG32(gpio_regs, GPIO_MODE0) + pin / 10, shift = (pin % 10) * 3;
    *reg = (*reg & ~(7 << shift)) | (mode << shift);
}

// Set an O/P pin
void gpio_out(int pin, int val)
{
    volatile uint32_t *reg = REG32(gpio_regs, val ? GPIO_SET0 : GPIO_CLR0) + pin/32;
    *reg = 1 << (pin % 32);
}

// Get an I/P pin value
uint8_t gpio_in(int pin)
{
    volatile uint32_t *reg = REG32(gpio_regs, GPIO_LEV0) + pin/32;
    return (((*reg) >> (pin % 32)) & 1);
}

// Display the values in a GPIO mode register
void disp_mode_vals(uint32_t mode)
{
    int i;

    for (i=0; i<10; i++)
        printf("%u:%-4s ", i, gpio_mode_strs[(mode>>(i*3)) & 7]);
    printf("\n");
}

// ----- VIDEOCORE MAILBOX -----

// Open mailbox interface, return file descriptor
int open_mbox(void)
{
   int fd;

   if ((fd = open("/dev/vcio", 0)) < 0)
       fail("Error: can't open VC mailbox\n");
   return(fd);
}
// Close mailbox interface
void close_mbox(int fd)
{
    if (fd >= 0)
        close(fd);
}

// Send message to mailbox, return first response int, 0 if error
uint32_t msg_mbox(int fd, VC_MSG *msgp)
{
    uint32_t ret=0, i;

    for (i=msgp->dlen/4; i<=msgp->blen/4; i+=4)
        msgp->uints[i++] = 0;
    msgp->len = (msgp->blen + 6) * 4;
    msgp->req = 0;
    if (ioctl(fd, _IOWR(100, 0, void *), msgp) < 0)
        printf("VC IOCTL failed\n");
    else if ((msgp->req&0x80000000) == 0)
        printf("VC IOCTL error\n");
    else if (msgp->req == 0x80000001)
        printf("VC IOCTL partial error\n");
    else
        ret = msgp->uints[0];
#if DEBUG
    disp_vc_msg(msgp);
#endif
    return(ret);
}

// Allocate memory on PAGE_SIZE boundary, return handle
uint32_t alloc_vc_mem(int fd, uint32_t size, VC_ALLOC_FLAGS flags)
{
    VC_MSG msg={.tag=0x3000c, .blen=12, .dlen=12,
        .uints={PAGE_ROUNDUP(size), PAGE_SIZE, flags}};
    return(msg_mbox(fd, &msg));
}
// Lock allocated memory, return bus address
void *lock_vc_mem(int fd, int h)
{
    VC_MSG msg={.tag=0x3000d, .blen=4, .dlen=4, .uints={h}};
    return(h ? (void *)msg_mbox(fd, &msg) : 0);
}
// Unlock allocated memory
uint32_t unlock_vc_mem(int fd, int h)
{
    VC_MSG msg={.tag=0x3000e, .blen=4, .dlen=4, .uints={h}};
    return(h ? msg_mbox(fd, &msg) : 0);
}
// Free memory
uint32_t free_vc_mem(int fd, int h)
{
    VC_MSG msg={.tag=0x3000f, .blen=4, .dlen=4, .uints={h}};
    return(h ? msg_mbox(fd, &msg) : 0);
}
uint32_t set_vc_clock(int fd, int id, uint32_t freq)
{
    VC_MSG msg1={.tag=0x38001, .blen=8, .dlen=8, .uints={id, 1}};
    VC_MSG msg2={.tag=0x38002, .blen=12, .dlen=12, .uints={id, freq, 0}};
    msg_mbox(fd, &msg1);
    disp_vc_msg(&msg1);
    msg_mbox(fd, &msg2);
    disp_vc_msg(&msg2);
    return(0);
}

// Display mailbox message
void disp_vc_msg(VC_MSG *msgp)
{
    int i;

    printf("VC msg len=%X, req=%X, tag=%X, blen=%x, dlen=%x, data ",
        msgp->len, msgp->req, msgp->tag, msgp->blen, msgp->dlen);
    for (i=0; i<msgp->blen/4; i++)
        printf("%08X ", msgp->uints[i]);
    printf("\n");
}

// ----- VIRTUAL MEMORY -----

// Get virtual memory segment for peripheral regs or physical mem
void *map_segment(void *addr, int size)
{
    int fd;
    void *mem;

    size = PAGE_ROUNDUP(size);
    if ((fd = open ("/dev/mem", O_RDWR|O_SYNC|O_CLOEXEC)) < 0)
        fail("Error: can't open /dev/mem, run using sudo\n");
    mem = mmap(0, size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, (uint32_t)addr);
    close(fd);
#if DEBUG
    printf("Map %p -> %p\n", (void *)addr, mem);
#endif
    if (mem == MAP_FAILED)
        fail("Error: can't map memory\n");
    return(mem);
}
// Free mapped memory
void unmap_segment(void *mem, int size)
{
    if (mem)
        munmap(mem, PAGE_ROUNDUP(size));
}

// ----- DMA -----

// Enable and reset DMA
void enable_dma(int chan)
{
    *REG32(dma_regs, DMA_ENABLE) |= (1 << chan);
    *REG32(dma_regs, DMA_REG(chan, DMA_CS)) = 1 << 31;
}

// Start DMA, given first control block
void start_dma(MEM_MAP *mp, int chan, DMA_CB *cbp, uint32_t csval)
{
    *REG32(dma_regs, DMA_REG(chan, DMA_CONBLK_AD)) = MEM_BUS_ADDR(mp, cbp);
    *REG32(dma_regs, DMA_REG(chan, DMA_CS)) = 2;        // Clear 'end' flag
    *REG32(dma_regs, DMA_REG(chan, DMA_DEBUG)) = 7;     // Clear error bits
    *REG32(dma_regs, DMA_REG(chan, DMA_CS)) = 1|csval;  // Start DMA
}

// Return remaining transfer length
uint32_t dma_transfer_len(int chan)
{
    return(*REG32(dma_regs, DMA_REG(chan, DMA_TXFR_LEN)));
}

// Check if DMA is active
uint32_t dma_active(int chan)
{
    return((*REG32(dma_regs, DMA_REG(chan, DMA_CS))) & 1);
}

// Halt current DMA operation by resetting controller
void stop_dma(int chan)
{
    if (dma_regs.virt)
        *REG32(dma_regs, DMA_REG(chan, DMA_CS)) = 1 << 31;
}

// Display DMA registers
void disp_dma(int chan)
{
    volatile uint32_t *p=REG32(dma_regs, DMA_REG(chan, DMA_CS));
    int i=0;

    while (dma_regstrs[i][0])
    {
        printf("%-7s %08X ", dma_regstrs[i++], *p++);
        if (i%5==0 || dma_regstrs[i][0]==0)
            printf("\n");
    }
}

// ----- PWM -----

// Initialise PWM
void init_pwm(int freq, int range, int val)
{
    stop_pwm();
    if (*REG32(pwm_regs, PWM_STA) & 0x100)
    {
        printf("PWM bus error\n");
        *REG32(pwm_regs, PWM_STA) = 0x100;
    }
#if USE_VC_CLOCK_SET
    set_vc_clock(mbox_fd, PWM_CLOCK_ID, freq);
#else
    int divi=CLOCK_HZ / freq;
    *REG32(clk_regs, CLK_PWM_CTL) = CLK_PASSWD | (1 << 5);
    while (*REG32(clk_regs, CLK_PWM_CTL) & (1 << 7)) ;
    *REG32(clk_regs, CLK_PWM_DIV) = CLK_PASSWD | (divi << 12);
    *REG32(clk_regs, CLK_PWM_CTL) = CLK_PASSWD | 6 | (1 << 4);
    while ((*REG32(clk_regs, CLK_PWM_CTL) & (1 << 7)) == 0) ;
#endif
    usleep(100);
    *REG32(pwm_regs, PWM_RNG1) = range;
    *REG32(pwm_regs, PWM_FIF1) = val;
#if PWM_OUT
    gpio_mode(PWM_PIN, PWM_PIN==12 ? GPIO_ALT0 : GPIO_ALT5);
#endif
}

// Start PWM operation
void start_pwm(void)
{
    *REG32(pwm_regs, PWM_CTL) = PWM_CTL_USEF1 | PWM_ENAB;
}

// Stop PWM operation
void stop_pwm(void)
{
    if (pwm_regs.virt)
    {
        *REG32(pwm_regs, PWM_CTL) = 0;
        usleep(100);
    }
}

// EOF
