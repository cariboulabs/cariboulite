#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Prod"
#include "zf_log/zf_log.h"

#include "cariboulite_setup.h"
#include "cariboulite_events.h"
#include "cariboulite.h"
#include "hat/hat.h"
#include "cariboulite_dtbo.h"
#include "production_utils/production_utils.h"
#include "io_utils/io_utils_sys_info.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include "cariboulite_production.h"

CARIBOULITE_CONFIG_DEFAULT(cariboulite_sys);

hat_st hat = 
{
	.vendor_name = "CaribouLabs LTD",
	.product_name = "CaribouLite RPI Hat",
	.product_id = cariboulite_system_type_full,
	.product_version = 0x01,
	.device_tree_buffer = cariboulite_dtbo,
	.device_tree_buffer_size = sizeof(cariboulite_dtbo),

	.dev = {
		.i2c_address =  0x50,    // the i2c address of the eeprom chip
		.eeprom_type = eeprom_type_24c32,
	},
};

//=================================================
int stop_program ()
{
    ZF_LOGD("program termination requested");
    return 0;
}

//=================================================
void sighandler( struct sys_st_t *sys,
                 void* context,
                 int signal_number,
                 siginfo_t *si)
{
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
int cariboulite_prod_eeprom_programming(sys_st* sys)
{
	int led0 = 0, led1 = 0, btn = 0, cfg = 0;
	
	// get the configuration resistors
	caribou_fpga_get_io_ctrl_dig (&sys->fpga, &led0, &led1, &btn, &cfg);
	

	cariboulite_system_type_en type = (cfg&0x1)?cariboulite_system_type_full:cariboulite_system_type_ism;
	if (type == cariboulite_system_type_full) ZF_LOGI("Detected CaribouLite FULL Version");
	else if (type == cariboulite_system_type_ism) ZF_LOGI("Detected CaribouLite ISM Version");
	hat.product_id = type;
	
	hat_generate_write_config(&hat);

	sleep(1);
	caribou_fpga_set_io_ctrl_dig (&sys->fpga, 0, 0);
	return 0;
}

//=================================================
int main(int argc, char *argv[])
{
	int ret = 0;

	production_utils_rpi_leds_init(1);
	production_utils_rpi_leds_blink_start_tests();

	//production_wifi_status_st wifi_stat;
	//production_check_wifi_state(&wifi_stat);
	//printf("Wifi Status: available: %d, wlan_id = %d, ESSID: %s, InternetAccess: %d\n", 
	//	wifi_stat.available, wifi_stat.wlan_id, wifi_stat.essid, wifi_stat.internet_access);

	io_utils_sys_info_st rpi = {0};
	if (io_utils_get_rpi_info(&rpi) != 0)
	{
		// error
		ZF_LOGE("Raspberry Pi system detection failed. INFO not retreivable...");
		return -1;
	}
	io_utils_print_rpi_info(&rpi);
	
    // init the minimal set of drivers and FPGA
	hat_board_info_st board_info = {0};
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
	hat_init(&hat);
	cariboulite_prod_eeprom_programming(&cariboulite_sys);
	hat_close(&hat);

	ZF_LOGI("Verifying EEPROM");
	hat_init(&hat);
	if (hat.eeprom_initialized == 0)
	{
		// report eeprom error
	}
	hat_print(&hat);

	// Testing the system
	///

    // setup the signal handler
    cariboulite_setup_signal_handler (&cariboulite_sys, sighandler, cariboulite_signal_handler_op_last, &cariboulite_sys);
    // close the driver and release resources
	cariboulite_production_utils_rpi_leds_init(0);
    cariboulite_release_driver(&cariboulite_sys);
    return 0;
}
