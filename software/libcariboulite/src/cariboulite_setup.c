#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Setup"
#include "zf_log/zf_log.h"


#include <stdio.h>
#include "cariboulite_setup.h"
#include "cariboulite_events.h"

//=======================================================================================
// SYSTEM DEFINITIONS & CONFIGURATIONS
//=======================================================================================
cariboulite_st sys =
{
    //-----------------------------------------
    // Low level (chip-level) peripherals
    //-----------------------------------------
    .spi_dev =
    {
        .miso = CARIBOULITE_MISO,
        .mosi = CARIBOULITE_MOSI,
        .sck = CARIBOULITE_SCK,
        .initialized = 0,
    },

    .smi =
    {
        .initialized = 0,
    },

    .timer =
    {
        .initialized = 0,
    },

    //-----------------------------------------
    // External peripherals
    //-----------------------------------------
    .ice40 =
    {
	    .cs_pin = CARIBOULITE_FPGA_SS,
	    .cdone_pin = CARIBOULITE_FPGA_CDONE,
	    .reset_pin = CARIBOULITE_FPGA_CRESET,
        .verbose = 1,
        .initialized = 0,
    },

    .fpga =
    {
        .reset_pin = CARIBOULITE_FPGA_CRESET,
        .cs_pin = CARIBOULITE_FPGA_SS,
        .spi_dev = CARIBOULITE_SPI_DEV,
        .spi_channel = CARIBOULITE_FPGA_SPI_CHANNEL,
        .initialized = 0,
    },

    .modem =
    {
        .reset_pin = CARIBOULITE_MODEM_RESET,
        .irq_pin = CARIBOULITE_MODEM_IRQ,
        .cs_pin = CARIBOULITE_MODEM_SS,
        .spi_dev = CARIBOULITE_SPI_DEV,
        .spi_channel = CARIBOULITE_MODEM_SPI_CHANNEL,
        .initialized = 0,
    },

    .mixer =
    {
        .cs_pin = CARIBOULITE_MIXER_SS,
        .reset_pin = CARIBOULITE_MIXER_RESET,
        .initialized = 0,
    },

    //-----------------------------------------
    // Configurations
    //-----------------------------------------
    .reset_fpga_on_startup = 1,
    .firmware_path_operational = "",
    .firmware_path_testing = "",
};

//=======================================================================================
int cariboulite_setup_io ()
{
    ZF_LOGI("Setting up board I/Os");
    if (io_utils_setup() < 0)
    {
        ZF_LOGE("Error setting up io_utils");
        return -1;
    }

    if (sys.reset_fpga_on_startup)
    {
        latticeice40_hard_reset(&sys.ice40, 0);
    }

    if (io_utils_spi_init(&sys.spi_dev) < 0)
    {
        ZF_LOGE("Error setting up io_utils_spi");
        io_utils_cleanup();
        return -1;
    }

    // Setup the initial states for components reset and SS
    // ICE40
    io_utils_set_gpio_mode(CARIBOULITE_FPGA_SS, io_utils_alt_gpio_out);
    io_utils_write_gpio(CARIBOULITE_FPGA_SS, 1);

    // AT86RF215
    io_utils_set_gpio_mode(CARIBOULITE_MODEM_SS, io_utils_alt_gpio_out);
    io_utils_write_gpio(CARIBOULITE_MODEM_SS, 1);
    io_utils_set_gpio_mode(CARIBOULITE_MODEM_RESET, io_utils_alt_gpio_out);
    io_utils_write_gpio(CARIBOULITE_MODEM_RESET, 0);
    io_utils_set_gpio_mode(CARIBOULITE_MODEM_IRQ, io_utils_alt_gpio_in);

    // RFFC5072
    io_utils_set_gpio_mode(CARIBOULITE_MIXER_SS, io_utils_alt_gpio_out);
    io_utils_write_gpio(CARIBOULITE_MIXER_SS, 1);
    io_utils_set_gpio_mode(CARIBOULITE_MIXER_RESET, io_utils_alt_gpio_out);
    io_utils_write_gpio(CARIBOULITE_MIXER_RESET, 0);

    return 0;
}

