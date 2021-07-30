#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "caribou_smi.h"

//===========================================================================
// Display bit values in register
static void caribou_smi_disp_reg_fields(char *regstrs, char *name, uint32_t val)
{
    char *p=regstrs, *q, *r=regstrs;
    uint32_t nbits, v;

    printf("%s %08X", name, val);
    while ((q = strchr(p, ':')) != 0)
    {
        p = q + 1;
        nbits = 0;
        while (*p>='0' && *p<='9')
        {
            nbits = nbits * 10 + *p++ - '0';
        }
        v = val & ((1 << nbits) - 1);
        val >>= nbits;
        if (v && *r!='_')
        {
            printf(" %.*s=%X", q-r, r, v);
        }
        while (*p==',' || *p==' ')
        {
            p = r = p + 1;
        }
    }
    printf("\n");
}


//===========================================================================
// Display SMI registers
// SMI register names for diagnostic print
static char *smi_regstrs[] = {
    "CS","LEN","A","D","DSR0","DSW0","DSR1","DSW1",
    "DSR2","DSW2","DSR3","DSW3","DMC","DCS","DCA","DCD",""
};

static void caribou_smi_disp_smi(caribou_smi_st* dev)
{
    volatile uint32_t *p=REG32(dev->smi_regs, SMI_CS);
    int i=0;

    while (smi_regstrs[i][0])
    {
        printf("%4s=%08X ", smi_regstrs[i++], *p++);
        if (i%8==0 || smi_regstrs[i][0]==0)
            printf("\n");
    }
}


//=====================================================================
// Open mailbox interface, return file descriptor
static int caribou_smi_open_mbox(void)
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
static void caribou_smi_close_mbox(int fd)
{
    if (fd >= 0)
    {
        close(fd);
    }
}

//=====================================================================
// Send message to mailbox, return first response int, 0 if error
static uint32_t caribou_smi_msg_mbox(int fd, VC_MSG *msgp)
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
// Get virtual memory segment for peripheral regs or physical mem
static void* caribou_smi_map_segment(void *addr, int size)
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
static void caribou_smi_unmap_segment(void *mem, int size)
{
    if (mem) 
    {
        munmap(mem, PAGE_ROUNDUP(size));
    }
}

//=====================================================================
// Free memory
static uint32_t caribou_smi_free_vc_mem(int fd, int h)
{
    VC_MSG msg={.tag=0x3000f, .blen=4, .dlen=4, .uints={h}};
    return (h ? caribou_smi_msg_mbox(fd, &msg) : 0);
}


//=====================================================================
// Allocate memory on PAGE_SIZE boundary, return handle
static uint32_t caribou_smi_alloc_vc_mem(int fd, uint32_t size, VC_ALLOC_FLAGS flags)
{
    VC_MSG msg={.tag=0x3000c, .blen=12, .dlen=12, .uints={PAGE_ROUNDUP(size), PAGE_SIZE, flags}};
    return (caribou_smi_msg_mbox(fd, &msg));
}

//=====================================================================
// Lock allocated memory, return bus address
static void *caribou_smi_lock_vc_mem(int fd, int h)
{
    VC_MSG msg={.tag=0x3000d, .blen=4, .dlen=4, .uints={h}};
    return (h ? (void *)caribou_smi_msg_mbox(fd, &msg) : 0);
}

//=====================================================================
// Unlock allocated memory
static uint32_t caribou_smi_unlock_vc_mem(int fd, int h)
{
    VC_MSG msg={.tag=0x3000e, .blen=4, .dlen=4, .uints={h}};
    return (h ? caribou_smi_msg_mbox(fd, &msg) : 0);
}

//=====================================================================
// Use mmap to obtain virtual address, given physical
static void* caribou_smi_map_periph(caribou_smi_st* dev, caribou_smi_mem_map_st *mp, void *phys, int size)
{
    mp->phys = phys;
    mp->size = PAGE_ROUNDUP(size);
    mp->bus = (void *)((uint32_t)phys - dev->phys_reg_base + dev->bus_reg_base);
    mp->virt = caribou_smi_map_segment(phys, mp->size);
    return(mp->virt);
}

//=====================================================================
// Free mapped peripheral or memory
static void caribou_smi_unmap_periph_mem(caribou_smi_st* dev, caribou_smi_mem_map_st *mp)
{
    if (mp)
    {
        if (mp->fd)
        {
            caribou_smi_unmap_segment(mp->virt, mp->size);
            caribou_smi_unlock_vc_mem(mp->fd, mp->h);
            caribou_smi_free_vc_mem(mp->fd, mp->h);
            caribou_smi_close_mbox(mp->fd);
        }
        else
        {
            caribou_smi_unmap_segment(mp->virt, mp->size);
        }
    }
}

