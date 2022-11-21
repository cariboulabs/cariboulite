#include <stdio.h>
#include "cariboulite.h"
#include "cariboulite_setup.h"

//=================================================
typedef enum
{
	app_selection_hard_reset_fpga = 0,
	app_selection_versions = 1,
	app_selection_program_fpga = 2,
	app_selection_self_test = 3,
	app_selection_fpga_dig_control = 4,
	app_selection_fpga_rffe_control = 5,
	app_selection_fpga_smi_fifo = 6,
	app_selection_modem_tx_cw = 7,
	app_selection_smi_streaming = 8,
	app_selection_smi_stress_test = 9,
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
static void app_versions_printout(sys_st *sys);
static void app_fpga_programming(sys_st *sys);
static void app_self_test(sys_st *sys);
static void fpga_control_io(sys_st *sys);
static void fpga_rf_control(sys_st *sys);
static void fpga_smi_fifo(sys_st *sys);
static void modem_tx_cw(sys_st *sys);
static void smi_streaming(sys_st *sys);
static void smi_stress_test(sys_st *sys);

//=================================================
app_menu_item_st handles[] =
{
	{app_selection_hard_reset_fpga, app_hard_reset_fpga, "Hard reset FPGA",},
	{app_selection_versions, app_versions_printout, "Print out versions",},
	{app_selection_program_fpga, app_fpga_programming, "Program FPGA",},
	{app_selection_self_test, app_self_test, "Perform a Self-Test",},
	{app_selection_fpga_dig_control, fpga_control_io, "FPGA Diginal I/O",},
	{app_selection_fpga_rffe_control, fpga_rf_control, "FPGA RFFE control",},
	{app_selection_fpga_smi_fifo, fpga_smi_fifo, "FPGA SMI fifo status",},
	{app_selection_modem_tx_cw, modem_tx_cw, "Modem transmit CW signal",},
	{app_selection_smi_streaming, smi_streaming, "SMI streaming",},
	{app_selection_smi_stress_test, smi_stress_test, "SMI Stress Testing",},
};
#define NUM_HANDLES 	(int)(sizeof(handles)/sizeof(app_menu_item_st))


//=================================================
static void app_hard_reset_fpga(sys_st *sys)
{
	hermon_fpga_hard_reset(&sys->fpga);
	io_utils_usleep(100000);
	hermon_fpga_hard_reset(&sys->fpga);
}

//=================================================
static void app_versions_printout(sys_st *sys)
{
	printf("\nBoard Information (HAT)\n");
	hat_print_board_info(&sys->board_info);

	printf("\nFPGA Versions:\n");
	hermon_fpga_get_versions (&sys->fpga, NULL);

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

	hermon_fpga_soft_reset(&sys->fpga, true);
	io_utils_usleep(100000);

	hermon_fpga_get_versions (&sys->fpga, NULL);

	hermon_fpga_set_io_ctrl_mode (&sys->fpga, 0, hermon_fpga_io_ctrl_rx_ant1_lna_bypass);
	//app_self_test(sys);
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
		hermon_fpga_get_io_ctrl_dig (&sys->fpga, &led0, &led1, &btn, &cfg);
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
				hermon_fpga_set_io_ctrl_dig (&sys->fpga, led0, led1);
				break;
			case 2:
				led1 = !led1;
				hermon_fpga_set_io_ctrl_dig (&sys->fpga, led0, led1);
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
	hermon_fpga_io_ctrl_rf_pin_modes_en mode;
	while (1)
	{
		hermon_fpga_get_io_ctrl_mode (&sys->fpga, &debug, &mode);
		printf("\n	FPGA RFFE state:\n");
		printf("		DEBUG = %d, MODE: '%s'\n", debug, hermon_fpga_get_mode_name (mode));

		printf("	Available Modes:\n");
		for (int i=hermon_fpga_io_ctrl_low_power; i<=hermon_fpga_io_ctrl_tx_ant2; i++)
		{
			if (i != hermon_fpga_io_ctrl_reserved) printf("	[%d] %s\n", i, hermon_fpga_get_mode_name (i));
		}
		printf("	[99] Return to main menu\n");
		printf("\n	Choose a new mode:    ");
		if (scanf("%d", &choice) != 1) continue;

		if (choice == 99) return;
		if (choice <hermon_fpga_io_ctrl_low_power || choice >hermon_fpga_io_ctrl_tx_ant2 ||
			choice == hermon_fpga_io_ctrl_reserved)
		{
			printf("	Wrong choice '%d'\n", choice);
			continue;
		}

		hermon_fpga_set_io_ctrl_mode (&sys->fpga, 0, (hermon_fpga_io_ctrl_rf_pin_modes_en)choice);
	}
}

//=================================================
static void fpga_smi_fifo(sys_st *sys)
{
	hermon_fpga_smi_fifo_status_st status = {0};
	hermon_fpga_get_smi_ctrl_fifo_status (&sys->fpga, &status);
	printf("	FPGA SMI info: RX_FIFO_EMPTY: %d, RX_FIFO_FULL: %d\n", status.rx_fifo_09_empty, status.rx_fifo_09_full);
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
				case 4: hermon_fpga_set_io_ctrl_mode (&sys->fpga, 0, hermon_fpga_io_ctrl_tx_ant1); break;
				case 5: hermon_fpga_set_io_ctrl_mode (&sys->fpga, 0, hermon_fpga_io_ctrl_tx_ant2); break;
				case 7: hermon_fpga_set_io_ctrl_mode (&sys->fpga, 0, hermon_fpga_io_ctrl_rx_ant2_lna); break;
				case 6:
				default: hermon_fpga_set_io_ctrl_mode (&sys->fpga, 0, hermon_fpga_io_ctrl_rx_ant1_lna); break;
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
static void app_test_smi_data_callback (void *ctx,                              	// The context of the requesting application
                                        void *serviced_context,                 	// the context of the session within the app
                                        hermon_smi_stream_type_en type,        		// which type of stream is it? read / write?
										hermon_smi_event_type_en ev,				// the event (start / stop)
                                        size_t num_samples,                    		// for "read stream only" - number of read data bytes in buffer
                                        hermon_smi_sample_complex_int16 *cplx_vec, 	// for "read" - complex vector of samples to be analyzed
                                                                                    // for "write" - complex vector of samples to be written into
										hermon_smi_sample_meta *metadat_vec,		// for "read" - the metadata send by the receiver for each sample
																					// for "write" - the metadata to be written by app for each sample
                                        size_t total_length_samples)
{
	hermon_smi_st* smi = (hermon_smi_st*)ctx;
	sys_st* sys_serviced = (sys_st*)serviced_context;

	printf("	Received SMI type: '%s', event: '%s', #Samples: %ld\n", type==0?"Write":"Read",
					ev==0?"Data":(ev==1?"Start":"End"), num_samples);

	if (ev == hermon_smi_event_type_data && cplx_vec != NULL)	// data
	{
		for (int i = 0; i < 10; i++)
		{
			printf("%c(%d, %d), ", metadat_vec[i].sync?'S':' ', cplx_vec[i].i, cplx_vec[i].q);
		}
		printf("\n");
	}
}

static void smi_streaming(sys_st *sys)
{
	bool state = false;
	int choice = 0;
	int streamid = -1;
	double current_freq = 900e6;
	int current_freq_ind = 0;
	cariboulite_antenna_en ant_state = cariboulite_ant1;
	cariboulite_bypass_state_en bypass_state = bypass_off;

	// create the radio
	cariboulite_radio_state_st radio = {0};
	cariboulite_radio_init(&radio, sys);
	cariboulite_radio_set_frequency(&radio, true, &current_freq);
	cariboulite_radio_activate_channel(&radio, false);
	hermon_fpga_set_debug_modes (&sys->fpga, false, false, false);

	cariboulite_set_frontend_state (&radio,
                                    ant_state,
                                    bypass_state,
                                    false);
									
	//hermon_fpga_set_io_ctrl_mode (&radio.cariboulite_sys->fpga, false, hermon_fpga_io_ctrl_rx_ant1_lna_bypass);
	
	while (1)
	{
		printf("	Stream control:\n");
		printf("	[1] %s Stream\n", state?"Stop":"Start");
		printf("	[2] Switch frequency (to %.2f Hz)\n", current_freq_ind==0?910e6:900e6);
		printf("	[3] Switch antenna (to %d)\n", ant_state==cariboulite_ant1?2:1);
		printf("	[4] Switch bypass (to %s)\n", bypass_state==bypass_off?"ON":"OFF");
		printf("	[99] Return to main menu\n");
		printf("	Choice: ");
		if (scanf("%d", &choice) != 1) continue;
		if (choice == 99) break;

		if (choice == 1)
		{
			if (state == false)
			{
				if (cariboulite_radio_create_smi_stream(&radio,
										cariboulite_channel_dir_rx,
										app_test_smi_data_callback,
										sys) != -1)
				{
					state = true;

					// start the modem RX mode
					cariboulite_radio_activate_channel(&radio, true);
					io_utils_usleep(200000);

					fpga_smi_fifo(sys);
					cariboulite_radio_run_pause_stream(&radio,
										cariboulite_channel_dir_rx,
										true);
					//cariboulite_radio_sync_information(&radio);
				}
			}
			else
			{
				state = false;
				cariboulite_radio_run_pause_stream(&radio,
										cariboulite_channel_dir_rx,
										false);

				cariboulite_radio_destroy_smi_stream(&radio, cariboulite_channel_dir_rx);

				// stop the modem RX mode
				cariboulite_radio_activate_channel(&radio, false);

				fpga_smi_fifo(sys);
			}
		}
		else if (choice == 2)
		{
			if (current_freq_ind == 0) 
			{
				current_freq_ind = 1;
				current_freq = 910e6;
			}
			else 
			{
				current_freq_ind = 0;
				current_freq = 900e6;
			}
			cariboulite_radio_set_frequency(&radio, true, &current_freq);
		}
		else if (choice == 3)
		{
			ant_state = (ant_state==cariboulite_ant1)?cariboulite_ant2:cariboulite_ant1;
			cariboulite_set_frontend_state (&radio,
                                    ant_state,
                                    bypass_state,
                                    false);
		}
		else if (choice == 4)
		{
			bypass_state = (bypass_state==bypass_off)?bypass_on:bypass_off;
			cariboulite_set_frontend_state (&radio,
                                    ant_state,
                                    bypass_state,
                                    false);
		}
	}
}

//=================================================
static void app_test_smi_stress_data_callback (void *ctx,                           // The context of the requesting application
                                        void *serviced_context,                 	// the context of the session within the app
                                        hermon_smi_stream_type_en type,        		// which type of stream is it? read / write?
										hermon_smi_event_type_en ev,				// the event (start / stop)
                                        size_t num_samples,                    		// for "read stream only" - number of read data bytes in buffer
                                        hermon_smi_sample_complex_int16 *cplx_vec, 	// for "read" - complex vector of samples to be analyzed
                                                                                    // for "write" - complex vector of samples to be written into
										hermon_smi_sample_meta *metadat_vec,		// for "read" - the metadata send by the receiver for each sample
																					// for "write" - the metadata to be written by app for each sample
                                        size_t total_length_samples)
{
	hermon_smi_st* smi = (hermon_smi_st*)ctx;
	sys_st* sys_serviced = (sys_st*)serviced_context;

	/*printf("	Received SMI type: '%s', event: '%s', #Samples: %ld\n", 
				type==0?"Write":"Read", ev==0?"Data":(ev==1?"Start":"End"), num_samples);*/
				
	if (ev == hermon_smi_event_type_data && cplx_vec != NULL)	// data
	{
		/*int i = 0;
		for (i = 0; i < 8; i ++)
		{
			uint8_t *v = (uint8_t*)&cplx_vec[i];
			printf("  %02X, %02X, %02X, %02X, ", v[0], v[1], v[2], v[3]);
		}
		printf("\n");*/
	}
}

//=================================================
static void smi_stress_test(sys_st *sys)
{
	bool state = false;
	bool push_debug_state = false;
	bool pull_debug_state = false;
	bool smi_debug_state = false;
	int choice = 0;
	int streamid = -1;
	cariboulite_radio_state_st radio = {0};
	cariboulite_radio_init(&radio, sys);
	cariboulite_radio_activate_channel(&radio, false);

	while (1)
	{
		printf("	Stress Stream control:\n");
		printf("	[1] %s Stream\n", state?"Stop":"Start");
		printf("	[2] %s Fifo Push Debug\n", push_debug_state?"Stop":"Start");
		printf("	[3] %s Fifo Pull Debug\n", pull_debug_state?"Stop":"Start");
		printf("	[4] %s SMI Debug\n", smi_debug_state?"Stop":"Start");
		printf("	[99] Return to main menu\n");
		printf("	Choice: ");
		if (scanf("%d", &choice) != 1) continue;
		if (choice == 99) break;

		if (choice == 1)
		{
			if (state == false)
			{
				if (cariboulite_radio_create_smi_stream(&radio,
										cariboulite_channel_dir_rx,
										app_test_smi_stress_data_callback,
										sys) != -1)
				{
					state = true;
					fpga_smi_fifo(sys);
					cariboulite_radio_run_pause_stream(&radio,
										cariboulite_channel_dir_rx,
										true);
				}
			}
			else
			{
				state = false;
				cariboulite_radio_run_pause_stream(&radio,
										cariboulite_channel_dir_rx,
										false);
				cariboulite_radio_destroy_smi_stream(&radio, cariboulite_channel_dir_rx);
				fpga_smi_fifo(sys);
			}
		}
		else if (choice == 2)
		{
			push_debug_state = !push_debug_state;
			hermon_fpga_set_debug_modes (&sys->fpga, push_debug_state, pull_debug_state, smi_debug_state);
			hermon_smi_set_debug_mode(&sys->smi, smi_debug_state, push_debug_state, pull_debug_state);
		}
		else if (choice == 3)
		{
			pull_debug_state = !pull_debug_state;
			hermon_fpga_set_debug_modes (&sys->fpga, push_debug_state, pull_debug_state, smi_debug_state);
			hermon_smi_set_debug_mode(&sys->smi, smi_debug_state, push_debug_state, pull_debug_state);
		}
		else if (choice == 4)
		{
			smi_debug_state = !smi_debug_state;
			hermon_fpga_set_debug_modes (&sys->fpga, push_debug_state, pull_debug_state, smi_debug_state);
			hermon_smi_set_debug_mode(&sys->smi, smi_debug_state, push_debug_state, pull_debug_state);
		}
	}
	hermon_fpga_set_debug_modes (&sys->fpga, false, false, false);
	hermon_smi_set_debug_mode(&sys->smi, false, false, false);
}

//=================================================
int app_menu(sys_st* sys)
{
	int choice = 0;
	while (1)
	{
		printf("\n");
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