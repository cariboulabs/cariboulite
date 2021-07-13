// Test of parallel AD9226 ADC using Raspberry Pi SMI (Secondary Memory Interface)
// For detailed description, see https://iosoft.blog
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
// v0.06 JPB 16/7/20 Tidied up for Github

#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rpi_dma_utils.h"
#include "rpi_smi_defs.h"

// Set zero for single value, non-zero for block read
#define USE_DMA         1
// Use test pin in place of GPIO mode setting (to check timing)
#define USE_TEST_PIN    0

// SMI cycle timings
#define SMI_NUM_BITS    SMI_16_BITS
#define SMI_TIMING      SMI_TIMING_20M

#if PHYS_REG_BASE==PI_4_REG_BASE        // Timings for RPi v4 (1.5 GHz)
#define SMI_TIMING_1M   10, 38, 74, 38  // 1 MS/s
#define SMI_TIMING_10M   6,  6, 13,  6  // 10 MS/s
#define SMI_TIMING_20M   4,  5,  9,  5  // 19.74 MS/s
#define SMI_TIMING_25M   4,  3,  8,  4  // 25 MS/s
#define SMI_TIMING_31M   4,  3,  6,  3  // 31.25 MS/s
#else                                   // Timings for RPi v0-3 (1 GHz)
#define SMI_TIMING_1M   10, 25, 50, 25  // 1 MS/s
#define SMI_TIMING_10M   4,  6, 13,  6  // 10 MS/s
#define SMI_TIMING_20M   2,  6, 13,  6  // 20 MS/s
#define SMI_TIMING_25M   2,  5, 10,  5  // 25 MS/s
#define SMI_TIMING_31M   2,  4,  6,  4  // 31.25 MS/s
#define SMI_TIMING_42M   2,  3,  6,  3  // 41.66 MS/s
#define SMI_TIMING_50M   2,  3,  5,  2  // 50 MS/s
#endif

// Number of raw bytes per ADC sample
#define SAMPLE_SIZE     2

// Number of samples to be captured, and number to be discarded
#define NSAMPLES        500
#define PRE_SAMP        24

// Voltage calibration
#define ADC_ZERO        2080
#define ADC_SCALE       410.0

// GPIO pin numbers
#define ADC_D0_PIN      12
#define ADC_NPINS       12
#define SMI_SOE_PIN     6
#define SMI_SWE_PIN     7
#define SMI_DREQ_PIN    24
#define TEST_PIN        25

// DMA request threshold
#define REQUEST_THRESH  4

// SMI register names for diagnostic print
char *smi_regstrs[] = {
    "CS","LEN","A","D","DSR0","DSW0","DSR1","DSW1",
    "DSR2","DSW2","DSR3","DSW3","DMC","DCS","DCA","DCD",""
};

// SMI CS register field names for diagnostic print
#define STRS(x)     STRS_(x) ","
#define STRS_(...)  #__VA_ARGS__
char *smi_cs_regstrs = STRS(SMI_CS_FIELDS);

// Structures for mapped I/O devices, and non-volatile memory
extern MEM_MAP gpio_regs, dma_regs;
MEM_MAP vc_mem, clk_regs, smi_regs;

// Pointers to SMI registers
volatile SMI_CS_REG  *smi_cs;
volatile SMI_L_REG   *smi_l;
volatile SMI_A_REG   *smi_a;
volatile SMI_D_REG   *smi_d;
volatile SMI_DMC_REG *smi_dmc;
volatile SMI_DSR_REG *smi_dsr;
volatile SMI_DSW_REG *smi_dsw;
volatile SMI_DCS_REG *smi_dcs;
volatile SMI_DCA_REG *smi_dca;
volatile SMI_DCD_REG *smi_dcd;

// Buffer for captured samples
uint16_t sample_data[NSAMPLES];

// Non-volatile memory size
#define VC_MEM_SIZE(nsamp) (PAGE_SIZE + ((nsamp)+4)*SAMPLE_SIZE)

