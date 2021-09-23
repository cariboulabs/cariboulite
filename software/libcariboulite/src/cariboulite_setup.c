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

    // Configure modem
    //------------------------------------------------------
    ZF_LOGD("Configuring modem initial state");
    at86rf215_set_clock_output(&sys->modem, at86rf215_drive_current_8ma, at86rf215_clock_out_freq_26mhz);
    at86rf215_setup_rf_irq(&sys->modem,  0, 1, at86rf215_drive_current_4ma);
    at86rf215_radio_set_state(&sys->modem, at86rf215_rf_channel_900mhz, at86rf215_radio_state_cmd_trx_off);
    at86rf215_radio_set_state(&sys->modem, at86rf215_rf_channel_2400mhz, at86rf215_radio_state_cmd_trx_off);

    at86rf215_radio_irq_st int_mask = {
        .wake_up_por = 1,
        .trx_ready = 1,
        .energy_detection_complete = 1,
        .battery_low = 1,
        .trx_error = 1,
        .IQ_if_sync_fail = 1,
        .res = 0,
    };
    at86rf215_radio_setup_interrupt_mask(&sys->modem, at86rf215_rf_channel_900mhz, &int_mask);
    at86rf215_radio_setup_interrupt_mask(&sys->modem, at86rf215_rf_channel_2400mhz, &int_mask);

    at86rf215_iq_interface_config_st modem_iq_config = {
        .loopback_enable = 0,
        .drv_strength = at86rf215_iq_drive_current_2ma,
        .common_mode_voltage = at86rf215_iq_common_mode_v_ieee1596_1v2,
        .tx_control_with_iq_if = false,
        .radio09_mode = at86rf215_iq_if_mode,
        .radio24_mode = at86rf215_iq_if_mode,
        .clock_skew = at86rf215_iq_clock_data_skew_1_906ns,
    };
    at86rf215_setup_iq_if(&sys->modem, &modem_iq_config);

    at86rf215_radio_external_ctrl_st ext_ctrl = {
        .ext_lna_bypass_available = 0,
        .agc_backoff = 0,
        .analog_voltage_external = 0,
        .analog_voltage_enable_in_off = 0,
        .int_power_amplifier_voltage = 2,
        .fe_pad_configuration = 1,   
    };
    at86rf215_radio_setup_external_settings(&sys->modem, at86rf215_rf_channel_900mhz, &ext_ctrl);
    at86rf215_radio_setup_external_settings(&sys->modem, at86rf215_rf_channel_2400mhz, &ext_ctrl);

    at86rf215_setup_iq_radio_receive (&sys->modem, at86rf215_rf_channel_2400mhz, 2400e6, 
                                            0, at86rf215_iq_clock_data_skew_1_906ns);

    // RFFC5072
    //------------------------------------------------------
    ZF_LOGD("INIT MIXER - RFFC5072");
    res = rffc507x_init(&sys->mixer, &sys->spi_dev);
    if (res < 0)
    {
        ZF_LOGE("Error initializing mixer 'rffc5072'");
        goto cariboulite_init_submodules_fail;
    }

    // Configure mixer
    //------------------------------------------------------


    ZF_LOGI("Cariboulite submodules successfully initialized");
    return 0;

cariboulite_init_submodules_fail:
    // release the resources
    cariboulite_release_submodules(sys);
	return -1;
}

