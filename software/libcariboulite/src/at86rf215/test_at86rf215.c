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

int main ()
{
    at86rf215_iq_interface_config_st cfg = {0};
    at86rf215_irq_st irq = {0};

    // Init GPIOs and set FPGA on reset
	io_utils_setup();
    io_utils_set_gpio_mode(FPGA_RESET, io_utils_alt_gpio_out);
    io_utils_set_gpio_mode(ICE40_CS, io_utils_alt_gpio_out);
    io_utils_write_gpio(FPGA_RESET, 0);
    io_utils_write_gpio(ICE40_CS, 0);

    // Init spi
	io_utils_spi_init(&io_spi_dev);

	at86rf215_init(&dev, &io_spi_dev);
    at86rf215_reset(&dev);

    uint8_t pn, vn;
	at86rf215_get_versions(&dev, &pn, &vn);
    printf("VN = %02x, PN = %02X\n", vn, pn);

    io_utils_usleep(10000);

    int f = 900000000;
    //while (f < 910000000)
    {
        printf("Frequency = %.2f MHz\n", ((float)f) / 1e6);
        at86rf215_setup_iq_radio_dac_value_override (&dev,
                                                    at86rf215_rf_channel_900mhz,
                                                    f,
                                                    31);


        //printf("Press enter to switch\n");
        io_utils_usleep(100000);
        getchar();
        f += 20000;
    }

    at86rf215_radio_set_state(&dev, at86rf215_rf_channel_900mhz, at86rf215_radio_state_cmd_trx_off);

	at86rf215_close(&dev);
	io_utils_spi_close(&io_spi_dev);
    io_utils_cleanup();

    return 0;
}