//=====================================================================
static void caribou_smi_map_devices(caribou_smi_st* dev)
{
    caribou_smi_map_periph(dev, &dev->dma_regs, (void *)(DMA_BASE + dev->phys_reg_base), PAGE_SIZE);
    caribou_smi_map_periph(dev, &dev->clk_regs, (void *)(CLK_BASE + dev->phys_reg_base), PAGE_SIZE);
    caribou_smi_map_periph(dev, &dev->smi_regs, (void *)(SMI_BASE + dev->phys_reg_base), PAGE_SIZE);
}

//=====================================================================
// Free memory segments and exit
static void caribou_smi_unmap_devices(caribou_smi_st* dev)
{    
    caribou_smi_unmap_periph_mem(dev, &dev->vc_mem);
    caribou_smi_unmap_periph_mem(dev, &dev->smi_regs);
    caribou_smi_unmap_periph_mem(dev, &dev->dma_regs);
}

//=====================================================================
// Display mailbox message
void caribou_smi_disp_vc_msg(VC_MSG *msgp)
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


//=====================================================================
// Allocate uncached memory, get bus & phys addresses
static void* caribou_smi_map_uncached_mem(caribou_smi_st* dev, caribou_smi_mem_map_st *mp, int size)
{
    void *ret;
    mp->size = PAGE_ROUNDUP(size);
    mp->fd = caribou_smi_open_mbox();
    ret = (mp->h = caribou_smi_alloc_vc_mem(mp->fd, mp->size, DMA_MEM_FLAGS)) > 0 &&
        (mp->bus = caribou_smi_lock_vc_mem(mp->fd, mp->h)) != 0 &&
        (mp->virt = caribou_smi_map_segment(BUS_PHYS_ADDR(mp->bus), mp->size)) != 0
        ? mp->virt : 0;
    printf("VC mem handle %u, phys %p, virt %p\n", mp->h, mp->bus, mp->virt);
    return ret;
}

//=====================================================================
// Initialise SMI, given data width, time step, and setup/hold/strobe counts
static void caribou_smi_init_internal(  caribou_smi_st* dev, 
                                        caribou_smi_transaction_size_bits_en width, 
                                        caribou_smi_timing_st* timing)
{
    int divi = timing->step_size / 2;

    dev->smi_cs  = (SMI_CS_REG *) REG32(dev->smi_regs, SMI_CS);
    dev->smi_l   = (SMI_L_REG *)  REG32(dev->smi_regs, SMI_L);
    dev->smi_a   = (SMI_A_REG *)  REG32(dev->smi_regs, SMI_A);
    dev->smi_d   = (SMI_D_REG *)  REG32(dev->smi_regs, SMI_D);
    dev->smi_dmc = (SMI_DMC_REG *)REG32(dev->smi_regs, SMI_DMC);
    dev->smi_dsr0 = (SMI_DSR_REG *)REG32(dev->smi_regs, SMI_DSR0);
    dev->smi_dsw0 = (SMI_DSW_REG *)REG32(dev->smi_regs, SMI_DSW0);
    dev->smi_dsr1 = (SMI_DSR_REG *)REG32(dev->smi_regs, SMI_DSR1);
    dev->smi_dsw1 = (SMI_DSW_REG *)REG32(dev->smi_regs, SMI_DSW1);
    dev->smi_dsr2 = (SMI_DSR_REG *)REG32(dev->smi_regs, SMI_DSR2);
    dev->smi_dsw2 = (SMI_DSW_REG *)REG32(dev->smi_regs, SMI_DSW2);
    dev->smi_dsr3 = (SMI_DSR_REG *)REG32(dev->smi_regs, SMI_DSR3);
    dev->smi_dsw3 = (SMI_DSW_REG *)REG32(dev->smi_regs, SMI_DSW3);
    dev->smi_dcs = (SMI_DCS_REG *)REG32(dev->smi_regs, SMI_DCS);
    dev->smi_dca = (SMI_DCA_REG *)REG32(dev->smi_regs, SMI_DCA);
    dev->smi_dcd = (SMI_DCD_REG *)REG32(dev->smi_regs, SMI_DCD);

    dev->smi_cs->value = 0;
    dev->smi_l->value = 0;
    dev->smi_a->value = 0;
    dev->smi_dsr0->value = 0;
    dev->smi_dsw0->value = 0;
    dev->smi_dsr1->value = 0;
    dev->smi_dsw1->value = 0;
    dev->smi_dsr2->value = 0;
    dev->smi_dsw2->value = 0;
    dev->smi_dsr3->value = 0;
    dev->smi_dsw3->value = 0;
    dev->smi_dcs->value = 0;
    dev->smi_dca->value = 0;

    // Clock configuration
    if (*REG32(dev->clk_regs, CLK_SMI_DIV) != divi << 12)
    {
        *REG32(dev->clk_regs, CLK_SMI_CTL) = CLK_PASSWD | (1 << 5);
        io_utils_usleep(10);
        while (*REG32(dev->clk_regs, CLK_SMI_CTL) & (1 << 7)) ;
        io_utils_usleep(10);
        *REG32(dev->clk_regs, CLK_SMI_DIV) = CLK_PASSWD | (divi << 12);
        io_utils_usleep(10);
        *REG32(dev->clk_regs, CLK_SMI_CTL) = CLK_PASSWD | 6 | (1 << 4);
        io_utils_usleep(10);
        while ((*REG32(dev->clk_regs, CLK_SMI_CTL) & (1 << 7)) == 0) ;
        io_utils_usleep(100);
    }

    // if error exist in the SMI Control & Status (CS), latch it to clear
    if (dev->smi_cs->seterr)
    {
        dev->smi_cs->seterr = 1;
    }

    dev->smi_dsr0->rsetup = timing->setup_steps;
    dev->smi_dsw0->wsetup = timing->setup_steps;
    dev->smi_dsr0->rstrobe = timing->strobe_steps;
    dev->smi_dsw0->wstrobe = timing->strobe_steps;
    dev->smi_dsr0->rhold = timing->hold_steps;
    dev->smi_dsw0->whold = timing->hold_steps;
    dev->smi_dmc->panicr = 8;
    dev->smi_dmc->panicw = 8;
    dev->smi_dmc->reqr = 4;
    dev->smi_dmc->reqw = 4;
    dev->smi_dsr0->rwidth = width;
    dev->smi_dsw0->wwidth = width;
}

