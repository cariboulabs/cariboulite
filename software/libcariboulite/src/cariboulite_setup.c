#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Setup"
#include "zf_log/zf_log.h"


#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#include "cariboulite_setup.h"
#include "cariboulite_events.h"
#include "cariboulite_fpga_firmware.h"


// Global system object for signals
sys_st* sigsys = NULL;

//=================================================================
void print_siginfo(siginfo_t *si)
{
    printf("Signal [%d] caught, with the following information: \n", si->si_signo);
    printf("   signal errno = %d\n", si->si_errno);
    printf("   signal process pid = %d\n", si->si_pid);
    printf("   signal process uid = %d\n", (int)si->si_uid);
    printf("   signal status = %d\n", si->si_status);
    switch (si->si_code)
    {
        case SI_ASYNCIO: printf("   signal errno / async IO completed\n"); break;
        case SI_KERNEL: printf("   signal errno / signal raised by kernel\n"); break;
        case SI_MESGQ: printf("   signal errno / state change of POSIX message queue\n"); break;
        case SI_QUEUE: printf("   signal errno / sigqueue sent a message\n"); break;
        case SI_TIMER: printf("   signal errno / POSIX timer expiration\n"); break;
        case SI_TKILL: printf("   signal errno / tkill / tgkill sent the signal\n"); break;
        case SI_SIGIO: printf("   signal errno / queueing of SIGIO\n"); break;
        case SI_USER: printf("   signal errno / send by user kill() or raise()\n"); break;
        default: break;
    }
    if (si->si_signo == SIGBUS)
        switch (si->si_code)
        {
            case BUS_ADRALN: printf("   signal errno / SIGNBUS / alignment error\n"); break;
            case BUS_ADRERR: printf("   signal errno / SIGNBUS / invalid physical address error\n");break;
            case BUS_OBJERR: printf("   signal errno / SIGNBUS / general hardware arror\n"); break;
            default: break;
        }
    
    if (si->si_signo == SIGCHLD)
        switch (si->si_code)
        {    
            case CLD_CONTINUED: printf("   signal errno / SIGCHLD / child stopped but then resumed\n"); break;
            case CLD_DUMPED: printf("   signal errno / SIGCHLD / child terminated abnormally\n"); break;
            case CLD_EXITED: printf("   signal errno / SIGCHLD / child terminated normally by exit()\n"); break;
            case CLD_KILLED: printf("   signal errno / SIGCHLD / child was killed\n"); break;
            case CLD_STOPPED: printf("   signal errno / SIGCHLD / child stopped\n"); break;
            case CLD_TRAPPED: printf("   signal errno / SIGCHLD / child hit a trap\n"); break;
            default: break;
        }
    
    if (si->si_signo == SIGFPE)
        switch (si->si_code)
        {       
            case FPE_FLTDIV: printf("   signal errno / SIGFPE / division by zero\n"); break;
            case FPE_FLTOVF: printf("   signal errno / SIGFPE / float operation resulted in an overflow\n"); break;
            case FPE_FLTINV: printf("   signal errno / SIGFPE / invalid floating operation\n"); break;
            case FPE_FLTRES: printf("   signal errno / SIGFPE / float operation with invalid or inaxect result\n"); break;
            case FPE_FLTSUB: printf("   signal errno / SIGFPE / floating operation resulted in an out of range subscript\n"); break;
            case FPE_FLTUND: printf("   signal errno / SIGFPE / float operation resulted in underflow\n"); break;
            case FPE_INTDIV: printf("   signal errno / SIGFPE / integer operation - division by zero\n"); break;
            case FPE_INTOVF: printf("   signal errno / SIGFPE / integet operation resulted in overflow\n"); break;
            default: break;
        }
    
    if (si->si_signo == SIGILL)
        switch (si->si_code)
        {        
            case ILL_ILLADR: printf("   signal errno / SIGILL / process attemped to enter an illegal addressing mode\n"); break;
            case ILL_ILLOPC: printf("   signal errno / SIGILL / process attemped to execute illegal opcode\n"); break;
            case ILL_ILLOPN: printf("   signal errno / SIGILL / process attemped to execute illegal operand\n"); break;
            case ILL_PRVOPC: printf("   signal errno / SIGILL / process attemped to execute privileged opcode\n"); break;
            case ILL_PRVREG: printf("   signal errno / SIGILL / process attemped to execute privileged register\n"); break;
            case ILL_ILLTRP: printf("   signal errno / SIGILL / process attemped to enter illegal trap\n"); break;
            default: break;
        }
    
    if (si->si_signo == SIGPOLL)
        switch (si->si_code)
        {        
            case POLL_ERR: printf("   signal errno / SIGPOLL / poll i/o error occured\n"); break;
            case POLL_HUP: printf("   signal errno / SIGPOLL / the device hungup othe socked disconnecte\n"); break;
            case POLL_IN: printf("   signal errno / SIGPOLL / poll - device available for read\n"); break;
            case POLL_MSG: printf("   signal errno / SIGPOLL / poll - message is available\n"); break;
            case POLL_OUT: printf("   signal errno / SIGPOLL / poll - device available for writing\n"); break;
            case POLL_PRI: printf("   signal errno / SIGPOLL / poll - high priority data available for read\n"); break;
            default: break;
        }
    
    if (si->si_signo == SIGSEGV)
        switch (si->si_code)
        {        
            case SEGV_ACCERR: printf("   signal errno / SIGSEGV / the process access a valid region of memory in an invalid way - violated memory access permissions\n"); break;
            case SEGV_MAPERR: printf("   signal errno / SIGSEGV / the process access invalid region of memory\n"); break;
            default: break;
        }
    
    /*if (si->si_signo == SIGTRAP)
    switch (si->si_code)
    {        
        case TRAP_BRKPT: printf("   signal errno / SIGTRAP / the process hit a breakpoint\n"); break;
        case TRAP_TRACE: printf("   signal errno / SIGTRAP / the process hit a trace trap\n"); break;
        default: printf("   signal errno unknown!\n"); break;
    }*/

}

