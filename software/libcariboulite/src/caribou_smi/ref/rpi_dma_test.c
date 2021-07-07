// DMA tests for the Raspberry Pi, see https://iosoft.blog for details
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
// v0.10 JPB 25/5/20
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

// Location of peripheral registers in physical memory
//#define PHYS_REG_BASE  0x20000000  // Pi Zero or 1
#define PHYS_REG_BASE    0x3F000000  // Pi 2 or 3
//#define PHYS_REG_BASE  0xFE000000  // Pi 4

// Location of peripheral registers in bus memory
#define BUS_REG_BASE    0x7E000000

// If non-zero, print debug information
#define DEBUG           0

// Output pin to use for LED
//#define LED_PIN         47    // Pi Zero onboard LED
#define LED_PIN         21      // Offboard LED pin

// PWM clock frequency and range (FREQ/RANGE = LED flash freq)
#define PWM_FREQ        100000
#define PWM_RANGE       20000

// If non-zero, set PWM clock using VideoCore mailbox
#define USE_VC_CLOCK_SET 0

// Size of memory page
#define PAGE_SIZE       0x1000
// Round up to nearest page
#define PAGE_ROUNDUP(n) ((n)%PAGE_SIZE==0 ? (n) : ((n)+PAGE_SIZE)&~(PAGE_SIZE-1))

// Size of uncached memory for DMA control blocks and data
#define DMA_MEM_SIZE    PAGE_SIZE

// GPIO definitions
#define GPIO_BASE       (PHYS_REG_BASE + 0x200000)
#define GPIO_MODE0      0x00
#define GPIO_SET0       0x1c
#define GPIO_CLR0       0x28
#define GPIO_LEV0       0x34
#define VIRT_GPIO_REG(a) ((uint32_t *)((uint32_t)virt_gpio_regs + (a)))
#define BUS_GPIO_REG(a) (GPIO_BASE-PHYS_REG_BASE+BUS_REG_BASE+(uint32_t)(a))
#define GPIO_IN         0
#define GPIO_OUT        1
#define GPIO_ALT0       4
#define GPIO_ALT2       6
#define GPIO_ALT3       7
#define GPIO_ALT4       3
#define GPIO_ALT5       2

// Virtual memory pointers to acceess GPIO, DMA and PWM from user space
void *virt_gpio_regs, *virt_dma_regs, *virt_pwm_regs;
// VC mailbox file descriptor & handle, and bus memory pointer
int mbox_fd, dma_mem_h;
void *bus_dma_mem;

// Convert memory bus address to physical address (for mmap)
#define BUS_PHYS_ADDR(a) ((void *)((uint32_t)(a)&~0xC0000000))

// Videocore mailbox memory allocation flags, see:
//     https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
typedef enum {
    MEM_FLAG_DISCARDABLE    = 1<<0, // can be resized to 0 at any time. Use for cached data
    MEM_FLAG_NORMAL         = 0<<2, // normal allocating alias. Don't use from ARM
    MEM_FLAG_DIRECT         = 1<<2, // 0xC alias uncached
    MEM_FLAG_COHERENT       = 2<<2, // 0x8 alias. Non-allocating in L2 but coherent
    MEM_FLAG_ZERO           = 1<<4, // initialise buffer to all zeros
    MEM_FLAG_NO_INIT        = 1<<5, // don't initialise (default is initialise to all ones)
    MEM_FLAG_HINT_PERMALOCK = 1<<6, // Likely to be locked for long periods of time
    MEM_FLAG_L1_NONALLOCATING=(MEM_FLAG_DIRECT | MEM_FLAG_COHERENT) // Allocating in L2
} VC_ALLOC_FLAGS;
// VC flags for unchached DMA memory
#define DMA_MEM_FLAGS (MEM_FLAG_DIRECT|MEM_FLAG_ZERO)

// Mailbox command/response structure
typedef struct {
    uint32_t len,   // Overall length (bytes)
        req,        // Zero for request, 1<<31 for response
        tag,        // Command number
        blen,       // Buffer length (bytes)
        dlen;       // Data length (bytes)
        uint32_t uints[32-5];   // Data (108 bytes maximum)
} VC_MSG __attribute__ ((aligned (16)));

