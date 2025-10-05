// Enable POSIX/GNU extensions
#ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif
#ifndef CLOCK_MONOTONIC
#  define CLOCK_MONOTONIC CLOCK_REALTIME
#endif

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

_Static_assert(sizeof(caribou_smi_sample_complex_int16) == 4,
               "caribou_smi_sample_complex_int16 must be 4 bytes (2x int16)");

#ifdef CARIBOU_SMI_BYTES_PER_SAMPLE
_Static_assert(CARIBOU_SMI_BYTES_PER_SAMPLE == sizeof(caribou_smi_sample_complex_int16),
               "CARIBOU_SMI_BYTES_PER_SAMPLE must match complex sample size");
#endif

#include "audio48k_source.h"
#include "alsa48k_source.h"
#include "nbfm4m_mod.h"
 

// included here, to use ncurcus for a text-baused UI for my additions
#include "ncurses.h"

// include here, for testing and access to level registers
#include <at86rf215.h>

// include here, for testing
#define SOURCE "/home/pi/src/cariboulite/firmware/top.bin"



#include <pthread.h>
static pthread_mutex_t g_hw_lock = PTHREAD_MUTEX_INITIALIZER;
#define HW_LOCK()   pthread_mutex_lock(&g_hw_lock)
#define HW_UNLOCK() pthread_mutex_unlock(&g_hw_lock)
#include <sched.h>
#include <sys/mman.h>
#include <sys/select.h> 

bool nbfm_tx_ready = false;
volatile bool nbfm_tx_active = false;
bool nbfm_rx_ready = false;
volatile bool nbfm_rx_active = false;

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
static void monitor_modem_status(sys_st *sys);

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
	{app_selection_nbfm_tx_tone, nbfm_tx_tone, "NBFM TX Tone",},
	{app_selection_monitor_modem_status, monitor_modem_status, "Monitor Modem Status",},
};
#define NUM_HANDLES 	(int)(sizeof(handles)/sizeof(app_menu_item_st))

// constants
#define SR   4000000.0   // sample rate
#define DF   2500.0      // peak deviation (Hz)
#define FM   700.0       // modulating tone (Hz)
#define AMP  2047.0      // amplitude (safe for 13-bit signed)

//=================================================
static inline uint64_t mono_ns(void){
    struct timespec ts; 
	clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*1000000000ull + ts.tv_nsec;
}


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

//=================================================
// --- If needed (when this function appears before the declarations), un-comment these:
//typedef struct tx_writer_ctrl_st tx_writer_ctrl_st;
//typedef struct dsp_producer_ctrl_t dsp_producer_ctrl_t;
//typedef struct rf10_fifo_s rf10_fifo_t;
//static void* dsp_producer_thread_func(void*);
//static void* tx_writer_thread_func(void*);


// forward decls placed before tx_writer_ctrl_st
typedef struct rf10_fifo_s rf10_fifo_t;
typedef struct rf10_frame_s rf10_frame_t;

typedef struct 
{
    bool active;
    cariboulite_radio_state_st *radio;
    cariboulite_sample_complex_int16 *rx_buffer;
    size_t rx_buffer_size;
} rx_reader_ctrl_st;

typedef struct 
{
    bool active;
    cariboulite_radio_state_st *radio;
    cariboulite_sample_complex_int16 *tx_buffer;
    size_t tx_buffer_size;

	// (live path)
    bool    live_from_mic;   // set true to enable live generation
    alsa48k_source_t* mic;   // ALSA handle
    nbfm4m_mod_t*     fm;    // 48k->4M NBFM
    float*            a48k;  // 480-float scratch
    iq16_t*           iq4m;  // 40k-IQ scratch
	
	// test tone generator for the FM modulator
    bool     tone_mode;        // true => synthesize 600 Hz audio
    float    tone_phase;       // [0..2π)
    float    tone_hz;          // default 600.0f
    float    tone_amp;         // audio amplitude (0..1), e.g. 0.8f
	
	// new
	rf10_fifo_t* fifo;         // FIFO for 10 ms frames

} tx_writer_ctrl_st;

// ======================= 10 ms frame FIFO (producer: DSP, consumer: TX writer) =======================
struct rf10_frame_s {
    // One 10 ms RF frame @ 4 MS/s = 40,000 IQ16 pairs
    // Reuse your iq16_t type: struct { int16_t i, q; };
    iq16_t data[40000];
};

struct rf10_fifo_s {
    rf10_frame_t*   q;
    size_t          cap;         // number of frames (e.g., 8)
    size_t          r;           // read index (consumer)
    size_t          w;           // write index (producer)
    size_t          count;       // how many frames ready
    pthread_mutex_t m;
    pthread_cond_t  can_put;
    pthread_cond_t  can_get;
    bool            drop_oldest_on_full;  // if true, overwrite oldest when full
    bool            stop;

	// diagnostics
	size_t max_depth, min_depth;
	size_t drops, puts, gets, timeouts_put, timeouts_get;
};

static void rf10_fifo_init(rf10_fifo_t* f, size_t cap, bool drop_oldest)
{
    memset(f, 0, sizeof(*f));
	f->q = (rf10_frame_t*)calloc(cap, sizeof(rf10_frame_t));
    f->cap = cap;
	f->min_depth = cap;
	f->drop_oldest_on_full = drop_oldest;
    pthread_mutex_init(&f->m, NULL);
    pthread_cond_init(&f->can_put, NULL);
    pthread_cond_init(&f->can_get, NULL);
}

