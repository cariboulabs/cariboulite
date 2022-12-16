#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Prod"
#include "zf_log/zf_log.h"

//=================================================
// INTERNAL INCLUDES
#include "production_utils/production_testing.h"
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

//=================================================
hat_st hat =
{
	.vendor_name = "CaribouLabs LTD",
	.product_name = "CaribouLite RPI Hat",
	.product_id = system_type_cariboulite_full,
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
	cariboulite_test_en_current_system = 0,
	cariboulite_test_en_fpga_programming, 		
	cariboulite_test_en_fpga_communication, 		
	cariboulite_test_en_fpga_id_resistors, 		
	cariboulite_test_en_fpga_reset, 				
	cariboulite_test_en_fpga_switch, 			
	cariboulite_test_en_fpga_leds, 				
	cariboulite_test_en_fpga_smi, 				
	cariboulite_test_en_rpi_id_eeprom,	
	cariboulite_test_en_mixer_reset, 			
	cariboulite_test_en_mixer_communication, 	
	cariboulite_test_en_mixer_versions, 			
	cariboulite_test_en_modem_reset, 			
	cariboulite_test_en_modem_communication, 	
	cariboulite_test_en_modem_versions, 			
	cariboulite_test_en_modem_leds, 				
	cariboulite_test_en_modem_interrupt, 		
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
static int cariboulite_test_hat_eeprom(void* context, void* test_context, int test_num);		
static int cariboulite_test_fpga_programming(void* context, void* test_context, int test_num);
static int cariboulite_test_fpga_communication(void* context, void* test_context, int test_num);
static int cariboulite_test_fpga_id_resistors(void* context, void* test_context, int test_num);
static int cariboulite_test_fpga_soft_reset(void* context, void* test_context, int test_num);
static int cariboulite_test_fpga_switch(void* context, void* test_context, int test_num);
static int cariboulite_test_fpga_leds(void* context, void* test_context, int test_num);
static int cariboulite_test_fpga_smi(void* context, void* test_context, int test_num);
static int cariboulite_test_mixer_reset(void* context, void* test_context, int test_num);
static int cariboulite_test_mixer_communication(void* context, void* test_context, int test_num);
static int cariboulite_test_mixer_versions(void* context, void* test_context, int test_num);
static int cariboulite_test_modem_reset(void* context, void* test_context, int test_num);
static int cariboulite_test_modem_communication(void* context, void* test_context, int test_num);
static int cariboulite_test_modem_version(void* context, void* test_context, int test_num);
static int cariboulite_test_modem_leds(void* context, void* test_context, int test_num);
static int cariboulite_test_modem_interrupt(void* context, void* test_context, int test_num);
static int cariboulite_test_current_system(void* context, void* test_context, int test_num);
static int cariboulite_test_current_modem_rx(void* context, void* test_context, int test_num);
static int cariboulite_test_current_modem_tx(void* context, void* test_context, int test_num);
static int cariboulite_test_current_mixer(void* context, void* test_context, int test_num);
static int cariboulite_test_smi_data(void* context, void* test_context, int test_num);
static int cariboulite_test_rf_loopback(void* context, void* test_context, int test_num);
static int cariboulite_test_rf_tx_power(void* context, void* test_context, int test_num);

//================================================================
// TEST DEFINITIONS
production_test_st tests[] = 
{
	{.name_short = "CURR. SYS",		.test_name = "current_system", 			.group = 4, 	.test_number = cariboulite_test_en_current_system, 			.func = cariboulite_test_current_system,		},
	{.name_short = "FPGA PROG",		.test_name = "fpga_programming", 		.group = 1, 	.test_number = cariboulite_test_en_fpga_programming, 		.func = cariboulite_test_fpga_programming,		},
	{.name_short = "FPGA COMM",		.test_name = "fpga_communication", 		.group = 1, 	.test_number = cariboulite_test_en_fpga_communication, 		.func = cariboulite_test_fpga_communication,	},
	{.name_short = "FPGA IDRES",	.test_name = "fpga_id_resistors", 		.group = 1, 	.test_number = cariboulite_test_en_fpga_id_resistors, 		.func = cariboulite_test_fpga_id_resistors,		},
	{.name_short = "FPGA SFTRST",	.test_name = "fpga_soft_reset", 		.group = 1, 	.test_number = cariboulite_test_en_fpga_reset, 				.func = cariboulite_test_fpga_soft_reset,		},
	{.name_short = "FPGA SWTCH",	.test_name = "fpga_switch", 			.group = 1, 	.test_number = cariboulite_test_en_fpga_switch, 			.func = cariboulite_test_fpga_switch,			},
	{.name_short = "FPGA LEDS",		.test_name = "fpga_leds", 				.group = 1, 	.test_number = cariboulite_test_en_fpga_leds, 				.func = cariboulite_test_fpga_leds,				},
	{.name_short = "FPGA SMI",		.test_name = "fpga_smi", 				.group = 1, 	.test_number = cariboulite_test_en_fpga_smi, 				.func = cariboulite_test_fpga_smi,				},
	{.name_short = "EEPROM",		.test_name = "hat_eeprom", 				.group = 0, 	.test_number = cariboulite_test_en_rpi_id_eeprom, 			.func = cariboulite_test_hat_eeprom,			},
	{.name_short = "MXR RST",		.test_name = "mixer_reset", 			.group = 2, 	.test_number = cariboulite_test_en_mixer_reset, 			.func = cariboulite_test_mixer_reset,			},
	{.name_short = "MXR COMM",		.test_name = "mixer_communication", 	.group = 2, 	.test_number = cariboulite_test_en_mixer_communication, 	.func = cariboulite_test_mixer_communication,	},
	{.name_short = "MXR VER",		.test_name = "mixer_version_id", 		.group = 2, 	.test_number = cariboulite_test_en_mixer_versions, 			.func = cariboulite_test_mixer_versions,		},
	{.name_short = "MDM RST",		.test_name = "modem_reset", 			.group = 3, 	.test_number = cariboulite_test_en_modem_reset, 			.func = cariboulite_test_modem_reset,			},
	{.name_short = "MDM COMM", 		.test_name = "modem_communication", 	.group = 3, 	.test_number = cariboulite_test_en_modem_communication, 	.func = cariboulite_test_modem_communication,	},
	{.name_short = "MDM VER",		.test_name = "modem_version", 			.group = 3, 	.test_number = cariboulite_test_en_modem_versions, 			.func = cariboulite_test_modem_version,			},
	{.name_short = "MDM LED",		.test_name = "modem_leds", 				.group = 3, 	.test_number = cariboulite_test_en_modem_leds, 				.func = cariboulite_test_modem_leds,			},	
	{.name_short = "MDM INT",		.test_name = "modem_interrupt", 		.group = 3, 	.test_number = cariboulite_test_en_modem_interrupt, 		.func = cariboulite_test_modem_interrupt,		},
	{.name_short = "CURR. RX",		.test_name = "current_modem rx", 		.group = 4, 	.test_number = cariboulite_test_en_current_modem_rx, 		.func = cariboulite_test_current_modem_rx,		},
	{.name_short = "CURR. TX",		.test_name = "current_modem tx", 		.group = 4, 	.test_number = cariboulite_test_en_current_modem_tx, 		.func = cariboulite_test_current_modem_tx,		},
	{.name_short = "CURR. MXR",		.test_name = "current_mixer", 			.group = 4, 	.test_number = cariboulite_test_en_current_mixer, 			.func = cariboulite_test_current_mixer,			},
	{.name_short = "SMI DATA",		.test_name = "system_smi_data", 		.group = 5, 	.test_number = cariboulite_test_en_system_smi_data, 		.func = cariboulite_test_smi_data,				},
	{.name_short = "RF LB",			.test_name = "system_rf_loopback",		.group = 5, 	.test_number = cariboulite_test_en_system_rf_loopback, 		.func = cariboulite_test_rf_loopback,			},
	{.name_short = "RF TXPWR",		.test_name = "system_rf_tx_power",		.group = 5, 	.test_number = cariboulite_test_en_system_rf_tx_power, 		.func = cariboulite_test_rf_tx_power,			},
};

#define NUM_OF_TESTS  (sizeof(tests)/sizeof(production_test_st))

//=================================================
int cariboulite_test_current_system(void *context, void* test_context, int test_num)
{
	bool fault = false;
	float current_ma = 0.0f, voltage_mv = 0.0f, power_mw = 0.0f;
	
	bool pass = true;
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	//lcd_writeln(&prod.lcd, "Power on...", "", true);
	hat_powermon_set_power_state(&prod->powermon, true);
	io_utils_usleep(400000);
	
	for (int k = 0; k < 10; k++)
	{
		io_utils_usleep(100000);
		production_monitor_power_fault(prod, &fault, &current_ma, &voltage_mv, &power_mw);
		
		if (fault || current_ma > 250.0f || voltage_mv < 2500.0f || current_ma < 10.0f)
		{
			tests[test_num].test_result_float = current_ma;
			sprintf(tests[test_num].test_result_textual, "Wrong current %.1f mA, low voltage (%.1f mV), fault: %d", current_ma, voltage_mv, fault);
			pass = false;
			hat_powermon_set_power_state(&prod->powermon, false);
		}
		else
		{
			tests[test_num].test_result_float = current_ma;
			sprintf(tests[test_num].test_result_textual, "Pass");
			pass = true;
		}			
	}
	
	return pass;
}

//=================================================
int cariboulite_test_fpga_programming(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;

	ZF_LOGD("Programming FPGA");
	if (cariboulite_configure_fpga (sys, cariboulite_firmware_source_blob, NULL/*sys->firmware_path_operational*/) < 0)
	{
		ZF_LOGE("FPGA programming failed");
		caribou_fpga_close(&sys->fpga);

        sprintf(tests[test_num].test_result_textual, "FPGA programming failed");
		tests[test_num].test_result_float = -1;
		return false;
    }

	sys->system_status = sys_status_minimal_init;
	
	tests[test_num].test_result_float = -1;
	sprintf(tests[test_num].test_result_textual, "Pass");
	tests[test_num].test_pass = true;

	return tests[test_num].test_pass;
}


//=================================================
int cariboulite_test_fpga_communication(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	caribou_fpga_versions_st vers = {0};
	caribou_fpga_get_versions (&sys->fpga, &vers);
	
	if (vers.sys_ver == 1 && vers.sys_manu_id == 1 && vers.sys_ctrl_mod_ver == 1
		&& vers.io_ctrl_mod_ver == 1 && vers.smi_ctrl_mod_ver == 1)
	{
		tests[test_num].test_result_float = -1;
		sprintf(tests[test_num].test_result_textual, "Pass");
		tests[test_num].test_pass = true;
	}
	else
	{
		tests[test_num].test_result_float = -1;
		sprintf(tests[test_num].test_result_textual, 
					"Fail - sys_ver: %02X, sys_manu_id: %02X, sys_ctrl_mod_ver: %02X, io_ctrl_mod_ver: %02X, smi_ctrl_mod_ver: %02X",
					vers.sys_ver, vers.sys_manu_id, vers.sys_ctrl_mod_ver, vers.io_ctrl_mod_ver, vers.smi_ctrl_mod_ver);
		tests[test_num].test_pass = true;
	}
	
	return tests[test_num].test_pass;
}

//=================================================
int cariboulite_test_fpga_id_resistors(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	
	// Reading the configuration from the FPGA (resistor set)
	int led0 = 0, led1 = 0, btn = 0, cfg = 0;
	caribou_fpga_get_io_ctrl_dig (&sys->fpga, &led0, &led1, &btn, &cfg);

	ZF_LOGD("FPGA Digital Values: led0: %d, led1: %d, btn: %d, CFG[0..3]: [%d,%d,%d,%d]",
		led0, led1, btn, (cfg >> 0) & 0x1, (cfg >> 1) & 0x1, (cfg >> 2) & 0x1, (cfg >> 3) & 0x1);
	sys->fpga_config_resistor_state = cfg;
	
	if (sys->fpga_config_resistor_state != 0xF && sys->fpga_config_resistor_state != 0xE)
	{
		tests[test_num].test_result_float = -1; 
		sprintf(tests[test_num].test_result_textual, "Failed - unrecognized fpga id resistor config %01X (check R38, R39, R40, R41)", cfg);
		tests[test_num].test_pass = true;
	}
	else
	{
		system_type_en type = (cfg & 0x1) ? system_type_cariboulite_full : system_type_cariboulite_ism;
		
		tests[test_num].test_result_float = -1;
		sprintf(tests[test_num].test_result_textual, "Pass - detected %s", (cfg & 0x1) ? "CaribouFull" : "CaribouISM");
		tests[test_num].test_pass = true;
	}
	
	
	return tests[cariboulite_test_en_fpga_programming].test_pass;
}

//=================================================
int cariboulite_test_fpga_soft_reset(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	caribou_fpga_soft_reset(&sys->fpga);
	return cariboulite_test_fpga_communication(context, test_context, cariboulite_test_en_fpga_communication);
}

//=================================================
int cariboulite_test_fpga_switch(void *context, void* test_context, int test_num)
{
	bool pass = false;
	int key1 = 0;
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	lcd_writeln(&prod->lcd, "PRESS CARIBU SW", "<= OR CLICK FAIL", true);
	
	int led0 = 0, led1 = 0, btn = 0, cfg = 0;
	while (1)
	{
		io_utils_usleep(300000);
		caribou_fpga_get_io_ctrl_dig (&sys->fpga, &led0, &led1, &btn, &cfg);
		if (btn == 0)
		{
			pass = true;
			break;
		}
		
		lcd_get_keys(&prod->lcd, &key1, NULL);
		
		if (key1)
		{
			pass = false;
			break;
		}
	}
	
	if (pass)
	{
		tests[test_num].test_result_float = -1;
		sprintf(tests[test_num].test_result_textual, "Pass");
		tests[test_num].test_pass = true;
	}
	else
	{
		tests[test_num].test_result_float = -1;
		sprintf(tests[test_num].test_result_textual, "Fail - didn't detect switch press - aborted by operator. Check switch assembly (S1)");
		tests[test_num].test_pass = false;
	}
	
	return tests[test_num].test_pass;
}

//=================================================
int cariboulite_test_fpga_leds(void *context, void* test_context, int test_num)
{
	bool pass = false;
	int led = 0;
	int key1 = 0, key2 = 0;
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	lcd_writeln(&prod->lcd, "<= YES   BLINKING?", "<= NO", true);
	
	int led0 = 0, led1 = 0, btn = 0, cfg = 0;
	while (1)
	{
		io_utils_usleep(500000);
		caribou_fpga_set_io_ctrl_dig (&sys->fpga, led, led);
		
		led = !led;
		
		lcd_get_keys(&prod->lcd, &key1, &key2);
		if (key1)
		{
			pass = false;
			break;
		}
		
		if (key2)
		{
			pass = true;
			break;
		}
			
	}
	
	if (pass)
	{
		tests[test_num].test_result_float = -1;
		sprintf(tests[test_num].test_result_textual, "Pass");
		tests[test_num].test_pass = true;
	}
	else
	{
		tests[test_num].test_result_float = -1;
		sprintf(tests[test_num].test_result_textual, "Fail - CaribouLite LEDs didn't blink - check D11, D12");
		tests[test_num].test_pass = false;
	}
	
	return tests[test_num].test_pass;
}

//=================================================
int cariboulite_test_fpga_smi(void *context, void* test_context, int test_num)
{
	caribou_fpga_smi_fifo_status_st status = {0};
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	caribou_fpga_get_smi_ctrl_fifo_status (&sys->fpga, &status);
	
	if (status.rx_fifo_09_empty && !status.rx_fifo_09_full &&
		status.rx_fifo_24_empty && !status.rx_fifo_24_full)
	{
		tests[test_num].test_result_float = -1;
		sprintf(tests[test_num].test_result_textual, "Pass");
		tests[test_num].test_pass = true;
	}
	else
	{
		tests[test_num].test_result_float = -1;
		sprintf(tests[test_num].test_result_textual, "Fail - SMI FIFO is not consistent");
		tests[test_num].test_pass = false;
	}
	return tests[test_num].test_pass;
}

//=================================================
int cariboulite_prod_eeprom_programming(sys_st* sys)
{
	int led0 = 0, led1 = 0, btn = 0, cfg = 0;

	// get the configuration resistors
	caribou_fpga_get_io_ctrl_dig (&sys->fpga, &led0, &led1, &btn, &cfg);

	system_type_en type = (cfg & 0x1) ? system_type_cariboulite_full : system_type_cariboulite_ism;
	if (type == system_type_cariboulite_full) ZF_LOGI("Detected CaribouLite FULL Version");
	else if (type == system_type_cariboulite_ism) ZF_LOGI("Detected CaribouLite ISM Version");
	hat.product_id = type;

	hat_generate_write_config(&hat);

	sleep(1);
	caribou_fpga_set_io_ctrl_dig (&sys->fpga, 0, 0);
	return 0;
}

//=================================================
int cariboulite_test_hat_eeprom(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	// EEPROM programming
	ZF_LOGI("Starting EEPROM programming sequence");
	hat_init(&hat);
	cariboulite_prod_eeprom_programming(&cariboulite_sys);
	hat_close(&hat);

	ZF_LOGI("Verifying EEPROM");
	hat_init(&hat);
	
	if (hat.eeprom_initialized == 0)
	{
		tests[test_num].test_result_float = -1;
		sprintf(tests[test_num].test_result_textual, "Fail - hat eeprom programming failed, check U26, R43, R44");
		tests[test_num].test_pass = false;
	}
	else
	{
		tests[test_num].test_result_float = -1;
		sprintf(tests[test_num].test_result_textual, "Pass");
		tests[test_num].test_pass = true;
		hat_print(&hat);
	}

	return tests[test_num].test_pass;
}

//=================================================
int cariboulite_test_mixer_reset(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}


//=================================================
int cariboulite_test_mixer_communication(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}


//=================================================
int cariboulite_test_mixer_versions(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_modem_reset(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_modem_communication(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_modem_version(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_modem_leds(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_modem_interrupt(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}


//=================================================
int cariboulite_test_current_modem_rx(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_current_modem_tx(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_current_mixer(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_smi_data(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_rf_loopback(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}

//=================================================
int cariboulite_test_rf_tx_power(void *context, void* test_context, int test_num)
{
	sys_st* sys = (sys_st*)context;
	production_sequence_st* prod = (production_sequence_st*)test_context;
	
	return 0;
}




// GIT REPO
#define PRODUCTION_GIT_DIR		"/home/pi/cariboulite_production_results"
#define PRODUCTION_PAT_PATH		"/home/pi/manufacturing_PAT.txt"
#define PRODUCTION_DIT_URI		"github.com/cariboulabs/cariboulite_production_results.git"

//=================================================
int cariboulite_production_connectivity_init(production_sequence_st* prod)
{
	lcd_writeln(&prod->lcd, "CaribouLite Tst", "CONNECTING WIFI", true);
	production_utils_rpi_leds_init(1);
	production_utils_rpi_leds_blink_start_tests();

	// Initialize the wifi
	production_wifi_status_st wifi_stat;
	production_check_wifi_state(&wifi_stat);
	printf("Wifi Status: available: %d, wlan_id = %d, ESSID: %s, InternetAccess: %d\n",
		wifi_stat.available, wifi_stat.wlan_id, wifi_stat.essid, wifi_stat.internet_access);
		
	return 0;
}

//=================================================
int cariboulite_production_app_close(production_sequence_st* prod)
{
	ZF_LOGI("CLOSING...");
	production_close(prod);
	return 0;
}

//=================================================
int main(int argc, char *argv[])
{
	bool fault = false;
	float i, v, p;
	production_sequence_st prod = {};
	
	cariboulite_init_system_production(&cariboulite_sys);

	if (production_init(&prod, tests, NUM_OF_TESTS, &cariboulite_sys) != 0)
	{
		ZF_LOGE("Couldn't init production testing");
		return 0;
	}

	// wifi connection
	if (cariboulite_production_connectivity_init(&prod) != 0)
	{
		ZF_LOGE("error loading init source");
		return -1;
	}
	
	production_set_git_repo(&prod, PRODUCTION_PAT_PATH, PRODUCTION_DIT_URI, PRODUCTION_GIT_DIR);
	production_git_sync_sequence(&prod, "auto commit");

	ZF_LOGI("WELLCOME!!");
	lcd_writeln(&prod.lcd, "CaribouLite Tst", "WELLCOME! (3)", true);
	sleep(1);
	lcd_writeln(&prod.lcd, "CaribouLite Tst", "WELLCOME! (2)", true);
	sleep(1);
	lcd_writeln(&prod.lcd, "CaribouLite Tst", "WELLCOME! (1)", true);
	sleep(1);
	
	while (1)
	{
		int ret = 0;
		production_wait_for_button(&prod, lcd_button_bottom, "MOUNT, START", "<== CLICK HERE");
		
		ret = production_start_tests(&prod);

		// close the driver and release resources
		production_utils_rpi_leds_init(0);
				
		if (ret == false)
		{
			production_wait_for_button(&prod, lcd_button_bottom, "FAIL! UNMOUNT", "<== CLICK HERE");
		}
		production_rewind(&prod);
	}
	
	cariboulite_production_app_close(&prod);
	cariboulite_deinit_system_production(&cariboulite_sys);
	
    return 0;
}
