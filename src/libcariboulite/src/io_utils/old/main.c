#include <stdio.h>
#include <signal.h>
#include <lgpio.h>
#include "io_utils.h"
#include "io_utils_spi.h"
#include "io_utils_sys_info.h"

#define GPIO_PIN 10

void *virt_gpio_regs;
void terminate(int sig);

io_utils_spi_st spi_dev =
{
    .miso = 19,
	.mosi = 20,
	.sck = 21,
};

#define ICE40_CS 18
#define FPGA_CS 18
#define MIXER_CS 16
#define MODEM_CS 17

#define SPI_DEV 1
#define SPI_CH_FPGA 0
#define SPI_CH_MODEM 1
#define SPI_CH_MIXER 2

int setup_lgpio_spi(int *fpga, int *modem, int *mixer)
{
    *fpga = lgSpiOpen(SPI_DEV, SPI_CH_FPGA, 5000000, 0);
    if (*fpga < 0)
    {
        printf("fpga spi device-channel opening failed\n");
        return -1;
    }

    *modem = lgSpiOpen(SPI_DEV, SPI_CH_MODEM, 5000000, 0);
    if (*modem < 0)
    {
        printf("modem spi device-channel opening failed\n");
        lgSpiClose(*fpga);
        return -1;
    }

    // Open the spi dev-channel
    *mixer = lgSpiOpen(SPI_DEV, SPI_CH_MIXER, 5000000, 0);
    if (*mixer < 0)
    {
        printf("mixer spi device-channel opening failed\n");
        lgSpiClose(*modem);
        lgSpiClose(*fpga);
        return -1;
    }
    return 0;
}

void close_lgpio_spi(int fpga, int modem, int mixer)
{
    lgSpiClose(fpga);
    lgSpiClose(modem);
    lgSpiClose(mixer);
}

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
    int lgpio_spi_fpga_handle = 0;
    int lgpio_spi_modem_handle = 0;
    int lgpio_spi_mixer_handle = 0;

    io_utils_sys_info_st info = {0};
    io_utils_get_rpi_info(&info);
    io_utils_print_rpi_info(&info);

    io_utils_setup();
    /*for (int i = 0; i < 28; i ++)
    {
        io_utils_get_gpio_mode(&gpio, i, 1);
    }*/

    io_utils_spi_init(&spi_dev);

    if (setup_lgpio_spi(&lgpio_spi_fpga_handle, &lgpio_spi_modem_handle, &lgpio_spi_mixer_handle) < 0)
    {
        printf("Error opening lgpio spi\n");
        io_utils_spi_close(&spi_dev);
        io_utils_cleanup();
        return -1;
    }

    // add ice40
    int hspi_ice40 = io_utils_spi_add_chip(&spi_dev, ICE40_CS, 5000000, 1, 0,
                                        io_utils_spi_chip_ice40_prog,
                                        io_utils_spi_chip_transfer_type_poll,
							            lgpio_spi_fpga_handle);

    int hspi_fpga = io_utils_spi_add_chip(&spi_dev, FPGA_CS, 5000000, 0, 0,
                                        io_utils_spi_chip_type_fpga_comm,
                                        io_utils_spi_chip_transfer_type_int,
							            lgpio_spi_fpga_handle);

    int hspi_mixer = io_utils_spi_add_chip(&spi_dev, MIXER_CS, 5000000, 0, 0,
                                        io_utils_spi_chip_type_rffc,
                                        io_utils_spi_chip_transfer_type_poll,
							            lgpio_spi_mixer_handle);

    int hspi_modem = io_utils_spi_add_chip(&spi_dev, MODEM_CS, 5000000, 0, 0,
                                        io_utils_spi_chip_type_modem,
                                        io_utils_spi_chip_transfer_type_int,
							            lgpio_spi_modem_handle);


    printf("\n");
    io_utils_spi_print_setup(&spi_dev);
    printf("\n");

    // Write the data
    uint8_t tx_buf[256] = {0};
    uint8_t rx_buf[256] = {0};
    int data_len = 256;
    prepare_tx_data(tx_buf, data_len, 0);

    /*for (int j = 0; j<10000; j++)
    {
        prepare_tx_data(tx_buf, data_len, j);

        // The sending
        io_utils_spi_transmit(&spi_dev, hspi_ice40, tx_buf, rx_buf, data_len, io_utils_spi_write);

        for (int i = 0; i < 10; i++)
        {
            io_utils_spi_transmit(&spi_dev, hspi_fpga, tx_buf+i, rx_buf, 2, io_utils_spi_read_write);
            //printf("FPGA: Sent %02X, %02X, Received: %02X, %02X\n", *(tx_buf+i), *(tx_buf+i+1), rx_buf[0], rx_buf[1]);
        }

        for (int i = 0; i < 10; i++)
        {
            io_utils_spi_transmit(&spi_dev, hspi_mixer, tx_buf+i, rx_buf, 3, io_utils_spi_write);
            io_utils_spi_transmit(&spi_dev, hspi_mixer, tx_buf+i, rx_buf, 3, io_utils_spi_read);
            //printf("MIXER: Sent %02X, %02X, %02X, Received: %02X, %02X\n",
            //                *(tx_buf+i), *(tx_buf+i+1), *(tx_buf+i+2), rx_buf[0], rx_buf[1]);
        }

        for (int i = 0; i < 10; i++)
        {
            io_utils_spi_transmit(&spi_dev, hspi_modem, tx_buf+i, rx_buf, 2, io_utils_spi_read_write);
            //printf("FPGA: Sent %02X, %02X, Received: %02X, %02X\n", *(tx_buf+i), *(tx_buf+i+1), rx_buf[0], rx_buf[1]);
        }
    }*/

    for (int i = 0; i < 10; i++)
    {
        //io_utils_spi_transmit(&spi_dev, hspi_mixer, tx_buf+i, rx_buf, 3, io_utils_spi_write);
        io_utils_spi_transmit(&spi_dev, hspi_mixer, tx_buf+i, rx_buf, 3, io_utils_spi_read);
        printf("MIXER: Requested Reg %02X, Received: %02X, %02X\n",
                        *(tx_buf+i), rx_buf[0], rx_buf[1]);
    }

    close_lgpio_spi(lgpio_spi_fpga_handle, lgpio_spi_modem_handle, lgpio_spi_mixer_handle);

    io_utils_spi_close(&spi_dev);

    io_utils_cleanup();
}

// Free memory segments and exit
void terminate(int sig)
{

}