static void rf10_fifo_reset_stats(rf10_fifo_t* f)
{
    pthread_mutex_lock(&f->m);
    f->puts = f->gets = f->drops = 0;
    f->timeouts_put = f->timeouts_get = 0;
    f->max_depth = f->count;
    f->min_depth = f->count;
    pthread_mutex_unlock(&f->m);
}

typedef struct {
    size_t cap, count;
    size_t puts, gets, drops;
    size_t timeouts_put, timeouts_get;
    size_t max_depth, min_depth;
} rf10_stats_t;

static void rf10_fifo_get_stats(rf10_fifo_t* f, rf10_stats_t* s)
{
    pthread_mutex_lock(&f->m);
    s->cap          = f->cap;
    s->count        = f->count;
    s->puts         = f->puts;
    s->gets         = f->gets;
    s->drops        = f->drops;
    s->timeouts_put = f->timeouts_put;
    s->timeouts_get = f->timeouts_get;
    s->max_depth    = f->max_depth;
    s->min_depth    = f->min_depth;
    pthread_mutex_unlock(&f->m);
}

static void rf10_fifo_destroy(rf10_fifo_t* f)
{
    if (!f) return;
    pthread_mutex_destroy(&f->m);
    pthread_cond_destroy(&f->can_put);
    pthread_cond_destroy(&f->can_get);
    free(f->q);
}

static bool rf10_fifo_put(rf10_fifo_t* f, const rf10_frame_t* frm, int timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_nsec += (long)timeout_ms * 1000000L;
    while (ts.tv_nsec >= 1000000000L) { ts.tv_nsec -= 1000000000L; ts.tv_sec++; }

    pthread_mutex_lock(&f->m);
    while (!f->stop && f->count == f->cap && !f->drop_oldest_on_full) {
        if (timeout_ms < 0) {
            pthread_cond_wait(&f->can_put, &f->m);
        } else {
            if (pthread_cond_timedwait(&f->can_put, &f->m, &ts) == ETIMEDOUT) {
                f->timeouts_put++;                  // <-- count the timeout
                pthread_mutex_unlock(&f->m);
                return false;
            }
        }
    }
    if (f->stop) { pthread_mutex_unlock(&f->m); return false; }

    if (f->count == f->cap && f->drop_oldest_on_full) {
        // overwrite oldest
        f->r = (f->r + 1) % f->cap;
        f->count--;
        f->drops++;                               // <-- count the drop
    }

    f->q[f->w] = *frm;
    f->w = (f->w + 1) % f->cap;
    f->count++;
    f->puts++;                                    // <-- count after success
    if (f->count > f->max_depth) f->max_depth = f->count;

    pthread_cond_signal(&f->can_get);
    pthread_mutex_unlock(&f->m);
    return true;
}

static bool rf10_fifo_get(rf10_fifo_t* f, rf10_frame_t* out, int timeout_ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_nsec += (long)timeout_ms * 1000000L;
    while (ts.tv_nsec >= 1000000000L) { ts.tv_nsec -= 1000000000L; ts.tv_sec++; }

    pthread_mutex_lock(&f->m);
    while (!f->stop && f->count == 0) {
        if (timeout_ms < 0) {
            pthread_cond_wait(&f->can_get, &f->m);
        } else {
            if (pthread_cond_timedwait(&f->can_get, &f->m, &ts) == ETIMEDOUT) {
                f->timeouts_get++;                 // <-- count the timeout
                pthread_mutex_unlock(&f->m);
                return false;
            }
        }
    }
    if (f->stop) { pthread_mutex_unlock(&f->m); return false; }

    *out = f->q[f->r];
    f->r = (f->r + 1) % f->cap;
    f->count--;
    f->gets++;                                    // <-- count after success
    if (f->count < f->min_depth) f->min_depth = f->count;

    pthread_cond_signal(&f->can_put);
    pthread_mutex_unlock(&f->m);
    return true;
}

static void rf10_fifo_stop(rf10_fifo_t* f)
{
    pthread_mutex_lock(&f->m);
    f->stop = true;
    pthread_cond_broadcast(&f->can_put);
    pthread_cond_broadcast(&f->can_get);
    pthread_mutex_unlock(&f->m);
}

typedef struct {
    bool                active;
    tx_writer_ctrl_st*  tx;      // reuse your modulator/mic/tone fields
    rf10_fifo_t*        fifo;
} dsp_producer_ctrl_t;

// must be visible before any thread uses them
static cariboulite_sample_complex_int16 latest_rx_sample = (cariboulite_sample_complex_int16){0};
static cariboulite_sample_complex_int16 latest_tx_sample = (cariboulite_sample_complex_int16){0};

// prototypes so C knows exact signatures before first use
static inline void fill_tone_48k(tx_writer_ctrl_st* ctrl, float* buf, size_t n);
static void read_audio_exact(alsa48k_source_t* mic, float* buf, size_t need);