void map_devices(void);
void fail(char *s);
void terminate(int sig);
void smi_start(int nsamples, int packed);
uint32_t *adc_dma_start(MEM_MAP *mp, int nsamp);
int adc_dma_end(void *buff, uint16_t *data, int nsamp);
void init_smi(int width, int ns, int setup, int hold, int strobe);
void disp_smi(void);
void mode_word(uint32_t *wp, int n, uint32_t mode);
float val_volts(int val);
int adc_gpio_val(void);
void disp_reg_fields(char *regstrs, char *name, uint32_t val);
void dma_wait(int chan);

int main(int argc, char *argv[])
{
    void *rxbuff;
    int i;

    signal(SIGINT, terminate);
    map_devices();
    for (i=0; i<ADC_NPINS; i++)
        gpio_mode(ADC_D0_PIN+i, GPIO_IN);
    gpio_mode(SMI_SOE_PIN, GPIO_ALT1);
#if !USE_DMA
    init_smi(SMI_NUM_BITS, SMI_TIMING_1M);
    while (1)
    {
        smi_start(PRE_SAMP, 1);
        usleep(20);
        int val = adc_gpio_val();
        printf("%4u %1.3f\n", val, val_volts(val));
        sleep(1);
    }
#else
    init_smi(SMI_NUM_BITS, SMI_TIMING);
#if USE_TEST_PIN
    gpio_mode(TEST_PIN, GPIO_OUT);
    gpio_out(TEST_PIN, 0);
#endif
    map_uncached_mem(&vc_mem, VC_MEM_SIZE(NSAMPLES+PRE_SAMP));
    smi_dmc->dmaen = 1;
    smi_cs->enable = 1;
    smi_cs->clear = 1;
    rxbuff = adc_dma_start(&vc_mem, NSAMPLES);
    smi_start(NSAMPLES, 1);
    while (dma_active(DMA_CHAN_A)) ;
    adc_dma_end(rxbuff, sample_data, NSAMPLES);
    disp_reg_fields(smi_cs_regstrs, "CS", *REG32(smi_regs, SMI_CS));
    smi_cs->enable = smi_dcs->enable = 0;
    for (i=0; i<NSAMPLES; i++)
        printf("%1.3f\n", val_volts(sample_data[i]));
#endif
    terminate(0);
    return(0);
}

// Map GPIO, DMA and SMI registers into virtual mem (user space)
// If any of these fail, program will be terminated
void map_devices(void)
{
    map_periph(&gpio_regs, (void *)GPIO_BASE, PAGE_SIZE);
    map_periph(&dma_regs, (void *)DMA_BASE, PAGE_SIZE);
    map_periph(&clk_regs, (void *)CLK_BASE, PAGE_SIZE);
    map_periph(&smi_regs, (void *)SMI_BASE, PAGE_SIZE);
}

// Catastrophic failure in initial setup
void fail(char *s)
{
    printf(s);
    terminate(0);
}

// Free memory segments and exit
void terminate(int sig)
{
    int i;

    printf("Closing\n");
    if (gpio_regs.virt)
    {
        for (i=0; i<ADC_NPINS; i++)
            gpio_mode(ADC_D0_PIN+i, GPIO_IN);
    }
    if (smi_regs.virt)
        *REG32(smi_regs, SMI_CS) = 0;
    stop_dma(DMA_CHAN_A);
    unmap_periph_mem(&vc_mem);
    unmap_periph_mem(&smi_regs);
    unmap_periph_mem(&dma_regs);
    unmap_periph_mem(&gpio_regs);
    exit(0);
}

// Start SMI, given number of samples, optionally pack bytes into words
void smi_start(int nsamples, int packed)
{
    smi_l->len = nsamples + PRE_SAMP;
    smi_cs->pxldat = (packed != 0);
    smi_cs->enable = 1;
    smi_cs->clear = 1;
    smi_cs->start = 1;
}

