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
	app_selection_modem_rx_iq,
	
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
static void modem_rx_iq(sys_st *sys);

//=================================================
app_menu_item_st handles[] =
{
	{app_selection_hard_reset_fpga, app_hard_reset_fpga, "Hard reset FPGA",},
	{app_selection_soft_reset_fpga, app_soft_reset_fpga, "Soft reset FPGA",},
	{app_selection_versions, app_versions_printout, "Print board info and versions",},
	{app_selection_program_fpga, app_fpga_programming, "Program FPGA",},
	{app_selection_self_test, app_self_test, "Perform a Self-Test",},
	{app_selection_fpga_dig_control, fpga_control_io, "FPGA Diginal I/O",},
	{app_selection_fpga_rffe_control, fpga_rf_control, "FPGA RFFE control",},
	{app_selection_fpga_smi_fifo, fpga_smi_fifo, "FPGA SMI fifo status",},
	{app_selection_modem_tx_cw, modem_tx_cw, "Modem transmit CW signal",},
	{app_selection_modem_rx_iq, modem_rx_iq, "Modem receive I/Q stream",},
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
	printf("Board Information (HAT)\n");
	cariboulite_print_board_info(sys, false);
	caribou_fpga_get_versions (&sys->fpga, NULL);
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
	double current_freq_lo = 900e6;
	double current_freq_hi = 2400e6;
	float current_power_lo = -12;
	float current_power_hi = -12;
	
	int state_lo = 0;
	int state_hi = 0;
	int choice = 0;

	cariboulite_radio_state_st *radio_low = &sys->radio_low;
	cariboulite_radio_state_st *radio_hi = &sys->radio_high;

	// output power
	cariboulite_radio_set_tx_power(radio_low, current_power_lo);
	cariboulite_radio_set_tx_power(radio_hi, current_power_hi);
	
	// frequency
	cariboulite_radio_set_frequency(radio_low, true, &current_freq_lo);
	cariboulite_radio_set_frequency(radio_hi, true, &current_freq_hi);
	
	// deactivate - just to be sure
	cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_tx, false);
	cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_tx, false);
	
	// setup cw outputs from modem
	cariboulite_radio_set_cw_outputs(radio_low, false, true);
	cariboulite_radio_set_cw_outputs(radio_hi, false, true);
	
	// synchronize
	cariboulite_radio_sync_information(radio_low);
	cariboulite_radio_sync_information(radio_hi);

	// update params
	current_freq_lo = radio_low->actual_rf_frequency;
	current_freq_hi = radio_hi->actual_rf_frequency;
	current_power_lo = radio_low->tx_power;
	current_power_hi = radio_hi->tx_power;
	
	state_lo = radio_low->state == at86rf215_radio_state_cmd_rx;
	state_hi = radio_hi->state == at86rf215_radio_state_cmd_rx;

	while (1)
	{
		printf("	Parameters:\n");
		printf("	[1] Frequency @ Low Channel [%.2f MHz]\n", current_freq_lo);
		printf("	[2] Frequency @ High Channel [%.2f MHz]\n", current_freq_hi);
		printf("	[3] Power out @ Low Channel [%.2f dBm]\n", current_power_lo);
		printf("	[4] Power out @ High Channel [%.2f dBm]\n", current_power_hi);
		printf("	[5] On/off CW output @ Low Channel [Currently %s]\n", state_lo?"ON":"OFF");
		printf("	[6] On/off CW output @ High Channel [Currently %s]\n", state_hi?"ON":"OFF");
		printf("	[99] Return to Main Menu\n");
		printf("	Choice: ");
		if (scanf("%d", &choice) != 1) continue;
		
		switch (choice)
		{
			//---------------------------------------------------------
			case 1:
			{
				printf("	Enter frequency @ Low Channel [Hz]:   ");
				if (scanf("%lf", &current_freq_lo) != 1) continue;

				cariboulite_radio_set_frequency(radio_low, true, &current_freq_lo);
				cariboulite_radio_set_tx_power(radio_low, current_power_lo);
				if (state_lo == false)
				{
					cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_tx, false);
				}
				current_freq_lo = radio_low->actual_rf_frequency;
			}
			break;
			
			//---------------------------------------------------------
			case 2:
			{
				printf("	Enter frequency @ High Channel [Hz]:   ");
				if (scanf("%lf", &current_freq_hi) != 1) continue;

				cariboulite_radio_set_frequency(radio_hi, true, &current_freq_hi);
				cariboulite_radio_set_tx_power(radio_hi, current_power_hi);
				if (state_hi == false)
				{
					cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_tx, false);
				}
				current_freq_hi = radio_hi->actual_rf_frequency;
			}
			break;
			
			//---------------------------------------------------------
			case 3:
			{
				printf("	Enter power @ Low Channel [dBm]:   ");
				if (scanf("%f", &current_power_lo) != 1) continue;

				cariboulite_radio_set_tx_power(radio_low, current_power_lo);
				current_power_lo = radio_low->tx_power;
			}
			break;
			
			//---------------------------------------------------------
			case 4:
			{
				printf("	Enter power @ High Channel [dBm]:   ");
				if (scanf("%f", &current_power_hi) != 1) continue;

				cariboulite_radio_set_tx_power(radio_hi, current_power_hi);
				current_power_hi = radio_hi->tx_power;
			}
			break;
			
			//---------------------------------------------------------
			case 5:
			{
				state_lo = !state_lo;
				cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_tx, state_lo);
				//printf("	Power output was %s\n\n", state_lo?"ENABLED":"DISABLED");
				if (state_lo == 1) cariboulite_radio_set_tx_power(radio_low, current_power_lo);
			}
			break;
			
			//---------------------------------------------------------
			case 6: 
			{
				state_hi = !state_hi;
				cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_tx, state_hi);
				//printf("	Power output was %s\n\n", state_hi?"ENABLED":"DISABLED");
				if (state_hi == 1) cariboulite_radio_set_tx_power(radio_hi, current_power_hi);
			}
			break;
			
			//---------------------------------------------------------
			case 99: 
			{
				return;
			}
			break;
			
			//---------------------------------------------------------
			default: break;
		}
	}
}

