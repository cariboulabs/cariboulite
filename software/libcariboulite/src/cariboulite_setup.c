#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Setup"
#include "zf_log/zf_log.h"


#include <stdio.h>
#include "cariboulite_setup.h"
#include "cariboulite_events.h"
#include "cariboulite_fpga_firmware.h"

//=======================================================================================
int cariboulite_setup_io (cariboulite_st* sys, void* sighandler)
{
    ZF_LOGI("Setting up board I/Os");
    if (io_utils_setup(sighandler) < 0)
    {
        ZF_LOGE("Error setting up io_utils");
        return -1;
    }

    if (sys->reset_fpga_on_startup)
    {
        latticeice40_hard_reset(&sys->ice40, 0);
    }

    if (io_utils_spi_init(&sys->spi_dev) < 0)
    {
        ZF_LOGE("Error setting up io_utils_spi");
        io_utils_cleanup();
        return -1;
    }

    // Setup the initial states for components reset and SS
    // ICE40
    io_utils_set_gpio_mode(sys->fpga.cs_pin, io_utils_alt_gpio_out);
    io_utils_write_gpio(sys->fpga.cs_pin, 1);

    // AT86RF215
    io_utils_set_gpio_mode(sys->modem.cs_pin, io_utils_alt_gpio_out);
    io_utils_write_gpio(sys->modem.cs_pin, 1);
    io_utils_set_gpio_mode(sys->modem.reset_pin, io_utils_alt_gpio_out);
    io_utils_write_gpio(sys->modem.reset_pin, 0);
    io_utils_set_gpio_mode(sys->modem.irq_pin, io_utils_alt_gpio_in);

    // RFFC5072
    io_utils_set_gpio_mode(sys->mixer.cs_pin, io_utils_alt_gpio_out);
    io_utils_write_gpio(sys->mixer.cs_pin, 1);
    io_utils_set_gpio_mode(sys->mixer.reset_pin, io_utils_alt_gpio_out);
    io_utils_write_gpio(sys->mixer.reset_pin, 0);

    return 0;
}

//=======================================================================================
int cariboulite_release_io (cariboulite_st* sys)
{
    ZF_LOGI("Releasing board I/Os");
    io_utils_spi_close(&sys->spi_dev);
    io_utils_cleanup();
    return 0;
}

//=======================================================================================
int cariboulite_configure_fpga (cariboulite_st* sys, cariboulite_firmware_source_en src, char* fpga_bin_path)
{
    int res = 0;
    int error = 0;

    // Init FPGA programming
    res = latticeice40_init(&sys->ice40, &sys->spi_dev);
    if (res < 0)
    {
        ZF_LOGE("lattice ice40 init failed");
        return -1;
    }

    if (src == cariboulite_firmware_source_file)
    {
        ZF_LOGI("Configuring the FPGA from '%s'", fpga_bin_path);
        // push in the firmware / bitstream
        res = latticeice40_configure(&sys->ice40, fpga_bin_path);
        if (res < 0)
        {
            ZF_LOGE("lattice ice40 configuration failed");
            // do not exit the function - releasing resources is needed anyway
            error = 1;
        }
    }
    else if (src == cariboulite_firmware_source_blob)
    {
        ZF_LOGI("Configuring the FPGA a internal firmware blob");
        // push in the firmware / bitstream
        res = latticeice40_configure_from_buffer(&sys->ice40, cariboulite_firmware, sizeof(cariboulite_firmware));
        if (res < 0)
        {
            ZF_LOGE("lattice ice40 configuration failed");
            // do not exit the function - releasing resources is needed anyway
            error = 1;
        }
    }
    else
    {
        ZF_LOGE("lattice ice40 configuration source is invalid");
            // do not exit the function - releasing resources is needed anyway
            error = 1;
    }

    // release the programming specific resources
    res = latticeice40_release(&sys->ice40);
    if (res < 0)
    {
        ZF_LOGE("lattice ice40 release failed");
        return -1;
    }

    return -error;
}

//=======================================================================================
int cariboulite_init_submodules (cariboulite_st* sys)
{
    int res = 0;
    ZF_LOGI("initializing submodules");
    
    // FPGA Init
    //------------------------------------------------------
    ZF_LOGD("INIT FPGA SPI communication");
    res = caribou_fpga_init(&sys->fpga, &sys->spi_dev);
    if (res < 0)
    {
        ZF_LOGE("FPGA communication init failed");
        goto cariboulite_init_submodules_fail;
    }

    // SMI Init
    //------------------------------------------------------
    ZF_LOGD("INIT FPGA SMI communication");
    res = caribou_smi_init(&sys->smi, caribou_smi_error_event, &sys);
    if (res < 0)
    {
        ZF_LOGE("Error setting up smi submodule");
        goto cariboulite_init_submodules_fail;
    }

    // AT86RF215
    //------------------------------------------------------
    ZF_LOGD("INIT MODEM - AT86RF215");
    res = at86rf215_init(&sys->modem, &sys->spi_dev);
    if (res < 0)
    {
        ZF_LOGE("Error initializing modem 'at86rf215'");
        goto cariboulite_init_submodules_fail;
    }

    // RFFC5072
    //------------------------------------------------------
    ZF_LOGD("INIT MIXER - RFFC5072");
    res = rffc507x_init(&sys->mixer, &sys->spi_dev);
    if (res < 0)
    {
        ZF_LOGE("Error initializing mixer 'rffc5072'");
        goto cariboulite_init_submodules_fail;
    }

    ZF_LOGI("Cariboulite submodules successfully initialized");
    return 0;

cariboulite_init_submodules_fail:
    // release the resources
    cariboulite_release_submodules(sys);
	return -1;
}

