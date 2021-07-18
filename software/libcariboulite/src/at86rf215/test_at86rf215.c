#include <stdio.h>
#include "at86rf215.h"
#include "at86rf215_radio.h"
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"

#define CARIBOULITE_MOSI 20
#define CARIBOULITE_SCK 21
#define CARIBOULITE_MISO 19

#define FPGA_RESET 24
#define ICE40_CS 18
#define CARIBOULITE_SPI_DEV 1
#define CARIBOULITE_MODEM_SPI_CHANNEL 1

#define CARIBOULITE_MODEM_SS 17
#define CARIBOULITE_MODEM_RESET 23
#define CARIBOULITE_MODEM_IRQ 22


io_utils_spi_st io_spi_dev =
{
    .miso = CARIBOULITE_MISO,
	.mosi = CARIBOULITE_MOSI,
	.sck = CARIBOULITE_SCK,
};

at86rf215_st dev =
{
    .reset_pin = CARIBOULITE_MODEM_RESET,
	.irq_pin = CARIBOULITE_MODEM_IRQ,
    .cs_pin = CARIBOULITE_MODEM_SS,
    .spi_dev = CARIBOULITE_SPI_DEV,
    .spi_channel = CARIBOULITE_MODEM_SPI_CHANNEL,
};

// -----------------------------------------------------------------------------------------
// This test checks the version number and part numbers of the device
int test_at86rf215_read_chip_vn_pn(at86rf215_st* dev)
{
    int pass = 0;
    uint8_t pn, vn;
    char pn_st[15] = {0};
	at86rf215_get_versions(dev, &pn, &vn);

    if (pn==0x34 && vn > 0 && vn < 5) pass = 1;
    if (pn==0x34)
        sprintf(pn_st, "AT86RF215");
    else if (pn==0x35)
        sprintf(pn_st, "AT86RF215IQ");
    else if (pn==0x36)
        sprintf(pn_st, "AT86RF215M");
    else
        sprintf(pn_st, "UNKNOWN");

    printf("TEST:AT86RF215:VERSIONS:PN=0x%02X\n", pn);
    printf("TEST:AT86RF215:VERSIONS:VN=%d\n", vn);
    printf("TEST:AT86RF215:VERSIONS:PASS=%d\n", pass);
    printf("TEST:AT86RF215:VERSIONS:INFO=The component PN is %s (0x%02X), Version %d\n", pn_st, pn, vn);

    return pass;
}

// -----------------------------------------------------------------------------------------
// This test performs a frequency sweep on a certain channel:
//      at86rf215_rf_channel_900mhz / at86rf215_rf_channel_2400mhz
// and does it from "start_freq" to "start_freq + num_freq * step_freq"
// usec_gaps - specifies the micro-second gaps between freq steps or '-1' that
//             tell the function to put "getchars" (wait for enter key)
void test_at86rf215_sweep_frequencies(at86rf215_st* dev,
                                        at86rf215_rf_channel_en channel, 
                                        int start_freq,
                                        int num_freq,
                                        int step_freq,
                                        int usec_gaps)
{
    char channel_name[10];

    if (channel == at86rf215_rf_channel_900mhz)
        sprintf(channel_name, "900MHz");
    else sprintf(channel_name, "2400MHz");

    io_utils_usleep(usec_gaps);
    int stop_freq = start_freq + step_freq*num_freq;
    int f = start_freq;
    while (f <= stop_freq)
    {
        printf("TEST:AT86RF215:FREQ_SWEEP:FREQ=%.2fMHz, CH=%s)\n", ((float)f) / 1e6, channel_name);
        at86rf215_setup_iq_radio_dac_value_override (dev,
                                                    channel,
                                                    f,
                                                    31);


        //printf("Press enter to switch\n");
        if (usec_gaps > 0) io_utils_usleep(usec_gaps);
        else getchar();
        f += step_freq;
    }

    // back to TX off state
    at86rf215_radio_set_state(dev, at86rf215_rf_channel_900mhz, at86rf215_radio_state_cmd_trx_off);
}


#define TEST_VERSIONS       1
#define TEST_FREQ_SWEEP     0

int main ()
{
    at86rf215_iq_interface_config_st cfg = {0};
    at86rf215_irq_st irq = {0};

    // Init GPIOs and set FPGA on reset
	io_utils_setup();

    #ifdef NO_FPGA_MODE
    // When the FPGA wakes up non-programmed, it starts interogating
    // the SPI interface for SPI-Flash hosted firmware. This process
    // interfered with other bus users. Thus in the case that the FPGA
    // is not progreammed, we need it to be in RESET state.
    // As soon as the FPGA is programmed, it behaves and doesn't interfere
    // anymore with other ICs so this code is not anymore relevant.
        io_utils_set_gpio_mode(FPGA_RESET, io_utils_alt_gpio_out);
        io_utils_set_gpio_mode(ICE40_CS, io_utils_alt_gpio_out);
        io_utils_write_gpio(FPGA_RESET, 0);
        io_utils_write_gpio(ICE40_CS, 0);
    #endif

    // Init spi
	io_utils_spi_init(&io_spi_dev);

	at86rf215_init(&dev, &io_spi_dev);
    at86rf215_reset(&dev);

    // TEST: read the p/n and v/n from the IC
    #if TEST_VERSIONS
        test_at86rf215_read_chip_vn_pn(&dev);
    #endif

    // TEST: Frequency sweep
    #if TEST_FREQ_SWEEP
        test_at86rf215_sweep_frequencies(&dev, at86rf215_rf_channel_900mhz, 900e6, 1, 0, 10000);
    #endif

	at86rf215_close(&dev);
	io_utils_spi_close(&io_spi_dev);
    io_utils_cleanup();

    return 0;
}