//=======================================================================================
int cariboulite_self_test(cariboulite_st* sys, cariboulite_self_test_result_st* res)
{
    memset(res, 0, sizeof(cariboulite_self_test_result_st));

    //------------------------------------------------------
    ZF_LOGI("Testing FPGA communication and versions...");
    caribou_fpga_get_versions (&sys->fpga, &sys->fpga_versions);
    caribou_fpga_get_errors (&sys->fpga, &sys->fpga_error_status);
    ZF_LOGI("FPGA Versions: sys: %d, manu.id: %d, sys_ctrl_mod: %d, io_ctrl_mod: %d, smi_ctrl_mot: %d", 
                sys->fpga_versions.sys_ver, 
                sys->fpga_versions.sys_manu_id, 
                sys->fpga_versions.sys_ctrl_mod_ver, 
                sys->fpga_versions.io_ctrl_mod_ver, 
                sys->fpga_versions.smi_ctrl_mod_ver);
    ZF_LOGI("FPGA Errors: %02X", sys->fpga_error_status);

    if (sys->fpga_versions.sys_ver != 0x01 || sys->fpga_versions.sys_manu_id != 0x01)
    {
        ZF_LOGI("FPGA firmware didn't pass - sys_ver = %02X, manu_id = %02X", 
            sys->fpga_versions.sys_ver, sys->fpga_versions.sys_manu_id);
        res->fpga_fail = 1;
    }

    //------------------------------------------------------
    ZF_LOGI("Testing modem communication and versions");
    
    uint8_t modem_pn = 0, modem_vn = 0;
    at86rf215_get_versions(&sys->modem, &modem_pn, &modem_vn);
    if (modem_pn != 0x34)
    {
        ZF_LOGI("The assembled modem is not AT86RF215 (product number: 0x%02x)", modem_pn);
        res->modem_fail = 1;
    }

    //------------------------------------------------------
    ZF_LOGI("Testing mixer communication and versions");
    rffc507x_device_id_st dev_id;
    rffc507x_readback_status(&sys->mixer, &dev_id, NULL);
	if (dev_id.device_id != 0x1140 && dev_id.device_id != 0x11C0)
    {
        ZF_LOGI("The assembled mixer is not RFFC5071/2[A]");
        res->mixer_fail = 1;
    }

    //------------------------------------------------------
    ZF_LOGI("Testing smi communication");
    // TBD

    // check and report problems
    /*if (!memcmp(res, 0, sizeof(cariboulite_self_test_result_st)))
    {
        ZF_LOGI("Self-test process finished successfully!");
        return 0;
    }*/
    
    //ZF_LOGI("Self-test process finished with errors");
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
    at86rf215_stop_iq_radio_receive (&sys->modem, at86rf215_rf_channel_900mhz);
    at86rf215_stop_iq_radio_receive (&sys->modem, at86rf215_rf_channel_2400mhz);
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

    cariboulite_self_test_result_st self_tes_res = {0};
    if (cariboulite_self_test(sys, &self_tes_res) != 0)
    {
        cariboulite_release_io (sys);
        return -cariboulite_self_test_failed;
    }

    return cariboulite_ok;
}