static void* dsp_producer_thread_func(void* arg)
{
    pthread_setname_np(pthread_self(), "dsp_producer");
    set_rt_and_affinity();   // make sure this logs failures

    dsp_producer_ctrl_t* ctrl = (dsp_producer_ctrl_t*)arg;
    if (!ctrl || !ctrl->tx || !ctrl->tx->fm || !ctrl->fifo ||
        !ctrl->tx->a48k || !ctrl->tx->iq4m)
        return NULL;

    const uint64_t PERIOD_NS = 10ull * 1000ull * 1000ull; // 10 ms
    uint64_t next_ns = mono_ns();   // anchor current time
    uint64_t last_wake = 0;
    size_t frame_idx = 0;

    while (ctrl->active) {
        // ---- schedule next absolute wake ----
        next_ns += PERIOD_NS;
        struct timespec next_ts = {
            .tv_sec  = (time_t)(next_ns / 1000000000ull),
            .tv_nsec = (long)(next_ns % 1000000000ull)
        };

        // ---- sleep until the absolute deadline, handle EINTR ----
        int rc;
        do {
            rc = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_ts, NULL);
        } while (rc == EINTR);

        // ---- timing diagnostics ----
        uint64_t now = mono_ns();
        if (last_wake) {
            double dt_ms = (now - last_wake) / 1e6;
            if ((frame_idx++ % 50) == 0)
                fprintf(stderr, "producer: dt = %.3f ms  rc = %d\n", dt_ms, rc);
        }
        last_wake = now;

        // ---- if sleep failed or we drifted >50 ms, re-anchor ----
        if (rc != 0 || now > next_ns + 5 * PERIOD_NS) {
            next_ns = now;
            fprintf(stderr, "producer: re-anchor (rc=%d)\n", rc);
        }

        // ============================================================
        // 1) Generate 10 ms of audio @ 48 kHz (480 samples)
        // ============================================================
        if (ctrl->tx->tone_mode) {
            fill_tone_48k(ctrl->tx, ctrl->tx->a48k, 480);
        } else if (ctrl->tx->mic) {
            read_audio_exact(ctrl->tx->mic, ctrl->tx->a48k, 480);
        } else {
            memset(ctrl->tx->a48k, 0, 480 * sizeof(float));
        }

        // ============================================================
        // 2) Push into NBFM modulator, pull 10 ms @ 4 MS/s (40 k IQ)
        // ============================================================
        nbfm4m_push_audio(ctrl->tx->fm, ctrl->tx->a48k, 480);

        size_t pulled = 0;
        while (pulled < 40000) {
            pulled += nbfm4m_pull_iq(ctrl->tx->fm,
                                     ctrl->tx->iq4m + pulled,
                                     40000 - pulled);
        }

        // ============================================================
        // 3) Pack one rf10_frame_t and push to FIFO (tag TX_EN)
        // ============================================================
        rf10_frame_t frm;
        for (size_t i = 0; i < 40000; i++) {
            frm.data[i].i = ctrl->tx->iq4m[i].i | 0x0001;  // TX_EN in LSB
            frm.data[i].q = ctrl->tx->iq4m[i].q;
        }

        // Optional live sample for UI/debug
        latest_tx_sample.i = frm.data[20000].i;
		latest_tx_sample.q = frm.data[20000].q;

        // Blocking put; don’t drop frames
        bool ok = rf10_fifo_put(ctrl->fifo, &frm, -1);
        if (!ok) break; // stop signal
    }

    return NULL;
}

static size_t tx_sample_index = 0; 

// static float g_sine80[80];
// static void init_sine80(void){
//     for (int i=0;i<80;i++)
//         g_sine80[i] = sinf(2.f * (float)M_PI * (float)i / 80.f);
// }

static inline void fill_tone_48k(tx_writer_ctrl_st* ctrl, float* buf, size_t n)
{
    // audio sample rate is fixed at 48 kHz
    const float fs   = 48000.0f;
    const float amp  = ctrl->tone_amp;              // 0..1
    const float dphi = 2.0f * (float)M_PI * (ctrl->tone_hz / fs);
    float phase = ctrl->tone_phase;

    for (size_t i = 0; i < n; i++) {
        phase += dphi;
        if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        buf[i] = amp * sinf(phase);
    }

    ctrl->tone_phase = phase;
}

static void* rx_reader_thread_func(void* arg)
{
    pthread_setname_np(pthread_self(), "rx_reader");

	rx_reader_ctrl_st* ctrl = (rx_reader_ctrl_st*)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    caribou_smi_st *smi = &ctrl->radio->sys->smi;

    // Start with a sane batch; may grow if native gets bigger
    size_t native_bytes = caribou_smi_get_native_batch_samples(smi);
    size_t batch        = native_bytes ? (native_bytes / CARIBOU_SMI_BYTES_PER_SAMPLE) : 32768;
    if (batch > ctrl->rx_buffer_size) batch = ctrl->rx_buffer_size;

    cariboulite_sample_meta* metadata = malloc(sizeof(*metadata) * batch);
    if (!metadata) return NULL;

    // tiny nap helper (50 µs)
    const struct timespec ts50us = { .tv_sec = 0, .tv_nsec = 50 * 1000 };

    while (ctrl->active) {
        pthread_testcancel();

        if (!nbfm_rx_active) {
            nanosleep(&ts50us, NULL);
            continue;
        }

        // Track native batch occasionally (in case driver changes it)
        size_t nb = caribou_smi_get_native_batch_samples(smi);
        size_t new_batch = nb ? (nb / CARIBOU_SMI_BYTES_PER_SAMPLE) : batch;
        if (new_batch == 0) new_batch = batch;
        if (new_batch > ctrl->rx_buffer_size) new_batch = ctrl->rx_buffer_size;

        if (new_batch > batch) {
            // grow metadata if needed
            cariboulite_sample_meta* m2 = realloc(metadata, sizeof(*metadata) * new_batch);
            if (m2) {
                metadata = m2;
                batch = new_batch;
            }
        } else {
            batch = new_batch;
        }

        // Blocking read for *one* native batch keeps latency low and UI snappy
        int ret = cariboulite_radio_read_samples(ctrl->radio,
                                                 ctrl->rx_buffer,
                                                 metadata,
                                                 batch);
        if (ret > 0) {
            latest_rx_sample = ctrl->rx_buffer[ret >> 1];  // mid-sample snapshot
        } else if (ret == 0) {
            // no data right now; avoid hot spin
            nanosleep(&ts50us, NULL);
        } else {
            // error; brief backoff
            const struct timespec ts200us = { .tv_sec = 0, .tv_nsec = 200 * 1000 };
            nanosleep(&ts200us, NULL);
        }
    }

    free(metadata);
    return NULL;
}