//=======================================================================================
int cariboulite_self_test(cariboulite_st* sys)
{
    ZF_LOGI("Testing FPGA communication and versions...");
    // read out version information from the FPGA
    caribou_fpga_get_versions (&sys->fpga, &sys->fpga_versions);
    caribou_fpga_get_errors (&sys->fpga, &sys->fpga_error_status);
    ZF_LOGI("FPGA Versions: sys: %d, manu.id: %d, sys_ctrl_mod: %d, io_ctrl_mod: %d, smi_ctrl_mot: %d", 
                sys->fpga_versions.sys_ver, 
                sys->fpga_versions.sys_manu_id, 
                sys->fpga_versions.sys_ctrl_mod_ver, 
                sys->fpga_versions.io_ctrl_mod_ver, 
                sys->fpga_versions.smi_ctrl_mod_ver);
    ZF_LOGI("FPGA Errors: %02X", sys->fpga_error_status);

    //------------------------------------------------------

    ZF_LOGI("Self-test process finished successfully!");
    return 0;
}

//=======================================================================================
int cariboulite_release_submodules(cariboulite_st* sys)
{
    int res = 0;

    // SMI Module
    //------------------------------------------------------
    ZF_LOGD("CLOSE SMI");
    caribou_smi_close(&sys->smi);

    // AT86RF215
    //------------------------------------------------------
    ZF_LOGD("CLOSE MODEM - AT86RF215");
    at86rf215_close(&sys->modem);

    // RFFC5072
    //------------------------------------------------------
    ZF_LOGD("CLOSE MIXER - RFFC5072");
    rffc507x_release(&sys->mixer);

    // FPGA Module
    //------------------------------------------------------
    printf("CLOSE FPGA communication");
    res = caribou_fpga_close(&sys->fpga);
    if (res < 0)
    {
        ZF_LOGE("FPGA communication release failed (%d)", res);
        return -1;
    }

    return 0;
}

//=================================================
int cariboulite_init_driver(cariboulite_st *sys, void* signal_handler_cb, cariboulite_board_info_st *info)
{
    ZF_LOGI("driver initializing");
    if (info == NULL)
    {
        int detected = cariboulite_config_detect_board(&sys->board_info);
        if (!detected)
        {
            ZF_LOGE("The RPI HAT interface didn't detect any connected boards");
            return -cariboulite_board_detection_failed;
        }
    }
    else
    {
        memcpy(&sys->board_info, info, sizeof(cariboulite_board_info_st));
    }
    ZF_LOGI("Detected Board Information:");
    cariboulite_config_print_board_info(&sys->board_info);
    
    if (cariboulite_setup_io (sys, signal_handler_cb) != 0)
    {
        return -cariboulite_io_setup_failed;
    }

    if (cariboulite_configure_fpga (sys, cariboulite_firmware_source_blob, NULL/*sys->firmware_path_operational*/) != 0)
    {
        cariboulite_release_io (sys);
        return -cariboulite_fpga_configuration_failed;
    }

    if (cariboulite_init_submodules (sys) != 0)
    {
        cariboulite_release_io (sys);
        return -cariboulite_submodules_init_failed;
    }

    if (cariboulite_self_test(sys) != 0)
    {
        cariboulite_release_io (sys);
        return -cariboulite_self_test_failed;
    }

    return cariboulite_ok;
}

//=================================================
void cariboulite_release_driver(cariboulite_st *sys)
{
    ZF_LOGI("driver releasing");

    cariboulite_release_submodules(sys);
    cariboulite_release_io (sys);
}

//=================================================
int cariboulite_get_serial_number(cariboulite_st *sys, uint32_t* serial_number, int *count)
{
    if (serial_number) *serial_number = sys->board_info.numeric_serial_number;
    if (count) *count = 1;
    return 0;
}

//=================================================
void cariboulite_lib_version(cariboulite_lib_version_st* v)
{
    v->major_version = CARIBOULITE_MAJOR_VERSION;
    v->minor_version = CARIBOULITE_MINOR_VERSION;
    v->revision = CARIBOULITE_REVISION;
}