// DMA register definitions
#define DMA_CHAN        5
#define DMA_PWM_DREQ    5
#define DMA_BASE        (PHYS_REG_BASE + 0x007000)
#define DMA_CS          (DMA_CHAN*0x100)
#define DMA_CONBLK_AD   (DMA_CHAN*0x100 + 0x04)
#define DMA_TI          (DMA_CHAN*0x100 + 0x08)
#define DMA_SRCE_AD     (DMA_CHAN*0x100 + 0x0c)
#define DMA_DEST_AD     (DMA_CHAN*0x100 + 0x10)
#define DMA_TXFR_LEN    (DMA_CHAN*0x100 + 0x14)
#define DMA_STRIDE      (DMA_CHAN*0x100 + 0x18)
#define DMA_NEXTCONBK   (DMA_CHAN*0x100 + 0x1c)
#define DMA_DEBUG       (DMA_CHAN*0x100 + 0x20)
#define DMA_ENABLE      0xff0
#define VIRT_DMA_REG(a) ((volatile uint32_t *)((uint32_t)virt_dma_regs + a))
char *dma_regstrs[] = {"DMA CS", "CB_AD", "TI", "SRCE_AD", "DEST_AD",
    "TFR_LEN", "STRIDE", "NEXT_CB", "DEBUG", ""};

// DMA control block (must be 32-byte aligned)
typedef struct {
    uint32_t ti,    // Transfer info
        srce_ad,    // Source address
        dest_ad,    // Destination address
        tfr_len,    // Transfer length
        stride,     // Transfer stride
        next_cb,    // Next control block
        debug,      // Debug register, zero in control block
        unused;
} DMA_CB __attribute__ ((aligned(32)));
#define DMA_CB_DEST_INC (1<<4)
#define DMA_CB_SRC_INC  (1<<8)

// Virtual memory for DMA descriptors and data buffers (uncached)
void *virt_dma_mem;

// Convert virtual DMA data address to a bus address
#define BUS_DMA_MEM(a)  ((uint32_t)a-(uint32_t)virt_dma_mem+(uint32_t)bus_dma_mem)

// PWM controller
#define PWM_BASE        (PHYS_REG_BASE + 0x20C000)
#define PWM_CTL         0x00   // Control
#define PWM_STA         0x04   // Status
#define PWM_DMAC        0x08   // DMA control
#define PWM_RNG1        0x10   // Channel 1 range
#define PWM_DAT1        0x14   // Channel 1 data
#define PWM_FIF1        0x18   // Channel 1 fifo
#define PWM_RNG2        0x20   // Channel 2 range
#define PWM_DAT2        0x24   // Channel 2 data
#define VIRT_PWM_REG(a) ((volatile uint32_t *)((uint32_t)virt_pwm_regs + (a)))
#define BUS_PWM_REG(a)  (PWM_BASE-PHYS_REG_BASE+BUS_REG_BASE+(uint32_t)(a))
#define PWM_CTL_RPTL1   (1<<2)  // Chan 1: repeat last data when FIFO empty
#define PWM_CTL_USEF1   (1<<5)  // Chan 1: use FIFO
#define PWM_DMAC_ENAB   (1<<31) // Start PWM DMA
#define PWM_ENAB        1       // Enable PWM
#define PWM_PIN         18      // GPIO pin for PWM output
#define PWM_OUT         1       // Set non-zero to enable PWM output pin

// Clock
void *virt_clk_regs;
#define CLK_BASE        (PHYS_REG_BASE + 0x101000)
#define CLK_PWM_CTL     0xa0
#define CLK_PWM_DIV     0xa4
#define VIRT_CLK_REG(a) ((volatile uint32_t *)((uint32_t)virt_clk_regs + (a)))
#define CLK_PASSWD      0x5a000000
#define CLOCK_KHZ       250000
#define PWM_CLOCK_ID    0xa

#define FAIL(x) {printf(x); terminate(0);}

int dma_test_mem_transfer(void);
void dma_test_led_flash(int pin);
void dma_test_pwm_trigger(int pin);
void terminate(int sig);
void gpio_mode(int pin, int mode);
void gpio_out(int pin, int val);
uint8_t gpio_in(int pin);
int open_mbox(void);
void close_mbox(int fd);
uint32_t msg_mbox(int fd, VC_MSG *msgp);
void *map_segment(void *addr, int size);
void unmap_segment(void *addr, int size);
uint32_t alloc_vc_mem(int fd, uint32_t size, VC_ALLOC_FLAGS flags);
void *lock_vc_mem(int fd, int h);
uint32_t unlock_vc_mem(int fd, int h);
uint32_t free_vc_mem(int fd, int h);
uint32_t set_vc_clock(int fd, int id, uint32_t freq);
void disp_vc_msg(VC_MSG *msgp);
void enable_dma(void);
void start_dma(DMA_CB *cbp);
void stop_dma(void);
void disp_dma(void);
void init_pwm(int freq);
void start_pwm(void);
void stop_pwm(void);

