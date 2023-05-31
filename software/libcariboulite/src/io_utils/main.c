#include <stdio.h>
#include <signal.h>
#include "io_utils.h"
#include "io_utils_spi.h"
#include "io_utils_sys_info.h"

void terminate(int sig);

io_utils_spi_st spi_dev =
{
    .miso = 19,
	.mosi = 20,
	.sck = 21,
};

#define FPGA_RESET 24
#define ICE40_CS 18
#define FPGA_CS 18
#define MIXER_CS 16
#define MODEM_CS 17

#define SPI_DEV 1
#define SPI_CH_FPGA 0
#define SPI_CH_MODEM 1
#define SPI_CH_MIXER 2

void prepare_tx_data(uint8_t *buf, int data_len, uint8_t pre)
{
    for (int i = 0; i < data_len; i++)
    {
        buf[i] = (pre+i)&0xFF;
    }
}

// Main program
int main(int argc, char *argv[])
{
    io_utils_sys_info_st info = {0};
    io_utils_get_rpi_info(&info);

    printf("----------------------------------------------------\n");
    printf("RPI information\n");
    io_utils_print_rpi_info(&info);

    printf("----------------------------------------------------\n");
    printf("GPIO Init\n");
    io_utils_setup();
    // Reset the FPGA
    //io_utils_set_gpio_mode(FPGA_RESET, io_utils_alt_gpio_out);
    //io_utils_set_gpio_mode(ICE40_CS, io_utils_alt_gpio_out);
    //io_utils_write_gpio(FPGA_RESET, 0);
    //io_utils_write_gpio(ICE40_CS, 0);
    
    // setup the addresses
    io_utils_set_gpio_mode(2, io_utils_alt_1);  // addr
    io_utils_set_gpio_mode(3, io_utils_alt_1);  // addr
	
	// Setup the bus I/Os
	// --------------------------------------------
	for (int i = 6; i <= 15; i++)
	{
		io_utils_set_gpio_mode(i, io_utils_alt_1);  // 8xData + SWE + SOE
	}
	
	io_utils_set_gpio_mode(24, io_utils_alt_1); // rwreq
	io_utils_set_gpio_mode(25, io_utils_alt_1); // rwreq


    for (int i = 0; i < 28; i ++)
    {
        io_utils_get_gpio_mode(i, 1);
    }

    /*printf("----------------------------------------------------\n");
    printf("SPI Init\n");
    io_utils_spi_init(&spi_dev);

    int hspi_ice40 = io_utils_spi_add_chip(&spi_dev, ICE40_CS, 5000000, 1, 0, io_utils_spi_chip_ice40_prog, NULL);

    io_utils_hard_spi_st hard_dev_fpga = { .spi_dev_id = 1, .spi_dev_channel = 0, };
    int hspi_fpga = io_utils_spi_add_chip(&spi_dev, FPGA_CS, 5000000, 0, 0, io_utils_spi_chip_type_fpga_comm, &hard_dev_fpga);

    int hspi_mixer = io_utils_spi_add_chip(&spi_dev, MIXER_CS, 5000000, 0, 0, io_utils_spi_chip_type_rffc, NULL);

    io_utils_hard_spi_st hard_dev_modem = { .spi_dev_id = 1, .spi_dev_channel = 1, };
    int hspi_modem = io_utils_spi_add_chip(&spi_dev, MODEM_CS, 5000000, 0, 0, io_utils_spi_chip_type_modem, &hard_dev_modem);


    printf("\n");
    io_utils_spi_print_setup(&spi_dev);
    printf("\n");

    // Write the data
    uint8_t tx_buf[256] = {0};
    uint8_t rx_buf[256] = {0};
    int data_len = 256;
    prepare_tx_data(tx_buf, data_len, 0);

    for (int j = 0; j<100; j++)
    {
        prepare_tx_data(tx_buf, data_len, j);

        // The sending
        //io_utils_spi_transmit(&spi_dev, hspi_ice40, tx_buf, rx_buf, data_len, io_utils_spi_write);

        for (int i = 0; i < 16; i++)
        {
            io_utils_spi_transmit(&spi_dev, hspi_fpga, tx_buf+2*i, rx_buf, 2, io_utils_spi_read_write);
            //printf("FPGA: Sent %02X, %02X, Received: %02X, %02X\n", *(tx_buf+i), *(tx_buf+i+1), rx_buf[0], rx_buf[1]);
        }

        //for (int i = 0; i < 10; i++)
        //{
        //    io_utils_spi_transmit(&spi_dev, hspi_mixer, tx_buf+i, rx_buf, 3, io_utils_spi_write);
        //    io_utils_spi_transmit(&spi_dev, hspi_mixer, tx_buf+i, rx_buf, 3, io_utils_spi_read);
        //    //printf("MIXER: Sent %02X, %02X, %02X, Received: %02X, %02X\n",
        //    //                *(tx_buf+i), *(tx_buf+i+1), *(tx_buf+i+2), rx_buf[0], rx_buf[1]);
        //}

        //for (int i = 0; i < 10; i++)
        //{
        //    io_utils_spi_transmit(&spi_dev, hspi_modem, tx_buf+i, rx_buf, 2, io_utils_spi_read_write);
        //    //printf("FPGA: Sent %02X, %02X, Received: %02X, %02X\n", *(tx_buf+i), *(tx_buf+i+1), rx_buf[0], rx_buf[1]);
        //}
    }

    //for (int i = 0; i < 10; i++)
    //{
    //    io_utils_spi_transmit(&spi_dev, hspi_mixer, tx_buf+i, rx_buf, 3, io_utils_spi_read);
    //    printf("MIXER: Requested Reg %02X, Received: %02X, %02X\n",
    //                    *(tx_buf+i), rx_buf[0], rx_buf[1]);
    //}

    io_utils_spi_close(&spi_dev);*/
    io_utils_cleanup();
}

// Free memory segments and exit
void terminate(int sig)
{

}

