#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Prod"
#include "zf_log/zf_log.h"

#include "cariboulite_setup.h"
#include "cariboulite_events.h"
#include "cariboulite.h"
#include "cariboulite_eeprom/cariboulite_eeprom.h"
#include "production_utils/production_utils.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "cariboulite_production.h"

struct sigaction act;
int program_running = 1;
int signal_shown = 0;
CARIBOULITE_CONFIG_DEFAULT(cariboulite_sys);

//=================================================
int stop_program ()
{
    if (program_running) ZF_LOGD("program termination requested");
    program_running = 0;
    return 0;
}

//=================================================
void sighandler( struct cariboulite_st_t *sys,
                 void* context,
                 int signal_number,
                 siginfo_t *si)
{
    if (signal_shown != signal_number) 
    {
        ZF_LOGI("Received signal %d", signal_number);
        signal_shown = signal_number;
    }

    switch (signal_number)
    {
        case SIGINT:
        case SIGTERM:
        case SIGABRT:
        case SIGILL:
        case SIGSEGV:
        case SIGFPE: stop_program (); break;
        default: return; break;
    }
}

//=================================================
cariboulite_eeprom_st ee = { .i2c_address = 0x50, .eeprom_type = eeprom_type_24c32,};
int cariboulite_prod_eeprom_programming(cariboulite_st* sys, cariboulite_eeprom_st* eeprom)
{
	int led0 = 0, led1 = 0, btn = 0, cfg = 0;
	ZF_LOGI("==============================================");
	ZF_LOGI("EEPROM CONFIG - PRESS AND HOLD BUTTON");

	int c = 0;
	while (1)
	{
		caribou_fpga_get_io_ctrl_dig (&sys->fpga, &led0, &led1, &btn, &cfg);
		if (btn == 0)	// pressed
		{
			ZF_LOGI("    <=== KEEP HOLDING THE BUTTON ====>");
			caribou_fpga_set_io_ctrl_dig (&sys->fpga, 1, 1);
			break;
		}

		if (c == 0) caribou_fpga_set_io_ctrl_dig (&sys->fpga, 0, 0);
		else if (c == 1) caribou_fpga_set_io_ctrl_dig (&sys->fpga, 0, 1);
		else if (c == 2) caribou_fpga_set_io_ctrl_dig (&sys->fpga, 1, 1);
		else if (c == 3) caribou_fpga_set_io_ctrl_dig (&sys->fpga, 1, 0);

		usleep(200000);
		c = (c + 1) % 4;
	}

	cariboulite_system_type_en type = (cfg&0x1)?cariboulite_system_type_full:cariboulite_system_type_ism;
	if (type == cariboulite_system_type_full) ZF_LOGI("Detected CaribouLite FULL Version");
	else if (type == cariboulite_system_type_ism) ZF_LOGI("Detected CaribouLite ISM Version");
	cariboulite_eeprom_generate_write_config(eeprom, (int)(type), 0x1);

	sleep(1);
	caribou_fpga_set_io_ctrl_dig (&sys->fpga, 0, 0);
	ZF_LOGI("    <=== DONE - YOU CAN RELEASE BUTTON ====>");
	return 0;
}

//=================================================
int main(int argc, char *argv[])
{
	int ret = 0;

	cariboulite_production_utils_rpi_leds_init(1);
	cariboulite_production_utils_rpi_leds_blink_start_tests();

	cariboulite_production_wifi_status_st wifi_stat;
	cariboulite_production_check_wifi_state(&wifi_stat);
	printf("Wifi Status: available: %d, wlan_id = %d, ESSID: %s, InternetAccess: %d\n", 
		wifi_stat.available, wifi_stat.wlan_id, wifi_stat.essid, wifi_stat.internet_access);

	cariboulite_rpi_info_st rpi = {0};
	cariboulite_production_get_rpi_info(&rpi);
	printf("uname: %s, cpu: %s-R%s, sn: %s, model: %s\n", rpi.uname, rpi.cpu_name, rpi.cpu_revision, rpi.cpu_serial_number, rpi.model);

    // init the minimal set of drivers and FPGA
	cariboulite_board_info_st board_info = {0};
	ret = cariboulite_init_driver_minimal(&cariboulite_sys, &board_info);
	if (ret != 0)
	{
		switch(-ret)
		{
			case cariboulite_board_detection_failed:
				ZF_LOGI("This is a new board - board detection failed");
				break;

			case cariboulite_signal_registration_failed:
				cariboulite_production_utils_rpi_leds_blink_fatal_error();
				ZF_LOGE("Internal RPI error: Signal registration failed");
				break;

			case cariboulite_io_setup_failed:
				cariboulite_production_utils_rpi_leds_blink_fatal_error();
				ZF_LOGE("Internal RPI error: I/O setup failed");
				break;

			case cariboulite_fpga_configuration_failed:
				cariboulite_production_utils_rpi_leds_blink_fatal_error();
				ZF_LOGE("FPGA error: couldn't program, read or write");
				//set error at: cariboulite_test_en_fpga_programming
				break;

			default:
				break;
		}
	}
	else
	{
		ZF_LOGI("Driver init seccessfull - the board has already been initialized before");
	}

	// EEPROM programming
	ZF_LOGI("Starting EEPROM programming sequence");
	cariboulite_eeprom_init(&ee);
	cariboulite_prod_eeprom_programming(&cariboulite_sys, &ee);
	cariboulite_eeprom_close(&ee);

	ZF_LOGI("Verifying EEPROM");
	cariboulite_eeprom_init(&ee);
	if (ee.eeprom_initialized == 0)
	{
		// report eeprom error
	}
	cariboulite_eeprom_print(&ee);

	// Testing the system
	///

    // setup the signal handler
    cariboulite_setup_signal_handler (&cariboulite_sys, sighandler, cariboulite_signal_handler_op_last, &cariboulite_sys);



    // dummy loop
    sleep(1);
    while (program_running)
    {
        usleep(300000);
    }

    // close the driver and release resources
	cariboulite_production_utils_rpi_leds_init(0);
    cariboulite_release_driver(&cariboulite_sys);
    return 0;
}
