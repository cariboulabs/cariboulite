#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Prod"
#include "zf_log/zf_log.h"

//=================================================
// INTERNAL INCLUDES
#include "production_utils/cariboulite_production_tests.h"
#include "cariboulite_setup.h"
#include "cariboulite_events.h"
#include "cariboulite.h"
#include "hat/hat.h"
#include "cariboulite_dtbo.h"
#include "production_utils/production_utils.h"
#include "io_utils/io_utils_sys_info.h"

//=================================================
// EXTERNAL INCLUDES
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

//=================================================
// FLOW MANAGEMENT
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

//================================================================
//================================================================
//================================================================
// TEST TYPES
typedef enum
{
	cariboulite_test_en_rpi_id_eeprom = 0,
	cariboulite_test_en_fpga_programming, 		
	cariboulite_test_en_fpga_communication, 		
	cariboulite_test_en_fpga_id_resistors, 		
	cariboulite_test_en_fpga_reset, 				
	cariboulite_test_en_fpga_pmod, 				
	cariboulite_test_en_fpga_switch, 			
	cariboulite_test_en_fpga_leds, 				
	cariboulite_test_en_fpga_smi, 				
	cariboulite_test_en_mixer_reset, 			
	cariboulite_test_en_mixer_communication, 	
	cariboulite_test_en_mixer_versions, 			
	cariboulite_test_en_modem_reset, 			
	cariboulite_test_en_modem_communication, 	
	cariboulite_test_en_modem_versions, 			
	cariboulite_test_en_modem_leds, 				
	cariboulite_test_en_modem_interrupt, 		
	cariboulite_test_en_current_system, 			
	cariboulite_test_en_current_modem_rx, 		
	cariboulite_test_en_current_modem_tx, 		
	cariboulite_test_en_current_mixer, 			
	cariboulite_test_en_system_smi_data, 		
	cariboulite_test_en_system_rf_loopback, 		
	cariboulite_test_en_system_rf_tx_power, 		
    cariboulite_test_en_max,
} cariboulite_test_en;

//================================================================
// TEST FUNCTIONS
static int cariboulite_test_parameters(void* context, void* test_context);		
static int cariboulite_test_hat_eeprom(void* context, void* test_context);		
static int cariboulite_test_fpga_programming(void* context, void* test_context);
static int cariboulite_test_fpga_communication(void* context, void* test_context);
static int cariboulite_test_fpga_id_resistors(void* context, void* test_context);
static int cariboulite_test_fpga_soft_reset(void* context, void* test_context);
static int cariboulite_test_fpga_pmod(void* context, void* test_context);
static int cariboulite_test_fpga_switch(void* context, void* test_context);
static int cariboulite_test_fpga_leds(void* context, void* test_context);
static int cariboulite_test_fpga_smi(void* context, void* test_context);
static int cariboulite_test_mixer_reset(void* context, void* test_context);
static int cariboulite_test_mixer_communication(void* context, void* test_context);
static int cariboulite_test_mixer_versions(void* context, void* test_context);
static int cariboulite_test_modem_reset(void* context, void* test_context);
static int cariboulite_test_modem_communication(void* context, void* test_context);
static int cariboulite_test_modem_version(void* context, void* test_context);
static int cariboulite_test_modem_leds(void* context, void* test_context);
static int cariboulite_test_modem_interrupt(void* context, void* test_context);
static int cariboulite_test_current_system(void* context, void* test_context);
static int cariboulite_test_current_modem_rx(void* context, void* test_context);
static int cariboulite_test_current_modem_tx(void* context, void* test_context);
static int cariboulite_test_current_mixer(void* context, void* test_context);
static int cariboulite_test_smi_data(void* context, void* test_context);
static int cariboulite_test_rf_loopback(void* context, void* test_context);
static int cariboulite_test_rf_tx_power(void* context, void* test_context);

