#include <stdio.h>
#include "cariboulite.h"
#include "cariboulite_setup.h"

//=================================================
typedef enum
{
	app_selection_hard_reset_fpga = 0,
	app_selection_soft_reset_fpga,
	app_selection_versions,
	app_selection_program_fpga,
	app_selection_self_test,
	app_selection_fpga_dig_control,
	app_selection_fpga_rffe_control,
	app_selection_fpga_smi_fifo,
	app_selection_modem_tx_cw,
	app_selection_smi_streaming,
	app_selection_smi_stress_test,
	app_selection_quit = 99,
} app_selection_en;

typedef void (*handle_cb)(sys_st *sys);

typedef struct
{
	app_selection_en num;
	handle_cb handle;
	char text[256];
} app_menu_item_st;

static void app_hard_reset_fpga(sys_st *sys);
static void app_soft_reset_fpga(sys_st *sys);
static void app_versions_printout(sys_st *sys);
static void app_fpga_programming(sys_st *sys);
static void app_self_test(sys_st *sys);
static void fpga_control_io(sys_st *sys);
static void fpga_rf_control(sys_st *sys);
static void fpga_smi_fifo(sys_st *sys);
static void modem_tx_cw(sys_st *sys);

//=================================================
app_menu_item_st handles[] =
{
	{app_selection_hard_reset_fpga, app_hard_reset_fpga, "Hard reset FPGA",},
	{app_selection_hard_reset_fpga, app_soft_reset_fpga, "Soft reset FPGA",},
	{app_selection_versions, app_versions_printout, "Print out versions",},
	{app_selection_program_fpga, app_fpga_programming, "Program FPGA",},
	{app_selection_self_test, app_self_test, "Perform a Self-Test",},
	{app_selection_fpga_dig_control, fpga_control_io, "FPGA Diginal I/O",},
	{app_selection_fpga_rffe_control, fpga_rf_control, "FPGA RFFE control",},
	{app_selection_fpga_smi_fifo, fpga_smi_fifo, "FPGA SMI fifo status",},
	{app_selection_modem_tx_cw, modem_tx_cw, "Modem transmit CW signal",},
};
#define NUM_HANDLES 	(int)(sizeof(handles)/sizeof(app_menu_item_st))


//=================================================
static void app_hard_reset_fpga(sys_st *sys)
{
	caribou_fpga_hard_reset(&sys->fpga);
}

//=================================================
static void app_soft_reset_fpga(sys_st *sys)
{
	caribou_fpga_soft_reset(&sys->fpga);
}

//=================================================
static void app_versions_printout(sys_st *sys)
{
	printf("\nBoard Information (HAT)\n");
	hat_print_board_info(&sys->board_info);

	printf("\nFPGA Versions:\n");
	caribou_fpga_get_versions (&sys->fpga, NULL);

	printf("\nModem Versions:\n");
	uint8_t pn, vn;
	at86rf215_print_version(&sys->modem);

	printf("\nLibrary Versions:\n");
	cariboulite_lib_version_st lib_vers = {0};
	cariboulite_lib_version(&lib_vers);
	printf("	(Major, Minor, Rev): (%d, %d, %d)\n", lib_vers.major_version,
												lib_vers.minor_version,
												lib_vers.revision);
}