// Start DMA for SMI ADC, return Rx data buffer
uint32_t *adc_dma_start(MEM_MAP *mp, int nsamp)
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

// Initialise SMI, given data width, time step, and setup/hold/strobe counts
// Step value is in nanoseconds: even numbers, 2 to 30
void init_smi(int width, int ns, int setup, int strobe, int hold)
{
    int divi = ns / 2;

    smi_cs  = (SMI_CS_REG *) REG32(smi_regs, SMI_CS);
    smi_l   = (SMI_L_REG *)  REG32(smi_regs, SMI_L);
    smi_a   = (SMI_A_REG *)  REG32(smi_regs, SMI_A);
    smi_d   = (SMI_D_REG *)  REG32(smi_regs, SMI_D);
    smi_dmc = (SMI_DMC_REG *)REG32(smi_regs, SMI_DMC);
    smi_dsr = (SMI_DSR_REG *)REG32(smi_regs, SMI_DSR0);
    smi_dsw = (SMI_DSW_REG *)REG32(smi_regs, SMI_DSW0);
    smi_dcs = (SMI_DCS_REG *)REG32(smi_regs, SMI_DCS);
    smi_dca = (SMI_DCA_REG *)REG32(smi_regs, SMI_DCA);
    smi_dcd = (SMI_DCD_REG *)REG32(smi_regs, SMI_DCD);
    smi_cs->value = smi_l->value = smi_a->value = 0;
    smi_dsr->value = smi_dsw->value = smi_dcs->value = smi_dca->value = 0;
    if (*REG32(clk_regs, CLK_SMI_DIV) != divi << 12)
    {
        *REG32(clk_regs, CLK_SMI_CTL) = CLK_PASSWD | (1 << 5);
        usleep(10);
        while (*REG32(clk_regs, CLK_SMI_CTL) & (1 << 7)) ;
        usleep(10);
        *REG32(clk_regs, CLK_SMI_DIV) = CLK_PASSWD | (divi << 12);
        usleep(10);
        *REG32(clk_regs, CLK_SMI_CTL) = CLK_PASSWD | 6 | (1 << 4);
        usleep(10);
        while ((*REG32(clk_regs, CLK_SMI_CTL) & (1 << 7)) == 0) ;
        usleep(100);
    }
    if (smi_cs->seterr)
        smi_cs->seterr = 1;
    smi_dsr->rsetup = smi_dsw->wsetup = setup;
    smi_dsr->rstrobe = smi_dsw->wstrobe = strobe;
    smi_dsr->rhold = smi_dsw->whold = hold;
    smi_dmc->panicr = smi_dmc->panicw = 8;
    smi_dmc->reqr = smi_dmc->reqw = REQUEST_THRESH;
    smi_dsr->rwidth = smi_dsw->wwidth = width;
}

// Display SMI registers
void disp_smi(void)
{
    volatile uint32_t *p=REG32(smi_regs, SMI_CS);
    int i=0;

    while (smi_regstrs[i][0])
    {
        printf("%4s=%08X ", smi_regstrs[i++], *p++);
        if (i%8==0 || smi_regstrs[i][0]==0)
            printf("\n");
    }
}

// Get GPIO mode value into 32-bit word
void mode_word(uint32_t *wp, int n, uint32_t mode)
{
    uint32_t mask = 7 << (n * 3);

    *wp = (*wp & ~mask) | (mode << (n * 3));
}

// Convert ADC value to voltage
float val_volts(int val)
{
    return((ADC_ZERO - val) / ADC_SCALE);
}

// Return ADC value, using GPIO inputs
int adc_gpio_val(void)
{
    int v = *REG32(gpio_regs, GPIO_LEV0);

    return((v>>ADC_D0_PIN) & ((1 << ADC_NPINS)-1));
}

// Display bit values in register
void disp_reg_fields(char *regstrs, char *name, uint32_t val)
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

// EOF