//=======================================================================================
int cariboulite_release_io ()
{
    ZF_LOGI("Releasing board I/Os");
    io_utils_spi_close(&sys.spi_dev);
    io_utils_cleanup();
    return 0;
}

//=======================================================================================
int cariboulite_configure_fpga (char* fpga_bin_path)
{
    int res = 0;

    ZF_LOGI("Configuring the FPGA from '%s'", fpga_bin_path);
    
    // Init FPGA programming
    res = latticeice40_init(&sys.ice40, &sys.spi_dev);
    if (res < 0)
    {
        ZF_LOGE("lattice ice40 init failed");
        return -1;
    }

    // push in the firmware / bitstream
    res = latticeice40_configure(&sys.ice40, fpga_bin_path);
    if (res < 0)
    {
        ZF_LOGE("lattice ice40 configuration failed");
        // do not exit the function - releasing resources is needed anyway
    }

    // release the programming specific resources
    res = latticeice40_release(&sys.ice40);
    if (res < 0)
    {
        ZF_LOGE("lattice ice40 release failed");
        return -1;
    }

    return 0;
}

//=======================================================================================
int cariboulite_init_submodules ()
{
    int res = 0;
    ZF_LOGI("initializing submodules");
    
    // FPGA Init
    //------------------------------------------------------
    ZF_LOGD("INIT FPGA SPI communication");
    res = caribou_fpga_init(&sys.fpga, &sys.spi_dev);
    if (res < 0)
    {
        ZF_LOGE("FPGA communication init failed");
        goto cariboulite_init_submodules_fail;
    }

    // SMI Init
    //------------------------------------------------------
    ZF_LOGD("INIT FPGA SMI communication");
    res = caribou_smi_init(&sys.smi, caribou_smi_error_event, &sys);
    if (res < 0)
    {
        ZF_LOGE("Error setting up smi submodule");
        goto cariboulite_init_submodules_fail;
    }

    // AT86RF215
    //------------------------------------------------------
    ZF_LOGD("INIT MODEM - AT86RF215");
    res = at86rf215_init(&sys.modem, &sys.spi_dev);
    if (res < 0)
    {
        ZF_LOGE("Error initializing modem 'at86rf215'");
        goto cariboulite_init_submodules_fail;
    }

    // RFFC5072
    //------------------------------------------------------
    ZF_LOGD("INIT MIXER - RFFC5072");
    res = rffc507x_init(&sys.mixer, &sys.spi_dev);
    if (res < 0)
    {
        ZF_LOGE("Error initializing mixer 'rffc5072'");
        goto cariboulite_init_submodules_fail;
    }

    ZF_LOGI("Cariboulite submodules successfully initialized");
    return 0;

cariboulite_init_submodules_fail:
    // release the resources
    cariboulite_release_submodules();
	return -1;
}

//=======================================================================================
int cariboulite_self_test()
{
    ZF_LOGI("Testing FPGA communication and versions...");
    // read out version information from the FPGA
    caribou_fpga_get_versions (&sys.fpga, &sys.fpga_versions);
    caribou_fpga_get_errors (&sys.fpga, &sys.fpga_error_status);
    
    //------------------------------------------------------
}

//=======================================================================================
int cariboulite_release_submodules()
{
    int res = 0;

    // SMI Module
    //------------------------------------------------------
    ZF_LOGD("CLOSE SMI");
    caribou_smi_close(&sys.smi);

    // AT86RF215
    //------------------------------------------------------
    ZF_LOGD("CLOSE MODEM - AT86RF215");
    at86rf215_close(&sys.modem);

    // RFFC5072
    //------------------------------------------------------
    ZF_LOGD("CLOSE MIXER - RFFC5072");
    rffc507x_release(&sys.mixer);

    // FPGA Module
    //------------------------------------------------------
    printf("CLOSE FPGA communication");
    res = caribou_fpga_close(&sys.fpga);
    if (res < 0)
    {
        ZF_LOGE("FPGA communication release failed (%d)", res);
        return -1;
    }

    return 0;
}