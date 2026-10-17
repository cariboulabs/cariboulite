// Enable POSIX/GNU extensions
#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include "audio_demod.h"
#include <stdio.h>
#include "cariboulite.h"
#include "cariboulite_setup.h"
#include "caribou_smi/caribou_smi.h"
#include <time.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <math.h>
#include <assert.h>
#include <stddef.h>   // if you want offsetof
#include <stdint.h>
#include "alsa_sink.h"

_Static_assert(sizeof(caribou_smi_sample_complex_int16) == 4,
               "caribou_smi_sample_complex_int16 must be 4 bytes (2x int16)");

#ifdef CARIBOU_SMI_BYTES_PER_SAMPLE
_Static_assert(CARIBOU_SMI_BYTES_PER_SAMPLE == sizeof(caribou_smi_sample_complex_int16),
               "CARIBOU_SMI_BYTES_PER_SAMPLE must match complex sample size");
#endif

#include "tone_source.h"
#include "alsa_source.h"
#include "nbfm_mod.h"
#include "noise_squelch.h"
#include "demod_worker.h"
#include "pipeline_runtime.h"
#include "tx_pipeline.h"
#include "rx_pipeline.h"
#include "modem_selftest.h"

// included here, to use ncurcus for a text-baused UI for my additions
#include "ncurses.h"

// include here, for testing and access to level registers
#include <at86rf215.h>
#include <rffc507x.h>

// include here, for testing
#define SOURCE "firmware/top.bin"

#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h> 

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
	app_selection_synthesizer,
	app_selection_nbfm_tx_tone,
	app_selection_nbfm_rx,
    app_selection_nbfm_modem_selftest,
	app_selection_monitor_modem_status,
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
static void synthesizer(sys_st *sys);
static void nbfm_tx_tone(sys_st *sys);
static void nbfm_rx(sys_st *sys);
static void monitor_modem_status(sys_st *sys);

// --- forward declarations for pthread entry points ---
//static void* wbfm_demod_thread(void* arg);

//=================================================
app_menu_item_st handles[] =
{
	{app_selection_hard_reset_fpga, app_hard_reset_fpga, "Hard reset FPGA",},
	{app_selection_soft_reset_fpga, app_soft_reset_fpga, "Soft reset FPGA",},
	{app_selection_versions, app_versions_printout, "Print board info and versions",},
	{app_selection_program_fpga, app_fpga_programming, "Program FPGA",},
	{app_selection_self_test, app_self_test, "Perform a Self-Test",},
	{app_selection_fpga_dig_control, fpga_control_io, "FPGA Digital I/O",},
	{app_selection_fpga_rffe_control, fpga_rf_control, "FPGA RFFE control",},
	{app_selection_fpga_smi_fifo, fpga_smi_fifo, "FPGA SMI fifo status",},
	{app_selection_modem_tx_cw, modem_tx_cw, "Modem transmit CW signal",},
	{app_selection_modem_rx_iq, modem_rx_iq, "Modem receive I/Q stream",},
    {app_selection_synthesizer, synthesizer, "Synthesizer 85-4200 MHz",},
	{app_selection_nbfm_tx_tone, nbfm_tx_tone, "NBFM TX Tone (HiF)",},
	{app_selection_nbfm_rx, nbfm_rx, "NBFM RX (HiF)",},
    {app_selection_nbfm_modem_selftest, nbfm_modem_selftest, "NBFM modem Self-Test",},
	{app_selection_monitor_modem_status, monitor_modem_status, "Monitor Modem Status (HiF)",},
};
#define NUM_HANDLES 	(int)(sizeof(handles)/sizeof(app_menu_item_st))

// constants
#define SR   4000000.0   // sample rate
#define DF   NBFM_DEFAULT_DEVIATION_HZ // peak deviation (Hz)
#define FM   700.0       // modulating tone (Hz)
#define AMP  2047.0      // amplitude (safe for 13-bit signed)

//=================================================

static inline void set_rt_and_affinity(void)
{
    // Hard RT priority and CPU affinity for the calling thread
    struct sched_param sp = { .sched_priority = 40 };     // 1..99; 40 is sane
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);

#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(2, &set);                                     // keep this away from CPU0 IRQs
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#endif

    // Avoid page faults while streaming
    mlockall(MCL_CURRENT | MCL_FUTURE);
}

static inline void smi_idle(sys_st* sys) {
    caribou_smi_set_driver_streaming_state(&sys->smi, (smi_stream_state_en)0);
}

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
	int res = cariboulite_configure_fpga (sys, cariboulite_firmware_source_file, SOURCE);
	//int res = cariboulite_configure_fpga (sys, cariboulite_firmware_source_blob, NULL);
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
					(cfg >> 3) & (0x1 == 1),
					(cfg >> 2) & (0x1 == 1),
					(cfg >> 1) & (0x1 == 1),
					(cfg >> 0) & (0x1 == 1));

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
    uint8_t *val = (uint8_t *)&status;
	caribou_fpga_get_smi_ctrl_fifo_status (&sys->fpga, &status);
	
	printf("    FPGA SMI info (%02X):\n", *val);
    printf("        RX FIFO EMPTY: %d\n", status.rx_fifo_empty);
    printf("        TX FIFO FULL: %d\n", status.tx_fifo_full);
    printf("        RX CHANNEL: %d\n", status.smi_channel);
    printf("        RX SMI TEST: %d\n", status.i_smi_test);
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
	
	state_lo = radio_low->state == cariboulite_radio_state_cmd_rx;
	state_hi = radio_hi->state == cariboulite_radio_state_cmd_rx;

	while (1)
	{
		printf("	Parameters:\n");
		printf("	[ 1] Frequency @ Low Channel [%.2f MHz]\n", current_freq_lo);
		printf("	[ 2] Frequency @ High Channel [%.2f MHz]\n", current_freq_hi);
		printf("	[ 3] Power out @ Low Channel [%.2f dBm]\n", current_power_lo);
		printf("	[ 4] Power out @ High Channel [%.2f dBm]\n", current_power_hi);
		printf("	[ 5] On/off CW output @ Low Channel [Currently %s]\n", state_lo?"ON":"OFF");
		printf("	[ 6] On/off CW output @ High Channel [Currently %s]\n", state_hi?"ON":"OFF");
        printf("	[ 7] Low Channel decrease frequency (5MHz)\n");
        printf("	[ 8] Low Channel increase frequency (5MHz)\n");
        printf("	[ 9] Hi Channel decrease frequency (5MHz)\n");
        printf("	[10] Hi Channel increase frequency (5MHz)\n");
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

                cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_tx, false);
				cariboulite_radio_set_frequency(radio_low, true, &current_freq_lo);
				cariboulite_radio_set_tx_power(radio_low, current_power_lo);
				if (state_lo)
				{
					cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_tx, true);
				}
				current_freq_lo = radio_low->actual_rf_frequency;
			}
			break;
			
			//---------------------------------------------------------
			case 2:
			{
				printf("	Enter frequency @ High Channel [Hz]:   ");
				if (scanf("%lf", &current_freq_hi) != 1) continue;

                cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_tx, false);
				cariboulite_radio_set_frequency(radio_hi, true, &current_freq_hi);
				cariboulite_radio_set_tx_power(radio_hi, current_power_hi);               
                
				if (state_hi)
				{
					cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_tx, true);
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
                if (state_lo == 1) cariboulite_radio_set_tx_power(radio_low, current_power_lo);
                cariboulite_radio_set_cw_outputs(radio_low, false, state_lo);
				cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_tx, state_lo);
			}
			break;
			
			//---------------------------------------------------------
			case 6: 
			{
				state_hi = !state_hi;
                if (state_hi == 1) cariboulite_radio_set_tx_power(radio_hi, current_power_hi);
                cariboulite_radio_set_cw_outputs(radio_hi, false, state_hi);
				cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_tx, state_hi);				
			}
			break;
			
            //---------------------------------------------------------
			case 7: 
			{
				current_freq_lo -= 5e6;
                cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_tx, false);
				cariboulite_radio_set_frequency(radio_low, true, &current_freq_lo);
				cariboulite_radio_set_tx_power(radio_low, current_power_lo);
				if (state_lo)
				{
					cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_tx, true);
				}
				//current_freq_lo = radio_low->actual_rf_frequency;
			}
			break;
            
            //---------------------------------------------------------
			case 8: 
			{
				current_freq_lo += 5e6;
                cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_tx, false);
				cariboulite_radio_set_frequency(radio_low, true, &current_freq_lo);
				cariboulite_radio_set_tx_power(radio_low, current_power_lo);
				if (state_lo)
				{
					cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_tx, true);
				}
				//current_freq_lo = radio_low->actual_rf_frequency;
			}
			break;
            
            //---------------------------------------------------------
			case 9: 
			{
				current_freq_hi -= 5e6;
                cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_tx, false);
				cariboulite_radio_set_frequency(radio_hi, true, &current_freq_hi);
				cariboulite_radio_set_tx_power(radio_hi, current_power_hi);               
                
				if (state_hi)
				{
					cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_tx, true);
				}
				//current_freq_hi = radio_hi->actual_rf_frequency;
			}
			break;
            
            //---------------------------------------------------------
			case 10: 
			{
				current_freq_hi += 5e6;
                cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_tx, false);
				cariboulite_radio_set_frequency(radio_hi, true, &current_freq_hi);
				cariboulite_radio_set_tx_power(radio_hi, current_power_hi);               
                
				if (state_hi)
				{
					cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_tx, true);
				}
				//current_freq_hi = radio_hi->actual_rf_frequency;
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