// Main program
int main(int argc, char *argv[])
{
    // Ensure cleanup if user hits ctrl-C
    signal(SIGINT, terminate);

    // Map GPIO, DMA and PWM registers into virtual mem (user space)
    virt_gpio_regs = map_segment((void *)GPIO_BASE, PAGE_SIZE);
    virt_dma_regs = map_segment((void *)DMA_BASE, PAGE_SIZE);
    virt_pwm_regs = map_segment((void *)PWM_BASE, PAGE_SIZE);
    virt_clk_regs = map_segment((void *)CLK_BASE, PAGE_SIZE);
    enable_dma();

    // Set LED pin as output, and set high
    gpio_mode(LED_PIN, GPIO_OUT);
    gpio_out(LED_PIN, 1);

    // Use mailbox to get uncached memory for DMA decriptors and buffers
    mbox_fd = open_mbox();
    if ((dma_mem_h = alloc_vc_mem(mbox_fd, DMA_MEM_SIZE, DMA_MEM_FLAGS)) <= 0 ||
        (bus_dma_mem = lock_vc_mem(mbox_fd, dma_mem_h)) == 0 ||
        (virt_dma_mem = map_segment(BUS_PHYS_ADDR(bus_dma_mem), DMA_MEM_SIZE)) == 0)
            FAIL("Error: can't allocate uncached memory\n");
    printf("VC mem handle %u, phys %p, virt %p\n", dma_mem_h, bus_dma_mem, virt_dma_mem);

    // Run DMA tests
    dma_test_mem_transfer();
    dma_test_led_flash(LED_PIN);
    dma_test_pwm_trigger(LED_PIN);
    terminate(0);
}

// DMA memory-to-memory test
int dma_test_mem_transfer(void)
{
    DMA_CB *cbp = virt_dma_mem;
    char *srce = (char *)(cbp+1);
    char *dest = srce + 0x100;

    strcpy(srce, "memory transfer OK");
    memset(cbp, 0, sizeof(DMA_CB));
    cbp->ti = DMA_CB_SRC_INC | DMA_CB_DEST_INC;
    cbp->srce_ad = BUS_DMA_MEM(srce);
    cbp->dest_ad = BUS_DMA_MEM(dest);
    cbp->tfr_len = strlen(srce) + 1;
    start_dma(cbp);
    usleep(10);
#if DEBUG
    disp_dma();
#endif
    printf("DMA test: %s\n", dest[0] ? dest : "failed");
    return(dest[0] != 0);
}

// DMA memory-to-GPIO test: flash LED
void dma_test_led_flash(int pin)
{
    DMA_CB *cbp=virt_dma_mem;
    uint32_t *data = (uint32_t *)(cbp+1), n;

    printf("DMA test: flashing LED on GPIO pin %u\n", pin);
    memset(cbp, 0, sizeof(DMA_CB));
    *data = 1 << pin;
    cbp->tfr_len = 4;
    cbp->srce_ad = BUS_DMA_MEM(data);
    for (n=0; n<16; n++)
    {
        usleep(200000);
        cbp->dest_ad = BUS_GPIO_REG(n&1 ? GPIO_CLR0 : GPIO_SET0);
        start_dma(cbp);
    }
}

// DMA trigger test: fLash LED using PWM trigger
void dma_test_pwm_trigger(int pin)
{
    DMA_CB *cbs=virt_dma_mem;
    uint32_t n, *pindata=(uint32_t *)(cbs+4), *pwmdata=pindata+1;

    printf("DMA test: PWM trigger, ctrl-C to exit\n");
    memset(cbs, 0, sizeof(DMA_CB)*4);
    // Transfers are triggered by PWM request
    cbs[0].ti = cbs[1].ti = cbs[2].ti = cbs[3].ti = (1 << 6) | (DMA_PWM_DREQ << 16);
    // Control block 0 and 2: clear & set LED pin, 4-byte transfer
    cbs[0].srce_ad = cbs[2].srce_ad = BUS_DMA_MEM(pindata);
    cbs[0].dest_ad = BUS_GPIO_REG(GPIO_CLR0);
    cbs[2].dest_ad = BUS_GPIO_REG(GPIO_SET0);
    cbs[0].tfr_len = cbs[2].tfr_len = 4;
    *pindata = 1 << pin;
    // Control block 1 and 3: update PWM FIFO (to clear DMA request)
    cbs[1].srce_ad = cbs[3].srce_ad = BUS_DMA_MEM(pwmdata);
    cbs[1].dest_ad = cbs[3].dest_ad = BUS_PWM_REG(PWM_FIF1);
    cbs[1].tfr_len = cbs[3].tfr_len = 4;
    *pwmdata = PWM_RANGE / 2;
    // Link control blocks 0 to 3 in endless loop
    for (n=0; n<4; n++)
        cbs[n].next_cb = BUS_DMA_MEM(&cbs[(n+1)%4]);
    // Enable PWM with data threshold 1, and DMA
    init_pwm(PWM_FREQ);
    *VIRT_PWM_REG(PWM_DMAC) = PWM_DMAC_ENAB|1;
    start_pwm();
    start_dma(&cbs[0]);
    // Nothing to do while LED is flashing
    sleep(4);
}

