#include <stdio.h>
#include "rffc507x.h"
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"

#define CARIBOULITE_MOSI 20
#define CARIBOULITE_SCK 21
#define CARIBOULITE_MISO 19

#define FPGA_RESET 26
#define ICE40_CS 18
#define CARIBOULITE_MXR_SS 16
#define CARIBOULITE_MXR_RESET 5

io_utils_spi_st io_spi_dev =
{
    .miso = CARIBOULITE_MISO,
	.mosi = CARIBOULITE_MOSI,
	.sck = CARIBOULITE_SCK,
};

rffc507x_st dev =
{
    .cs_pin = CARIBOULITE_MXR_SS,
    .reset_pin = CARIBOULITE_MXR_RESET,
};

int main ()
{
	io_utils_setup();
	io_utils_set_gpio_mode(FPGA_RESET, io_utils_alt_gpio_out);
    io_utils_set_gpio_mode(ICE40_CS, io_utils_alt_gpio_out);
	io_utils_setup_gpio(CARIBOULITE_MXR_RESET, io_utils_dir_output, io_utils_pull_up);
	
    //io_utils_write_gpio(FPGA_RESET, 0);
    //io_utils_write_gpio(ICE40_CS, 0);
	
	io_utils_write_gpio(CARIBOULITE_MXR_RESET, 0);
	printf("RFFC5072 is reset, press enter to release...\n");
	getchar();
	io_utils_write_gpio(CARIBOULITE_MXR_RESET, 1);
	printf("RFFC5072 is not reset.\n");

	io_utils_spi_init(&io_spi_dev);
	rffc507x_init(&dev, &io_spi_dev);

	printf("RFFC507X Registers:\n");
	for (int i = 0; i < RFFC507X_NUM_REGS; i ++)
	{
		uint16_t reg_val = rffc507x_reg_read(&dev, i);
		printf("REG #%d => %04X\n", i, reg_val);
	}

	rffc507x_release(&dev);
	io_utils_spi_close(&io_spi_dev);
    io_utils_cleanup();

    /*rffc507x_setup();
	rffc507x_tx(0);
	rffc507x_set_frequency(500, 0);
	rffc507x_set_frequency(525, 0);
	rffc507x_set_frequency(550, 0);
	rffc507x_set_frequency(1500, 0);
	rffc507x_set_frequency(1525, 0);
	rffc507x_set_frequency(1550, 0);
	rffc507x_disable();
	rffc507x_rx(0);
	rffc507x_disable();
	rffc507x_rxtx();
	rffc507x_disable();*/
    return 0;
}