static void print_iq(char* prefix, cariboulite_sample_complex_int16* buffer, size_t num_samples, int num_head_tail)
{
    int i;
    
    if (num_samples == 0) return;
    
    printf("NS: %lu > ", num_samples);
    
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
    pthread_setname_np(pthread_self(), "reader_thread");
    set_rt_and_affinity();
    iq_test_reader_st* ctrl = (iq_test_reader_st*)arg;
    cariboulite_radio_state_st *cur_radio = NULL;
    size_t read_len = caribou_smi_get_native_batch_samples(&ctrl->sys->smi);
    
    // allocate buffer
    cariboulite_sample_complex_int16* buffer = malloc(sizeof(cariboulite_sample_complex_int16)*read_len);
    cariboulite_sample_meta* metadata = malloc(sizeof(cariboulite_sample_meta)*read_len);
    
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
                cariboulite_radio_activate_channel(radio_low, cariboulite_channel_dir_rx, low_active_rx);
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
                cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_rx, high_active_rx);
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
static void synthesizer(sys_st *sys)
{
    int choice = 0;
    cariboulite_radio_state_st *radio_hi = &sys->radio_high;
    double current_freq = 100000000.0;
    bool active = false;
    bool lock = false;
    
    //cariboulite_radio_set_cw_outputs(radio_hi, false, false);
    //cariboulite_radio_activate_channel(radio_hi, cariboulite_channel_dir_tx, false);
    caribou_fpga_set_io_ctrl_mode (&radio_hi->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_tx_lowpass);
    cariboulite_radio_ext_ref (radio_hi->sys, cariboulite_ext_ref_32mhz);
    rffc507x_set_frequency(&radio_hi->sys->mixer, current_freq);
    lock = cariboulite_radio_wait_mixer_lock(radio_hi, 10);
    rffc507x_calibrate(&radio_hi->sys->mixer);
    
    while (1)
	{
		printf("	Parameters:\n");
		printf("	[1] Set Frequency (%.5f MHz, LOCKED=%d)\n", current_freq / 1e6, lock);
        printf("	[2] Activate [%s]\n", active?"Active":"Not Active");
		printf("	[99] Return to main menu\n");
	
		printf("	Choice: ");
		if (scanf("%d", &choice) != 1) continue;
		
		switch (choice)
		{
			//--------------------------------------------
			case 1:
			{   
                printf("	Enter frequency [Hz]:   ");
				if (scanf("%lf", &current_freq) != 1) continue;
                
                if (current_freq < 2400e6) caribou_fpga_set_io_ctrl_mode (&radio_hi->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_tx_lowpass);
                else caribou_fpga_set_io_ctrl_mode (&radio_hi->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_tx_hipass);
                
                double act_freq = rffc507x_set_frequency(&radio_hi->sys->mixer, current_freq);
                lock = cariboulite_radio_wait_mixer_lock(radio_hi, 10);
                
				if (active)
				{
					rffc507x_output_lo(&radio_hi->sys->mixer, active);
				}
				current_freq = act_freq;
			}
			break;
            
            //--------------------------------------------
			case 2:
			{   
                active = !active;
                rffc507x_output_lo(&radio_hi->sys->mixer, active);
			}
			break;
            
            //--------------------------------------------
			case 99:
                active = false;
                
                rffc507x_output_lo(&radio_hi->sys->mixer, false);
                cariboulite_radio_set_cw_outputs(radio_hi, false, false);
                caribou_fpga_set_io_ctrl_mode (&radio_hi->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_bypass);
				return;
            
            //--------------------------------------------
			default:
			{
			}
			break;
        }
    }
    
}

//======helper function=====display a 8 bit number==========================
void print_binairy8(uint8_t value) {
    for (int i = 7; i >= 0; --i) {
        putchar((value & (1 << i)) ? '1' : '0');
        if (i % 8 == 0 && i != 0) putchar(' ');
    }
    putchar('\n');
}