//=====================================================================
static void caribou_smi_fill_sys_info(caribou_smi_st* dev, io_utils_sys_info_st *sys_info)
{
    dev->use_video_core_clock = 1;
    
    if (!strcmp(sys_info->processor, "BCM2835"))
    {
        // PI ZERO / PI1
        dev->processor_type = caribou_smi_processor_BCM2835;
        dev->phys_reg_base = 0x20000000;
        dev->sys_clock_hz = 700000000;
        dev->bus_reg_base = 0x7E000000;
    } 
    else if (!strcmp(sys_info->processor, "BCM2836") || !strcmp(sys_info->processor, "BCM2837"))
    {
        // PI2 / PI3
        dev->processor_type = caribou_smi_processor_BCM2836;
        dev->phys_reg_base = 0x3F000000;
        dev->sys_clock_hz = 600000000;
        dev->bus_reg_base = 0x7E000000;
    }
    else if (!strcmp(sys_info->processor, "BCM2711"))
    {
        // PI4 / PI400
        dev->processor_type = caribou_smi_processor_BCM2711;
        dev->phys_reg_base = 0xFE000000;
        dev->sys_clock_hz = 600000000;
        dev->bus_reg_base = 0x7E000000;
    }
    else
    {
        // USE ZERO AS DEFAULT
        dev->processor_type = caribou_smi_processor_UNKNOWN;
        dev->phys_reg_base = 0x20000000;
        dev->sys_clock_hz = 400000000;
        dev->bus_reg_base = 0x7E000000;
    }

    if (!strcmp(sys_info->ram, "256M")) dev->ram_size_mbytes = 256;
    else if (!strcmp(sys_info->ram, "512M")) dev->ram_size_mbytes = 512;
    else if (!strcmp(sys_info->ram, "1G")) dev->ram_size_mbytes = 1000;
    else if (!strcmp(sys_info->ram, "2G")) dev->ram_size_mbytes = 2000;
    else if (!strcmp(sys_info->ram, "4G")) dev->ram_size_mbytes = 4000;
    else if (!strcmp(sys_info->ram, "8G")) dev->ram_size_mbytes = 8000;
}

//=====================================================================
static float caribou_smi_calculate_clocking (caribou_smi_st* dev, float freq_sps, caribou_smi_timing_st* timing)
{
    float clock_rate = dev->sys_clock_hz;
    float cc_in_ns = 1e9 / clock_rate;
    float sample_time_ns = 1e9 / freq_sps;
    
    int cc_budget = (int)(sample_time_ns / cc_in_ns);
    if (cc_budget < 6) 
    {
        printf("The clock cycle budget for the calculation is too small.\n");
        return -1;
    }
    // try to make a 1:2:1 ratio between setup, strobe and hold (but step needs to be even)
    timing->step_size = (int)(roundf((float)cc_budget / 8.0f))*2;
    timing->setup_steps = 1;
    timing->strobe_steps = 2;
    timing->hold_steps = 1;

    return 1e9 / (timing->step_size*cc_in_ns*(timing->setup_steps + timing->strobe_steps + timing->hold_steps));
}

