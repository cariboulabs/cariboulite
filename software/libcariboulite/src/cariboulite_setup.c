#include <stdio.h>
#include "cariboulite_setup.h"

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
};

//=======================================================================================
int cariboulite_setup_io ()
{
    if (io_utils_setup() < 0)
    {
        printf("Error setting up io_utils\n");
        return -1;
    }

    if (sys.reset_fpga_on_startup)
    {
        io_utils_set_gpio_mode(CARIBOULITE_FPGA_CRESET, io_utils_alt_gpio_out);
        io_utils_write_gpio(CARIBOULITE_FPGA_CRESET, 0);
    }

    if (io_utils_spi_init(&sys.spi_dev) < 0)
    {
        printf("Error setting up io_utils_spi\n");
        io_utils_cleanup();
        return -1;
    }

    if (caribou_smi_init(&sys.smi) < 0)
    {
        printf("Error setting up io_utils_spi\n");
        io_utils_cleanup();
        io_utils_spi_close(&sys.spi_dev);
        return -1;
    }

    return 0;
}

//=======================================================================================
int cariboulite_release_io ()
{
    caribou_smi_close(&sys.smi);
    io_utils_spi_close(&sys.spi_dev);
    io_utils_cleanup();
    return 0;
}

//=======================================================================================
int cariboulite_configure_fpga (char* fpga_bin_path)
{
    int res = 0;

    // Init FPGA programming
    res = latticeice40_init(&sys.ice40, &sys.spi_dev);
    if (res < 0)
    {
        printf("ERROR @ cariboulite_configure_fpga: lattice ice40 init failed\n");
        return -1;
    }

    // push in the firmware / bitstream
    res = latticeice40_configure(&sys.ice40, fpga_bin_path);
    if (res < 0)
    {
        printf("ERROR @ cariboulite_configure_fpga: lattice ice40 configuration failed\n");
        // do not exit the function - releasing resources is needed anyway
    }

    // release the programming specific resources
    res = latticeice40_release(&sys.ice40);
    if (res < 0)
    {
        printf("ERROR @ cariboulite_configure_fpga: lattice ice40 release failed\n");
        return -1;
    }

    return 0;
}

//=======================================================================================
int cariboulite_init_submodules ()
{
    int res = 0;
    printf("INFO @ cariboulite_init_submodules: initializing submodules.\n");

    //------------------------------------------------------
    printf("INFO @ cariboulite_init_submodules: init FPGA communication\n");
    res = caribou_fpga_init(&sys.fpga, &sys.spi_dev);
    if (res < 0)
    {
        printf("ERROR @ cariboulite_init_submodules: FPGA init failed\n");
        return -1;
    }
    // read out version information from the FPGA
    caribou_fpga_get_versions (&sys.fpga, &sys.fpga_versions);
    caribou_fpga_get_errors (&sys.fpga, &sys.fpga_error_status);

    //------------------------------------------------------

    return 0;

cariboulite_init_submodules_fail:
	printf("ERROR @ cariboulite_init_submodules\n");
	return -1;
}

//=======================================================================================
int cariboulite_release_submodules()
{
    int res = 0;

    //------------------------------------------------------
    printf("INFO @ cariboulite_release_submodules: releasing FPGA communication\n");
    res = caribou_fpga_close(&sys.fpga);
    if (res < 0)
    {
        printf("ERROR @ cariboulite_release_submodules: FPGA release failed\n");
        return -1;
    }
    //------------------------------------------------------

    return 0;

cariboulite_release_submodules_lg_fail:
	printf("ERROR @ cariboulite_release_submodules\n");
	return -1;
}