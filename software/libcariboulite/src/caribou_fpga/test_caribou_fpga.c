#include <stdio.h>
#include "caribou_fpga.h"

#define CARIBOULITE_MOSI 20
#define CARIBOULITE_SCK 21
#define CARIBOULITE_MISO 19

#define FPGA_RESET 24
#define FPGA_CS 18
#define FPGA_IRQ 5  // originally SA0

#define CARIBOULITE_SPI_DEV 1
#define CARIBOULITE_FPGA_SPI_CHANNEL 0

io_utils_spi_st io_spi_dev =
{
    .miso = CARIBOULITE_MISO,
	.mosi = CARIBOULITE_MOSI,
	.sck = CARIBOULITE_SCK,
};

caribou_fpga_st dev =
{
    .reset_pin = FPGA_RESET,
	.irq_pin = FPGA_IRQ,
    .cs_pin = FPGA_CS,
    .spi_dev = CARIBOULITE_SPI_DEV,
    .spi_channel = CARIBOULITE_FPGA_SPI_CHANNEL,
};

int main ()
{
    printf("Hello from CaribouFPGA!\n");

    // Init GPIOs and set FPGA on reset
	io_utils_setup();

    // Init spi
	io_utils_spi_init(&io_spi_dev);

    // init fpga comm
    caribou_fpga_init(&dev, &io_spi_dev);

    printf("MODE = caribou_fpga_io_ctrl_rfm_low_power\npress enter\n");
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_low_power);

    getchar();

    printf("MODE = caribou_fpga_io_ctrl_rfm_bypass\npress enter\n");
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_bypass);

    getchar();

    printf("MODE = caribou_fpga_io_ctrl_rfm_rx_lowpass\npress enter\n");
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_rx_lowpass);

    getchar();

    printf("MODE = caribou_fpga_io_ctrl_rfm_rx_hipass\npress enter\n");
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_rx_hipass);

    getchar();

    printf("MODE = caribou_fpga_io_ctrl_rfm_tx_lowpass\npress enter\n");
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_tx_lowpass);

    getchar();

    printf("MODE = caribou_fpga_io_ctrl_rfm_tx_hipass\npress enter\n");
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_tx_hipass);

    getchar();
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_low_power);
    // read out stuff
    /*caribou_fpga_versions_st vers = {0};
    uint8_t err_map = 0;

    int error_count = 0;
    int error_count1 = 0;
    for (int i = 0; i < 1000; i++)
    {
        caribou_fpga_get_versions (&dev, &vers);

        if (vers.sys_ver != 0x01 || vers.sys_manu_id != 0x01 ||
            vers.sys_ctrl_mod_ver != 0x01 || vers.io_ctrl_mod_ver != 0x01 ||
            vers.smi_ctrl_mod_ver != 0x01)
            error_count ++;

        uint8_t val = 0;
        caribou_fpga_set_io_ctrl_pmod_val (&dev, i&0xFF);
        caribou_fpga_get_io_ctrl_pmod_val (&dev, &val);
        if (val != (i&0xFF))
        {
            error_count1 ++ ;
        }
        //printf("Versions: %d, %d, %d, %d, %d\n", vers.sys_ver, vers.sys_manu_id,
        //                         vers.sys_ctrl_mod_ver, vers.io_ctrl_mod_ver, vers.smi_ctrl_mod_ver);
    }

    printf("Errors # = %d, error_count1 = %d\n", error_count, error_count1);

    //caribou_fpga_get_errors (&dev, &err_map);
    //printf("Received Error map: %02X\n", err_map);

    // settings
    for (int i = 0; i < 5; i++)
    {
        caribou_fpga_set_io_ctrl_dig (&dev, 1, 1, 1);
        caribou_fpga_set_io_ctrl_pmod_val (&dev, 0xAA);
        io_utils_usleep(500000);
        caribou_fpga_set_io_ctrl_dig (&dev, 0, 0, 0);
        caribou_fpga_set_io_ctrl_pmod_val (&dev, 0x55);
        io_utils_usleep(500000);
    }
    caribou_fpga_set_io_ctrl_dig (&dev, 1, 0, 1);
    //caribou_fpga_set_io_ctrl_pmod_dir (&dev, 0x55);
    caribou_fpga_set_io_ctrl_pmod_val (&dev, 0xAA);

    uint8_t val = 0;
    caribou_fpga_get_io_ctrl_pmod_val (&dev, &val);
    printf("PMOD VAL: %02X\n", val);*/



    // close everything
    caribou_fpga_close(&dev);
    io_utils_spi_close(&io_spi_dev);
    io_utils_cleanup();

    return 0;
}