//=================================================
static void app_fpga_programming(sys_st *sys)
{
	app_hard_reset_fpga(sys);

	printf("FPGA Programming:\n");
	sys->force_fpga_reprogramming = true;
	int res = cariboulite_configure_fpga (sys, cariboulite_firmware_source_blob, NULL);
	if (res < 0)
	{
		printf("	ERROR: FPGA programming failed `%d`\n", res);
		return;
	}
	printf("	FPGA programming successful, Versions:\n");

	caribou_fpga_soft_reset(&sys->fpga);
	io_utils_usleep(100000);

	caribou_fpga_get_versions (&sys->fpga, NULL);

	caribou_fpga_set_io_ctrl_mode (&sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
}

//=================================================
static void app_self_test(sys_st *sys)
{
	cariboulite_self_test_result_st res = {0};
	cariboulite_self_test(sys, &res);
}

//=================================================
static void fpga_control_io(sys_st *sys)
{
	int choice = 0;
	int led0 = 0, led1 = 0, btn = 0, cfg = 0;
	while (1)
	{
		caribou_fpga_get_io_ctrl_dig (&sys->fpga, &led0, &led1, &btn, &cfg);
		printf("\n	FPGA Digital I/O state:\n");
		printf("		LED0 = %d, LED1 = %d, BTN = %d, CFG = (%d, %d, %d, %d)\n",
					led0, led1, btn,
					(cfg >> 3) & 0x1 == 1,
					(cfg >> 2) & 0x1 == 1,
					(cfg >> 1) & 0x1 == 1,
					(cfg >> 0) & 0x1 == 1);

		printf("	[1] Toggle LED0\n	[2] Toggle LED1\n	[99] Return to Menu\n	Choice:");
		if (scanf("%d", &choice) != 1) continue;
		switch(choice)
		{
			case 1:
				led0 = !led0;
				caribou_fpga_set_io_ctrl_dig (&sys->fpga, led0, led1);
				break;
			case 2:
				led1 = !led1;
				caribou_fpga_set_io_ctrl_dig (&sys->fpga, led0, led1);
				break;
			case 99: return;
			default: continue;
		}
	}
}

//=================================================
static void fpga_rf_control(sys_st *sys)
{
	int choice = 0;
	uint8_t debug = 0;
	caribou_fpga_io_ctrl_rfm_en mode;
	while (1)
	{
		caribou_fpga_get_io_ctrl_mode (&sys->fpga, &debug, &mode);
		printf("\n	FPGA RFFE state:\n");
		printf("		DEBUG = %d, MODE: '%s'\n", debug, caribou_fpga_get_mode_name (mode));

		printf("	Available Modes:\n");
		for (int i=caribou_fpga_io_ctrl_rfm_low_power; i<=caribou_fpga_io_ctrl_rfm_tx_hipass; i++)
		{
			printf("	[%d] %s\n", i, caribou_fpga_get_mode_name (i));
		}
		printf("	[99] Return to main menu\n");
		printf("\n	Choose a new mode:    ");
		if (scanf("%d", &choice) != 1) continue;

		if (choice == 99) return;
		if (choice <caribou_fpga_io_ctrl_rfm_low_power || choice >caribou_fpga_io_ctrl_rfm_tx_hipass)
		{
			printf("	Wrong choice '%d'\n", choice);
			continue;
		}

		caribou_fpga_set_io_ctrl_mode (&sys->fpga, 0, (caribou_fpga_io_ctrl_rfm_en)choice);
	}
}

//=================================================
static void fpga_smi_fifo(sys_st *sys)
{
	caribou_fpga_smi_fifo_status_st status = {0};
	caribou_fpga_get_smi_ctrl_fifo_status (&sys->fpga, &status);
	
	printf("	FPGA SMI info: RX_FIFO_09_EMPTY: %d, RX_FIFO_09_FULL: %d\n", status.rx_fifo_09_empty, status.rx_fifo_09_full);
	printf("	FPGA SMI info: RX_FIFO_24_EMPTY: %d, RX_FIFO_24_FULL: %d\n", status.rx_fifo_24_empty, status.rx_fifo_24_full);
}

//=================================================
static void modem_tx_cw(sys_st *sys)
{
	double current_freq = 900e6;
	float current_power = 14;
	int state = 0;
	int choice = 0;

	// create the radio
	cariboulite_radio_state_st radio = {0};

	cariboulite_radio_init(&radio, sys);
	cariboulite_radio_set_tx_power(&radio, -12);		// start low to not burn system when not needed :)
	cariboulite_radio_set_frequency(&radio, true, &current_freq);
	cariboulite_radio_activate_channel(&radio, 0);
	cariboulite_radio_set_cw_outputs(&radio, true);
	cariboulite_radio_sync_information(&radio);

	current_freq = radio.actual_rf_frequency;
	current_power = radio.tx_power;
	state = radio.state == at86rf215_radio_state_cmd_rx;

	while (1)
	{
		printf("	Parameters:\n");
		printf("	[1] Frequency [%.2f MHz]\n", current_freq);
		printf("	[2] Power out [%.2f dBm]\n", current_power);
		printf("	[3] On/off CW output [%s]\n", state?"ON":"OFF");
		printf("	[4] TX PA ANT1\n");
		printf("	[5] TX PA ANT2\n");
		printf("	[6] TX Bypass ANT1\n");
		printf("	[7] TX Bypass ANT2\n");
		printf("	[99] Return to Main Menu\n");
		printf("	Choice: ");
		if (scanf("%d", &choice) != 1) continue;

		if (choice == 1)
		{
			printf("	Enter frequency [MHz]:   ");
			if (scanf("%lf", &current_freq) != 1) continue;

			cariboulite_radio_set_frequency(&radio, true, &current_freq);
			cariboulite_radio_set_tx_power(&radio, current_power);
			if (state == false)
			{
				cariboulite_radio_activate_channel(&radio, 0);
			}
			current_freq = radio.actual_rf_frequency;
		}
		else if (choice == 2)
		{
			printf("	Enter power [dBm]:   ");
			if (scanf("%f", &current_power) != 1) continue;

			cariboulite_radio_set_tx_power(&radio, current_power);
			current_power = radio.tx_power;
		}
		else if (choice == 3)
		{
			state = !state;
			cariboulite_radio_activate_channel(&radio, state);
			printf("	Power output was %s\n\n", state?"ENABLED":"DISABLED");
			if (state == 1) cariboulite_radio_set_tx_power(&radio, current_power);
		}
		else if (choice >= 4 && choice <= 7)
		{
			switch(choice)
			{
				//// TODO
				default: break;
			}
		}
		else if (choice == 99)
		{
			break;
		}
	}

	cariboulite_radio_dispose(&radio);
}

//=================================================
int app_menu(sys_st* sys)
{
	int choice = 0;
	while (1)
	{
		printf("\n");																			
		printf("	   ____           _ _                 _     _ _         \n");
		printf("	  / ___|__ _ _ __(_) |__   ___  _   _| |   (_) |_ ___   \n");
		printf("	 | |   / _` | '__| | '_ \\ / _ \\| | | | |   | | __/ _ \\  \n");
		printf("	 | |__| (_| | |  | | |_) | (_) | |_| | |___| | ||  __/  \n");
		printf("	  \\____\\__,_|_|  |_|_.__/ \\___/ \\__,_|_____|_|\\__\\___|  \n");
		printf("\n\n");

		printf("	Select a function:\n");
		for (int i = 0; i < NUM_HANDLES; i++)
		{
			printf("      [%d]  %s\n", handles[i].num, handles[i].text);
		}
		printf("      [%d]  %s\n", app_selection_quit, "Quit");

		printf("    Choice:   ");
		if (scanf("%d", &choice) != 1) continue;

		if ((app_selection_en)(choice) == app_selection_quit) return 0;
		for (int i = 0; i < NUM_HANDLES; i++)
		{
			if (handles[i].num == (app_selection_en)(choice))
			{
				if (handles[i].handle != NULL)
				{
					printf("\n\n=====================================\n");
					handles[i].handle(sys);
					printf("\n=====================================\n\n");
				}
				else
				{
					printf("    Choice %d is not implemented\n", choice);
				}
			}
		}
	}
	return 1;
}