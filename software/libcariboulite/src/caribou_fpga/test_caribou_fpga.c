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

    // get versions
    caribou_fpga_versions_st versions = {0};
    caribou_fpga_get_versions (&dev, &versions);
    printf("VERSIONS: sys: 0x%02X, manu: 0x%02X, sys_ctrl: 0x%02X, io_ctrl: 0x%02X, smi_ctrl: 0x%02X\n", 
                            versions.sys_ver,
                            versions.sys_manu_id,
                            versions.sys_ctrl_mod_ver,
                            versions.io_ctrl_mod_ver,
                            versions.smi_ctrl_mod_ver);

    // get errors
    uint8_t err_map = 0;
    caribou_fpga_get_errors (&dev, &err_map);
    printf("ERRORS: 0x%02X\n", err_map);

    // io control mode
    uint8_t debug_mode = 0;
    caribou_fpga_io_ctrl_rfm_en rfmode = 0;
    caribou_fpga_get_io_ctrl_mode (&dev, &debug_mode, &rfmode);
    printf("IO_CTRL MODE: debug = %d, rfm = %d (should be %d)\n", debug_mode, rfmode);
    
    // io_ctrl_dig
    int ldo = 0;
    int led0 = 0;
    int led1 = 0;
    int btn = 0;
    int cfg = 0;
    caribou_fpga_get_io_ctrl_dig (&dev, &ldo, &led0, &led1, &btn, &cfg);
    printf("IO_CTRL: ldo: %d, led0: %d, led1: %d, btn: %d, cfg: 0x%02X\n", ldo, led0, led1, btn, cfg);

    // pmod dir
    uint8_t dir = 0;
    caribou_fpga_get_io_ctrl_pmod_dir (&dev, &dir);
    printf("PMOD_DIR: dir = 0x%02X\n", dir);

    // pmod val
    uint8_t val = 0;
    caribou_fpga_get_io_ctrl_pmod_val (&dev, &val);
    printf("PMOD_VAL: val = 0x%02X\n", val);

    // rf state
    caribou_fpga_rf_pin_st pins = {0};
    caribou_fpga_get_io_ctrl_rf_state (&dev, &pins);
    printf("RF_PIN_STATE: val = 0x%02X\n", pins);

    printf("MODE = caribou_fpga_io_ctrl_rfm_low_power\npress enter\n");
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_low_power);
    caribou_fpga_get_io_ctrl_mode (&dev, &debug_mode, &rfmode);
    printf("IO_CTRL MODE: debug = %d, rfm = %d (should be %d)\n", debug_mode, rfmode, caribou_fpga_io_ctrl_rfm_low_power);
    printf("RF_PIN_STATE: val = 0x%02X\n", pins);

    getchar();

    printf("MODE = caribou_fpga_io_ctrl_rfm_bypass\npress enter\n");
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_bypass);
    caribou_fpga_get_io_ctrl_mode (&dev, &debug_mode, &rfmode);
    printf("IO_CTRL MODE: debug = %d, rfm = %d (should be %d)\n", debug_mode, rfmode, caribou_fpga_io_ctrl_rfm_bypass);
    printf("RF_PIN_STATE: val = 0x%02X\n", pins);

    getchar();

    printf("MODE = caribou_fpga_io_ctrl_rfm_rx_lowpass\npress enter\n");
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_rx_lowpass);
    caribou_fpga_get_io_ctrl_mode (&dev, &debug_mode, &rfmode);
    printf("IO_CTRL MODE: debug = %d, rfm = %d (should be %d)\n", debug_mode, rfmode, caribou_fpga_io_ctrl_rfm_rx_lowpass);
    printf("RF_PIN_STATE: val = 0x%02X\n", pins);

    getchar();

    printf("MODE = caribou_fpga_io_ctrl_rfm_rx_hipass\npress enter\n");
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_rx_hipass);
    caribou_fpga_get_io_ctrl_mode (&dev, &debug_mode, &rfmode);
    printf("IO_CTRL MODE: debug = %d, rfm = %d (should be %d)\n", debug_mode, rfmode, caribou_fpga_io_ctrl_rfm_rx_hipass);
    printf("RF_PIN_STATE: val = 0x%02X\n", pins);

    getchar();

    printf("MODE = caribou_fpga_io_ctrl_rfm_tx_lowpass\npress enter\n");
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_tx_lowpass);
    caribou_fpga_get_io_ctrl_mode (&dev, &debug_mode, &rfmode);
    printf("IO_CTRL MODE: debug = %d, rfm = %d (should be %d)\n", debug_mode, rfmode, caribou_fpga_io_ctrl_rfm_tx_lowpass);
    printf("RF_PIN_STATE: val = 0x%02X\n", pins);

    getchar();

    printf("MODE = caribou_fpga_io_ctrl_rfm_tx_hipass\npress enter\n");
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_tx_hipass);
    caribou_fpga_get_io_ctrl_mode (&dev, &debug_mode, &rfmode);
    printf("IO_CTRL MODE: debug = %d, rfm = %d (should be %d)\n", debug_mode, rfmode, caribou_fpga_io_ctrl_rfm_tx_hipass);
    printf("RF_PIN_STATE: val = 0x%02X\n", pins);

    getchar();
    caribou_fpga_set_io_ctrl_mode (&dev, 0, caribou_fpga_io_ctrl_rfm_low_power);
    caribou_fpga_get_io_ctrl_mode (&dev, &debug_mode, &rfmode);
    printf("IO_CTRL MODE: debug = %d, rfm = %d (should be %d)\n", debug_mode, rfmode, caribou_fpga_io_ctrl_rfm_low_power);
    printf("RF_PIN_STATE: val = 0x%02X\n", pins);

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