#include <stdio.h>
#include "caribou_smi.h"

//=====================================================================
static void caribou_smi_map_devices(caribou_smi_st* dev)
{
    map_periph(&dev->dma_regs, (void *)(DMA_BASE + dev->phys_reg_base), PAGE_SIZE);
    map_periph(&dev->clk_regs, (void *)(CLK_BASE + dev->phys_reg_base), PAGE_SIZE);
    map_periph(&dev->smi_regs, (void *)(SMI_BASE + dev->phys_reg_base), PAGE_SIZE);
}

//=====================================================================
// Free memory segments and exit
static void caribou_smi_unmap_devices(caribou_smi_st* dev)
{    
    unmap_periph_mem(&dev->vc_mem);
    unmap_periph_mem(&dev->smi_regs);
    unmap_periph_mem(&dev->dma_regs);
}

//=====================================================================
// Initialise SMI, given data width, time step, and setup/hold/strobe counts
// step: in clock cycles of the specific platform (RPI0-3 or RPI4)
// setup, strobe, and hold: calculated in number of steps.
// 
// Thus, the total sample period (in nanosecs) is given by:
//    step * ( setup + strobe + hold ) * ns_per_clock_cycle
//
// For an RPI4 @ 1500 MHz clock => 0.667 nsec / clock cycle
// The configuration of: (step, setup, strobe, hold) = (4,  3,  8,  4)
// yields: 0.667nsec*4*(3+8+4) = 8/3*15 = 40 nanoseconds/sample => 25 MSPS
static void caribou_smi_init_internal(caribou_smi_st* dev, 
                                                int width, 
                                                int step, 
                                                int setup, 
                                                int strobe, 
                                                int hold)
{
    int divi = step / 2;

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

    dev->smi_dsr0->rsetup = setup;
    dev->smi_dsw0->wsetup = setup;
    dev->smi_dsr0->rstrobe = strobe;
    dev->smi_dsw0->wstrobe = strobe;
    dev->smi_dsr0->rhold = hold;
    dev->smi_dsw0->whold = hold;
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
        dev->sys_clock_hz = 400000000;
        dev->bus_reg_base = 0x7E000000;
    } 
    else if (!strcmp(sys_info->processor, "BCM2836") || !strcmp(sys_info->processor, "BCM2837"))
    {
        // PI2 / PI3
        dev->processor_type = caribou_smi_processor_BCM2836;
        dev->phys_reg_base = 0x3F000000;
        dev->sys_clock_hz = 250000000;
        dev->bus_reg_base = 0x7E000000;
    }
    else if (!strcmp(sys_info->processor, "BCM2711"))
    {
        // PI4 / PI400
        dev->processor_type = caribou_smi_processor_BCM2711;
        dev->phys_reg_base = 0xFE000000;
        dev->sys_clock_hz = 250000000;
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

    init_smi(SMI_NUM_BITS, SMI_TIMING);

    /*map_uncached_mem(&dev->vc_mem, VC_MEM_SIZE(NSAMPLES+PRE_SAMP));
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

    /*
    if (dev->smi_regs.virt)
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


//===========================================================================
// Display bit values in register
void caribou_smi_disp_reg_fields(char *regstrs, char *name, uint32_t val)
{
    char *p=regstrs, *q, *r=regstrs;
    uint32_t nbits, v;

    printf("%s %08X", name, val);
    while ((q = strchr(p, ':')) != 0)
    {
        p = q + 1;
        nbits = 0;
        while (*p>='0' && *p<='9')
            nbits = nbits * 10 + *p++ - '0';
        v = val & ((1 << nbits) - 1);
        val >>= nbits;
        if (v && *r!='_')
            printf(" %.*s=%X", q-r, r, v);
        while (*p==',' || *p==' ')
            p = r = p + 1;
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

void caribou_smi_disp_smi(caribou_smi_st* dev)
{
    volatile uint32_t *p=REG32(dev->smi_regs, SMI_CS);
    int i=0;

    while (smi_regstrs[i][0])
    {
        printf("%4s=%08X ", smi_regstrs[i++], *p++);
        if (i%8==0 || smi_regstrs[i][0]==0)
            printf("\n");
    }
}*/