//================================================================
// TEST DEFINITIONS
production_test_st tests[] = 
{
	{.test_name = "parameters", 				.group = 0, 	.test_number = cariboulite_test_en_rpi_params, 				.func = cariboulite_test_parameters,			},
	{.test_name = "hat_eeprom", 				.group = 0, 	.test_number = cariboulite_test_en_rpi_id_eeprom, 			.func = cariboulite_test_hat_eeprom,			},
	{.test_name = "fpga_programming", 			.group = 1, 	.test_number = cariboulite_test_en_fpga_programming, 		.func = cariboulite_test_fpga_programming,		},
	{.test_name = "fpga_communication", 		.group = 1, 	.test_number = cariboulite_test_en_fpga_communication, 		.func = cariboulite_test_fpga_communication,	},
	{.test_name = "fpga_id_resistors", 			.group = 1, 	.test_number = cariboulite_test_en_fpga_id_resistors, 		.func = cariboulite_test_fpga_id_resistors,		},
	{.test_name = "fpga_soft_reset", 			.group = 1, 	.test_number = cariboulite_test_en_fpga_reset, 				.func = cariboulite_test_fpga_soft_reset,		},
	{.test_name = "fpga_pmod", 					.group = 1, 	.test_number = cariboulite_test_en_fpga_pmod, 				.func = cariboulite_test_fpga_pmod,				},
	{.test_name = "fpga_switch", 				.group = 1, 	.test_number = cariboulite_test_en_fpga_switch, 			.func = cariboulite_test_fpga_switch,			},
	{.test_name = "fpga_leds", 					.group = 1, 	.test_number = cariboulite_test_en_fpga_leds, 				.func = cariboulite_test_fpga_leds,				},
	{.test_name = "fpga_smi", 					.group = 1, 	.test_number = cariboulite_test_en_fpga_smi, 				.func = cariboulite_test_fpga_smi,				},
	{.test_name = "mixer_reset", 				.group = 2, 	.test_number = cariboulite_test_en_mixer_reset, 			.func = cariboulite_test_mixer_reset,			},
	{.test_name = "mixer_communication", 		.group = 2, 	.test_number = cariboulite_test_en_mixer_communication, 	.func = cariboulite_test_mixer_communication,	},
	{.test_name = "mixer_version_id", 			.group = 2, 	.test_number = cariboulite_test_en_mixer_versions, 			.func = cariboulite_test_mixer_versions,		},
	{.test_name = "modem_reset", 				.group = 3, 	.test_number = cariboulite_test_en_modem_reset, 			.func = cariboulite_test_modem_reset,			},
	{.test_name = "modem_communication", 		.group = 3, 	.test_number = cariboulite_test_en_modem_communication, 	.func = cariboulite_test_modem_communication,	},
	{.test_name = "modem_version", 				.group = 3, 	.test_number = cariboulite_test_en_modem_versions, 			.func = cariboulite_test_modem_version,			},
	{.test_name = "modem_leds", 				.group = 3, 	.test_number = cariboulite_test_en_modem_leds, 				.func = cariboulite_test_modem_leds,			},	
	{.test_name = "modem_interrupt", 			.group = 3, 	.test_number = cariboulite_test_en_modem_interrupt, 		.func = cariboulite_test_modem_interrupt,		},
	{.test_name = "current_system", 			.group = 4, 	.test_number = cariboulite_test_en_current_system, 			.func = cariboulite_test_current_system,		},
	{.test_name = "current_modem rx", 			.group = 4, 	.test_number = cariboulite_test_en_current_modem_rx, 		.func = cariboulite_test_current_modem_rx,		},
	{.test_name = "current_modem tx", 			.group = 4, 	.test_number = cariboulite_test_en_current_modem_tx, 		.func = cariboulite_test_current_modem_tx,		},
	{.test_name = "current_mixer", 				.group = 4, 	.test_number = cariboulite_test_en_current_mixer, 			.func = cariboulite_test_current_mixer,			},
	{.test_name = "system_smi_data", 			.group = 5, 	.test_number = cariboulite_test_en_system_smi_data, 		.func = cariboulite_test_smi_data,				},
	{.test_name = "system_rf_loopback",			.group = 5, 	.test_number = cariboulite_test_en_system_rf_loopback, 		.func = cariboulite_test_rf_loopback,			},
	{.test_name = "system_rf_tx_power",			.group = 5, 	.test_number = cariboulite_test_en_system_rf_tx_power, 		.func = cariboulite_test_rf_tx_power,			},
};

//=================================================
int cariboulite_test_parameters(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_hat_eeprom(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_fpga_programming(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}


//=================================================
int cariboulite_test_fpga_communication(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_fpga_id_resistors(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_fpga_soft_reset(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_fpga_pmod(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_fpga_switch(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_fpga_leds(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_fpga_smi(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_mixer_reset(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}


//=================================================
int cariboulite_test_mixer_communication(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}


//=================================================
int cariboulite_test_mixer_versions(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_modem_reset(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_modem_communication(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_modem_version(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_modem_leds(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_modem_interrupt(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_current_system(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_current_modem_rx(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_current_modem_tx(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_current_mixer(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_smi_data(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_rf_loopback(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_rf_tx_power(void *context, void* test_context)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* tests = (production_sequence_st*)test_context;
	
	return 0;
}


//=================================================
int cariboulite_prod_eeprom_programming(sys_st* sys)
{
	int led0 = 0, led1 = 0, btn = 0, cfg = 0;

	// get the configuration resistors
	caribou_fpga_get_io_ctrl_dig (&sys->fpga, &led0, &led1, &btn, &cfg);

	cariboulite_system_type_en type = (cfg & 0x1) ? cariboulite_system_type_full : cariboulite_system_type_ism;
	if (type == cariboulite_system_type_full) ZF_LOGI("Detected CaribouLite FULL Version");
	else if (type == cariboulite_system_type_ism) ZF_LOGI("Detected CaribouLite ISM Version");
	hat.product_id = type;

	hat_generate_write_config(&hat);

	sleep(1);
	caribou_fpga_set_io_ctrl_dig (&sys->fpga, 0, 0);
	return 0;
}

void hat_powermon_event(void* context, hat_powermon_state_st* state)
{

}

//=================================================
int cariboulite_production_init(void)
{
	production_utils_rpi_leds_init(1);
	production_utils_rpi_leds_blink_start_tests();

	// initialize the wifi
	production_wifi_status_st wifi_stat;
	production_check_wifi_state(&wifi_stat);
	printf("Wifi Status: available: %d, wlan_id = %d, ESSID: %s, InternetAccess: %d\n",
		wifi_stat.available, wifi_stat.wlan_id, wifi_stat.essid, wifi_stat.internet_access);

	// Rpi info
	io_utils_sys_info_st rpi = {0};
	if (io_utils_get_rpi_info(&rpi) != 0)
	{
		// error
		ZF_LOGE("Raspberry Pi system detection failed. INFO not retreivable...");
		return -1;
	}
	io_utils_print_rpi_info(&rpi);

	// init the power-monitor
	return hat_powermon_init(&prod->powermon, HAT_POWER_MON_ADDRESS, hat_powermon_event, prod);
}

//=================================================
int main(int argc, char *argv[])
{
	cariboulite_production_sequence_st prod = {};

	while (1)
	{
		if (cariboulite_production_init(&prod) != 0)
		{
			ZF_LOGE("error loading init source")
			return -1;
		}

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
			ZF_LOGI("Driver init successful - the board has already been initialized before");
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
	}
    return 0;
}