//=================================================
typedef struct 
{
    bool active;
    sys_st *sys;
    
    cariboulite_radio_state_st *radio_low;
    cariboulite_radio_state_st *radio_hi;
    
    bool *low_active;
    bool *high_active;
} iq_test_reader_st;

static void print_iq(char* prefix, caribou_smi_sample_complex_int16* buffer, size_t num_samples, int num_head_tail)
{
    int i;
    
    int ii = 10000, qq = 10000;
    size_t same_count = 0;
    size_t max_same_count = 0;
    
    for (i = 0; i < (int)num_samples; i++)
    {
        if (ii == buffer[i].i && qq == buffer[i].q)
        {
            same_count ++;
            if (same_count > max_same_count)
            {
                max_same_count = same_count;
            }
        }
        else
        {
            same_count = 1;
        }
        
        ii = buffer[i].i;
        qq = buffer[i].q;
    }
    
    printf("%s N=%d SC=%d: ", prefix, num_samples, max_same_count);
    
    for (i = 0; i < num_head_tail; i++)
    {
        printf("[%d, %d] ", buffer[i].i, buffer[i].q);
    }
    printf(". . . ");
    for (i = num_samples-num_head_tail; i < (int)num_samples; i++)
    {
        printf("[%d, %d] ", buffer[i].i, buffer[i].q);
    }
    printf("\n");
}

static void* reader_thread_func(void* arg)
{
    iq_test_reader_st* ctrl = (iq_test_reader_st*)arg;
    cariboulite_radio_state_st *cur_radio = NULL;
    size_t read_len = caribou_smi_get_native_batch_samples(&ctrl->sys->smi);
    
    // allocate buffer
    caribou_smi_sample_complex_int16* buffer = malloc(sizeof(caribou_smi_sample_complex_int16)*read_len);
    caribou_smi_sample_meta* metadata = malloc(sizeof(caribou_smi_sample_meta)*read_len);
    
    printf("Entering sampling thread\n");
	while (ctrl->active)
    {
        if (*ctrl->low_active)
        {
            cur_radio = ctrl->radio_low;
        }
        else if (*ctrl->high_active)
        {
            cur_radio = ctrl->radio_hi;
        }
        else
        {
            cur_radio = NULL;
            usleep(10000);
        }
        
        if (cur_radio)
        {
            int ret = cariboulite_radio_read_samples(cur_radio, buffer, metadata, read_len);
            if (ret < 0)
            {
                if (ret == -1)
                {
                    printf("reader thread failed to read SMI!\n");
                }
            }
            else print_iq("Rx", buffer, ret, 4);
        }
    }
    printf("Leaving sampling thread\n");
    free(buffer);
    free(metadata);
    return NULL;
}