static void nbfm_tx_tone(sys_st *sys)
{
    tx_pipeline_t tx = {0};
    tx_params_t par = {
        .freq_hz      = 430100000.0,
        .tx_power_dbm = -3,
        .tone_mode    = true,
        .tone_hz      = 600.0f,
        .tone_amp     = 0.4f,
        .mic_dev      = NULL,
        .out_scale    = 4000.0f,
        .f_dev_hz     = NBFM_DEFAULT_DEVIATION_HZ,
    };

    if (tx_pipeline_init(&tx, sys, &sys->radio_high, &par) != 0) {
        fprintf(stderr, "[tx_tone] init failed\n");
        return;
    }

    for (;;) {
        int choice = -1;
        printf("TX freq: %.0f Hz  power: %d dBm\n", par.freq_hz, par.tx_power_dbm);
        printf("TX rate: %u samples/s; %zu samples/10ms; gap %u (verified at setup)\n",
               par.rf_fs ? par.rf_fs : 4000000, tx_pipeline_frame_samples(&tx),
               4000000 / (par.rf_fs ? par.rf_fs : 4000000) - 1);
        printf(" [1] Toggle NBFM TX   [2] Select 2 MS/s   [4] Select 4 MS/s   [99] Return\n");
        printf(" Choice: ");
        if (scanf("%d", &choice) != 1) continue;
        if (choice == 1) {
            if (!tx.running) {
                // Join idle workers before resetting shared FPGA FIFO state.
                // Recreate DSP too, discarding audio left by the previous run.
                tx_pipeline_destroy(&tx);
                if (caribou_fpga_soft_reset(&sys->fpga) != 0 ||
                    tx_pipeline_init(&tx, sys, &sys->radio_high, &par) != 0) {
                    fprintf(stderr, "[tx_tone] clean restart failed\n");
                    break;
                }
                if (tx_pipeline_start(&tx) == 0) printf("TX: ON\n");
            } else {
                tx_pipeline_stop(&tx);
                printf("TX: OFF\n");
            }
        } else if (choice == 2 || choice == 4) {
            if (tx.running) {
                printf("Stop TX before changing sample rate.\n");
                continue;
            }
            tx_pipeline_destroy(&tx);
            par.rf_fs = choice * 1000000;
            if (tx_pipeline_init(&tx, sys, &sys->radio_high, &par) != 0) {
                fprintf(stderr, "[tx_tone] rate change failed\n");
                break;
            }
        } else if (choice == 99) {
            break;
        }
    }

    tx_pipeline_destroy(&tx);
    cariboulite_radio_set_tx_samp_cutoff_flt(&sys->radio_high, 4000000);
    printf("NBFM TX tone stopped.\n");
}

static void nbfm_rx(sys_st *sys)
{
    rx_pipeline_t rx = {0};
    rx_params_t par = {
        .freq_hz       = 430100000.0,
        .pcm_dev       = "plughw:Loopback,0,0",
        .deemph_tau_s  = 50e-6f,
        .pcm_gain      = 8000.0f,
        .fs_rf         = 4000000.0f,
        .fs_audio      = 48000.0f,
    };

    if (rx_pipeline_init(&rx, sys, &sys->radio_high, &par) != 0) {
        fprintf(stderr, "[rx] init failed\n");
        return;
    }

    for (;;) {
        int choice = -1;
        printf("RX freq: %.0f Hz\n", par.freq_hz);
        printf("RX rate: %.0f samples/s (%.0f samples/10ms)\n", par.fs_rf, par.fs_rf / 100);
        printf(" [1] Toggle NBFM RX   [2] Select 2 MS/s   [4] Select 4 MS/s   [99] Return\n");
        printf(" Choice: ");
        if (scanf("%d", &choice) != 1) continue;
        if (choice == 1) {
            if (!rx.running) {
                if (rx_pipeline_start(&rx) == 0) printf("RX: ON\n");
            } else {
                rx_pipeline_stop(&rx);
                printf("RX: OFF\n");
            }
        } else if (choice == 2 || choice == 4) {
            if (rx.running) {
                printf("Stop RX before changing sample rate.\n");
                continue;
            }
            rx_pipeline_destroy(&rx);
            par.fs_rf = choice * 1000000.0f;
            if (rx_pipeline_init(&rx, sys, &sys->radio_high, &par) != 0) {
                fprintf(stderr, "[rx] rate change failed\n");
                break;
            }
        } else if (choice == 99) {
            break;
        }
    }

    rx_pipeline_destroy(&rx);
    cariboulite_radio_set_rx_sample_rate_flt(&sys->radio_high, 4000000);
    printf("NBFM RX stopped.\n");
}

#include "monitor_loopback.h"

static bool monitor_init_pipelines(tx_pipeline_t* tx, rx_pipeline_t* rx,
                                   sys_st* sys, cariboulite_radio_state_st* radio,
                                   const tx_params_t* txpar,
                                   const rx_params_t* rxpar)
{
    if (tx_pipeline_init(tx, sys, radio, txpar) != 0) {
        fprintf(stderr, "[monitor] TX initialization failed; returning to menu\n");
        return false;
    }
    if (rx_pipeline_init(rx, sys, radio, rxpar) != 0) {
        fprintf(stderr, "[monitor] RX initialization failed; returning to menu\n");
        tx_pipeline_destroy(tx);
        return false;
    }
    return true;
}

// Configuration entry is separate from tuning: shared hardware is tuned only
// after stopping the opposite direction, immediately before starting a stream.
static bool monitor_parse_frequency(const char* text,
                                    const cariboulite_radio_state_st* radio,
                                    double* hz)
{
    char* end;
    errno = 0;
    double mhz = strtod(text, &end);
    if (end == text || errno || !isfinite(mhz)) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end) return false;
    double value = mhz * 1000000.0;
    bool valid;
    if (radio->type == cariboulite_channel_s1g) {
        valid = (value >= CARIBOULITE_S1G_MIN1 && value <= CARIBOULITE_S1G_MAX1) ||
                (value >= CARIBOULITE_S1G_MIN2 && value <= CARIBOULITE_S1G_MAX2);
    } else if (radio->sys->board_info.numeric_product_id == system_type_cariboulite_full) {
        valid = value >= CARIBOULITE_6G_MIN && value < CARIBOULITE_6G_MAX;
    } else {
        valid = value >= CARIBOULITE_2G4_MIN && value <= CARIBOULITE_2G4_MAX;
    }
    if (!valid) return false;
    *hz = value;
    return true;
}

static bool monitor_frequency_prompt(bool tx,
                                     const cariboulite_radio_state_st* radio,
                                     double* hz)
{
    char input[32] = {0};
    size_t used = 0;
    timeout(-1);
    for (;;) {
        move(getmaxy(stdscr)-1, 0); clrtoeol();
        printw("%s MHz [%0.6f]: %s  (Enter saves; Esc cancels)",
               tx ? "TX" : "RX", *hz/1e6, input);
        refresh();
        int key = getch();
        if (key == 27 || key == ERR) { timeout(200); return false; }
        if (key == '\n' || key == '\r' || key == KEY_ENTER) {
            timeout(200);
            return used && monitor_parse_frequency(input, radio, hz);
        }
        if (key == KEY_BACKSPACE || key == 127 || key == 8) {
            if (used) input[--used] = 0;
        } else if (key >= 32 && key <= 126 && used < sizeof(input)-1) {
            input[used++] = (char)key; input[used] = 0;
        }
    }
}