//=======================================================================================
void cariboulite_sigaction_basehandler (int signo,
                                        siginfo_t *si,
                                        void *ucontext)
{
    int run_first = 0;
    int run_last = 0;

	// store the errno
	int internal_errno = errno;

    if (sigsys->signal_cb)
    {
        switch(sigsys->sig_op)
        {
            case signal_handler_op_last: run_last = 1; break;
            case signal_handler_op_first: run_first = 1; break;
            case signal_handler_op_override:
            default:
                sigsys->signal_cb(sigsys, sigsys->singal_cb_context, signo, si);
                return;
        }
    }

    if (run_first)
    {
        sigsys->signal_cb(sigsys, sigsys->singal_cb_context, signo, si);
    }

    // The default operation
    pid_t sender_pid = si->si_pid;
    printf("CaribouLite: Signal [%d] received from pid=[%d]\n", signo, (int)sender_pid);
    print_siginfo(si);
    switch (signo)
    {
    case SIGHUP: printf("SIGHUP: Terminal went away!\n"); cariboulite_release_driver(sigsys); break;
    case SIGINT: printf("SIGINT: interruption\n"); cariboulite_release_driver(sigsys); break;
    case SIGQUIT: printf("SIGQUIT: user generated quit char\n"); cariboulite_release_driver(sigsys); break;
    case SIGILL: printf("SIGILL: process tried to execute illegal instruction\n"); cariboulite_release_driver(sigsys); break;
    case SIGABRT: printf("SIGABRT: sent by abort() command\n"); cariboulite_release_driver(sigsys); break;
    case SIGBUS: printf("SIGBUS: hardware alignment error\n"); cariboulite_release_driver(sigsys); break;
    case SIGFPE: printf("SIGFPE: arithmetic exception\n"); cariboulite_release_driver(sigsys); break;
    case SIGKILL: printf("SIGKILL: upcatchable process termination\n"); cariboulite_release_driver(sigsys); break;
    case SIGSEGV: printf("SIGSEGV: memory access violation\n"); cariboulite_release_driver(sigsys); break;
    case SIGTERM: printf("SIGTERM: process termination\n"); cariboulite_release_driver(sigsys); break;
    default: break;
    }

	//getchar();

    if (run_last)
    {
        sigsys->signal_cb(sigsys, sigsys->singal_cb_context, signo, si);
    }

	errno = internal_errno;
	exit(0);
}