static void modem_rx_iq(sys_st *sys)
{
	int choice = 0;
	bool low_active_rx = false;
	bool high_active_rx = false;
    bool push_debug = false;
    bool pull_debug = false;
    bool lfsr_debug = false;
	double current_freq_lo = 900e6;
	double current_freq_hi = 2400e6;
    
    iq_test_reader_st ctrl = {0};
	
	// create the radio
	cariboulite_radio_state_st *radio_low = &sys->radio_low;
	cariboulite_radio_state_st *radio_hi = &sys->radio_high;
    
    ctrl.active = true;
    ctrl.radio_low = radio_low;
    ctrl.radio_hi = radio_hi;
    ctrl.sys = sys;
    ctrl.low_active = &low_active_rx;
    ctrl.high_active = &high_active_rx;
    
    // start the reader thread
    pthread_t reader_thread;
    if (pthread_create(&reader_thread, NULL, &reader_thread_func, &ctrl) != 0)
    {
        printf("reader thread creation failed\n");
        return;
    }

	// frequency
	cariboulite_radio_set_frequency(radio_low, true, &current_freq_lo);
	cariboulite_radio_set_frequency(radio_hi, true, &current_freq_hi);
	
	// synchronize
	cariboulite_radio_sync_information(radio_low);
	cariboulite_radio_sync_information(radio_hi);
	
	cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_rx, false);
	cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_rx, false);
    caribou_smi_set_debug_mode(&sys->smi, caribou_smi_none);
    
	while (1)
	{
		printf("	Parameters:\n");
		printf("	[1] Ch1 (%.5f MHz) RX %s\n", current_freq_lo / 1e6, low_active_rx?"Active":"Not Active");
		printf("	[2] Ch2 (%.5f MHz) RX %s\n", current_freq_hi / 1e6, high_active_rx?"Active":"Not Active");
        printf("	[3] Push Debug %s\n", push_debug?"Active":"Not Active");
        printf("	[4] Pull Debug %s\n", pull_debug?"Active":"Not Active");
        printf("	[5] LFSR Debug %s\n", lfsr_debug?"Active":"Not Active");
		printf("	[99] Return to main menu\n");
	
		printf("	Choice: ");
		if (scanf("%d", &choice) != 1) continue;
		
		switch (choice)
		{
			//--------------------------------------------
			case 1:
			{   
                if (!low_active_rx && high_active_rx)
                {
                    // if high is currently active - deactivate it
                    high_active_rx = false;
                    printf("Turning on Low channel => High channel off\n");
                    cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_rx, false);
                }
                
				low_active_rx = !low_active_rx;
			}
			break;
			
			//--------------------------------------------
			case 2:
			{
                if (!high_active_rx && low_active_rx)
                {
                    // if low is currently active - deactivate it
                    low_active_rx = false;
                    printf("Turning on High channel => Low channel off\n");
                    cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_rx, false);
                }
                
				high_active_rx = !high_active_rx;
			}
			break;
            
            //--------------------------------------------
			case 3:
			{
                push_debug = !push_debug;
                
                if (push_debug)
                {
                    pull_debug = false;
                    lfsr_debug = false;
                    caribou_smi_set_debug_mode(&sys->smi, caribou_smi_push);
                }
                else caribou_smi_set_debug_mode(&sys->smi, caribou_smi_none);
                
                caribou_fpga_set_debug_modes (&sys->fpga, push_debug, pull_debug, lfsr_debug);
			}
			break;
            
            //--------------------------------------------
			case 4:
			{
                pull_debug = !pull_debug;
                
                if (pull_debug)
                {
                    push_debug = false;
                    lfsr_debug = false;
                    caribou_smi_set_debug_mode(&sys->smi, caribou_smi_pull);
                }
                else caribou_smi_set_debug_mode(&sys->smi, caribou_smi_none);
                
                caribou_fpga_set_debug_modes (&sys->fpga, push_debug, pull_debug, lfsr_debug);
			}
			break;
            
            //--------------------------------------------
			case 5:
			{
                lfsr_debug = !lfsr_debug;
                
                if (lfsr_debug)
                {
                    push_debug = false;
                    pull_debug = false;
                    caribou_smi_set_debug_mode(&sys->smi, caribou_smi_lfsr);
                }
                else caribou_smi_set_debug_mode(&sys->smi, caribou_smi_none);
                
                caribou_fpga_set_debug_modes (&sys->fpga, push_debug, pull_debug, lfsr_debug);
			}
			break;
			
			//--------------------------------------------
			case 99:
                low_active_rx = false;
                high_active_rx = false;
                ctrl.active = false;
                pthread_join(reader_thread, NULL);
                
				cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_rx, false);
				cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_rx, false);
				return;
			
			//--------------------------------------------
			default:
			{
			}
			break;
		}
	}
}

//=================================================
int app_menu(sys_st* sys)
{
	printf("\n");																			
	printf("	   ____           _ _                 _     _ _         \n");
	printf("	  / ___|__ _ _ __(_) |__   ___  _   _| |   (_) |_ ___   \n");
	printf("	 | |   / _` | '__| | '_ \\ / _ \\| | | | |   | | __/ _ \\  \n");
	printf("	 | |__| (_| | |  | | |_) | (_) | |_| | |___| | ||  __/  \n");
	printf("	  \\____\\__,_|_|  |_|_.__/ \\___/ \\__,_|_____|_|\\__\\___|  \n");
	printf("\n\n");
	
	while (1)
	{
		int choice = -1;
		printf(" Select a function:\n");
		for (int i = 0; i < NUM_HANDLES; i++)
		{
			printf(" [%d]  %s\n", handles[i].num, handles[i].text);
		}
		printf(" [%d]  %s\n", app_selection_quit, "Quit");

		printf("    Choice:   ");
		if (scanf("%d", &choice) != 1) continue;

		if ((app_selection_en)(choice) == app_selection_quit) return 0;
		for (int i = 0; i < NUM_HANDLES; i++)
		{
			if (handles[i].num == (app_selection_en)(choice))
			{
				if (handles[i].handle != NULL)
				{
					printf("\n=====================================\n");
					handles[i].handle(sys);
					printf("\n=====================================\n");
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