static bool monitor_parse_noise_levels(const char* text, float* open_rms, float* close_rms)
{
    char* end;
    errno = 0;
    float opening = strtof(text, &end);
    if (end == text || errno || (*end != ' ' && *end != '\t')) return false;
    const char* second = end;
    float closing = strtof(second, &end);
    if (end == second || errno) return false;
    while (*end == ' ' || *end == '\t') ++end;
    uint32_t levels;
    if (*end || !noise_squelch_pack_levels(opening, closing, &levels)) return false;
    return noise_squelch_unpack_levels(levels, open_rms, close_rms);
}

static bool monitor_noise_levels_prompt(float* open_rms, float* close_rms)
{
    char input[48] = {0};
    size_t used = 0;
    timeout(-1);
    for (;;) {
        move(getmaxy(stdscr)-1, 0); clrtoeol();
        printw("Noise RMS open close [%.3f %.3f]: %s  (Enter applies; Esc cancels)",
               *open_rms, *close_rms, input);
        refresh();
        int key = getch();
        if (key == 27 || key == ERR) { timeout(200); return false; }
        if (key == '\n' || key == '\r' || key == KEY_ENTER) {
            timeout(200);
            return used && monitor_parse_noise_levels(input, open_rms, close_rms);
        }
        if (key == KEY_BACKSPACE || key == 127 || key == 8) {
            if (used) input[--used] = 0;
        } else if (key >= 32 && key <= 126 && used < sizeof(input)-1) {
            input[used++] = (char)key; input[used] = 0;
        }
    }
}

static int monitor_apply_noise_levels(rx_pipeline_t* rx, rx_params_t* par,
                                      float open_rms, float close_rms)
{
    if (rx_pipeline_set_noise_squelch_levels(rx, open_rms, close_rms) != 0) return -1;
    rx_noise_squelch_status_t status;
    if (rx_pipeline_get_noise_squelch_status(rx, &status) != 0) return -1;
    par->noise_squelch_open_rms = status.open_rms;
    par->noise_squelch_close_rms = status.close_rms;
    return 0;
}

static int monitor_start_tx(tx_pipeline_t* tx, rx_pipeline_t* rx, const tx_params_t* par)
{
    rx_pipeline_stop(rx);
    if (tx_pipeline_set_freq_power(tx, par->freq_hz, par->tx_power_dbm) != 0) return -1;
    return tx_pipeline_start(tx);
}

static int monitor_start_rx(tx_pipeline_t* tx, rx_pipeline_t* rx, const rx_params_t* par)
{
    tx_pipeline_stop(tx);
    if (rx_pipeline_set_freq(rx, par->freq_hz) != 0) return -1;
    return rx_pipeline_start(rx);
}

// Recreate only the stopped RX pipeline. TX modulation and saved frequencies
// are independent of the receive mode. A failed init is cleaned up by the caller.
static int monitor_cycle_rx_mode(tx_pipeline_t* tx, rx_pipeline_t* rx,
                                 const monitor_loopback_t* loopback,
                                 sys_st* sys, rx_params_t* par)
{
    if (tx_pipeline_running(tx) || rx_pipeline_running(rx) ||
        loopback->armed || loopback->active) return -EBUSY;
    rx_params_t next = *par;
    next.mode = par->mode == AUDIO_DEMOD_NBFM ? AUDIO_DEMOD_WBFM : AUDIO_DEMOD_NBFM;
    cariboulite_radio_state_st* radio = rx->radio;
    rx_pipeline_destroy(rx);
    if (rx_pipeline_init(rx, sys, radio, &next) != 0) return -1;
    *par = next;
    return 0;
}