// helper: fill exactly N audio frames (blocking in small steps)
static void read_audio_exact(alsa48k_source_t* mic, float* buf, size_t need)
{
    size_t have = 0;
    while (have < need) {
        size_t got = alsa48k_read(mic, buf + have, need - have);
        if (got == 0) {
            // tiny sleep to avoid hot spin if device is momentarily empty
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 2 * 1000 * 1000 }; // 2 ms
            nanosleep(&ts, NULL);
        }
        have += got;
    }
}

static void* tx_writer_thread_func(void* arg)
{
    pthread_setname_np(pthread_self(), "tx_writer");

    tx_writer_ctrl_st* ctrl = (tx_writer_ctrl_st*)arg;
    if (!ctrl || !ctrl->radio || !ctrl->radio->sys) return NULL;

    caribou_smi_st *smi = &ctrl->radio->sys->smi;
    if (!smi || smi->filedesc < 0) return NULL;

    rf10_fifo_t* fifo = ctrl->fifo;
    if (!fifo) return NULL;

    set_rt_and_affinity();

    const caribou_smi_channel_en ch =
        (ctrl->radio == &ctrl->radio->sys->radio_low) ?
            caribou_smi_channel_900 : caribou_smi_channel_2400;

    // non-blocking fd
    int flags = fcntl(smi->filedesc, F_GETFL, 0);
    if (flags != -1) fcntl(smi->filedesc, F_SETFL, flags | O_NONBLOCK);

    // Discover kernel "native" buffer and its quarter size (in samples)
    size_t native_bytes = caribou_smi_get_native_batch_samples(smi);
    const int BYTES_PER_SAMPLE = (int)sizeof(caribou_smi_sample_complex_int16);
    size_t quarter_samples = (native_bytes / 4) / BYTES_PER_SAMPLE;
    if (quarter_samples == 0) quarter_samples = 8192; // safe default if ioctl failed

    // Arm TX state once, then just keep feeding
    int tx_active_hw = 0;

    while (1) {
        pthread_testcancel();
        if (!ctrl->active) break;

        if (!nbfm_tx_active) {
            if (tx_active_hw) {
                caribou_smi_set_driver_streaming_state(smi, (smi_stream_state_en)0);
                tx_active_hw = 0;
            }
            // light idle: don't busy spin
            struct timespec ts = {0, 2000000}; // 2 ms
            nanosleep(&ts, NULL);
            continue;
        }

        if (!tx_active_hw) {
            caribou_smi_set_driver_streaming_state(smi, (smi_stream_state_en)3); // TX
            tx_active_hw = 1;
        }

        // Get one frame from the producer (blocking). Size = 40k IQ16 samples.
        rf10_frame_t frm;
        if (!rf10_fifo_get(fifo, &frm, /*timeout_ms=*/-1)) {
            continue;
        }

        // Stream it out in chunks ≈ kernel quarter (keep kfifo topped up)
        size_t off = 0;
        const size_t total = sizeof(frm.data) / sizeof(frm.data[0]); // 40000 samples
        struct pollfd pfd = { .fd = smi->filedesc, .events = POLLOUT, .revents = 0 };

        while (off < total && nbfm_tx_active) {
            // Aim for quarter-sized writes; last piece can be smaller
            size_t todo = total - off;
            if (todo > quarter_samples) todo = quarter_samples;

            // Wait until driver is ready to accept bytes
            int pr = poll(&pfd, 1, 10);  // short timeout; loop if needed
            if (pr <= 0 || !(pfd.revents & POLLOUT)) continue;

            caribou_smi_sample_complex_int16 *p =
                (caribou_smi_sample_complex_int16 *)(frm.data + off);

            int sent = caribou_smi_write_samples(smi, ch, p, (int)todo);  // returns *samples*
            if (sent > 0) {
                off += (size_t)sent;
            } else if (sent == 0 || (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
                // transient backpressure -> try again
                continue;
            } else {
                // hard error: drop to idle cleanly
                nbfm_tx_active = false;
                break;
            }
        }
    }

    if (tx_active_hw) {
        caribou_smi_set_driver_streaming_state(smi, (smi_stream_state_en)0);
        HW_LOCK();
        cariboulite_radio_activate_channel(ctrl->radio, cariboulite_channel_dir_tx, false);
        HW_UNLOCK();
    }
    return NULL;
}

static void nbfm_tx_tone(sys_st *sys)
{
    // mirror monitor_modem_status() semantics, but without ncurses/instrumentation
    nbfm_tx_active = false;
    nbfm_rx_active = false;

    double frequency = 430100000;   // Hz
    int    tx_power  = -3;          // dBm

    // --- TX buffers & control blocks (TX only; no RX thread here) ---
    const int iq_tx_buffer_size = (1u << 18);
    cariboulite_sample_complex_int16 iq_tx_buffer[iq_tx_buffer_size];

    cariboulite_radio_state_st *radio = &sys->radio_low;

    // FIFO for 10ms frames
    rf10_fifo_t txq;
    rf10_fifo_init(&txq, /*cap=*/32, /*drop_oldest_on_full=*/false);

    tx_writer_ctrl_st tx_ctrl = {
        .active        = true,
        .radio         = radio,
        .tx_buffer     = iq_tx_buffer,
        .tx_buffer_size= iq_tx_buffer_size,
        .live_from_mic = false,
        .mic           = NULL,
        .tone_mode     = true,
        .tone_hz       = 600.0f,
        .tone_amp      = 0.4f,
        .tone_phase    = 0.0f,
        .fifo          = &txq,
        .fm            = NULL,
        .a48k          = NULL,
        .iq4m          = NULL,
    };

    // Modulator & scratch (same config as monitor_modem_status)
    nbfm4m_cfg_t cfg = {
        .audio_fs      = 48000.0,
        .rf_fs         = 4000000.0,
        .f_dev_hz      = 2500.0,
        .preemph_tau_s = 0.0,
        .out_scale     = 4000.0f,
        .linear_interp = 1,
    };
    tx_ctrl.fm   = nbfm4m_create(&cfg);
    tx_ctrl.a48k = (float*)calloc(480,    sizeof(float));
    tx_ctrl.iq4m = (iq16_t*)calloc(40000, sizeof(iq16_t));

    if (!tx_ctrl.fm || !tx_ctrl.a48k || !tx_ctrl.iq4m) {
        fprintf(stderr, "[nbfm_tx_tone] init failed (fm=%p a48k=%p iq4m=%p)\n",
                (void*)tx_ctrl.fm, (void*)tx_ctrl.a48k, (void*)tx_ctrl.iq4m);
        if (tx_ctrl.fm)   nbfm4m_destroy(tx_ctrl.fm);
        if (tx_ctrl.a48k) free(tx_ctrl.a48k);
        if (tx_ctrl.iq4m) free(tx_ctrl.iq4m);
        rf10_fifo_destroy(&txq);
        return;
    }

    // Start DSP producer + TX writer threads (same as monitor_)
    pthread_t dsp_thread, tx_thread;

    dsp_producer_ctrl_t dsp_ctrl = {
        .active = true,
        .tx     = &tx_ctrl,
        .fifo   = &txq,
    };
    pthread_create(&dsp_thread, NULL, dsp_producer_thread_func, &dsp_ctrl);
    pthread_create(&tx_thread,  NULL, tx_writer_thread_func,    &tx_ctrl);

    // Radio base config
    HW_LOCK();
    cariboulite_radio_set_frequency(radio, true, &frequency);
    cariboulite_radio_set_tx_power(radio, tx_power);
    HW_UNLOCK();
    radio->tx_loopback_anabled   = false;
    radio->tx_control_with_iq_if = true;

    // --- Minimal CLI: 1 = toggle TX, 99 = exit ---
    for (;;) {
        int choice = -1;
		printf("TX frequency: %.0f Hz\n",round(frequency/1000)*1000);
		printf("TX power: %d dBm\n",tx_power);
        printf(" [ 1] Toggle NBFM TX\n"); 
		printf(" [99] Return to main menu\n");
        printf(" Choice: ");
        if (scanf("%d", &choice) != 1) {
            // flush junk on input error
            int c; while ((c = getchar()) != '\n' && c != EOF) {}
            continue;
        }

        if (choice == 1) {
            // exactly like 't' path in monitor_modem_status()
            if (!nbfm_tx_active) {
                if (nbfm_rx_active) {
                    nbfm_rx_active = false;
                    HW_LOCK();
                    cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_rx, false);
                    HW_UNLOCK();
                }
                HW_LOCK();
                caribou_fpga_set_io_ctrl_mode(&sys->fpga, 0, caribou_fpga_io_ctrl_rfm_tx_lowpass);
                cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_tx, true);
                caribou_smi_set_driver_streaming_state(&sys->smi, (smi_stream_state_en)3); // TX
                HW_UNLOCK();

                __sync_synchronize();   // memory barrier
                nbfm_tx_active = true;  // tell writer LAST
                printf("TX: ON\n");
            } else {
                nbfm_tx_active = false;
                __sync_synchronize();

                HW_LOCK();
                caribou_smi_set_driver_streaming_state(&sys->smi, (smi_stream_state_en)0); // idle
                cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_tx, false);
                caribou_fpga_set_io_ctrl_mode(&sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
                HW_UNLOCK();
                printf("TX: OFF\n");
            }
        } else if (choice == 99) {
            break;
        }
    }

    // --- Teardown (same order/pattern as monitor_modem_status) ---
    nbfm_tx_active = false;
    nbfm_rx_active = false;
    smi_idle(sys);

    HW_LOCK();
    cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_tx, false);
    cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_rx, false);
    HW_UNLOCK();
    usleep(30 * 1000);

    tx_ctrl.active  = false;
    dsp_ctrl.active = false;
    rf10_fifo_stop(&txq);

    pthread_cancel(tx_thread);
    pthread_cancel(dsp_thread);
    pthread_join(tx_thread, NULL);
    pthread_join(dsp_thread, NULL);

    rf10_fifo_destroy(&txq);

    if (tx_ctrl.iq4m) free(tx_ctrl.iq4m);
    if (tx_ctrl.a48k) free(tx_ctrl.a48k);
    if (tx_ctrl.fm)   nbfm4m_destroy(tx_ctrl.fm);

    printf("NBFM TX tone stopped.\n");
}

