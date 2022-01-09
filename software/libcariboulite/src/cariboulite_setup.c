#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Setup"
#include "zf_log/zf_log.h"


#include <signal.h>
#include <stdio.h>
#include <errno.h>

#include "cariboulite_setup.h"
#include "cariboulite_events.h"
#include "cariboulite_fpga_firmware.h"

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

cariboulite_st* sigsys = NULL;

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
            case cariboulite_signal_handler_op_last: run_last = 1; break;
            case cariboulite_signal_handler_op_first: run_first = 1; break;
            case cariboulite_signal_handler_op_override:
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

//=======================================================================================
int cariboulite_setup_io (cariboulite_st* sys)
{
    ZF_LOGI("Setting up board I/Os");
    if (io_utils_setup(NULL) < 0)
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
    ZF_LOGI("Releasing board I/Os - closing SPI");
    io_utils_spi_close(&sys->spi_dev);

    ZF_LOGI("Releasing board I/Os - io_utils_cleanup");
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
int cariboulite_setup_ext_ref ( cariboulite_st *sys, cariboulite_ext_ref_freq_en ref)
{
    switch(ref)
    {
        case cariboulite_ext_ref_26mhz:
            ZF_LOGD("Setting ext_ref = 26MHz");
            at86rf215_set_clock_output(&sys->modem, at86rf215_drive_current_2ma, at86rf215_clock_out_freq_26mhz);
            rffc507x_setup_reference_freq(&sys->mixer, 26e6);
            break;
        case cariboulite_ext_ref_32mhz:
            ZF_LOGD("Setting ext_ref = 32MHz");
            at86rf215_set_clock_output(&sys->modem, at86rf215_drive_current_2ma, at86rf215_clock_out_freq_32mhz);
            rffc507x_setup_reference_freq(&sys->mixer, 32e6);
            break;
        case cariboulite_ext_ref_off:
            ZF_LOGD("Setting ext_ref = OFF");
            at86rf215_set_clock_output(&sys->modem, at86rf215_drive_current_2ma, at86rf215_clock_out_freq_off);
        default:
            return -1;
        break;
    }
}

//=======================================================================================
int cariboulite_init_submodules (cariboulite_st* sys)
{
    int res = 0;
    ZF_LOGI("initializing submodules");

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

	switch (sys->board_info.sys_type)
	{
		case cariboulite_system_type_full:
		ZF_LOGD("This board is a Full version CaribouLite - setting ext_ref: modem, 32MHz");
			// by default the ext_ref for the mixer - from the modem, 32MHz
			sys->ext_ref_settings.src = cariboulite_ext_ref_src_modem;
    		sys->ext_ref_settings.freq_hz = 32000000;
            cariboulite_setup_ext_ref (sys, cariboulite_ext_ref_32mhz);
			break;
    	case cariboulite_system_type_ism:
            ZF_LOGD("This board is a ISM version CaribouLite - setting ext_ref: OFF");
			sys->ext_ref_settings.src = cariboulite_ext_ref_src_na;
    		sys->ext_ref_settings.freq_hz = 0;
            cariboulite_setup_ext_ref (sys, cariboulite_ext_ref_off);
		default:
			ZF_LOGE("Unknown board type - we sheuldn't get here");
			break;
	}

	// The mixer - only relevant to the full version
	if (sys->board_info.sys_type == cariboulite_system_type_full)
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
    int error_occured = 0;

    //------------------------------------------------------
    ZF_LOGI("Testing modem communication and versions");
    
    uint8_t modem_pn = 0, modem_vn = 0;
    at86rf215_get_versions(&sys->modem, &modem_pn, &modem_vn);
    if (modem_pn != 0x34)
    {
        ZF_LOGE("The assembled modem is not AT86RF215 (product number: 0x%02x)", modem_pn);
        res->modem_fail = 1;
        error_occured = 1;
    }

	//------------------------------------------------------
	// Mixer only relevant to the full version
	if (sys->board_info.sys_type == cariboulite_system_type_full)
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
    // TBD

    // check and report problems
    if (!error_occured)
    {
        ZF_LOGI("Self-test process finished successfully!");
        return 0;
    }
    
    ZF_LOGE("Self-test process finished with errors");
    return -1;
}

//=======================================================================================
int cariboulite_release_submodules(cariboulite_st* sys)
{
    int res = 0;

	if (sys->system_status == cariboulite_sys_status_minimal_full_init)
	{
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
		if (sys->board_info.sys_type == cariboulite_system_type_full)
		{
			ZF_LOGD("CLOSE MIXER - RFFC5072");
			rffc507x_release(&sys->mixer);
		}
	}

	if  (sys->system_status == cariboulite_sys_status_minimal_init)
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

//=================================================
static int cariboulite_register_many_signals(int *sig_nos, int nsigs, struct sigaction *sa)
{
    for (int i = 0; i < nsigs; i++)
    {
        if(sigaction(sig_nos[i], sa, NULL) != 0) 
        {
            ZF_LOGE("error sigaction() [%d] signal registration", sig_nos[i]);
            return -cariboulite_signal_registration_failed;
        }
    }
    return 0;
}

//=================================================
int cariboulite_init_driver_minimal(cariboulite_st *sys, cariboulite_board_info_st *info)
{
	//zf_log_set_output_level(ZF_LOG_ERROR);
    zf_log_set_output_level(ZF_LOG_VERBOSE);
	ZF_LOGI("driver initializing");

	if (sys->system_status != cariboulite_sys_status_unintialized)
	{
		ZF_LOGE("System is already initialized! returnig");
		return 0;
	}

    // signals
	ZF_LOGI("Initializing signals");
    cariboulite_setup_signal_handler (sys, NULL, cariboulite_signal_handler_op_last, NULL);

    int signals[] = {SIGHUP, SIGINT, SIGQUIT, SIGILL, SIGABRT, SIGBUS, SIGFPE, SIGSEGV, SIGTERM};
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigsys = sys;
    sa.sa_sigaction = cariboulite_sigaction_basehandler;
    sa.sa_flags |= SA_RESTART | SA_SIGINFO;
    
	// RPI Internal Configurations
    if(cariboulite_register_many_signals(signals, sizeof(signals)/sizeof(signals[0]), &sa) != 0) 
    {
        ZF_LOGE("error signal list registration");
        return -cariboulite_signal_registration_failed;
    }

	if (cariboulite_setup_io (sys) != 0)
    {
        return -cariboulite_io_setup_failed;
    }

	// External Configurations
	// FPGA Init
	//------------------------------------------------------
    if (cariboulite_configure_fpga (sys, cariboulite_firmware_source_blob, NULL/*sys->firmware_path_operational*/) != 0)
    {
        cariboulite_release_io (sys);
        return -cariboulite_fpga_configuration_failed;
    }
	
    ZF_LOGD("INIT FPGA SPI communication");
    if (caribou_fpga_init(&sys->fpga, &sys->spi_dev) < 0)
    {
        ZF_LOGE("FPGA communication init failed");
		cariboulite_release_io (sys);
        return -cariboulite_fpga_configuration_failed;
    }
	
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
        ZF_LOGE("FPGA firmware varsion error - sys_ver = %02X, manu_id = %02X", 
            sys->fpga_versions.sys_ver, sys->fpga_versions.sys_manu_id);
		caribou_fpga_close(&sys->fpga);
		cariboulite_release_io (sys);
        return -cariboulite_fpga_configuration_failed;
    }

	// Now read the configuration from the FPGA (resistor set)
	//caribou_fpga_set_io_ctrl_dig (&sys->fpga, int ldo, int led0, int led1);
	int led0 = 0, led1 = 0, btn = 0, cfg = 0;
	caribou_fpga_get_io_ctrl_dig (&sys->fpga, &led0, &led1, &btn, &cfg);
	ZF_LOGD("====> FPGA Digital Values: led0: %d, led1: %d, btn: %d, CFG[0..3]: [%d,%d,%d,%d]",
		led0, led1, btn, (cfg >> 0) & 0x1, (cfg >> 1) & 0x1, (cfg >> 2) & 0x1, (cfg >> 3) & 0x1);
	sys->fpga_config_res_state = cfg;

    ZF_LOGI("Detected Board Information:");
    if (info == NULL)
    {
        int detected = cariboulite_config_detect_board(&sys->board_info);
        if (!detected)
        {
            ZF_LOGW("The RPI HAT interface didn't detect any connected boards");
			caribou_fpga_close(&sys->fpga);
			cariboulite_release_io (sys);
            return -cariboulite_board_detection_failed;
        }
    }
    else
    {
        memcpy(&sys->board_info, info, sizeof(cariboulite_board_info_st));
    }
    cariboulite_config_print_board_info(&sys->board_info);

	sys->system_status = cariboulite_sys_status_minimal_init;

	return cariboulite_ok;
}


//=================================================
int cariboulite_init_driver(cariboulite_st *sys, cariboulite_board_info_st *info)
{
	int ret = cariboulite_init_driver_minimal(sys, info);
	if (ret < 0)
	{
		return ret;
	}

	if (sys->system_status == cariboulite_sys_status_minimal_full_init)
	{
		ZF_LOGE("System is already fully initialized! returnig");
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

	sys->system_status = cariboulite_sys_status_minimal_full_init;

    return cariboulite_ok;
}

//=================================================
int cariboulite_setup_signal_handler (cariboulite_st *sys, 
                                        caribou_signal_handler handler, 
                                        cariboulite_signal_handler_operation_en op,
                                        void *context)
{
    ZF_LOGI("setting up signal handler");

    sys->signal_cb = handler;
    sys->singal_cb_context = context;
    sys->sig_op = op;

    return 0;
}

//=================================================
void cariboulite_release_driver(cariboulite_st *sys)
{
    ZF_LOGI("driver being released");
	if (sys->system_status != cariboulite_sys_status_unintialized)
	{
		cariboulite_release_submodules(sys);
		cariboulite_release_io (sys);

		sys->system_status = cariboulite_sys_status_unintialized;
	}
    ZF_LOGI("driver released");
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