void monitor_modem_status(sys_st *sys)
{
	//mlockall(MCL_CURRENT | MCL_FUTURE);
    
    // --- NEW: pipelines ---
    tx_pipeline_t txp = {0};
    rx_pipeline_t rxp = {0};
    monitor_loopback_t loopback = {0};
    cariboulite_radio_state_st* radio = &sys->radio_high; // RF24 / HiF

    tx_params_t txpar = {
        .freq_hz      = 430100000.0,
        .tx_power_dbm = -13.0f,                 // Requested TX power for menu 14 (dBm)
        .tone_mode    = false,
        .tone_hz      = 600.0f,
        .tone_amp     = 0.4f,
        .mic_dev      = "plughw:Loopback,1,1", // app reads
        .out_scale    = 4000.0f,
        .f_dev_hz     = NBFM_DEFAULT_DEVIATION_HZ,
    };

    rx_params_t rxpar = {
        .freq_hz       = 430100000.0,
        .pcm_dev       = "plughw:Loopback,0,0", // app writes
        .deemph_tau_s  = 50e-6f,
        .pcm_gain      = 8000.0f,
        .noise_squelch_open_rms = NOISE_SQUELCH_OPEN_RMS,
        .noise_squelch_close_rms = NOISE_SQUELCH_CLOSE_RMS,
        .fs_rf         = 4000000.0f,
        .fs_audio      = 48000.0f,
    };

    // init once (threads idle until start)
    if (!monitor_init_pipelines(&txp, &rxp, sys, radio, &txpar, &rxpar)) return;

	nbfm_tx_active = false;
    nbfm_rx_active = false;

	initscr(); // Initialize ncurses mode
	cbreak();
	noecho();
	timeout(200);
        
	double frequency = 430100000;     // Default frequency in Hz
    float  tx_bw     = 0.0f;          // TX bandwidth readback in Hz
    float  tx_sr     = 0.0f;          // TX sample-rate readback in Hz
    float  rx_bw     = 2000000.0f;    // Default RX bandwidth in Hz
    float  rx_sr     = 4000000.0f;    // Default RX sample rate in Hz
    const char* rate_notice = "[1] 1 MS/s  [2] 2 MS/s  [4] 4 MS/s: stop TX and RX before changing";

	int iq_tx_buffer_size = (1u << 18);
	int iq_rx_buffer_size = (1u << 18);
	cariboulite_sample_complex_int16 iq_tx_buffer[iq_tx_buffer_size]; // complex CS16 samples (I, Q interleaved)
    cariboulite_sample_complex_int16 iq_rx_buffer[iq_rx_buffer_size]; // complex CS16 samples (I, Q interleaved)

    at86rf215_st *modem = &sys->modem;
	caribou_fpga_st *fpga = &sys->fpga;
	caribou_smi_st *smi = &sys->smi;
    rffc507x_st *mixer = &sys->mixer;
    rffc507x_device_id_st mix_id = {0};
    rffc507x_device_status_st mix_status = {0};
    
	caribou_fpga_smi_fifo_status_st status = {0};
    uint8_t *val = (uint8_t *)&status;

	uint8_t debug = 0x00;
	caribou_fpga_io_ctrl_rfm_en mode = 0x00;
    
    pthread_t rx_thread;
    rx_reader_ctrl_st rx_ctrl = {
    .active = true,
    .radio = radio,
    .rx_buffer = iq_rx_buffer,
    .rx_buffer_size = iq_rx_buffer_size,
    };

    tx_writer_ctrl_st tx_ctrl = {
    .active = true,
    .radio = radio,
    .tx_buffer = iq_tx_buffer,
    .tx_buffer_size = iq_tx_buffer_size,
    };
    
	//int screen_max_y; 
	int screen_max_x;
	// int ret = 0;

	// Set up the radio
	HW_LOCK(); 
	cariboulite_radio_set_frequency(radio, true, &frequency);
	cariboulite_radio_set_tx_power(radio, txpar.tx_power_dbm);
    // The shared TX pipeline already configured the NBFM filters and sample rate.
    cariboulite_radio_set_rx_bandwidth_flt(radio,rx_bw);
    cariboulite_radio_set_rx_sample_rate_flt(radio, rx_sr);
	HW_UNLOCK();
	
	time_t current_time;
	clock_t loop_start, loop_end;
	float elapsed_time = 0.0;
	
	static unsigned slow = 0;
	
    // main loop to monitor modem status
	while (1)
    {
		
		loop_start = clock();
		//getmaxyx(stdscr, screen_max_y, screen_max_x);
		screen_max_x = getmaxx(stdscr);
		clear();

		time(&current_time);
		move(0,0);
		printw("%s [T] TX [R] RX [M] RX mode [L] loopback [1/2/4] MS/s [Q] quit [X] stats",
               radio->type == cariboulite_channel_s1g ? "S1G/RF09" : "HiF/RF24");
		move(0, screen_max_x - 12);
		printw("%12ld",current_time);
        move(1,0);
        printw("TX Power: %d dBm  TX/RX %.0f MS/s", txpar.tx_power_dbm, rxpar.fs_rf / 1000000);
        move(1, screen_max_x - 12);
        printw("%12.5f", elapsed_time);
        move(2,0);
        printw("[F] TX %.6f MHz  [G] RX %.6f MHz", txpar.freq_hz/1e6, rxpar.freq_hz/1e6);
        move(3,0);
        printw("SMI timing settings: (n x 62.5ns)");
        struct smi_settings smi_set = {0};
        HW_LOCK();
        bool smi_settings_valid = ioctl(sys->smi.filedesc,
            BCM2835_SMI_IOC_GET_SETTINGS, &smi_set) == 0;
        HW_UNLOCK();
        move(4,0);
        if (smi_settings_valid)
            printw("    reading setup: %d strobe: %d  hold: %d pace: %d",
                smi_set.read_setup_time, smi_set.read_strobe_time,
                smi_set.read_hold_time, smi_set.read_pace_time);
        else
            printw("    reading timings: unavailable");
        move(5,0);
        if (smi_settings_valid)
            printw("    writing setup: %d strobe: %d  hold: %d pace: %d",
                smi_set.write_setup_time, smi_set.write_strobe_time,
                smi_set.write_hold_time, smi_set.write_pace_time);
        else
            printw("    writing timings: unavailable");
        move(6,0);
        printw("AT86RF215 Modem Status Registers:");
        move(7,0);
		//refresh();

        uint8_t data[3] = {0};
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF_IQIFC0, data, 3);
        HW_UNLOCK();
        uint8_t iqifc0 = data[0];
        uint8_t iqifc1 = data[1];
        uint8_t iqifc2 = data[2];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF_CFG, data, 1);
        HW_UNLOCK();
        uint8_t rf_cfg = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF_CLKO, data, 1);
        HW_UNLOCK();
        uint8_t rf_clko = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF_RST, data, 1);
        HW_UNLOCK();
        uint8_t rf_rst = data[0];
        printw("    RF_CFG:0x%02X  RF_CLKO:0x%02X  RF_RST:0x%02X\n", rf_cfg, rf_clko, rf_rst);
        printw("    IQIFC0:0x%02X  IQIFC1 :0x%02X  IQIFC2:0x%02X\n", iqifc0, iqifc1, iqifc2);
        //refresh();
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_TXDFE, data, 1);
        HW_UNLOCK();
        uint8_t rf09_txdfe = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_TXDFE, data, 1);
        HW_UNLOCK();
        uint8_t rf24_txdfe = data[0];

        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_TXCUTC, data, 1);
        HW_UNLOCK();
        uint8_t rf09_txcutc = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_TXCUTC, data, 1);
        HW_UNLOCK();
        uint8_t rf24_txcutc = data[0];

        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_PLL, data, 1);
        HW_UNLOCK();
        uint8_t rf09_pll = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_PLL, data, 1);
        HW_UNLOCK();
        uint8_t rf24_pll = data[0];

        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_PLLCF, data, 1);
        HW_UNLOCK();
        uint8_t rf09_pllcf = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_PLLCF, data, 1);
        HW_UNLOCK();
        uint8_t rf24_pllcf = data[0];

        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_RSSI, data, 1);
        HW_UNLOCK();
        uint8_t rf09_rssi = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_RSSI, data, 1);
        HW_UNLOCK();
        uint8_t rf24_rssi = data[0];
        
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_RXDFE, data, 1);
        HW_UNLOCK();
        uint8_t rf09_rxdfe = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_RXDFE, data, 1);
        HW_UNLOCK();
        uint8_t rf24_rxdfe = data[0];

        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_RXBWC, data, 1);
        HW_UNLOCK();
        uint8_t rf09_rxdbwc = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_RXBWC, data, 1);
        HW_UNLOCK();
        uint8_t rf24_rxbwc = data[0];

        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_AGCC, data, 1);
        HW_UNLOCK();
        uint8_t rf09_agcc = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_AGCC, data, 1);
        HW_UNLOCK();
        uint8_t rf24_agcc = data[0];

        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_AGCS, data, 1);
        HW_UNLOCK();
        uint8_t rf09_agcs = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_AGCS, data, 1);
        HW_UNLOCK();
        uint8_t rf24_agcs = data[0];

        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_PAC, data, 1);
        HW_UNLOCK();
        uint8_t rf09_pac = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_PAC, data, 1);
        HW_UNLOCK();
        uint8_t rf24_pac = data[0];
        printw("    RF09-RSSI  :0x%02X  RF24-RSSI  :0x%02X\n", rf09_rssi, rf24_rssi);
        printw("    RF09-RSSI  :%+4d  RF24-RSSI  :%+4d dBm\n", (int8_t)rf09_rssi, (int8_t)rf24_rssi);
        printw("    RF09-RXDFE :0x%02X  RF24-RXDFE :0x%02X\n", rf09_rxdfe, rf24_rxdfe);
        printw("    RF09-RXBWC :0x%02X  RF24-RXBWC :0x%02X\n", rf09_rxdbwc, rf24_rxbwc);
        printw("    RF09-AGCC  :0x%02X  RF24-AGCC  :0x%02X\n", rf09_agcc, rf24_agcc);
        printw("    RF09-AGCS  :0x%02X  RF24-AGCS  :0x%02X\n", rf09_agcs, rf24_agcs);
        printw("    RF09-TXFDE :0x%02X  RF24-TXDFE :0x%02X\n", rf09_txdfe, rf24_txdfe);
        printw("    RF09-TXCUTC:0x%02X  RF24-TXCUTC:0x%02X\n", rf09_txcutc, rf24_txcutc);
        printw("    RF09-PAC   :0x%02X  RF24-PAC   :0x%02X\n", rf09_pac,   rf24_pac);
        printw("    RF09-PLL   :0x%02X  RF24-PLL   :0x%02X\n", rf09_pll,   rf24_pll);
        printw("    RF09-PLLCF :0x%02X  RF24-PLLCF :0x%02X\n", rf09_pllcf, rf24_pllcf);
        
        //refresh();
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_STATE, data, 1);
        HW_UNLOCK();
        uint8_t rf09_state = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_STATE, data, 1);
        HW_UNLOCK();
        uint8_t rf24_state = data[0];

        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_AUXS, data, 1);
        HW_UNLOCK();
        uint8_t rf09_auxs = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_AUXS, data, 1);
        HW_UNLOCK();
        uint8_t rf24_auxs = data[0];
        
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_IRQS, data, 1);
        HW_UNLOCK();
        uint8_t rf09_irqs = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_IRQS, data, 1);
        HW_UNLOCK();
        uint8_t rf24_irqs = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_IRQM, data, 1);
        HW_UNLOCK();
        uint8_t rf09_irqm = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_IRQM, data, 1);
        HW_UNLOCK();
        uint8_t rf24_irqm = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF09_PADFE, data, 1);
        HW_UNLOCK();
        uint8_t rf09_padfe = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, REG_RF24_PADFE, data, 1);
        HW_UNLOCK();
        uint8_t rf24_padfe = data[0];
        HW_LOCK();
        at86rf215_read_buffer(modem, 0x0127, data, 2); //REG_RF09_TXDACI, REG_RF09_TXDACQ
        HW_UNLOCK();
        uint8_t rf09_txdaci = data[0];
        uint8_t rf09_txdacq = data[1];
        HW_LOCK();
        at86rf215_read_buffer(modem, 0x0227, data, 2); //REG_RF24_TXDACI, REG_RF24_TXDACQ
        HW_UNLOCK();
        uint8_t rf24_txdaci = data[0];
        uint8_t rf24_txdacq = data[1];
        printw("    RF09-IRQM  :0x%02X  RF24-IRQM  :0x%02X\n", rf09_irqm,  rf24_irqm);
        printw("    RF09-IQRS  :0x%02X  RF24-IRQS  :0x%02X\n", rf09_irqs,  rf24_irqs);	
        printw("    RF09-STATE :0x%02X  RF24-STATE :0x%02X\n", rf09_state, rf24_state);
        printw("    RF09-TXDACI:0x%02X  RF24-TXDACI:0x%02X\n", rf09_txdaci, rf24_txdaci);
        printw("    RF09-TXDACQ:0x%02X  RF24-TXDACQ:0x%02X\n", rf09_txdacq, rf24_txdacq);
        printw("    RF09-AUXS  :0x%02X  RF24-AUXS  :0x%02X\n", rf09_auxs, rf24_auxs);
        printw("    RF09-PADFE :0x%02X  RF24-PADFE :0x%02X\n", rf09_padfe, rf24_padfe);
        
        //HW_LOCK();
        rffc507x_readback_status(mixer, &mix_id, &mix_status);
        //HW_UNLOCK();
        printw("RFFC5071 Mixer Status Registers\n");
        printw("    ID: 0x%04X REV: 0x%04X\n", mix_id.fields.device_id, mix_id.fields.device_rev);
        printw("    STAT: 0x%04X PLL_LOCK: %d, CT_CAL: %d, KV_CAL: %d, CT_CAL_FAIL: %d\n",
			mix_status.raw,
			mix_status.fields.pll_lock, mix_status.fields.coarse_tune_cal_value, 
			mix_status.fields.kv_cal_value, mix_status.fields.coarse_tune_cal_fail);

        //refresh();
        HW_LOCK();
        caribou_fpga_get_smi_ctrl_fifo_status(&sys->fpga, &status);
        HW_UNLOCK();
        printw("FPGA SMI info (0x%02X):\n", *val);
        printw("    RX FIFO EMPTY: %d\n", status.rx_fifo_empty);
        printw("    TX FIFO FULL : %d\n", status.tx_fifo_full);
        printw("    RX SOURCE    : %d    // 0=RF09 1=RF24; unused in TX\n", status.smi_channel);
        printw("    SMI DIRECTION: %d    // 0=TX   1=RX\n", status.smi_direction);
        //refresh();
        HW_LOCK();
        caribou_fpga_get_io_ctrl_mode(&sys->fpga, &debug, &mode);
        HW_UNLOCK();
        printw("    DEBUG = %d, MODE: '%s'\n", debug, caribou_fpga_get_mode_name(mode));
        //refresh();
        if (loopback.active && monitor_loopback_read(sys, &loopback) != 0) {
            monitor_loopback_stop(sys, &loopback);
            rate_notice = "Loopback read failed. [L] retries cleanup if locked; see debug log.";
        }
        printw("Interface loopback: %s | RF TX blocked while enabled\n",
               loopback.active ? "ON" : loopback.armed ? "CLEANUP REQUIRED" : "OFF");
        if (loopback.armed) {
            printw("    FPGA test frame: 0x84037048; capture: HiF RX (decoded I/Q)\n");
            printw("    Samples: %llu  timeouts: %lu  latest batch preview: %d\n",
                   loopback.total, loopback.timeouts, loopback.count);
            if (!loopback.count) printw("    No fresh samples. Requires the loopback-capable FPGA image.\n");
            for (int j = 0; j < loopback.count; ++j) {
                printw(" %2d:I=%04X Q=%04X", j,
                       (unsigned)(uint16_t)loopback.samples[j].i,
                       (unsigned)(uint16_t)loopback.samples[j].q);
                if (j % 4 == 3 || j == loopback.count - 1) printw("\n");
            }
        }
        if (!loopback.armed) {
            printw("IQ Data Stream:\n");
            printw("    TX_I:0x%08X  TX_Q:0x%08X\n", latest_tx_sample.i, latest_tx_sample.q);
            printw("    RX_I:0x%08X  RX_Q:0x%08X\n", latest_rx_sample.i, latest_rx_sample.q);
        }
        //refresh();
        //smi_state = caribou_smi_get_driver_streaming_state(smi);
        //printw("SMI driver state: 0x%02X    // 0=idle 1=RX09 2=RX24 3=TX\n",(uint8_t) smi->state);
        uint8_t tx_sample_gap = 255;
        HW_LOCK();
        caribou_fpga_get_sys_ctrl_tx_sample_gap(fpga, &tx_sample_gap);
        cariboulite_radio_get_tx_bandwidth_flt(radio, &tx_bw);
        cariboulite_radio_get_tx_samp_cutoff_flt(radio, &tx_sr);
        HW_UNLOCK();
        printw("    TX sample gap : %d\n", tx_sample_gap);
        printw("    TX bandwidth  : %.0f Hz\n", tx_bw);
        printw("    TX sample rate: %.0f Hz\n", tx_sr);
        //refresh();

        // --- TX FIFO stats panel ---
        
        tx_pipeline_stats_t tst;
        tx_pipeline_get_stats(&txp, &tst);
        rf10_stats_t stx = tst.txq;
        float tx_fill_pct = (stx.cap ? (100.0f * (float)stx.count / (float)stx.cap) : 0.f);
        
        // optional: rates since last sample
        static struct timespec tx_last_ts = {0};
        static rf10_stats_t    tx_last_s  = {0};
        double tx_rate_puts = 0.0, tx_rate_gets = 0.0;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (tx_last_ts.tv_sec != 0) {
        double dt = (now.tv_sec - tx_last_ts.tv_sec) + (now.tv_nsec - tx_last_ts.tv_nsec)/1e9;
            if (dt > 0.0 && stx.puts >= tx_last_s.puts && stx.gets >= tx_last_s.gets) {
                tx_rate_puts = (double)(stx.puts - tx_last_s.puts) / dt;
                tx_rate_gets = (double)(stx.gets - tx_last_s.gets) / dt;
            }
        }
        tx_last_ts = now; tx_last_s = stx;
        
        printw("Linux TX FIFO: %zu samples/frame (10 ms)\n", tx_pipeline_frame_samples(&txp));
        printw("    depth: %zu/%zu (%.0f%%), min:%zu max:%zu\n",
            stx.count, stx.cap, tx_fill_pct, stx.min_depth, stx.max_depth);
        printw("    puts:%zu gets:%zu drops:%zu tO_put:%zu tO_get:%zu\n",
            stx.puts, stx.gets, stx.drops, stx.timeouts_put, stx.timeouts_get);
        printw("    rate: puts %.1f/s, gets %.1f/s  (expect ~100 fps @ 10ms)\n",
            tx_rate_puts, tx_rate_gets);

        // If you want to highlight trouble:
        if (stx.drops > 0)               printw("    NOTE: Overwrites occurred (producer faster than writer)\n");
        if (stx.timeouts_put > 0)        printw("    NOTE: Producer timed out waiting to enqueue\n");
        if (stx.timeouts_get > 0)        printw("    NOTE: Writer timed out waiting for frames\n");
    
        rx_pipeline_stats_t rst;
        rx_pipeline_get_stats(&rxp, &rst);
        rf10_stats_t srx = rst.rxq;
        float rx_fill_pct = (srx.cap ? (100.0f * (float)srx.count / (float)srx.cap) : 0.f);
        
        // optional: rates since last sample
        static struct timespec rx_last_ts = {0};
        static rf10_stats_t    rx_last_s  = {0};
        double rx_rate_puts = 0.0, rx_rate_gets = 0.0;

        if (rx_last_ts.tv_sec != 0) {
        double dt = (now.tv_sec - rx_last_ts.tv_sec) + (now.tv_nsec - rx_last_ts.tv_nsec)/1e9;
            if (dt > 0.0 && srx.puts >= rx_last_s.puts && srx.gets >= rx_last_s.gets) {
                rx_rate_puts = (double)(srx.puts - rx_last_s.puts) / dt;
                rx_rate_gets = (double)(srx.gets - rx_last_s.gets) / dt;
            }
        }
        rx_last_ts = now; rx_last_s = srx;
        
        printw("Linux RX FIFO: %zu samples/frame (10 ms)\n",
               rx_pipeline_frame_samples(&rxp));
        printw("    depth: %zu/%zu (%.0f%%), min:%zu max:%zu\n",
            srx.count, srx.cap, rx_fill_pct, srx.min_depth, srx.max_depth);
        printw("    puts:%zu gets:%zu drops:%zu tO_put:%zu tO_get:%zu\n",
            srx.puts, srx.gets, srx.drops, srx.timeouts_put, srx.timeouts_get);
        printw("    rate: puts %.1f/s, gets %.1f/s  (expect ~100 fps @ 10ms)\n",
            rx_rate_puts, rx_rate_gets);
        
        // Same “trouble” hints, adapted to RX roles
        if (srx.drops > 0)               printw("    NOTE: Overwrites occurred (demod slower than reader)\n");
        if (srx.timeouts_put > 0)        printw("    NOTE: Reader timed out waiting to enqueue\n");
        if (srx.timeouts_get > 0)        printw("    NOTE: Demod timed out waiting for frames\n");

        printw("RX mode: %s  [M] cycle (TX/RX/loopback stopped); TX mode: NBFM\n",
            rxpar.mode == AUDIO_DEMOD_WBFM ? "WBFM mono" : "NBFM");
        printw("Squelch: [N] noise %s  [C] carrier %s  audio %s\n",
            rxpar.mode == AUDIO_DEMOD_WBFM ? "N/A (WBFM)" : rxpar.noise_squelch_disabled ? "OFF" : "ON",
            rxpar.carrier_squelch_enabled ? "ON" : "OFF",
            !rx_pipeline_running(&rxp) ? "IDLE" :
            rx_pipeline_squelch_open(&rxp) ? "OPEN" : "MUTED");
        if (rxpar.mode == AUDIO_DEMOD_NBFM) {
            rx_noise_squelch_status_t noise;
            if (rx_pipeline_get_noise_squelch_status(&rxp, &noise) == 0) {
                if (noise.valid)
                    printw("Noise RMS %.3f  detector %s  [S] open %.3f / close %.3f\n",
                        noise.rms, noise.detector_open ? "OPEN" : "CLOSED",
                        noise.open_rms, noise.close_rms);
                else printw("Noise RMS --  [S] open %.3f / close %.3f\n",
                        noise.open_rms, noise.close_rms);
                printw("Higher noise thresholds admit noisier signals; levels can change during RX.\n");
            }
        }
        printw("\n%s\n", rate_notice);
        refresh();
        int key = getch();
        if (key == 's' || key == 'S') {
            if (rxpar.mode != AUDIO_DEMOD_NBFM) {
                rate_notice = "Noise-squelch levels are available in NBFM mode.";
                continue;
            }
            float opening = rxpar.noise_squelch_open_rms;
            float closing = rxpar.noise_squelch_close_rms;
            if (monitor_noise_levels_prompt(&opening, &closing) &&
                monitor_apply_noise_levels(&rxp, &rxpar, opening, closing) == 0)
                rate_notice = "Noise-squelch levels applied; saved for this monitor session.";
            else rate_notice = "Levels retained. Enter 0 < open < close <= 8.0 (0.001 RMS steps).";
            continue;
        }
        if (key == 'm' || key == 'M') {
            int result = monitor_cycle_rx_mode(&txp, &rxp, &loopback, sys, &rxpar);
            if (result == -EBUSY) {
                rate_notice = "Stop TX, RX and interface loopback before changing RX mode.";
            } else if (result != 0) {
                fprintf(stderr, "[monitor] RX mode initialization failed; returning to menu\n");
                break;
            } else {
                rate_notice = "RX mode selected. [R] starts RX; TX remains NBFM.";
            }
            continue;
        }
        if (key == 'n' || key == 'N' || key == 'c' || key == 'C') {
            if ((key == 'n' || key == 'N') && rxpar.mode == AUDIO_DEMOD_WBFM) {
                rate_notice = "Noise squelch is available in NBFM only; [C] controls carrier squelch.";
                continue;
            }
            if (key == 'n' || key == 'N') rxpar.noise_squelch_disabled = !rxpar.noise_squelch_disabled;
            else rxpar.carrier_squelch_enabled = !rxpar.carrier_squelch_enabled;
            rx_pipeline_set_squelch(&rxp, !rxpar.noise_squelch_disabled,
                                    rxpar.carrier_squelch_enabled);
            continue;
        }
        if (key == 'l' || key == 'L') {
            if (loopback.armed) {
                if (monitor_loopback_stop(sys, &loopback) != 0)
                    rate_notice = "Loopback cleanup failed; TX/RX locked. [L] retries cleanup.";
                else rate_notice = "Loopback OFF; radios stopped. [T]/[R] available.";
            } else {
                tx_pipeline_stop(&txp);
                rx_pipeline_stop(&rxp);
                if (monitor_loopback_start(sys, &loopback) != 0)
                    rate_notice = "Loopback start failed. [L] retries cleanup if locked.";
                else rate_notice = "Loopback ON: RF TX disabled. [L] stops; [Q] cleans up and exits.";
            }
            continue;
        }
        if (monitor_loopback_blocks_control(&loopback, key)) {
            rate_notice = "Stop interface loopback with [L] before TX, RX, frequency or rate changes.";
            continue;
        }
        if ((key == 'q' || key == 'Q') && loopback.armed &&
            monitor_loopback_stop(sys, &loopback) != 0) {
            rate_notice = "Cleanup failed; staying in monitor with TX blocked. [L] retries.";
            continue;
        }
        if (key == 'f' || key == 'F' || key == 'g' || key == 'G') {
            if (tx_pipeline_running(&txp) || rx_pipeline_running(&rxp)) {
                rate_notice = "Stop TX and RX before editing frequencies.";
                continue;
            }
            bool tx = key == 'f' || key == 'F';
            if (monitor_frequency_prompt(tx, radio, tx ? &txpar.freq_hz : &rxpar.freq_hz))
                rate_notice = "Frequency saved; applied when that direction starts.";
            else if (radio->type == cariboulite_channel_s1g)
                rate_notice = "Unchanged: cancelled or invalid MHz (377-530 or 779-1020).";
            else rate_notice = sys->board_info.numeric_product_id == system_type_cariboulite_full
                ? "Unchanged: cancelled or invalid MHz (1 <= MHz < 6000)."
                : "Unchanged: cancelled or invalid MHz (2385 <= MHz <= 2495).";
            continue;
        }
        if (key == '1' || key == '2' || key == '4') {
            if (tx_pipeline_running(&txp) || rx_pipeline_running(&rxp)) {
                rate_notice = "Stop TX and RX before changing sample rate.";
                continue;
            }
            tx_pipeline_destroy(&txp);
            rx_pipeline_destroy(&rxp);
            txpar.rf_fs = (key - '0') * 1000000;
            rxpar.fs_rf = txpar.rf_fs;
            if (!monitor_init_pipelines(&txp, &rxp, sys, radio, &txpar, &rxpar)) break;
            cariboulite_radio_set_rx_sample_rate_flt(radio, rxpar.fs_rf);
            rate_notice = "TX/RX rate selected. [T] starts TX; [R] starts RX.";
            continue;
        }
		
		if(key == 'q' || key == 'Q') // Press 'q' to exit
		{
			if (rx_pipeline_running(&rxp)) {
                rx_pipeline_stop(&rxp);
            }
            if (tx_pipeline_running(&txp)) {
                tx_pipeline_stop(&txp);
            }
            break;
		}

		if (key == 'x' || key == 'X') {      // reset FIFO diagnostics
			tx_pipeline_reset_stats(&txp);
            rx_pipeline_reset_stats(&rxp);
		}

        // --- T: toggle TX ---
        if (key == 't' || key == 'T') {
            if (!tx_pipeline_running(&txp)) {
                uint8_t iq_control = 0;
                if (at86rf215_read_buffer(modem, REG_RF_IQIFC0, &iq_control, 1) != 0 ||
                    (iq_control & 0x80)) {
                    rate_notice = "TX blocked: modem loopback enabled or register read failed.";
                    continue;
                }
                rx_pipeline_stop(&rxp);
                // Join both pipelines before resetting shared FPGA state.
                tx_pipeline_destroy(&txp);
                rx_pipeline_destroy(&rxp);
                if (caribou_fpga_soft_reset(fpga) != 0 ||
                    !monitor_init_pipelines(&txp, &rxp, sys, radio, &txpar, &rxpar)) break;
                if (monitor_start_tx(&txp, &rxp, &txpar) != 0)
                    rate_notice = "TX tuning/start failed; see debug log.";
                else rate_notice = "TX running at the saved TX frequency.";
            } else {
                tx_pipeline_stop(&txp);
            }
        }

        // --- R: toggle RX ---
        if (key == 'r' || key == 'R') {
            if (!rx_pipeline_running(&rxp)) {
                if (monitor_start_rx(&txp, &rxp, &rxpar) != 0)
                    rate_notice = "RX tuning/start failed; see debug log.";
                else rate_notice = "RX running at the saved RX frequency.";
            } else {
                rx_pipeline_stop(&rxp);
            }
        }
        
		loop_end = clock();
		elapsed_time = (float)(loop_end - loop_start)/ (float)CLOCKS_PER_SEC;

	}
    
    smi_idle(sys);  // force driver to IDLE (unblocks reads/writes if they’re waiting)
    
    HW_LOCK();
    cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_tx, false);
    cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_rx, false);
    HW_UNLOCK();
    usleep(30 * 1000);
    
    // put driver idle first
    smi_idle(sys);

    // stop/destroy pipelines (order doesn’t matter now)
    tx_pipeline_destroy(&txp);
    rx_pipeline_destroy(&rxp);

    cariboulite_radio_set_tx_samp_cutoff_flt(radio, 4000000);
    cariboulite_radio_set_rx_sample_rate_flt(radio, 4000000);
    printw("Monitoring stopped.\n");
	//refresh();
	endwin(); // End ncurses mode
	return;
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
			printf(" [%2d]  %s\n", handles[i].num, handles[i].text);
		}
		printf(" [%2d]  %s\n", app_selection_quit, "Quit");

		printf("    Choice:   ");
		if (scanf("%2d", &choice) != 1) continue;

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

#include "baseline_test.h"
#include "baseline_test.inc"