//=====================================================================
static void caribou_smi_print_timing (float actual_freq, caribou_smi_timing_st* timing)
{
    printf("Timing: step: %d, setup: %d, strobe: %d, hold: %d, actual_freq: %.2f MHz\n", 
                timing->step_size, timing->setup_steps, timing->strobe_steps, timing->hold_steps, actual_freq / 1e6);
}

//=====================================================================
int caribou_smi_init(caribou_smi_st* dev)
{
    // Get the RPI information
    io_utils_sys_info_st sys_info = {0};
    io_utils_get_rpi_info(&sys_info);
    caribou_smi_fill_sys_info(dev, &sys_info);

    printf("processor_type = %d, phys_reg_base = %08X, sys_clock_hz = %d Hz, bus_reg_base = %08X, RAM = %d Mbytes\n",
    dev->processor_type, dev->phys_reg_base, dev->sys_clock_hz, dev->bus_reg_base, dev->ram_size_mbytes);
    io_utils_print_rpi_info(&sys_info);

    for (int i=0; i<dev->num_data_pins; i++)
    {
        io_utils_set_gpio_mode(dev->data_0_pin + i, io_utils_alt_gpio_in);
    }
    io_utils_set_gpio_mode(dev->soe_pin, io_utils_alt_1);
    io_utils_set_gpio_mode(dev->swe_pin, io_utils_alt_1);

    //io_utils_set_gpio_mode(dev->read_req_pin, io_utils_alt_1);
    //io_utils_set_gpio_mode(dev->write_req_pin, io_utils_alt_1);

    for (int i=0; i<dev->num_addr_pins; i++)
    {
        io_utils_set_gpio_mode(dev->addr0_pin + i, io_utils_alt_1);
    }

    // Setup a reading rate of 32 MSPS (16 MSPS x2)
    caribou_smi_timing_st timing = {0};
    float actual_sps = caribou_smi_calculate_clocking (dev, 32e6, &timing);
    caribou_smi_print_timing (actual_sps, &timing);

    /*caribou_smi_init_internal(dev, caribou_smi_transaction_size_8bits, &timing);

    caribou_smi_map_uncached_mem(&dev->vc_mem, VC_MEM_SIZE(NSAMPLES+PRE_SAMP));
    dev->smi_dmc->dmaen = 1;
    dev->smi_cs->enable = 1;
    dev->smi_cs->clear = 1;

    rxbuff = adc_dma_start(&dev->vc_mem, NSAMPLES);
    smi_start(dev, NSAMPLES, 1);

    while (dma_active(DMA_CHAN_A)) ;
    
    adc_dma_end(dev, rxbuff, sample_data, NSAMPLES);

    disp_reg_fields(smi_cs_regstrs, "CS", *REG32(dev->smi_regs, SMI_CS));
    dev->smi_cs->enable = dev->smi_dcs->enable = 0;
    for (int i=0; i<NSAMPLES; i++)
    {
        printf("%1.3f\n", val_volts(sample_data[i]));
    }*/

    dev->initialized = 1;
    return 0;
}

//=====================================================================
int caribou_smi_close(caribou_smi_st* dev)
{
    if (!dev->initialized)
    {
        return 0;
    }

    dev->initialized = 0;

    // GPIO Setting back to default
    for (int i=0; i<dev->num_data_pins; i++)
    {
        io_utils_set_gpio_mode(dev->data_0_pin + i, io_utils_alt_gpio_in);
    }
    io_utils_set_gpio_mode(dev->soe_pin, io_utils_alt_gpio_in);
    io_utils_set_gpio_mode(dev->swe_pin, io_utils_alt_gpio_in);
    //io_utils_set_gpio_mode(dev->read_req_pin, io_utils_alt_gpio_in);
    //io_utils_set_gpio_mode(dev->write_req_pin, io_utils_alt_gpio_in);
    for (int i=0; i<dev->num_addr_pins; i++)
    {
        io_utils_set_gpio_mode(dev->addr0_pin + i, io_utils_alt_gpio_in);
    }

    
    /*if (dev->smi_regs.virt)
    {
        *REG32(dev->smi_regs, SMI_CS) = 0;
    }
    stop_dma(DMA_CHAN_A);
    caribou_smi_unmap_devices(dev);*/

    return 0;
}