//=================================================
void cariboulite_release_driver(cariboulite_st *sys)
{
    ZF_LOGI("driver being released");

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

//=================================================
#define CARIBOULITE_MIN_MIX     (30.0e6)
#define CARIBOULITE_MAX_MIX     (6000.0e6)
#define CARIBOULITE_MIN_LO      (85.0e6)
#define CARIBOULITE_MAX_LO      (4200.0e6)
#define CARIBOULITE_2G4_MIN     (2400.0e6)
#define CARIBOULITE_2G4_MAX     (2483.5e6)
#define CARIBOULITE_S1G_MIN1    (389.5e6)
#define CARIBOULITE_S1G_MAX1    (510.0e6)
#define CARIBOULITE_S1G_MIN2    (779.0e6)
#define CARIBOULITE_S1G_MAX2    (1020.0e6)

int cariboulite_setup_frequency(    cariboulite_st *sys, 
                                    cariboulite_channel_en ch, 
                                    cariboulite_channel_dir_en dir,
                                    double *freq)
{
    double f_rf = *freq;
    double modem_act_freq = 0.0;
    double lo_act_freq = 0.0;
    double act_freq = 0.0;
    int error = 0;
    int conversion_direction = 0;

    //--------------------------------------------------------------------------------
    // SUB 1GHZ CONFIGURATION
    //--------------------------------------------------------------------------------
    if (ch == cariboulite_channel_s1g)
    {
        if ( (f_rf >= CARIBOULITE_S1G_MIN1 && f_rf <= CARIBOULITE_S1G_MAX1) ||
             (f_rf >= CARIBOULITE_S1G_MIN2 && f_rf <= CARIBOULITE_S1G_MAX2)   )
        {
            // setup modem frequency <= f_rf
            at86rf215_radio_set_state(  &sys->modem, 
                                        at86rf215_rf_channel_900mhz, 
                                        at86rf215_radio_state_cmd_trx_off);
            modem_act_freq = at86rf215_setup_channel (&sys->modem, 
                                                    at86rf215_rf_channel_900mhz, 
                                                    (uint32_t)f_rf);
            at86rf215_radio_set_state(  &sys->modem, 
                                        at86rf215_rf_channel_900mhz, 
                                        at86rf215_radio_state_cmd_rx);

            // return actual frequency
            *freq = modem_act_freq;
        }
        else
        {
            // error - unsupported frequency for S1G channel
            ZF_LOGE("unsupported frequency for S1G channel - %.2f Hz", f_rf);
            error = 1;
        }
    }
    //--------------------------------------------------------------------------------
    // 30-6GHz CONFIGURATION
    //--------------------------------------------------------------------------------
    else if (ch == cariboulite_channel_6g)
    {
        at86rf215_radio_set_state(  &sys->modem, 
                                    at86rf215_rf_channel_2400mhz, 
                                    at86rf215_radio_state_cmd_trx_off);

        caribou_fpga_set_io_ctrl_mode (&sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);

        //-------------------------------------
            if ( f_rf >= CARIBOULITE_MIN_MIX && 
                f_rf <= (CARIBOULITE_2G4_MIN - CARIBOULITE_MIN_LO) )
        {
            // region #1
            // setup modem frequency <= CARIBOULITE_2G4_MIN
            modem_act_freq = (double)at86rf215_setup_channel (&sys->modem, 
                                                        at86rf215_rf_channel_2400mhz, 
                                                        (uint32_t)(CARIBOULITE_2G4_MIN));
            
            // setup mixer LO <= CARIBOULITE_2G4_MIN - f_rf
            lo_act_freq = rffc507x_set_frequency(&sys->mixer, modem_act_freq - f_rf);
            act_freq = modem_act_freq - lo_act_freq;

            // setup fpga RFFE <= upconvert (tx / rx)
            conversion_direction = 1;
        }
        //-------------------------------------
        else if ( f_rf > (CARIBOULITE_2G4_MIN - CARIBOULITE_MIN_LO) && 
                f_rf < CARIBOULITE_2G4_MIN )
        {
            // region #2
            // setup modem frequency <= f_rf + CARIBOULITE_MIN_LO
            modem_act_freq = (double)at86rf215_setup_channel (&sys->modem, 
                                                        at86rf215_rf_channel_2400mhz, 
                                                        (uint32_t)(CARIBOULITE_MIN_LO + f_rf));

            // setup mixer LO <= CARIBOULITE_MIN_LO
            lo_act_freq = rffc507x_set_frequency(&sys->mixer, modem_act_freq - f_rf);
            act_freq = modem_act_freq - lo_act_freq;

            // setup fpga RFFE <= upconvert (tx / rx)
            conversion_direction = 1;
        }
        //-------------------------------------
        else if ( f_rf >= CARIBOULITE_2G4_MIN && 
                f_rf <= CARIBOULITE_2G4_MAX )
        {
            // region #3 - bypass mode
            // setup modem frequency <= f_rf
            modem_act_freq = (double)at86rf215_setup_channel (&sys->modem, 
                                                        at86rf215_rf_channel_2400mhz, 
                                                        (uint32_t)f_rf);
            act_freq = modem_act_freq;
            conversion_direction = 2;
        }
        //-------------------------------------
        else if ( f_rf > CARIBOULITE_2G4_MAX && 
                f_rf <= (CARIBOULITE_2G4_MAX + CARIBOULITE_MIN_MIX) )
        {
            // region #4
            // setup modem frequency <= f_rf - CARIBOULITE_MIN_LO
            modem_act_freq = (double)at86rf215_setup_channel (&sys->modem, 
                                                        at86rf215_rf_channel_2400mhz, 
                                                        (uint32_t)(f_rf - CARIBOULITE_MIN_LO));

            // setup mixer LO <= CARIBOULITE_MIN_LO
            lo_act_freq = rffc507x_set_frequency(&sys->mixer, f_rf - modem_act_freq);
            act_freq = modem_act_freq + lo_act_freq;

            // setup fpga RFFE <= downconvert (tx / rx)
            conversion_direction = 3;
        }
        //-------------------------------------
        else if ( f_rf > (CARIBOULITE_2G4_MAX + CARIBOULITE_MIN_MIX) && 
                f_rf <= CARIBOULITE_MAX_MIX )
        {
            // region #5
            // here it would be beneficial to use lowest modem frequency - 2400 - TBD
            // setup modem frequency <= CARIBOULITE_2G4_MAX
            modem_act_freq = (double)at86rf215_setup_channel (&sys->modem, 
                                                        at86rf215_rf_channel_2400mhz, 
                                                        (uint32_t)(CARIBOULITE_2G4_MAX));

            // setup mixer LO <= f_rf - CARIBOULITE_2G4_MAX
            lo_act_freq = rffc507x_set_frequency(&sys->mixer, f_rf - modem_act_freq);
            act_freq = modem_act_freq + lo_act_freq;

            // setup fpga RFFE <= downconvert (tx / rx)
            conversion_direction = 3;
        }
        //-------------------------------------
        else
        {
            // error - unsupported frequency for 6G channel
            ZF_LOGE("unsupported frequency for 6GHz channel - %.2f Hz", f_rf);
            error = 1;
        }
        
        *freq = act_freq;

        rffc507x_device_status_st stat;
        // wait for the mixer to lock
        for (int i = 0; i<10; i++)
        {
            rffc507x_readback_status(&sys->mixer, NULL, &stat);
            if (stat.pll_lock) break;
            usleep(100000);
        }
        if (!stat.pll_lock)
        {
            ZF_LOGE("Mixer couldn't lock on LO = %.2f", lo_act_freq);
            rffc507x_print_stat(&stat);
            error = 3;
        }

        // Setup the frontend
        switch (conversion_direction)
        {
            case 1: if (dir == 0) caribou_fpga_set_io_ctrl_mode (&sys->fpga, 0, caribou_fpga_io_ctrl_rfm_rx_lowpass);
                    else if (dir == 1) caribou_fpga_set_io_ctrl_mode (&sys->fpga, 0, caribou_fpga_io_ctrl_rfm_tx_hipass);
                    break;
            case 2: caribou_fpga_set_io_ctrl_mode (&sys->fpga, 0, caribou_fpga_io_ctrl_rfm_bypass);
                    break;
            case 3: if (dir == 0) caribou_fpga_set_io_ctrl_mode (&sys->fpga, 0, caribou_fpga_io_ctrl_rfm_rx_hipass);
                    else if (dir == 1) caribou_fpga_set_io_ctrl_mode (&sys->fpga, 0, caribou_fpga_io_ctrl_rfm_tx_lowpass);
                    break;
            default: break;
        }

        // Activate the modem - this should be outside
        at86rf215_radio_set_state(  &sys->modem, 
                                    at86rf215_rf_channel_2400mhz, 
                                    at86rf215_radio_state_cmd_rx);
    }
    else
    {
        // unknown channel
        ZF_LOGE("The requested channel (%d) is unsupported", ch);
        error = 2;
    }

    if (error == 0)
    {
        ZF_LOGD("Frequency setting CH: %d, Wanted: %.2f Hz, Set: %.2f Hz (MOD: %.2f, MIX: %.2f)", 
                        ch, f_rf, act_freq, modem_act_freq, lo_act_freq);
    }

    return -error;
}