void monitor_modem_status(sys_st *sys)
{
	//mlockall(MCL_CURRENT | MCL_FUTURE);

	nbfm_tx_active = false;
    nbfm_rx_active = false;

	initscr(); // Initialize ncurses mode
	cbreak();
	noecho();
	timeout(100);
        
	double frequency = 430100000; // Default frequency in Hz
	int tx_power     = -3;	      // Default power in dBm

	int iq_tx_buffer_size = (1u << 18);
	int iq_rx_buffer_size = (1u << 18);
	cariboulite_sample_complex_int16 iq_tx_buffer[iq_tx_buffer_size]; // complex CS16 samples (I, Q interleaved)
    cariboulite_sample_complex_int16 iq_rx_buffer[iq_rx_buffer_size]; // complex CS16 samples (I, Q interleaved)


	cariboulite_radio_state_st *radio = &sys->radio_low; // radio_high (HiF) || radio_low (S1G)
    at86rf215_st *modem = &sys->modem;
	caribou_fpga_st *fpga = &sys->fpga;
	caribou_smi_st *smi = &sys->smi;
	
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

	// Start RX reader thread
    pthread_create(&rx_thread, NULL, rx_reader_thread_func, &rx_ctrl);
    
    tx_writer_ctrl_st tx_ctrl = {
    .active = true,
    .radio = radio,
    .tx_buffer = iq_tx_buffer,
    .tx_buffer_size = iq_tx_buffer_size,
    };

	// ---- create FIFO with a few frames of cushion (e.g., 8–12) ----
	rf10_fifo_t txq;
	//rf10_fifo_init(&txq, /*cap=*/32, /*drop_oldest_on_full=*/true);
	rf10_fifo_init(&txq, /*cap=*/64, /*drop_oldest_on_full=*/false);
    // ---- fill tx_ctrl, once ----
	tx_ctrl.active        = true;
	tx_ctrl.radio         = radio;
	tx_ctrl.fifo          = &txq;          // give writer the FIFO
	tx_ctrl.live_from_mic = false;	       // set true to enable audio input

	tx_ctrl.tone_mode     = true;         // true => synthesize a audio tone
	tx_ctrl.tone_hz       = 600.0f;
	tx_ctrl.tone_amp      = 0.4f;
	tx_ctrl.tone_phase    = 0.0f;

	// ALSA before threads
	//tx_ctrl.mic = alsa48k_create("plughw:10,1", 1.0f);  // input side of loopback device
	//tx_ctrl.mic = alsa48k_create("plughw:3,0", 1.0f); // input side of usb audio device
	tx_ctrl.mic = NULL; // uncommnent if you want the use the inbuild tone generator

	// Modulator & scratch before threads
	nbfm4m_cfg_t cfg = {
		.audio_fs      = 48000.0,
		.rf_fs         = 4000000.0,
		.f_dev_hz      = 2500.0,
		.preemph_tau_s = 0.0,
		.out_scale     = 4000.0f,
		.linear_interp = 1,
	};
	tx_ctrl.fm   = nbfm4m_create(&cfg);
	tx_ctrl.a48k = (float*)calloc(480,    sizeof(float));
	tx_ctrl.iq4m = (iq16_t*)calloc(40000, sizeof(iq16_t));

	// HARD FAIL if any is NULL — do NOT start threads
    if (!tx_ctrl.fm || !tx_ctrl.a48k || !tx_ctrl.iq4m) {
		endwin();
		fprintf(stderr, "[monitor] init failed (fm=%p a48k=%p iq4m=%p)\n",
				(void*)tx_ctrl.fm, (void*)tx_ctrl.a48k, (void*)tx_ctrl.iq4m);

		if (tx_ctrl.fm)   nbfm4m_destroy(tx_ctrl.fm);
		if (tx_ctrl.a48k) free(tx_ctrl.a48k);
		if (tx_ctrl.iq4m) free(tx_ctrl.iq4m);

		// If you created the FIFO already, destroy it:
		rf10_fifo_destroy(&txq);
		return;
    }
	

	// Start the DSP producer thread
	pthread_t dsp_thread;
	dsp_producer_ctrl_t dsp_ctrl = {
		.active = true,
		.tx     = &tx_ctrl,
		.fifo   = &txq,
	};
	pthread_create(&dsp_thread, NULL, dsp_producer_thread_func, &dsp_ctrl);

	// now start the thread
	pthread_t tx_thread;
	pthread_create(&tx_thread, NULL, tx_writer_thread_func, &tx_ctrl);

	//int screen_max_y; 
	int screen_max_x;
	// int ret = 0;

	// Set up the radio
	HW_LOCK(); 
	cariboulite_radio_set_frequency(radio, true, &frequency);
	cariboulite_radio_set_tx_power(radio, tx_power);
	HW_UNLOCK();
	radio->tx_loopback_anabled = false;  // disbale | enable TX loopback for testing
	radio->tx_control_with_iq_if = true; // disable | enable TX control with IQ interface

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
		printw("CaribouLite Radio    [T]=TX ON/OFF  [R]=RX ON/OFF  [Q]=QUIT  [X]=RESET");	
		move(0, screen_max_x - 12);
		printw("%12ld",current_time);
		move(1,0);
		printw("    TX Loopback: %s",radio->tx_loopback_anabled?"on":"off");
		printw("    TX Frequency: %.0f Hz", round(frequency/1000)*1000);
		printw("    TX Power: %d dBm", tx_power);
        move(1, screen_max_x - 12);
		printw("%12.0f",elapsed_time);
		move(2,0);
		printw("Modem Status Registers:");
		move(3,0);
		//refresh();

		{
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
			printw("    RF_CFG:0x%02X  RF_CLKO:0x%02X\n", rf_cfg, rf_clko);
			printw("    IQIFC0:0x%02X  IQIFC1:0x%02X  IQIFC2:0x%02X\n", iqifc0, iqifc1, iqifc2);
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
			at86rf215_read_buffer(modem, REG_RF09_RXDFE, data, 1);
			HW_UNLOCK();
			uint8_t rf09_rxdfe = data[0];
			HW_LOCK();
			at86rf215_read_buffer(modem, REG_RF24_RXDFE, data, 1);
			HW_UNLOCK();
			uint8_t rf24_rxdfe = data[0];
			printw("    RF09-RXFDE :0x%02X  RF24-RXDFE :0x%02X\n", rf09_rxdfe, rf24_rxdfe);
			printw("    RF09-TXFDE :0x%02X  RF24-TXDFE :0x%02X\n", rf09_txdfe, rf24_txdfe);
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
			at86rf215_read_buffer(modem, REG_RF09_PAC, data, 1);
			HW_UNLOCK();
			uint8_t rf09_pac = data[0];
			HW_LOCK();
			at86rf215_read_buffer(modem, REG_RF24_PAC, data, 1);
			HW_UNLOCK();
			uint8_t rf24_pac = data[0];
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
			printw("    RF09-PADFE :0x%02X  RF24-PADFE :0x%02X\n", rf09_padfe, rf24_padfe);
			printw("    RF09-PAC   :0x%02X  RF24-PAC   :0x%02X\n", rf09_pac,   rf24_pac);
			printw("    RF09-IRQM  :0x%02X  RF24-IRQM  :0x%02X\n", rf09_irqm,  rf24_irqm);
			printw("    RF09-IQRS  :0x%02X  RF24-IRQS  :0x%02X\n", rf24_irqs,  rf24_irqs);	
			printw("    RF09-STATE :0x%02X  RF24-STATE :0x%02X\n", rf09_state, rf24_state);
			printw("    RF09-TXDACI:0x%02X  RF24-TXDACI:0x%02X\n", rf09_txdaci, rf24_txdaci);
			printw("    RF09-TXDACQ:0x%02X  RF24-TXDACQ:0x%02X\n", rf09_txdacq, rf24_txdacq);
			//refresh();
			HW_LOCK();
			caribou_fpga_get_smi_ctrl_fifo_status(&sys->fpga, &status);
			HW_UNLOCK();
			printw("FPGA SMI info (0x%02X):\n", *val);
			printw("    RX FIFO EMPTY: %d\n", status.rx_fifo_empty);
			printw("    TX FIFO FULL : %d\n", status.tx_fifo_full);
			printw("    SMI CHANNEL  : %d    // 0=RX09 1=RX24\n", status.smi_channel);
			printw("    SMI DIRECTION: %d    // 0=TX   1=RX\n", status.smi_direction);
			//refresh();
			HW_LOCK();
			caribou_fpga_get_io_ctrl_mode(&sys->fpga, &debug, &mode);
			HW_UNLOCK();
			printw("    DEBUG = %d, MODE: '%s'\n", debug, caribou_fpga_get_mode_name(mode));
			//refresh();
			printw("IQ Data Stream:\n");
			printw("    TX_I:0x%08X  TX_Q:0x%08X\n", latest_tx_sample.i, latest_tx_sample.q);
			printw("    RX_I:0x%08X  RX_Q:0x%08X\n", latest_rx_sample.i, latest_rx_sample.q);
			//refresh();
			//smi_state = caribou_smi_get_driver_streaming_state(smi);
			//printw("SMI driver state: 0x%02X    // 0=idle 1=RX09 2=RX24 3=TX\n",(uint8_t) smi->state);
			uint8_t tx_sample_gap = 1;
			HW_LOCK();
			caribou_fpga_get_sys_ctrl_tx_sample_gap(fpga, &tx_sample_gap);
			HW_UNLOCK();
			//printw("TX sample gap: %d\n", tx_sample_gap);
			//refresh();

			// --- TX FIFO stats panel ---
			{
				rf10_stats_t s; 
				rf10_fifo_get_stats(&txq, &s);

				// simple fill % and lag (producer - consumer)
				float fill_pct = (s.cap ? (100.0f * (float)s.count / (float)s.cap) : 0.f);
				long  lag      = (long)s.puts - (long)s.gets;  // frames queued since start

				// optional: rates since last sample
				static struct timespec last_ts = {0};
				static rf10_stats_t    last_s  = {0};
				double rate_puts = 0.0, rate_gets = 0.0;

				struct timespec now;
				clock_gettime(CLOCK_MONOTONIC, &now);
				if (last_ts.tv_sec != 0) {
					double dt = (now.tv_sec - last_ts.tv_sec) + (now.tv_nsec - last_ts.tv_nsec)/1e9;
					if (dt > 0.0) {
						rate_puts = (double)(s.puts - last_s.puts) / dt;
						rate_gets = (double)(s.gets - last_s.gets) / dt;
					}
				}
				last_ts = now; last_s = s;

				printw("Linux TX FIFO:\n");
				printw("    depth: %zu/%zu (%.0f%%), min:%zu max:%zu, lag:%ld\n",
					s.count, s.cap, fill_pct, s.min_depth, s.max_depth, lag);
				printw("    puts:%zu  gets:%zu  drops:%zu  tO_put:%zu  tO_get:%zu\n",
					s.puts, s.gets, s.drops, s.timeouts_put, s.timeouts_get);
				printw("    rate: puts %.1f/s, gets %.1f/s  (expect ~100 fps @ 10ms)\n",
					rate_puts, rate_gets);
				// If you want to highlight trouble:
				if (s.min_depth == 0)          printw("    NOTE: Under-runs observed (producer late)\n");
				if (s.drops > 0)               printw("    NOTE: Overwrites occurred (producer faster than writer)\n");
				if (s.timeouts_put > 0)        printw("    NOTE: Producer timed out waiting to enqueue\n");
				if (s.timeouts_get > 0)        printw("    NOTE: Writer timed out waiting for frames\n");
			}
		} 
		
		char key = 0;
		key = getch();
		
		if(key == 'q') // Press 'q' to exit
		{
			break;
		}

		if (key == 'x' || key == 'X') {      // reset FIFO diagnostics
			rf10_fifo_reset_stats(&txq);
		}
		
		if(key == 't') // Press 't' to toggle TX state and RFFE
		{
			if (nbfm_rx_active) 
			{
				nbfm_rx_active = false;
				HW_LOCK();
				cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_rx, false);	
			    HW_UNLOCK();
			}
			
			if (!nbfm_tx_active)
			{
				HW_LOCK();
				caribou_fpga_set_io_ctrl_mode (&sys->fpga, debug, caribou_fpga_io_ctrl_rfm_tx_lowpass);
				cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_tx, true);
				caribou_smi_set_driver_streaming_state(&sys->smi, (smi_stream_state_en)3); // TX
				HW_UNLOCK();

				__sync_synchronize();          // memory barrier
                nbfm_tx_active = true;         // tell the writer thread LAST
			}
			else 
			{
				nbfm_tx_active = false;
				__sync_synchronize();
				
				HW_LOCK();
				caribou_smi_set_driver_streaming_state(&sys->smi, (smi_stream_state_en)0); // idle
				cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_tx, false);
				caribou_fpga_set_io_ctrl_mode (&sys->fpga, debug, caribou_fpga_io_ctrl_rfm_low_power);
				HW_UNLOCK();
			}
		}
		
		if(key == 'r') // Press 'r' to toggle RX state and RFFE
		{
			if (nbfm_tx_active)
			{
				nbfm_tx_active = false;
				__sync_synchronize();
				
				HW_LOCK();
				caribou_smi_set_driver_streaming_state(&sys->smi, (smi_stream_state_en)0); // idle
				cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_tx, false);
				caribou_fpga_set_io_ctrl_mode (&sys->fpga, debug, caribou_fpga_io_ctrl_rfm_low_power);
				HW_UNLOCK();
			}
				
			if (!nbfm_rx_active)
			{
				nbfm_rx_active = true;
				HW_LOCK();
				cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_rx, true);
				caribou_fpga_set_io_ctrl_mode (&sys->fpga, debug, caribou_fpga_io_ctrl_rfm_rx_lowpass);
				HW_UNLOCK();
			}
			else 
			{
				nbfm_rx_active = false;
				HW_LOCK();
				cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_rx, false);
				caribou_fpga_set_io_ctrl_mode (&sys->fpga, debug, caribou_fpga_io_ctrl_rfm_low_power);
				HW_UNLOCK();
			}
		}
        
		loop_end = clock();
		elapsed_time = loop_end - loop_start;

	}
    
	nbfm_tx_active = false;
	nbfm_rx_active = false;
	smi_idle(sys);  // force driver to IDLE (unblocks reads/writes if they’re waiting)

	HW_LOCK();
    cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_tx, false);
    cariboulite_radio_activate_channel(radio, cariboulite_channel_dir_rx, false);
    HW_UNLOCK();
    usleep(30 * 1000);

	tx_ctrl.active = false;
	dsp_ctrl.active = false;
	rx_ctrl.active = false;
	rf10_fifo_stop(&txq);

	pthread_cancel(rx_thread);
    pthread_cancel(tx_thread);
    pthread_cancel(dsp_thread);
    
	pthread_join(tx_thread, NULL);
	pthread_join(dsp_thread, NULL);
	pthread_join(rx_thread, NULL);

	rf10_fifo_destroy(&txq);

	if (tx_ctrl.live_from_mic) {
		if (tx_ctrl.iq4m) free(tx_ctrl.iq4m);
		if (tx_ctrl.a48k) free(tx_ctrl.a48k);
		if (tx_ctrl.fm)   nbfm4m_destroy(tx_ctrl.fm);
		if (tx_ctrl.mic)  alsa48k_destroy(tx_ctrl.mic);
    }

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