// Free memory segments and exit
void terminate(int sig)
{
    printf("Closing\n");
    stop_pwm();
    stop_dma();
    unmap_segment(virt_dma_mem, DMA_MEM_SIZE);
    unlock_vc_mem(mbox_fd, dma_mem_h);
    free_vc_mem(mbox_fd, dma_mem_h);
    close_mbox(mbox_fd);
    unmap_segment(virt_clk_regs, PAGE_SIZE);
    unmap_segment(virt_pwm_regs, PAGE_SIZE);
    unmap_segment(virt_dma_regs, PAGE_SIZE);
    unmap_segment(virt_gpio_regs, PAGE_SIZE);
    exit(0);
}

// ----- GPIO -----

// Set input or output
void gpio_mode(int pin, int mode)
{
    uint32_t *reg = VIRT_GPIO_REG(GPIO_MODE0) + pin / 10, shift = (pin % 10) * 3;
    *reg = (*reg & ~(7 << shift)) | (mode << shift);
}

// Set an O/P pin
void gpio_out(int pin, int val)
{
    uint32_t *reg = VIRT_GPIO_REG(val ? GPIO_SET0 : GPIO_CLR0) + pin/32;
    *reg = 1 << (pin % 32);
}

// Get an I/P pin value
uint8_t gpio_in(int pin)
{
    uint32_t *reg = VIRT_GPIO_REG(GPIO_LEV0) + pin/32;
    return (((*reg) >> (pin % 32)) & 1);
}

// ----- VIDEOCORE MAILBOX -----

// Open mailbox interface, return file descriptor
int open_mbox(void)
{
   int fd;

   if ((fd = open("/dev/vcio", 0)) < 0)
       FAIL("Error: can't open VC mailbox\n");
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
        FAIL("Error: can't open /dev/mem, run using sudo\n");
    mem = mmap(0, size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, (uint32_t)addr);
    close(fd);
#if DEBUG
    printf("Map %p -> %p\n", (void *)addr, mem);
#endif
    if (mem == MAP_FAILED)
        FAIL("Error: can't map memory\n");
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
void enable_dma(void)
{
    *VIRT_DMA_REG(DMA_ENABLE) |= (1 << DMA_CHAN);
    *VIRT_DMA_REG(DMA_CS) = 1 << 31;
}

// Start DMA, given first control block
void start_dma(DMA_CB *cbp)
{
    *VIRT_DMA_REG(DMA_CONBLK_AD) = BUS_DMA_MEM(cbp);
    *VIRT_DMA_REG(DMA_CS) = 2;       // Clear 'end' flag
    *VIRT_DMA_REG(DMA_DEBUG) = 7;    // Clear error bits
    *VIRT_DMA_REG(DMA_CS) = 1;       // Start DMA
}

// Halt current DMA operation by resetting controller
void stop_dma(void)
{
    if (virt_dma_regs)
        *VIRT_DMA_REG(DMA_CS) = 1 << 31;
}

// Display DMA registers
void disp_dma(void)
{
    uint32_t *p=(uint32_t *)VIRT_DMA_REG(DMA_CS);
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
void init_pwm(int freq)
{
    stop_pwm();
    if (*VIRT_PWM_REG(PWM_STA) & 0x100)
    {
        printf("PWM bus error\n");
        *VIRT_PWM_REG(PWM_STA) = 0x100;
    }
#if USE_VC_CLOCK_SET
    set_vc_clock(mbox_fd, PWM_CLOCK_ID, freq);
#else
    int divi=(CLOCK_KHZ*1000) / freq;
    *VIRT_CLK_REG(CLK_PWM_CTL) = CLK_PASSWD | (1 << 5);
    while (*VIRT_CLK_REG(CLK_PWM_CTL) & (1 << 7)) ;
    *VIRT_CLK_REG(CLK_PWM_DIV) = CLK_PASSWD | (divi << 12);
    *VIRT_CLK_REG(CLK_PWM_CTL) = CLK_PASSWD | 6 | (1 << 4);
    while ((*VIRT_CLK_REG(CLK_PWM_CTL) & (1 << 7)) == 0) ;
#endif
    usleep(100);
    *VIRT_PWM_REG(PWM_RNG1) = PWM_RANGE;
    *VIRT_PWM_REG(PWM_FIF1) = PWM_RANGE/2;
#if PWM_OUT
    gpio_mode(PWM_PIN, GPIO_ALT5);
#endif
}

// Start PWM operation
void start_pwm(void)
{
    *VIRT_PWM_REG(PWM_CTL) = PWM_CTL_USEF1 | PWM_ENAB;
}

// Stop PWM operation
void stop_pwm(void)
{
    if (virt_pwm_regs)
    {
        *VIRT_PWM_REG(PWM_CTL) = 0;
        usleep(100);
    }
}

// EOF
