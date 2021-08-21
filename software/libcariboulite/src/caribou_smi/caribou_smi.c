#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "DMA_Utils"
#include "zf_log/zf_log.h"

#include <stdio.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "caribou_smi.h"
#include "dma_utils.h"
#include "mbox_utils.h"
#include "register_utils.h"


//=====================================================================
// Initialise SMI, given data width, time step, and setup/hold/strobe counts
static void caribou_smi_init_registers( caribou_smi_st* dev, 
                                        caribou_smi_transaction_size_bits_en width, 
                                        caribou_smi_timing_st* timing)
{
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
static float caribou_smi_calculate_clocking (caribou_smi_st* dev, float freq_sps, caribou_smi_timing_st* timing)
{
    float clock_rate = dev->sys_info.sys_clock_hz;
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
int caribou_smi_init(caribou_smi_st* dev)
{
    // Get the RPI information
    //----------------------------------------
    io_utils_get_rpi_info(&dev->sys_info);

    printf("processor_type = %d, phys_reg_base = %08X, sys_clock_hz = %d Hz, bus_reg_base = %08X, RAM = %d Mbytes\n",
                    dev->sys_info.processor_type, dev->sys_info.phys_reg_base, dev->sys_info.sys_clock_hz, 
                    dev->sys_info.bus_reg_base, dev->sys_info.ram_size_mbytes);
    io_utils_print_rpi_info(&dev->sys_info);

    // Map devices and Allocate memory
    //----------------------------------------
    dma_utils_init (&dev->dma);
    map_periph(&dev->smi_regs, SMI_BASE, PAGE_SIZE);

    // the allocation is page_size rounded-up
    // The page-size if 0x1000 = (4096) decimal. Thus for a CB sized 32byte => 128 maximal number of CBs
    // Sample size from the modem is 4 bytes and each buffer contains "sample_buf_length" samples.
    // We allocate uncached memory for max 128 CBs and "num_sample_bufs" sample buffers
    uint32_t single_buffer_size_round = PAGE_ROUNDUP(dev->sample_buf_length * SAMPLE_SIZE_BYTES);
    dev->videocore_alloc_size = PAGE_SIZE + dev->num_sample_bufs * single_buffer_size_round;
    dev->actual_sample_buf_length_sec = (float)(single_buffer_size_round) / 16e6;
    
    map_uncached_mem(&dev->vc_mem, dev->videocore_alloc_size);

    printf("INFO: caribou_smi_init - The actual single buffer contains %.2f usec of data\n", 
            dev->actual_sample_buf_length_sec * 1e6);

    // Initialize the GPIO modes and states
    //----------------------------------------
    for (int i=0; i<dev->num_data_pins; i++)
    {
        io_utils_set_gpio_mode(dev->data_0_pin + i, io_utils_alt_gpio_in);
    }
    io_utils_set_gpio_mode(dev->soe_pin, io_utils_alt_1);
    io_utils_set_gpio_mode(dev->swe_pin, io_utils_alt_1);

    io_utils_set_gpio_mode(dev->read_req_pin, io_utils_alt_1);
    io_utils_set_gpio_mode(dev->write_req_pin, io_utils_alt_1);

    for (int i=0; i<dev->num_addr_pins; i++)
    {
        io_utils_set_gpio_mode(dev->addr0_pin + i, io_utils_alt_1);
    }

    // Setup a reading rate of 32 MSPS (16 MSPS x2)
    //---------------------------------------------
    caribou_smi_timing_st timing = {0};
    timing.hold_steps = 1;
    timing.setup_steps = 1;
    timing.strobe_steps = 1;
    caribou_smi_init_registers(dev, caribou_smi_transaction_size_8bits, &timing);

    // Initialize the reader
    caribou_smi_reset_read(dev);

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

    caribou_smi_reset_read(dev);

    // GPIO Setting back to default
    for (int i=0; i<dev->num_data_pins; i++)
    {
        io_utils_set_gpio_mode(dev->data_0_pin + i, io_utils_alt_gpio_in);
    }
    io_utils_set_gpio_mode(dev->soe_pin, io_utils_alt_gpio_in);
    io_utils_set_gpio_mode(dev->swe_pin, io_utils_alt_gpio_in);
    io_utils_set_gpio_mode(dev->read_req_pin, io_utils_alt_gpio_in);
    io_utils_set_gpio_mode(dev->write_req_pin, io_utils_alt_gpio_in);
    for (int i=0; i<dev->num_addr_pins; i++)
    {
        io_utils_set_gpio_mode(dev->addr0_pin + i, io_utils_alt_gpio_in);
    }

    
    if (dev->smi_regs.virt)
    {
        *REG32(dev->smi_regs, SMI_CS) = 0;
    }
    dma_utils_stop_dma(&dev->dma, dev->dma_channel);

    unmap_periph_mem(&dev->vc_mem);
    unmap_periph_mem(&dev->smi_regs);
    dma_utils_close (&dev->dma);

    return 0;
}

//===========================================================================
void caribou_smi_reset_read (caribou_smi_st* dev)
{
    ZF_LOGI("Reseting read process");
    int *ret;
    dev->read_active = 0;
    pthread_join(dev->read_thread, (void**)&ret);

    dev->read_cb = NULL;
    dev->read_buffer = NULL;
    dev->read_buffer_length = 0;
    dev->current_read_counter = 0;
    ZF_LOGI("Read process reset");
}

//===========================================================================
void *caribou_smi_read_thread( void *ptr )
{
    caribou_smi_st* dev = (caribou_smi_st*)ptr;
    ZF_LOGI("Reading thread started ID = %d", pthread_self());

    while (dev->read_active)
    {
        usleep(1000);
    }

    // idle the smi address
    dev->smi_a = caribou_smi_address_idle;

    // exit the thread
    dev->read_thread_ret_val = 0;
    pthread_exit(&dev->read_thread_ret_val);
}

//===========================================================================
// Setup SMI reading process
int caribou_smi_configure_read(caribou_smi_st* dev,
                                caribou_smi_channel_en channel,
                                uint32_t continues,
                                uint32_t single_number_of_samples,
                                caribou_smi_read_data_callback cb,
                                uint8_t *serv_buffer, uint32_t serv_buffer_len)
{
    ZF_LOGI("Configuring SMI read from Channel %d", channel);

    if (dev->read_active)
    {
        ZF_LOGI("Read process already active, deactivate and try again");
        return -1;
    }

    dev->smi_dmc->dmaen = 1;
    dev->smi_cs->enable = 1;
    dev->smi_cs->clear = 1;

    switch (channel)
    {
        case caribou_smi_channel_900: dev->smi_a->value = caribou_smi_address_read_900; break;
        case caribou_smi_channel_2400: dev->smi_a->value = caribou_smi_address_read_2400; break;
        default: 
        {
            ZF_LOGE("Channel %d is not implemented for read", channel); 
            return -1; 
            break;
        }
    }

    int ret = 0;
    if (continues)
    {
        // start a continues thread
        dev->read_cb = cb;
        dev->read_buffer = serv_buffer;
        dev->read_buffer_length = serv_buffer_len;
        dev->current_read_counter = 0;
        dev->read_active = 1;
        ret = pthread_create(&dev->read_thread, NULL, caribou_smi_read_thread, (void*) dev);
    }
    else
    {
        DMA_CB *cbs = dev->vc_mem.virt;
        // check if there is enough allocated space
        if (single_number_of_samples*SAMPLE_SIZE_BYTES > (dev->videocore_alloc_size - PAGE_SIZE))
        {
            ZF_LOGE("The Videocode allocated samples memory (%d) is not sufficient for the required number of samples (%d)",
                            dev->videocore_alloc_size - PAGE_SIZE, single_number_of_samples);
            dev->smi_a = caribou_smi_address_idle;
            return -1;
        }
        if (single_number_of_samples*SAMPLE_SIZE_BYTES > serv_buffer_len)
        {
            ZF_LOGE("The output Served_buffer size is too small (%d) to hold the required number of samples (%d)",
                            serv_buffer_len, single_number_of_samples);
            dev->smi_a = caribou_smi_address_idle;
            return -1;
        }
        uint8_t *rxdata = (uint8_t*)(cbs + 0x20);

        // Control block 2: read data
        cbs[0].ti = DMA_SRCE_DREQ | (DMA_SMI_DREQ << 16) | DMA_CB_DEST_INC;
        cbs[0].tfr_len = single_number_of_samples * SAMPLE_SIZE_BYTES;
        cbs[0].srce_ad = REG_BUS_ADDR(&(dev->smi_regs), SMI_D);
        cbs[0].dest_ad = MEM_BUS_ADDR((&(dev->vc_mem)), rxdata);
        cbs[0].next_cb = 0;         // stop the reception after a single shot
        
        dma_utils_start_dma(&dev->dma, &dev->vc_mem, dev->dma_channel, &cbs[0], 0);
        caribou_smi_start(dev, single_number_of_samples, 0, 1);

        // wait until dma finishes
        while (dma_utils_dma_is_active(&dev->dma, dev->dma_channel)) ;
        
        memcpy (serv_buffer, rxdata, single_number_of_samples * SAMPLE_SIZE_BYTES);
        dev->smi_a->value = caribou_smi_address_idle;
    }

    return ret;
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
}

//=====================================================================
void caribou_smi_print_timing (float actual_freq, caribou_smi_timing_st* timing)
{
    printf("Timing: step: %d, setup: %d, strobe: %d, hold: %d, actual_freq: %.2f MHz\n", 
                timing->step_size, timing->setup_steps, timing->strobe_steps, timing->hold_steps, actual_freq / 1e6);
}