//=================================================
static int cariboulite_setup_signals(sys_st *sys)
{
	cariboulite_setup_signal_handler (sys, NULL, signal_handler_op_last, NULL);
	int signals[] = {SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV, SIGTERM};
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigsys = sys;
    sa.sa_sigaction = cariboulite_sigaction_basehandler;
    sa.sa_flags |= SA_RESTART | SA_SIGINFO;
	
	int nsigs = sizeof(signals)/sizeof(signals[0]);
	
	for (int i = 0; i < nsigs; i++)
    {
        if(sigaction(signals[i], &sa, NULL) != 0)
        {
            ZF_LOGE("error sigaction() [%d] signal registration", signals[i]);
            return -cariboulite_signal_registration_failed;
        }
    }
	return 0;
}

//=======================================================================================
int cariboulite_setup_io (sys_st* sys)
{
    ZF_LOGD("Setting up board I/Os");
    if (io_utils_setup() < 0)
    {
        ZF_LOGE("Error setting up io_utils");
        return -1;
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
int cariboulite_release_io (sys_st* sys)
{
    ZF_LOGD("Releasing board I/Os - closing SPI");
    io_utils_spi_close(&sys->spi_dev);

    ZF_LOGD("Releasing board I/Os - io_utils_cleanup");
    io_utils_cleanup();
    return 0;
}

//=======================================================================================
int cariboulite_configure_fpga (sys_st* sys, cariboulite_firmware_source_en src, char* fpga_bin_path)
{
    int ret = 0;
 	switch (src)
	{
		case cariboulite_firmware_source_file:
			ret = caribou_fpga_program_to_fpga_from_file(&sys->fpga, fpga_bin_path, sys->force_fpga_reprogramming);
		break;

		case cariboulite_firmware_source_blob:
			ret = caribou_fpga_program_to_fpga(&sys->fpga, cariboulite_firmware, sizeof(cariboulite_firmware), sys->force_fpga_reprogramming);
		break;

		default:
			ZF_LOGE("lattice ice40 configuration source is invalid");
			ret = -1;
		break;
	}

	caribou_smi_setup_ios(&sys->smi);
	return ret;
}

//=======================================================================================
int cariboulite_init_submodules (sys_st* sys)
{
    int res = 0;
    ZF_LOGD("initializing submodules");

    // SMI Init
    //------------------------------------------------------
    ZF_LOGD("INIT FPGA SMI communication");
    res = caribou_smi_init(&sys->smi, &sys);
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
    at86rf215_setup_rf_irq(&sys->modem,  0, 1, at86rf215_drive_current_2ma);
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
        .drv_strength = at86rf215_iq_drive_current_4ma,
        .common_mode_voltage = at86rf215_iq_common_mode_v_ieee1596_1v2,
        .tx_control_with_iq_if = false,
        .radio09_mode = at86rf215_iq_if_mode,
        .radio24_mode = at86rf215_iq_if_mode,
        .clock_skew = at86rf215_iq_clock_data_skew_4_906ns,
    };
    at86rf215_setup_iq_if(&sys->modem, &modem_iq_config);

    at86rf215_radio_external_ctrl_st ext_ctrl = {
        .ext_lna_bypass_available = 0,
        .agc_backoff = 0,
        .analog_voltage_external = 0,
        .analog_voltage_enable_in_off = 1,
        .int_power_amplifier_voltage = 2,
        .fe_pad_configuration = 1,   
    };
    at86rf215_radio_setup_external_settings(&sys->modem, at86rf215_rf_channel_900mhz, &ext_ctrl);
    at86rf215_radio_setup_external_settings(&sys->modem, at86rf215_rf_channel_2400mhz, &ext_ctrl);

	switch (sys->board_info.numeric_product_id)
	{
		//---------------------------------------------------
		case system_type_cariboulite_full:
		ZF_LOGD("This board is a Full version CaribouLite - setting ext_ref: modem, 32MHz");
			// by default the ext_ref for the mixer - from the modem, 32MHz
			sys->ext_ref_settings.src = cariboulite_ext_ref_src_modem;
    		sys->ext_ref_settings.freq_hz = 32000000;
            cariboulite_radio_ext_ref (sys, cariboulite_ext_ref_32mhz);
			break;
			
		//---------------------------------------------------
    	case system_type_cariboulite_ism:
            ZF_LOGD("This board is a ISM version CaribouLite - setting ext_ref: OFF");
			sys->ext_ref_settings.src = cariboulite_ext_ref_src_na;
    		sys->ext_ref_settings.freq_hz = 0;
            cariboulite_radio_ext_ref (sys, cariboulite_ext_ref_off);
			break;
			
		//---------------------------------------------------
		default:
			ZF_LOGE("Unknown board type - we sheuldn't get here");
			break;
	}

	// The mixer - only relevant to the full version
	if (sys->board_info.numeric_product_id == system_type_cariboulite_full)
	{
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
		//rffc507x_setup_reference_freq(&sys->mixer, 26e6);
		rffc507x_calibrate(&sys->mixer);
	}

	// Print the SPI information
	//io_utils_spi_print_setup(&sys->spi_dev);
	
	// Initialize the two Radio High-Level devices	
	cariboulite_radio_init(&sys->radio_low, sys, cariboulite_channel_s1g);
	cariboulite_radio_init(&sys->radio_high, sys, cariboulite_channel_6g);
	
	cariboulite_radio_set_rx_samp_cutoff(&sys->radio_low, at86rf215_radio_rx_sample_rate_4000khz, at86rf215_radio_rx_f_cut_half_fs);
    cariboulite_radio_set_tx_samp_cutoff(&sys->radio_low, at86rf215_radio_rx_sample_rate_4000khz, at86rf215_radio_rx_f_cut_half_fs);
    cariboulite_radio_set_rx_bandwidth(&sys->radio_low, at86rf215_radio_rx_bw_BW2000KHZ_IF2000KHZ);
    cariboulite_radio_set_tx_bandwidth(&sys->radio_low, at86rf215_radio_tx_cut_off_1000khz);
	
	cariboulite_radio_set_rx_samp_cutoff(&sys->radio_high, at86rf215_radio_rx_sample_rate_4000khz, at86rf215_radio_rx_f_cut_half_fs);
    cariboulite_radio_set_tx_samp_cutoff(&sys->radio_high, at86rf215_radio_rx_sample_rate_4000khz, at86rf215_radio_rx_f_cut_half_fs);
    cariboulite_radio_set_rx_bandwidth(&sys->radio_high, at86rf215_radio_rx_bw_BW2000KHZ_IF2000KHZ);
    cariboulite_radio_set_tx_bandwidth(&sys->radio_high, at86rf215_radio_tx_cut_off_1000khz);
	
	cariboulite_radio_activate_channel(&sys->radio_low, cariboulite_channel_dir_rx, false);
	cariboulite_radio_activate_channel(&sys->radio_high, cariboulite_channel_dir_rx, false);
	cariboulite_radio_sync_information(&sys->radio_low);
	cariboulite_radio_sync_information(&sys->radio_high);

    ZF_LOGD("Cariboulite submodules successfully initialized");
    return 0;

cariboulite_init_submodules_fail:
    // release the resources
    cariboulite_release_submodules(sys);
	return -1;
}

//=======================================================================================
int cariboulite_release_submodules(sys_st* sys)
{
    int res = 0;

	if (sys->system_status == sys_status_full_init)
	{
		// Dispose high-level radio devices
		cariboulite_radio_dispose(&sys->radio_low);
		cariboulite_radio_dispose(&sys->radio_high);
		
		// SMI Module
		//------------------------------------------------------
		ZF_LOGD("CLOSE SMI");
		caribou_smi_close(&sys->smi);
		usleep(10000);

		// AT86RF215
		//------------------------------------------------------
		ZF_LOGD("CLOSE MODEM - AT86RF215");
		at86rf215_stop_iq_radio_receive (&sys->modem, at86rf215_rf_channel_900mhz);
		at86rf215_stop_iq_radio_receive (&sys->modem, at86rf215_rf_channel_2400mhz);
		at86rf215_close(&sys->modem);

		//------------------------------------------------------
		// RFFC5072 only relevant to the full version
		if (sys->board_info.numeric_product_id == system_type_cariboulite_full)
		{
			ZF_LOGD("CLOSE MIXER - RFFC5072");
			rffc507x_release(&sys->mixer);
		}
	}

	if  (sys->system_status == sys_status_minimal_init)
	{
		// FPGA Module
		//------------------------------------------------------
		ZF_LOGD("CLOSE FPGA communication");
		res = caribou_fpga_close(&sys->fpga);
		if (res < 0)
		{
			ZF_LOGE("FPGA communication release failed (%d)", res);
			return -1;
		}
	}

    return 0;
}

//=======================================================================================
int cariboulite_self_test(sys_st* sys, cariboulite_self_test_result_st* res)
{
    memset(res, 0, sizeof(cariboulite_self_test_result_st));
    int error_occured = 0;

    //------------------------------------------------------
    ZF_LOGI("Testing modem communication and versions");
    
    uint8_t modem_pn = 0;
	modem_pn = at86rf215_print_version(&sys->modem);
    if (modem_pn != 0x34 && modem_pn != 0x35)
    {
        ZF_LOGE("The assembled modem is not AT86RF215 / IQ variant (product number: 0x%02x)", modem_pn);
        res->modem_fail = 1;
        error_occured = 1;
    }

	//------------------------------------------------------
	// Mixer only relevant to the full version
	if (sys->board_info.numeric_product_id == system_type_cariboulite_full)
	{	
		ZF_LOGI("Testing mixer communication and versions");
		rffc507x_device_id_st dev_id;
		rffc507x_readback_status(&sys->mixer, &dev_id, NULL);
		if (dev_id.device_id != 0x1140 && dev_id.device_id != 0x11C0)
		{
			ZF_LOGE("The assembled mixer is not RFFC5071/2[A]");
			res->mixer_fail = 1;
			error_occured = 1;
		}
	}

    //------------------------------------------------------
    ZF_LOGI("Testing smi communication");

    // check and report problems
    if (!error_occured)
    {
        ZF_LOGI("Self-test process finished successfully!");
        return 0;
    }

    ZF_LOGE("Self-test process finished with errors");
    return -1;
}

//=================================================
int cariboulite_init_system_production(sys_st *sys)
{
	zf_log_set_output_level(ZF_LOG_VERBOSE);
	ZF_LOGI("driver initializing");

	if (sys->system_status != sys_status_unintialized)
	{
		ZF_LOGE("System is already initialized! returning");
		return 0;
	}

    // signals
	ZF_LOGI("Initializing signals");
    if(cariboulite_setup_signals(sys) != 0)
    {
        ZF_LOGE("error signal list registration");
        return -cariboulite_signal_registration_failed;
    }

    // IO
	if (cariboulite_setup_io(sys) != 0)
    {
        return -cariboulite_io_setup_failed;
    }
	
	// FPGA Init and Programming
    ZF_LOGD("Initializing FPGA");
    if (caribou_fpga_init(&sys->fpga, &sys->spi_dev) < 0)
    {
        ZF_LOGE("FPGA communication init failed");
		cariboulite_deinit_system_production(sys);
		return -1;
    }
	
	// Initialize the two Radio High-Level devices	
	cariboulite_radio_init(&sys->radio_low, sys, cariboulite_channel_s1g);
	cariboulite_radio_init(&sys->radio_high, sys, cariboulite_channel_6g);
	
	return 0;
}

//=================================================
int cariboulite_deinit_system_production(sys_st *sys)
{
	if (sys->sys_type == system_type_cariboulite_full)
	{
		ZF_LOGD("CLOSE MIXER - RFFC5072");
		rffc507x_release(&sys->mixer);
	}
		
	caribou_fpga_close(&sys->fpga);
	
    ZF_LOGI("Releasing board I/Os - closing SPI");
    io_utils_spi_close(&sys->spi_dev);

    ZF_LOGI("Releasing board I/Os - io_utils_cleanup");
    io_utils_cleanup();
    return 0;
}

//=================================================
int cariboulite_init_driver_minimal(sys_st *sys, hat_board_info_st *info, bool production)
{
	ZF_LOGD("driver initializing");

	if (sys->system_status != sys_status_unintialized)
	{
		ZF_LOGE("System is already initialized! returning");
		return 0;
	}

    // LINUX SIGNALS
    // --------------------------------------------------------------------
	ZF_LOGD("Initializing signals");
    if(cariboulite_setup_signals(sys) != 0)
    {
        ZF_LOGE("error signal list registration");
        return -cariboulite_signal_registration_failed;
    }
	
    // DETECT BOARD FROM DEVICE-TREE OR EEPROM
    // --------------------------------------------------------------------
	if (hat_detect_board(&sys->board_info) == 0)
	{
		if (hat_detect_from_eeprom(&sys->board_info) != 1)
		{
            ZF_LOGE("Failed to detect the board in /proc/device-tree/hat - EEPROM needs to be configured.");
            ZF_LOGE("Please run the hermon_prod application with sudo permissions...");
		}
        else
        {
            ZF_LOGE("Failed to detect the board in /proc/device-tree/hat, though EEPROM is configured. Please reboot system...");
        }
        return -cariboulite_board_detection_failed;
	}
	sys->sys_type = (system_type_en)sys->board_info.numeric_product_id;

    // CONFIGURE I/O
    // --------------------------------------------------------------------
	if (cariboulite_setup_io(sys) != 0)
    {
        return -cariboulite_io_setup_failed;
    }

	// FPGA Init and Programming
    ZF_LOGD("Initializing FPGA");
    if (caribou_fpga_init(&sys->fpga, &sys->spi_dev) < 0)
    {
        ZF_LOGE("FPGA communication init failed");
		cariboulite_release_io (sys);
        return -cariboulite_fpga_configuration_failed;
    }

	ZF_LOGD("Programming FPGA");
	if (cariboulite_configure_fpga (sys, cariboulite_firmware_source_blob, NULL/*sys->firmware_path_operational*/) < 0)
	{
		ZF_LOGE("FPGA programming failed");
		caribou_fpga_close(&sys->fpga);
		cariboulite_release_io (sys);
        return -cariboulite_fpga_configuration_failed;
    }
	if (sys->reset_fpga_on_startup)
    {
		caribou_fpga_soft_reset(&sys->fpga);
    }

	// Reading the configuration from the FPGA (resistor set)
	int led0 = 0, led1 = 0, btn = 0, cfg = 0;
	caribou_fpga_get_io_ctrl_dig (&sys->fpga, &led0, &led1, &btn, &cfg);

	ZF_LOGD("FPGA Digital Values: led0: %d, led1: %d, btn: %d, CFG[0..3]: [%d,%d,%d,%d]",
		led0, led1, btn, (cfg >> 0) & 0x1, (cfg >> 1) & 0x1, (cfg >> 2) & 0x1, (cfg >> 3) & 0x1);
	sys->fpga_config_resistor_state = cfg;

	// if we are in the production phase, don't check hat configurations
	if (production)
	{
		sys->system_status = sys_status_minimal_init;
		return cariboulite_ok;
	}
	
    ZF_LOGD("Detected Board Information:");
    cariboulite_print_board_info(sys, true);
    
    // if the board info is requested by caller, copy it
    if (info) memcpy(info, &sys->board_info, sizeof(hat_board_info_st));


	sys->system_status = sys_status_minimal_init;

	return cariboulite_ok;
}

//=================================================
void cariboulite_set_log_level(cariboulite_log_level_en lvl)
{
    if (lvl == cariboulite_log_level_verbose)
    {
        zf_log_set_output_level(ZF_LOG_VERBOSE);
    }
    else if (lvl == cariboulite_log_level_info)
    {
        zf_log_set_output_level(ZF_LOG_INFO);
    }
    else if (lvl == cariboulite_log_level_none)
    {
        zf_log_set_output_level(ZF_LOG_ERROR);
    }
}

//=================================================
int cariboulite_init_driver(sys_st *sys, hat_board_info_st *info)
{
	int ret = cariboulite_init_driver_minimal(sys, info, false);
	if (ret < 0)
	{
		return ret;
	}

	if (sys->system_status == sys_status_full_init)
	{
		ZF_LOGE("System is already fully initialized!");
		return 0;
	}

	// INIT other sub-modules
    if (cariboulite_init_submodules (sys) != 0)
    {
		caribou_fpga_close(&sys->fpga);
        cariboulite_release_io (sys);
        return -cariboulite_submodules_init_failed;
    }

	// Self-Test
    cariboulite_self_test_result_st self_tes_res = {0};
    if (cariboulite_self_test(sys, &self_tes_res) != 0)
    {
		caribou_fpga_close(&sys->fpga);
        cariboulite_release_io (sys);
        return -cariboulite_self_test_failed;
    }

	sys->system_status = sys_status_full_init;

    return cariboulite_ok;
}

//=================================================
int cariboulite_setup_signal_handler (sys_st *sys,
                                        signal_handler handler,
                                        signal_handler_operation_en op,
                                        void *context)
{
    ZF_LOGD("setting up signal handler");

    sys->signal_cb = handler;
    sys->singal_cb_context = context;
    sys->sig_op = op;

    return 0;
}

//=================================================
void cariboulite_release_driver(sys_st *sys)
{
    ZF_LOGD("driver being released");
	if (sys->system_status != sys_status_unintialized)
	{
		//caribou_fpga_set_io_ctrl_mode (&sys->fpga, false, ...);
		cariboulite_release_submodules(sys);
		cariboulite_release_io (sys);

		sys->system_status = sys_status_unintialized;
	}
    ZF_LOGD("driver released");
}

//=================================================
int cariboulite_get_serial_number(sys_st *sys, uint32_t* serial_number, int *count)
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

//===========================================================
int cariboulite_detect_board(sys_st *sys)
{
	if (hat_detect_board(&sys->board_info) == 0)
	{
		// the board was not configured as a hat. Lets try and detect it directly
		// through its EEPROM
		if (hat_detect_from_eeprom(&sys->board_info) == 0)
		{
			return 0;
		}
	}

	sys->sys_type = (system_type_en)sys->board_info.numeric_product_id;
    return 1;
}

//===========================================================
void cariboulite_print_board_info(sys_st *sys, bool log)
{
	hat_print_board_info(&sys->board_info, log);

	if (log)
	{
		switch (sys->sys_type)
		{
			case system_type_cariboulite_full: ZF_LOGD("# Board Info - Product Type: CaribouLite FULL"); break;
			case system_type_cariboulite_ism: ZF_LOGD("# Board Info - Product Type: CaribouLite ISM"); break;
			case system_type_unknown: 
			default: ZF_LOGD("# Board Info - Product Type: Unknown"); break;
		}
	}
	else
	{
		switch (sys->sys_type)
		{
			case system_type_cariboulite_full: printf("	Product Type: CaribouLite FULL"); break;
			case system_type_cariboulite_ism: printf("	Product Type: CaribouLite ISM"); break;
			case system_type_unknown: 
			default: printf("	Product Type: Unknown"); break;
		}
	}
}

//===========================================================
cariboulite_radio_state_st* cariboulite_get_radio_handle(sys_st* sys, cariboulite_channel_en type)
{
    if (type == cariboulite_channel_s1g) return &sys->radio_low;
    else return &sys->radio_high;
}