//===========================================================================
// Start SMI, given number of samples, optionally pack bytes into words
void caribou_smi_start(caribou_smi_st* dev, int nsamples, int pre_samp, int packed)
{
    dev->smi_l->len = nsamples + pre_samp;
    dev->smi_cs->pxldat = (packed != 0);
    dev->smi_cs->enable = 1;
    dev->smi_cs->clear = 1;
    dev->smi_cs->start = 1;
}

//===========================================================================
// Start DMA for SMI ADC, return Rx data buffer
/*uint32_t *adc_dma_start(caribou_smi_mem_map_st *mp, int nsamp)
{
    DMA_CB *cbs=mp->virt;
    uint32_t *data=(uint32_t *)(cbs+4), *pindata=data+8, *modes=data+0x10;
    uint32_t *modep1=data+0x18, *modep2=modep1+1, *rxdata=data+0x20, i;

    // Get current mode register values
    for (i=0; i<3; i++)
        modes[i] = modes[i+3] = *REG32(gpio_regs, GPIO_MODE0 + i*4);
    // Get mode values with ADC pins set to SMI
    for (i=ADC_D0_PIN; i<ADC_D0_PIN+ADC_NPINS; i++)
        mode_word(&modes[i/10], i%10, GPIO_ALT1);
    // Copy mode values into 32-bit words
    *modep1 = modes[1];
    *modep2 = modes[2];
    *pindata = 1 << TEST_PIN;
    enable_dma(DMA_CHAN_A);
    // Control blocks 0 and 1: enable SMI I/P pins
    cbs[0].ti = DMA_SRCE_DREQ | (DMA_SMI_DREQ << 16) | DMA_WAIT_RESP;
#if USE_TEST_PIN
    cbs[0].tfr_len = 4;
    cbs[0].srce_ad = MEM_BUS_ADDR(mp, pindata);
    cbs[0].dest_ad = REG_BUS_ADDR(gpio_regs, GPIO_SET0);
    cbs[0].next_cb = MEM_BUS_ADDR(mp, &cbs[2]);
#else
    cbs[0].tfr_len = 4;
    cbs[0].srce_ad = MEM_BUS_ADDR(mp, modep1);
    cbs[0].dest_ad = REG_BUS_ADDR(gpio_regs, GPIO_MODE0+4);
    cbs[0].next_cb = MEM_BUS_ADDR(mp, &cbs[1]);
#endif
    cbs[1].tfr_len = 4;
    cbs[1].srce_ad = MEM_BUS_ADDR(mp, modep2);
    cbs[1].dest_ad = REG_BUS_ADDR(gpio_regs, GPIO_MODE0+8);
    cbs[1].next_cb = MEM_BUS_ADDR(mp, &cbs[2]);
    // Control block 2: read data
    cbs[2].ti = DMA_SRCE_DREQ | (DMA_SMI_DREQ << 16) | DMA_CB_DEST_INC;
    cbs[2].tfr_len = (nsamp + PRE_SAMP) * SAMPLE_SIZE;
    cbs[2].srce_ad = REG_BUS_ADDR(smi_regs, SMI_D);
    cbs[2].dest_ad = MEM_BUS_ADDR(mp, rxdata);
    cbs[2].next_cb = MEM_BUS_ADDR(mp, &cbs[3]);
    // Control block 3: disable SMI I/P pins
    cbs[3].ti = DMA_CB_SRCE_INC | DMA_CB_DEST_INC;
#if USE_TEST_PIN
    cbs[3].tfr_len = 4;
    cbs[3].srce_ad = MEM_BUS_ADDR(mp, pindata);
    cbs[3].dest_ad = REG_BUS_ADDR(gpio_regs, GPIO_CLR0);
#else
    cbs[3].tfr_len = 3 * 4;
    cbs[3].srce_ad = MEM_BUS_ADDR(mp, &modes[3]);
    cbs[3].dest_ad = REG_BUS_ADDR(gpio_regs, GPIO_MODE0);
#endif
    start_dma(mp, DMA_CHAN_A, &cbs[0], 0);
    return(rxdata);
}

//===========================================================================
// ADC DMA is complete, get data
int adc_dma_end(void *buff, uint16_t *data, int nsamp)
{
    uint16_t *bp = (uint16_t *)buff;
    int i;

    for (i=0; i<nsamp+PRE_SAMP; i++)
    {
        if (i >= PRE_SAMP)
            *data++ = bp[i] >> 4;
    }
    return(nsamp);
}

*/
