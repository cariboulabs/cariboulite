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
#include <alsa/asoundlib.h>

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
static void nbfm_modem_selftest(sys_st *sys);
static void monitor_modem_status(sys_st *sys);

// --- forward declarations for pthread entry points ---
static void* dsp_producer_thread_func(void* arg);
static void* tx_writer_thread_func(void* arg);
static void* rx_reader_thread_func(void* arg);
static void* nbfm_demod_thread(void* arg);
static void* audio_writer_thread(void* arg);

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
	{app_selection_nbfm_rx, nbfm_rx, "NBFM RX",},
    {app_selection_nbfm_modem_selftest, nbfm_modem_selftest, "NBFM modem Self-Test",},
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

static int set_rt_and_affinity_prio(int prio, int cpu_req)
{
    struct sched_param sp = { .sched_priority = prio };
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        sp.sched_priority = 0;
        pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
    }

    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    if (ncpu < 1) ncpu = 1;
    int cpu = (cpu_req >= 0) ? cpu_req : 0;
    if (cpu >= ncpu) cpu = (int)(ncpu - 1);

    cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (rc != 0) {
        fprintf(stderr, "[affinity] pid=%ld th=%lu prio=%d cpu_req=%d FAILED: %s\n",
                (long)getpid(), (unsigned long)pthread_self(), prio, cpu_req, strerror(rc));
        return -1;
    }
#ifdef __linux__
    int on = sched_getcpu();
    fprintf(stderr, "[affinity] th=%lu prio=%d pinned to CPU %d/%ld\n",
            (unsigned long)pthread_self(), prio, on, ncpu);
#endif
    mlockall(MCL_CURRENT | MCL_FUTURE);
    return 0;
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

static void write_exact_alsa_16(snd_pcm_t* pcm,
                                const int16_t* mono,
                                size_t frames,
                                unsigned channels)
{
    if (channels == 1) {
        // write mono directly
        size_t left = frames;
        const int16_t* p = mono;
        while (left) {
            snd_pcm_sframes_t w = snd_pcm_writei(pcm, p, left);
            if (w == -EPIPE) { snd_pcm_prepare(pcm); continue; }
            if (w == -EAGAIN) continue;
            if (w < 0) { fprintf(stderr,"ALSA write err: %s\n", snd_strerror((int)w)); break; }
            p += w; left -= (size_t)w;
        }
        return;
    }

    // duplicate mono → stereo into a small stack buffer (safe for 480 frames)
    int16_t interleaved[480 * 2];
    while (frames) {
        size_t chunk = frames > 480 ? 480 : frames;
        for (size_t i = 0, j = 0; i < chunk; ++i) {
            int16_t s = mono[i];
            interleaved[j++] = s;
            interleaved[j++] = s;
        }
        size_t left = chunk;
        const int16_t* p = interleaved;
        while (left) {
            snd_pcm_sframes_t w = snd_pcm_writei(pcm, p, left);
            if (w == -EPIPE) { snd_pcm_prepare(pcm); continue; }
            if (w == -EAGAIN) continue;
            if (w < 0) { fprintf(stderr,"ALSA write err: %s\n", snd_strerror((int)w)); break; }
            p += w * 2;      // 2 samples per frame
            left -= (size_t)w;
        }
        mono   += chunk;
        frames -= chunk;
    }
}

//=================================================

// ===== 10 ms AUDIO FIFO (producer: demod, consumer: ALSA) =====
typedef struct {
    int16_t pcm[480];   // mono, 48 kHz, 10 ms
} aud10_frame_t;

typedef struct {
    aud10_frame_t* q;
    size_t cap, r, w, count;
    pthread_mutex_t m;
    pthread_cond_t  can_put, can_get;
    bool stop;
    size_t drops;
} aud10_fifo_t;

static void aud10_fifo_init(aud10_fifo_t* f, size_t cap){
    memset(f,0,sizeof(*f));
    f->q = (aud10_frame_t*)calloc(cap,sizeof(aud10_frame_t));
    f->cap = cap;
    pthread_mutex_init(&f->m,NULL);
    pthread_cond_init(&f->can_put,NULL);
    pthread_cond_init(&f->can_get,NULL);
}
static void aud10_fifo_destroy(aud10_fifo_t* f){
    if(!f) return;
    pthread_mutex_destroy(&f->m);
    pthread_cond_destroy(&f->can_put);
    pthread_cond_destroy(&f->can_get);
    free(f->q);
}
static void aud10_fifo_stop(aud10_fifo_t* f){
    pthread_mutex_lock(&f->m);
    f->stop = true;
    pthread_cond_broadcast(&f->can_put);
    pthread_cond_broadcast(&f->can_get);
    pthread_mutex_unlock(&f->m);
}
static bool aud10_fifo_put(aud10_fifo_t* f, const aud10_frame_t* frm, int timeout_ms){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    ts.tv_nsec += (long)timeout_ms*1000000L; while(ts.tv_nsec>=1000000000L){ts.tv_nsec-=1000000000L; ts.tv_sec++;}
    pthread_mutex_lock(&f->m);
    while(!f->stop && f->count==f->cap){
        if(timeout_ms<0){ pthread_cond_wait(&f->can_put,&f->m); }
        else if(pthread_cond_timedwait(&f->can_put,&f->m,&ts)==ETIMEDOUT){ pthread_mutex_unlock(&f->m); return false; }
    }
    if(f->stop){ pthread_mutex_unlock(&f->m); return false; }
    f->q[f->w] = *frm; f->w=(f->w+1)%f->cap; f->count++;
    pthread_cond_signal(&f->can_get);
    pthread_mutex_unlock(&f->m);
    return true;
}
static bool aud10_fifo_get(aud10_fifo_t* f, aud10_frame_t* out, int timeout_ms){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    ts.tv_nsec += (long)timeout_ms*1000000L; while(ts.tv_nsec>=1000000000L){ts.tv_nsec-=1000000000L; ts.tv_sec++;}
    pthread_mutex_lock(&f->m);
    while(!f->stop && f->count==0){
        if(timeout_ms<0){ pthread_cond_wait(&f->can_get,&f->m); }
        else if(pthread_cond_timedwait(&f->can_get,&f->m,&ts)==ETIMEDOUT){ pthread_mutex_unlock(&f->m); return false; }
    }
    if(f->stop){ pthread_mutex_unlock(&f->m); return false; }
    *out = f->q[f->r]; f->r=(f->r+1)%f->cap; f->count--;
    pthread_cond_signal(&f->can_put);
    pthread_mutex_unlock(&f->m);
    return true;
}

// Peek audio FIFO depth without disturbing it
static inline void aud10_fifo_peek_depth(aud10_fifo_t* f, size_t* count, size_t* cap){
    pthread_mutex_lock(&f->m);
    *count = f->count;
    *cap   = f->cap;
    pthread_mutex_unlock(&f->m);
}

typedef struct {
    bool active;
    snd_pcm_t* pcm;
    unsigned channels;
    aud10_fifo_t* fifo;     // source of 10 ms audio frames
    size_t xruns;
} audio_writer_ctrl_t;

static void* audio_writer_thread(void* arg){
    
    audio_writer_ctrl_t* a = (audio_writer_ctrl_t*)arg;
    pthread_setname_np(pthread_self(),"audio_writer_thread");
    
    //set_rt_and_affinity();
    //set_rt_and_affinity_prio(42,-1); 
    set_rt_and_affinity_prio(46,0);

    // optional: make period/blocking behavior nicer
    while(a->active){
        aud10_frame_t frm;
        if(!aud10_fifo_get(a->fifo,&frm, /*timeout_ms=*/-1)){
            // starved: write silence to keep clock steady
            //int16_t zeros[480]={0};
            //write_exact_alsa_16(a->pcm, zeros, 480, a->channels);
            //continue;
            break;
        }
        // write one 10 ms block; handle xrun inside write_exact_alsa_16()
        write_exact_alsa_16(a->pcm, frm.pcm, 480, a->channels);
    }
    return NULL;
}


// forward decls placed before tx_writer_ctrl_st
typedef struct rf10_fifo_s rf10_fifo_t;
typedef struct rf10_frame_s rf10_frame_t;

typedef struct {
    bool                active;
    rf10_fifo_t*        fifo_in;     // 10ms @ 4MS/s IQ frames from rx_reader
    float               deemph_tau;  // e.g., 75e-6 (NA) or 50e-6 (EU)
    float               fs_rf;       // 4e6
    float               fs_audio;    // 48000
    volatile bool       reset;       // set true to force state re-init
    int                 prime_blocks_10ms; // e.g., 20 blocks = 200 ms @ 48k
    bool                priming;     // internal flag
    
    // state
    float               deemph_y;
    int16_t             last_i, last_q; // FM discrim previous sample

    // ALSA sink
    snd_pcm_t*          pcm;
    unsigned            pcm_rate;
    unsigned            pcm_channels;   
    float               pcm_gain;       
    uint64_t            pcm_total_frames; // diag counter
    aud10_fifo_t*  afifo_out;   // where the 10 ms audio frames go
} nbfm_demod_ctrl_t;

typedef struct {
    bool active;
    cariboulite_radio_state_st *radio;
    cariboulite_sample_complex_int16 *rx_buffer;
    size_t rx_buffer_size;
	
	// new
	rf10_fifo_t* rx_fifo;
} rx_reader_ctrl_st;

typedef struct {
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

typedef struct {
    bool                active;
    tx_writer_ctrl_st*  tx;      // reuse your modulator/mic/tone fields
    rf10_fifo_t*        fifo;
} dsp_producer_ctrl_t;

// -------------------- PIPELINE PARAMS --------------------
typedef struct {
    // Radio
    double freq_hz;             // e.g., 430.1e6
    int    tx_power_dbm;        // e.g., -3

    // Baseband source for NBFM mod
    bool   tone_mode;           // true => synth audio
    float  tone_hz;             // default 600.0f
    float  tone_amp;            // 0..1 (e.g., 0.4f)
    const char* mic_dev;        // ALSA capture device or NULL for no mic

    // NBFM modulator config (kept same as your current)
    float  out_scale;           // e.g., 4000.0f
    float  f_dev_hz;            // e.g., 2500.0f
} tx_params_t;

typedef struct {
    // Radio
    double freq_hz;             // e.g., 430.1e6

    // Demod/audio
    const char* pcm_dev;        // ALSA playback device ("plughw:3,0" etc.)
    float  deemph_tau_s;        // 50e-6 (EU) or 75e-6 (NA)
    float  pcm_gain;            // e.g., 8000.0f

    // Fixed rates
    float  fs_rf;               // 4e6
    float  fs_audio;            // 48e3
} rx_params_t;


// -------------------- PIPELINE HANDLES --------------------
typedef struct {
    // Allocated/owned objects
    rf10_fifo_t         txq;
    tx_writer_ctrl_st   tx_ctrl;
    dsp_producer_ctrl_t dsp_ctrl;

    // Threads
    pthread_t           dsp_thread;
    pthread_t           tx_thread;

    // State
    bool inited;
    bool running;

    // Binding
    sys_st*                         sys;
    cariboulite_radio_state_st*     radio;
} tx_pipeline_t;

typedef struct {
    // Allocated/owned objects
    rf10_fifo_t          rxq;       // IQ@4M → 10ms frames
    aud10_fifo_t         afifo;     // 10ms PCM for ALSA
    rx_reader_ctrl_st    rx_ctrl;
    nbfm_demod_ctrl_t    demod;
    audio_writer_ctrl_t  aw;

    // Threads
    pthread_t            rx_thread;
    pthread_t            demod_thread;
    pthread_t            aw_thread;

    // State
    bool inited;
    bool running;

    // Binding
    sys_st*                         sys;
    cariboulite_radio_state_st*     radio;
} rx_pipeline_t;


// -------------------- PIPELINE APIS (TX) --------------------
int  tx_pipeline_init   (tx_pipeline_t* p, sys_st* sys,
                         cariboulite_radio_state_st* radio,
                         const tx_params_t* par);
int  tx_pipeline_start  (tx_pipeline_t* p);
void tx_pipeline_stop   (tx_pipeline_t* p);
void tx_pipeline_destroy(tx_pipeline_t* p);

// -------------------- PIPELINE APIS (RX) --------------------
int  rx_pipeline_init   (rx_pipeline_t* p, sys_st* sys,
                         cariboulite_radio_state_st* radio,
                         const rx_params_t* par);
int  rx_pipeline_start  (rx_pipeline_t* p);
void rx_pipeline_stop   (rx_pipeline_t* p);
void rx_pipeline_destroy(rx_pipeline_t* p);

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

// must be visible before any thread uses them
static cariboulite_sample_complex_int16 latest_rx_sample = (cariboulite_sample_complex_int16){0};
static cariboulite_sample_complex_int16 latest_tx_sample = (cariboulite_sample_complex_int16){0};

// prototypes so C knows exact signatures before first use
static inline void fill_tone_48k(tx_writer_ctrl_st* ctrl, float* buf, size_t n);
static void read_audio_exact(alsa48k_source_t* mic, float* buf, size_t need);


static void* dsp_producer_thread_func(void* arg)
{
    pthread_setname_np(pthread_self(), "dsp_producer_thread");
    //set_rt_and_affinity();   // make sure this logs failures
    //set_rt_and_affinity_prio(45,-1);
    set_rt_and_affinity_prio(40,0);

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
    pthread_setname_np(pthread_self(),"rx_reader_thread");
    //set_rt_and_affinity();
    //set_rt_and_affinity_prio(30, -1);
    // Highest prio; reader must never be blocked by DSP/ALSA
    set_rt_and_affinity_prio(70, 2);   // falls back to CPU0 if missing 

    rx_reader_ctrl_st* ctrl = (rx_reader_ctrl_st*)arg;
    caribou_smi_st *smi = &ctrl->radio->sys->smi;

    const size_t want = 40000; // 10 ms @ 4 MS/s
    cariboulite_sample_complex_int16* buf = ctrl->rx_buffer;
    cariboulite_sample_meta* meta = malloc(sizeof(*meta) * want);

    size_t have = 0;
    while (ctrl->active) {
        int ret = cariboulite_radio_read_samples(ctrl->radio, buf + have, meta, want - have);
        if (ret > 0) have += (size_t)ret;

        if (have == want) {
            rf10_frame_t frm;
            for (size_t i=0;i<want;i++) {
                frm.data[i].i = buf[i].i;
                frm.data[i].q = buf[i].q;
            }

            latest_rx_sample = (cariboulite_sample_complex_int16){
                .i = frm.data[20000].i,
                .q = frm.data[20000].q
            };

            //rf10_fifo_put(ctrl->rx_fifo /*add to ctrl*/, &frm, -1);
            if (!rf10_fifo_put(ctrl->rx_fifo, &frm, /*timeout_ms=*/-1)) {
                // FIFO full -> overwrite oldest already happened; just continue
                // (you can keep a counter if you want to log drops)
            }
            have = 0;
        }
    }
    free(meta);
    return NULL;
}


static const char* pcm_state_name(snd_pcm_state_t s){
    switch (s){
        case SND_PCM_STATE_OPEN: return "OPEN";
        case SND_PCM_STATE_SETUP: return "SETUP";
        case SND_PCM_STATE_PREPARED: return "PREPARED";
        case SND_PCM_STATE_RUNNING: return "RUNNING";
        case SND_PCM_STATE_XRUN: return "XRUN";
        case SND_PCM_STATE_DRAINING: return "DRAINING";
        case SND_PCM_STATE_PAUSED: return "PAUSED";
        case SND_PCM_STATE_SUSPENDED: return "SUSPENDED";
        case SND_PCM_STATE_DISCONNECTED: return "DISCONNECTED";
        default: return "?";
    }
}

// after alsa_open_playback() succeeds:
static void alsa_tune_sw(snd_pcm_t* pcm){
    snd_pcm_sw_params_t *sw = NULL;
    snd_pcm_sw_params_malloc(&sw);
    snd_pcm_sw_params_current(pcm, sw);

    snd_pcm_uframes_t psize=0, bsize=0;
    snd_pcm_get_params(pcm, &bsize, &psize); // (buffer_size, period_size)

    // Don't start until the buffer is nearly full — prevents initial XRUN/stutter.
    snd_pcm_sw_params_set_start_threshold(pcm, sw, bsize - psize);
    // Wake writer when at least one period is free.
    snd_pcm_sw_params_set_avail_min(pcm, sw, psize);

    snd_pcm_sw_params(pcm, sw);
    snd_pcm_sw_params_free(sw);
}

static int alsa_open_playback(snd_pcm_t **ppcm,
                              const char* dev,
                              unsigned *out_rate,
                              unsigned *out_channels)
{
    snd_pcm_t* pcm = NULL;
    snd_pcm_hw_params_t* hw = NULL;
    int rc;

    const char* card = dev && *dev ? dev : "default";
    if ((rc = snd_pcm_open(&pcm, card, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "ALSA: open(%s) failed: %s\n", card, snd_strerror(rc));
        return -1;
    }

    snd_pcm_hw_params_malloc(&hw);
    snd_pcm_hw_params_any(pcm, hw);
    snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, hw, SND_PCM_FORMAT_S16_LE);

    unsigned rate = 48000; int dir = 0;
    snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, &dir);

    // try mono first; if it fails we’ll retry with 2ch
    unsigned ch = 1;
    rc = snd_pcm_hw_params_set_channels(pcm, hw, ch);
    if (rc < 0) {
        ch = 2;
        rc = snd_pcm_hw_params_set_channels(pcm, hw, ch);
        if (rc < 0) { fprintf(stderr,"ALSA: channels failed: %s\n", snd_strerror(rc)); goto fail; }
    }

    // make buffer a bit deeper to avoid XRUN storms
    snd_pcm_uframes_t period = 480;    // 10 ms
    snd_pcm_uframes_t buffer = 2400;   // 50 ms
    snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, NULL);
    snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer);

    if ((rc = snd_pcm_hw_params(pcm, hw)) < 0) { fprintf(stderr,"ALSA: hw_params: %s\n", snd_strerror(rc)); goto fail; }
    snd_pcm_hw_params_free(hw);

    if ((rc = snd_pcm_prepare(pcm)) < 0) { fprintf(stderr,"ALSA: prepare: %s\n", snd_strerror(rc)); snd_pcm_close(pcm); return -1; }

    fprintf(stderr, "ALSA: opened %s, %uch, S16_LE, %u Hz\n", card, ch, rate);
    *ppcm = pcm;
    if (out_rate)     *out_rate = rate;
    if (out_channels) *out_channels = ch;
    return 0;

fail:
    snd_pcm_hw_params_free(hw);
    snd_pcm_close(pcm);
    return -1;
}

// ========================= TX PIPELINE =========================
int tx_pipeline_init(tx_pipeline_t* p, sys_st* sys,
                     cariboulite_radio_state_st* radio,
                     const tx_params_t* par)
{
    if (!p || !sys || !radio || !par) return -1;
    memset(p, 0, sizeof(*p));
    p->sys   = sys;
    p->radio = radio;

    // FIFOs
    rf10_fifo_init(&p->txq, /*cap=*/64, /*drop_oldest_on_full=*/false);

    // tx_ctrl wiring (reuse your structures/threads)
    p->tx_ctrl.active         = true;
    p->tx_ctrl.radio          = radio;
    p->tx_ctrl.fifo           = &p->txq;
    p->tx_ctrl.live_from_mic  = (par->mic_dev && *par->mic_dev);
    p->tx_ctrl.mic            = NULL;            // (open later if needed)
    p->tx_ctrl.tone_mode      = par->tone_mode;
    p->tx_ctrl.tone_hz        = par->tone_hz;
    p->tx_ctrl.tone_amp       = par->tone_amp;
    p->tx_ctrl.tone_phase     = 0.0f;
    p->tx_ctrl.fm             = NULL;
    p->tx_ctrl.a48k           = NULL;
    p->tx_ctrl.iq4m           = NULL;

    // NBFM mod init
    nbfm4m_cfg_t cfg = {
        .audio_fs      = 48000.0,
        .rf_fs         = 4000000.0,
        .f_dev_hz      = par->f_dev_hz,     // 2500.0
        .preemph_tau_s = 0.0,
        .out_scale     = par->out_scale,    // 4000.0
        .linear_interp = 1,
    };
    p->tx_ctrl.fm   = nbfm4m_create(&cfg);
    p->tx_ctrl.a48k = (float*) calloc(480,    sizeof(float));
    p->tx_ctrl.iq4m = (iq16_t*)calloc(40000,  sizeof(iq16_t));
    if (!p->tx_ctrl.fm || !p->tx_ctrl.a48k || !p->tx_ctrl.iq4m) {
        if (p->tx_ctrl.fm)   nbfm4m_destroy(p->tx_ctrl.fm);
        if (p->tx_ctrl.a48k) free(p->tx_ctrl.a48k);
        if (p->tx_ctrl.iq4m) free(p->tx_ctrl.iq4m);
        rf10_fifo_destroy(&p->txq);
        return -2;
    }

    // Optional mic
    if (p->tx_ctrl.live_from_mic) {
        p->tx_ctrl.mic = alsa48k_create(par->mic_dev, 1.0f);
        if (!p->tx_ctrl.mic) {
            // mic failed — fall back to tone
            p->tx_ctrl.live_from_mic = false;
            p->tx_ctrl.tone_mode     = true;
        }
    }

    // Radio basic set
    HW_LOCK();
    cariboulite_radio_set_frequency(radio, true, (double*)&par->freq_hz);
    cariboulite_radio_set_tx_power (radio, par->tx_power_dbm);
    HW_UNLOCK();

    // Prepare DSP/Writer threads (running idle until .start)
    p->dsp_ctrl.active = true;
    p->dsp_ctrl.tx     = &p->tx_ctrl;
    p->dsp_ctrl.fifo   = &p->txq;
    if (pthread_create(&p->dsp_thread, NULL, dsp_producer_thread_func, &p->dsp_ctrl) != 0)
        return -3;

    if (pthread_create(&p->tx_thread,  NULL, tx_writer_thread_func,    &p->tx_ctrl) != 0)
        return -4;

    p->inited = true;
    p->running = false;
    return 0;
}

int tx_pipeline_start(tx_pipeline_t* p)
{
    if (!p || !p->inited || p->running) return -1;

    // Disable RX if it was on and arm TX chain
    if (nbfm_rx_active) {
        nbfm_rx_active = false;
        HW_LOCK();
        cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_rx, false);
        HW_UNLOCK();
        usleep(2000);
    }

    HW_LOCK();
    caribou_fpga_set_io_ctrl_mode(&p->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_tx_lowpass);
    cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_tx, true);
    // give writer a head-start so FIFO fills a bit
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 30*1000*1000 };
    nanosleep(&ts, NULL);
    caribou_smi_set_driver_streaming_state(&p->sys->smi, (smi_stream_state_en)3); // TX
    HW_UNLOCK();

    __sync_synchronize();
    nbfm_tx_active = true;

    p->running = true;
    return 0;
}

void tx_pipeline_stop(tx_pipeline_t* p)
{
    if (!p || !p->inited || !p->running) return;

    nbfm_tx_active = false;
    __sync_synchronize();

    HW_LOCK();
    caribou_smi_set_driver_streaming_state(&p->sys->smi, (smi_stream_state_en)0); // idle
    cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_tx, false);
    caribou_fpga_set_io_ctrl_mode(&p->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
    HW_UNLOCK();

    p->running = false;
}

void tx_pipeline_destroy(tx_pipeline_t* p)
{
    if (!p || !p->inited) return;

    tx_pipeline_stop(p);

    // stop threads and free
    p->tx_ctrl.active  = false;
    p->dsp_ctrl.active = false;
    rf10_fifo_stop(&p->txq);

    pthread_cancel(p->tx_thread);
    pthread_cancel(p->dsp_thread);
    pthread_join(p->tx_thread, NULL);
    pthread_join(p->dsp_thread, NULL);

    rf10_fifo_destroy(&p->txq);

    if (p->tx_ctrl.iq4m) free(p->tx_ctrl.iq4m);
    if (p->tx_ctrl.a48k) free(p->tx_ctrl.a48k);
    if (p->tx_ctrl.fm)   nbfm4m_destroy(p->tx_ctrl.fm);
    if (p->tx_ctrl.mic)  alsa48k_destroy(p->tx_ctrl.mic);

    p->inited = false;
}

// ========================= RX PIPELINE =========================
int rx_pipeline_init(rx_pipeline_t* p, sys_st* sys,
                     cariboulite_radio_state_st* radio,
                     const rx_params_t* par)
{
    if (!p || !sys || !radio || !par) return -1;
    memset(p, 0, sizeof(*p));
    p->sys   = sys;
    p->radio = radio;

    // FIFOs
    rf10_fifo_init(&p->rxq,  /*cap=*/128, /*drop_oldest_on_full=*/true);
    aud10_fifo_init(&p->afifo, /*cap=*/24);

    // Open ALSA playback
    unsigned rate=0, channels=0;
    if (alsa_open_playback(&p->demod.pcm, par->pcm_dev, &rate, &channels) != 0) {
        aud10_fifo_destroy(&p->afifo);
        rf10_fifo_destroy(&p->rxq);
        return -2;
    }
    alsa_tune_sw(p->demod.pcm);

    // Audio writer
    p->aw.active   = true;
    p->aw.pcm      = p->demod.pcm;
    p->aw.channels = channels ? channels : 1;
    p->aw.fifo     = &p->afifo;
    if (pthread_create(&p->aw_thread, NULL, audio_writer_thread, &p->aw) != 0)
        return -3;

    // Demod setup
    p->demod.reset             = true;
    p->demod.prime_blocks_10ms = 20;    // 20 * 10ms = 200 ms
    p->demod.priming           = true;
    p->demod.active            = true;
    p->demod.fifo_in           = &p->rxq;
    p->demod.afifo_out         = &p->afifo;
    p->demod.fs_rf             = par->fs_rf;      // 4e6
    p->demod.fs_audio          = par->fs_audio;   // 48e3
    p->demod.deemph_tau        = par->deemph_tau_s;
    p->demod.pcm_gain          = par->pcm_gain;
    p->demod.pcm_total_frames  = 0;
    p->demod.pcm_channels      = p->aw.channels;
    p->demod.pcm_rate          = rate;

    if (pthread_create(&p->demod_thread, NULL, nbfm_demod_thread, &p->demod) != 0)
        return -4;

    // Reader (prepare only — start later in rx_pipeline_start)
    p->rx_ctrl.active         = false;
    p->rx_ctrl.radio          = radio;
    p->rx_ctrl.rx_buffer      = NULL;         // allocate on start
    p->rx_ctrl.rx_buffer_size = 0;
    p->rx_ctrl.rx_fifo        = &p->rxq;

    // Set radio frequency
    HW_LOCK();
    cariboulite_radio_set_frequency(radio, true, (double*)&par->freq_hz);
    HW_UNLOCK();

    p->inited = true;
    p->running = false;
    return 0;
}

int rx_pipeline_start(rx_pipeline_t* p)
{
    if (!p || !p->inited || p->running) return -1;

    // Stop TX if needed
    if (nbfm_tx_active) {
        nbfm_tx_active = false;
        __sync_synchronize();
        HW_LOCK();
        caribou_smi_set_driver_streaming_state(&p->sys->smi, (smi_stream_state_en)0);
        cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_tx, false);
        caribou_fpga_set_io_ctrl_mode(&p->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
        HW_UNLOCK();
    }

    HW_LOCK();
    caribou_fpga_set_io_ctrl_mode(&p->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_rx_lowpass);
    cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_rx, true);
    caribou_smi_set_driver_streaming_state(&p->sys->smi, (smi_stream_state_en)1); // RX on S1G
    HW_UNLOCK();

    // start reader now (only when RX is active)
    p->rx_ctrl.active        = true;
    p->rx_ctrl.radio         = p->radio;
    if (!p->rx_ctrl.rx_buffer) {
        p->rx_ctrl.rx_buffer      = malloc(sizeof(cariboulite_sample_complex_int16) * 40000);
        p->rx_ctrl.rx_buffer_size = 40000;
        p->rx_ctrl.rx_fifo        = &p->rxq;
    }
    pthread_create(&p->rx_thread, NULL, rx_reader_thread_func, &p->rx_ctrl);

    __sync_synchronize();

    p->demod.prime_blocks_10ms = 8;   // gentle start whenever RX is toggled on
    p->demod.reset = true;            // demod thread will reinit on next loop

    nbfm_rx_active = true;

    p->running = true;
    return 0;
}

void rx_pipeline_stop(rx_pipeline_t* p)
{
    if (!p || !p->inited || !p->running) return;

    nbfm_rx_active = false;
    __sync_synchronize();

    HW_LOCK();
    cariboulite_radio_activate_channel(p->radio, cariboulite_channel_dir_rx, false);
    caribou_smi_set_driver_streaming_state(&p->sys->smi, (smi_stream_state_en)0);
    caribou_fpga_set_io_ctrl_mode(&p->sys->fpga, 0, caribou_fpga_io_ctrl_rfm_low_power);
    HW_UNLOCK();

    // join reader here
    p->rx_ctrl.active = false;
    pthread_cancel(p->rx_thread);
    pthread_join(p->rx_thread, NULL);


    p->running = false;
}

void rx_pipeline_destroy(rx_pipeline_t* p)
{
    if (!p || !p->inited) return;

    rx_pipeline_stop(p);

    // stop threads and free
    p->rx_ctrl.active = false;
    p->demod.active   = false;
    p->aw.active      = false;
    rf10_fifo_stop(&p->rxq);
    aud10_fifo_stop(&p->afifo);

    pthread_cancel(p->rx_thread);
    pthread_cancel(p->demod_thread);
    pthread_cancel(p->aw_thread);
    pthread_join(p->rx_thread, NULL);
    pthread_join(p->demod_thread, NULL);
    pthread_join(p->aw_thread, NULL);

    if (p->rx_ctrl.rx_buffer) {
        free(p->rx_ctrl.rx_buffer);
        p->rx_ctrl.rx_buffer = NULL;
    }

    if (p->demod.pcm) snd_pcm_close(p->demod.pcm);
    aud10_fifo_destroy(&p->afifo);
    rf10_fifo_destroy(&p->rxq);

    p->inited = false;
}

// --- helper to reinitialize all demod state (C version) ---
static void reinit_demod_state(
    float *pi50, float *pq50, int *have_prev50,
    float *dc_y, float *x_prev_audio,
    float *deemph_state, float *lpf_y,
    float *y_prev_50k, float *y_curr_50k,
    double *phase48, double *corr48,
    double *depth_ema, int *servo_tick,
    int *primed, int *nout,
    int *priming_blocks_left,
    nbfm_demod_ctrl_t *c)
{
    // previous complex @50k
    *pi50 = 0.0f; *pq50 = 0.0f; *have_prev50 = 0;

    // 48k chain state
    *dc_y = 0.0f; *x_prev_audio = 0.0f;
    *deemph_state = 0.0f;
    *lpf_y = 0.0f;
    *y_prev_50k = 0.0f;
    *y_curr_50k = 0.0f;

    // resampler servo / accumulator
    *phase48 = 0.0;
    *corr48 = 0.0;
    *depth_ema = 0.0;
    *servo_tick = 0;
    *primed = 0;

    // audio packer
    *nout = 0;

    // priming counter
    *priming_blocks_left = c->prime_blocks_10ms;
    c->priming = (*priming_blocks_left > 0);
}

// --- simple 1st-order de-emphasis (continuous-time tau) at fs samples/s
static inline float deemph(float x, float *y, float tau, float fs)
{
    const float a = expf(-1.0f/(fs * tau));       // pole
    const float b = 1.0f - a;                     // zero gain so DC passes less
    *y = a * (*y) + b * x;
    return *y;
}

// --- audio-rate deemphasis (48 kHz) ---
static inline float deemph_48k(float x, float *z, float tau_s)
{
    if (tau_s <= 0.f) return x;          // bypass if tau==0
    const float fs = 48000.f;
    const float a  = expf(-1.0f/(fs * tau_s));
    const float b  = 1.0f - a;
    *z = a * (*z) + b * x;
    return *z;
}

static inline float fast_atan2f(float y, float x) {
    // 7th-order minimax (or a lighter 3rd-order) — plenty of references online
    // placeholder: use your preferred fast atan2f implementation
    const float ONEQTR_PI = (float)M_PI_4;        // π/4
    const float THRQTR_PI = (float)(3.0f * M_PI_4); // 3π/4
    float abs_y = fabsf(y) + 1e-10f;               // prevent 0/0
    float angle;
    if (x >= 0.0f) {
        float r = (x - abs_y) / (x + abs_y);
        angle = ONEQTR_PI - ONEQTR_PI * r;
    } else {
        float r = (x + abs_y) / (abs_y - x);
        angle = THRQTR_PI - ONEQTR_PI * r;
    }
    return (y < 0.0f) ? -angle : angle;
}

// Ultra-fast small-angle atan2f approximation
// Error < 0.005 rad for |y/x| < 0.3 (typical in NBFM discriminator)
static inline float fast_atan2f_small(float y, float x)
{
    // approximate atan(y/x) ≈ y / (|x| + 0.28f*|y|)
    float abs_y = fabsf(y);
    float abs_x = fabsf(x);
    float angle = y / (abs_x + 0.28f * abs_y + 1e-10f);
    if (x < 0.0f)
        angle = (y >= 0.0f ? (float)M_PI + angle : -((float)M_PI - angle));
    return angle;
}


// --- FM discriminator (atan2), 3/250 resample to 48k, deemphasis at 48k ---
// --- Pre-demod CIC decimator (20 x 4) -> 50 kS/s, then limiter+atan2, 50k->48k, deemph @48k ---
// --- FM discriminator (atan2), 4M->50k integrate&dump, adaptive 50k->48k, deemph @48k ---
// --- FM discriminator (atan2), 4M->50k I&D, fixed 50k->48k (24/25), DC block, deemph @48k
// --- NBFM demod: I/Q integrate&dump to 50k -> limiter -> discriminator @50k
//                  -> fixed 24/25 resample to 48k -> DC block -> deemph -> light LPF
static void* nbfm_demod_thread(void* arg)
{
    pthread_setname_np(pthread_self(),"nbfm_demod_thread");
    set_rt_and_affinity_prio(55,1);

    nbfm_demod_ctrl_t* c = (nbfm_demod_ctrl_t*)arg;
    if (!c || !c->fifo_in || !c->pcm) return NULL;

    // 4e6 -> 50k via integrate & dump: 20x then 4x (total 80x)
    enum { D1 = 20, D2 = 4 };                // 4e6 / 80 = 50 kS/s
    const float fs_mid = 50000.0f;

    // FM deviation (matches your TX)
    const float f_dev  = 2500.0f;
    const float K_norm = fs_mid / (2.0f * (float)M_PI * f_dev);   // scale dphi -> ~±1 at ±dev

    // 50k -> 48k via fixed rational 24/25 (exact)
    const int   L = 24, M = 25;              // kept for reference (acc not used)
    int         acc = 0;                     // (unused but harmless)
    float       y_prev_50k = 0.0f, y_curr_50k = 0.0f;

    // Optional vector limiter
    const int use_limiter = 1;

    // DC blocker at 48k (~5 Hz HPF)
    const float dc_fc = 5.0f;
    const float dc_a  = expf(-2.0f * (float)M_PI * dc_fc / 48000.0f);
    float dc_y = 0.0f, x_prev_audio = 0.0f;

    // De-emphasis state (48k)
    float deemph_state = 0.0f;

    // Gentle audio LPF post-deemph (~3.2 kHz, 1st order)
    const float lpf_fc = 3200.0f;
    const float lpf_a  = expf(-2.0f * (float)M_PI * lpf_fc / 48000.0f);
    const float lpf_b  = 1.0f - lpf_a;
    float lpf_y = 0.0f;

    // Previous decimated complex sample for discriminator
    float pi50 = 0.0f, pq50 = 0.0f;
    int   have_prev50 = 0;

    // --- adaptive 50k -> 48k servo (NON-static so reset works) ---
    double phase48 = 0.0;     // in [0..1)
    double corr48  = 0.0;     // small fractional correction (unitless)
    double depth_ema = 0.0;   // smoothed FIFO depth
    int    servo_tick = 0;
    int    primed = 0;

    // audio packer
    int    nout = 0;          // NOTE: int (matches reinit_demod_state signature)
    int    priming_blocks_left = 0;

    // ---------------- Working buffers ----------------
    int16_t audio_10ms[480];  // 10 ms @ 48 kHz
    size_t  a10_len = 0;      // (unused: you can remove if you like)

    // I&D accumulators on I and Q (do NOT demod at 4M)
    float ai1 = 0.0f, aq1 = 0.0f; int cnt1 = 0;
    float ai2 = 0.0f, aq2 = 0.0f; int cnt2 = 0;

    // heartbeat
    uint64_t last_log_ms = 0;

    // ------------- Initialize once -------------
    if (c->prime_blocks_10ms <= 0) c->prime_blocks_10ms = 8; // sensible default
    c->reset = true;  // force clean start
    reinit_demod_state(&pi50,&pq50,&have_prev50,
                       &dc_y,&x_prev_audio,
                       &deemph_state,&lpf_y,
                       &y_prev_50k,&y_curr_50k,
                       &phase48,&corr48,
                       &depth_ema,&servo_tick,
                       &primed,&nout,
                       &priming_blocks_left, c);
    c->reset = false;

    while (c->active) {

        // allow external reset (e.g., after first-start flicker or frequency change)
        if (c->reset) {
            reinit_demod_state(&pi50,&pq50,&have_prev50,
                               &dc_y,&x_prev_audio,
                               &deemph_state,&lpf_y,
                               &y_prev_50k,&y_curr_50k,
                               &phase48,&corr48,
                               &depth_ema,&servo_tick,
                               &primed,&nout,
                               &priming_blocks_left, c);
            c->reset = false;
        }

        rf10_frame_t frm;
        if (!rf10_fifo_get(c->fifo_in, &frm, -1)) continue;

        for (size_t n = 0; n < 40000; n++) {
            // --- accumulate @ 4M (stage-1) ---
            ai1 += (float)frm.data[n].i;
            aq1 += (float)frm.data[n].q;
            if (++cnt1 != D1) continue;

            // boxcar avg #1
            float i1 = ai1 / (float)D1;
            float q1 = aq1 / (float)D1;
            ai1 = aq1 = 0.0f; cnt1 = 0;

            // --- accumulate @ 200k (stage-2 to 50k) ---
            ai2 += i1;
            aq2 += q1;
            if (++cnt2 != D2) continue;

            // boxcar avg #2 -> 50 kS/s complex sample
            float i50 = ai2 / (float)D2;
            float q50 = aq2 / (float)D2;
            ai2 = aq2 = 0.0f; cnt2 = 0;

            // --- limiter (unit vector) ---
            if (use_limiter) {
                float m2 = i50*i50 + q50*q50;
                if (m2 > 0.0f) {
                    float invm = 1.0f / sqrtf(m2);
                    i50 *= invm; q50 *= invm;
                }
            }

            // --- discriminator at 50 kS/s using previous 50k sample ---
            float y50 = 0.0f;
            if (have_prev50) {
                const float re = i50 * pi50 + q50 * pq50;
                const float im = q50 * pi50 - i50 * pq50;
                const float dphi = fast_atan2f_small(im, re);
                y50 = dphi * K_norm;                   // normalize to ~±1 @ ±dev
            } else {
                have_prev50 = 1;
            }
            pi50 = i50; pq50 = q50;

            // --- 50k → 48k adaptive resampler (fractional-step with tiny PLL/servo) ---
            // Nominal outputs per 50k input (r < 1 for downsampling)
            const double r_nom = 48000.0 / 50000.0;      // 0.96
            const int    upd_every_inputs = 500;         // ≈10 ms @ 50 kS/s
            const double alpha = 0.05;                   // EMA smoothing
            const double ki    = 2.0e-4;                 // integral gain
            const double corr_ppm_cap  = 3.0e-4;         // ±300 ppm clamp
            const double corr_ppm_slew = 1.0e-5;         // ±10 ppm per update
            const double target_fill = 0.50;             // 50% of capacity
            const double deadband    = 0.01;             // ±2% fill deadband

            // Update every ~10 ms worth of 50k inputs
            if (++servo_tick >= upd_every_inputs) {
                servo_tick = 0;

                size_t acnt = 0, acap = 0;
                aud10_fifo_peek_depth(c->afifo_out, &acnt, &acap);
                const double fill = (acap ? (double)acnt / (double)acap : 0.0);

                // smooth depth
                if (depth_ema == 0.0) depth_ema = (double)acnt;  // init on first call
                depth_ema = (1.0 - alpha) * depth_ema + alpha * (double)acnt;

                // engage after we’re in the neighborhood (prevents big initial pulls)
                if (!primed) {
                    if (fill >= 0.35) primed = 1;   // start controlling once buffer >35%
                }

                double err = 0.0;
                if (primed) {
                    const double target = target_fill * (double)acap;
                    const double err_raw = (double)depth_ema - target; // +err => overfilling
                    if (fabs(err_raw) > deadband * (double)acap)
                        err = err_raw;
                }

                // integral update with slew limit
                double corr_prev = corr48;
                corr48 += -ki * (err / (double)acap);  // unitless; negative feedback

                // slew limit (per update) to avoid pitch steps
                double dc = corr48 - corr_prev;
                if (dc >  corr_ppm_slew) corr48 = corr_prev + corr_ppm_slew;
                if (dc < -corr_ppm_slew) corr48 = corr_prev - corr_ppm_slew;

                // hard clamp
                if (corr48 >  corr_ppm_cap) corr48 =  corr_ppm_cap;
                if (corr48 < -corr_ppm_cap) corr48 = -corr_ppm_cap;

                // emergency nudges
                if (fill > 0.95) corr48 = fmin(corr48, -5e-4);
                if (fill < 0.05) corr48 = fmax(corr48,  5e-4);
            }

            // Interpolation endpoints for this 50k interval
            y_prev_50k = y_curr_50k;
            y_curr_50k = y50;

            // Advance phase by "outputs per input" this step (r < 1)
            const double r = r_nom * (1.0 + corr48);
            phase48 += r;

            // If we crossed 1.0, emit exactly one 48k sample at that crossing
            if (phase48 >= 1.0) {
                const double frac = (phase48 - 1.0) / r;    // ∈ [0..1)
                float y_lin = y_prev_50k + (float)frac * (y_curr_50k - y_prev_50k);

                // === 48k audio chain ===
                float x = y_lin;
                float y = (x - x_prev_audio) + dc_a * dc_y;
                x_prev_audio = x;
                dc_y = y; if (fabsf(dc_y) < 1e-20f) dc_y = 0.0f;

                float yd = deemph_48k(y, &deemph_state, c->deemph_tau);
                if (fabsf(deemph_state) < 1e-20f) deemph_state = 0.0f;

                lpf_y = lpf_a * lpf_y + lpf_b * yd;
                float ya = lpf_y;
                if (fabsf(lpf_y) < 1e-20f) lpf_y = 0.0f;

                float s = ya * c->pcm_gain;
                if (s >  32767.f) s =  32767.f;
                if (s < -32768.f) s = -32768.f;
                audio_10ms[nout++] = (int16_t)lrintf(s);

                // hand off in 10 ms chunks
                if (nout == 480) {
                    aud10_frame_t af; memcpy(af.pcm, audio_10ms, sizeof(audio_10ms));
                    aud10_fifo_put(c->afifo_out, &af, /*timeout_ms=*/10);
                    c->pcm_total_frames += 480;
                    nout = 0;

                    // heartbeat (optional)
                    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
                    uint64_t ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
                    if (!last_log_ms) last_log_ms = ms;
                    if (ms - last_log_ms >= 1000) {
                        size_t acnt=0, acap=0;
                        aud10_fifo_peek_depth(c->afifo_out, &acnt, &acap);
                        fprintf(stderr,
                            "DEMOD: frames=%llu (%.1fs) ALSA=%s  aud_fifo=%zu/%zu (%.0f%%)  corr=%.5f\n",
                            (unsigned long long)c->pcm_total_frames,
                            (double)c->pcm_total_frames / (double)c->pcm_rate,
                            pcm_state_name(snd_pcm_state(c->pcm)),
                            acnt, acap, 100.0 * (double)acnt / (double)acap,
                            corr48);
                        last_log_ms = ms;
                    }
                }

                // keep fractional remainder
                phase48 -= 1.0;
            }
        }
    }
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
    pthread_setname_np(pthread_self(), "tx_writer_thread");
    //set_rt_and_affinity();
    //set_rt_and_affinity_prio(42,-1);
    set_rt_and_affinity_prio(48,0);


    tx_writer_ctrl_st* ctrl = (tx_writer_ctrl_st*)arg;
    if (!ctrl || !ctrl->radio || !ctrl->radio->sys) return NULL;

    caribou_smi_st *smi = &ctrl->radio->sys->smi;
    if (!smi || smi->filedesc < 0) return NULL;

    rf10_fifo_t* fifo = ctrl->fifo;
    if (!fifo) return NULL;

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

// ---------- convenience: running state ----------
static inline bool tx_pipeline_running(const tx_pipeline_t* p) { return p && p->running; }
static inline bool rx_pipeline_running(const rx_pipeline_t* p) { return p && p->running; }

// ---------- TX: retune / power (safe during run) ----------
int tx_pipeline_set_freq_power(tx_pipeline_t* p, double freq_hz, int tx_power_dbm)
{
    if (!p || !p->sys || !p->radio) return -1;
    HW_LOCK();
    cariboulite_radio_set_frequency(p->radio, true, &freq_hz);
    cariboulite_radio_set_tx_power(p->radio, tx_power_dbm);
    HW_UNLOCK();
    return 0;
}

// ---------- RX: retune ----------
int rx_pipeline_set_freq(rx_pipeline_t* p, double freq_hz)
{
    if (!p || !p->sys || !p->radio) return -1;
    HW_LOCK();
    cariboulite_radio_set_frequency(p->radio, true, &freq_hz);
    HW_UNLOCK();
    
    // If running, gently reset demod so clicks/flicker are avoided post-retune
    if (p->running) {
        p->demod.prime_blocks_10ms = 4;  // ~40 ms is often enough on retunes
        p->demod.reset = true;
    }
    return 0;
}

// ---------- RX: audio controls ----------
int rx_pipeline_set_pcm_gain(rx_pipeline_t* p, float gain)
{
    if (!p) return -1;
    p->demod.pcm_gain = gain;
    return 0;
}
int rx_pipeline_set_deemph(rx_pipeline_t* p, float tau_s)
{
    if (!p) return -1;
    p->demod.deemph_tau = tau_s;
    return 0;
}

// ---------- TX FIFO stats shim (useful for UI) ----------
typedef struct {
    rf10_stats_t txq;
} tx_pipeline_stats_t;

static inline void tx_pipeline_get_stats(tx_pipeline_t* p, tx_pipeline_stats_t* out)
{
    if (!p || !out) return;
    rf10_fifo_get_stats(&p->txq, &out->txq);
}

// ---------- RX FIFO stats shim (useful for UI) ----------
typedef struct {
    rf10_stats_t rxq;
} rx_pipeline_stats_t;

static inline void rx_pipeline_get_stats(rx_pipeline_t* p, rx_pipeline_stats_t* out)
{
    if (!p || !out) return;
    rf10_fifo_get_stats(&p->rxq, &out->rxq);
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
        .f_dev_hz     = 2500.0f,
    };

    if (tx_pipeline_init(&tx, sys, &sys->radio_low, &par) != 0) {
        fprintf(stderr, "[tx_tone] init failed\n");
        return;
    }

    for (;;) {
        int choice = -1;
        printf("TX freq: %.0f Hz  power: %d dBm\n", par.freq_hz, par.tx_power_dbm);
        printf(" [1] Toggle NBFM TX   [99] Return\n");
        printf(" Choice: ");
        if (scanf("%d", &choice) != 1) continue;
        if (choice == 1) {
            if (!tx.running) {
                if (tx_pipeline_start(&tx) == 0) printf("TX: ON\n");
            } else {
                tx_pipeline_stop(&tx);
                printf("TX: OFF\n");
            }
        } else if (choice == 99) {
            break;
        }
    }

    tx_pipeline_destroy(&tx);
    printf("NBFM TX tone stopped.\n");
}

static void nbfm_rx(sys_st *sys)
{
    rx_pipeline_t rx = {0};
    rx_params_t par = {
        .freq_hz       = 430100000.0,
        .pcm_dev       = "plughw:3,0",
        .deemph_tau_s  = 50e-6f,
        .pcm_gain      = 8000.0f,
        .fs_rf         = 4000000.0f,
        .fs_audio      = 48000.0f,
    };

    if (rx_pipeline_init(&rx, sys, &sys->radio_low, &par) != 0) {
        fprintf(stderr, "[rx] init failed\n");
        return;
    }

    for (;;) {
        int choice = -1;
        printf("RX freq: %.0f Hz\n", par.freq_hz);
        printf(" [1] Toggle NBFM RX   [99] Return\n");
        printf(" Choice: ");
        if (scanf("%d", &choice) != 1) continue;
        if (choice == 1) {
            if (!rx.running) {
                if (rx_pipeline_start(&rx) == 0) printf("RX: ON\n");
            } else {
                rx_pipeline_stop(&rx);
                printf("RX: OFF\n");
            }
        } else if (choice == 99) {
            break;
        }
    }

    rx_pipeline_destroy(&rx);
    printf("NBFM RX stopped.\n");
}

// --- Self-test: synthesize audio -> nbfm modulation -> IQ@4M -> nbfm demodulation -> ALSA ---
static void nbfm_modem_selftest(sys_st *sys)
{
    (void)sys;

    // 1) Create RX FIFO and start the existing demod thread pointing to ALSA
    rf10_fifo_t rxq;
    rf10_fifo_init(&rxq, /*cap=*/128, /*drop_oldest_on_full=*/false);

    nbfm_demod_ctrl_t dm = {
        .active      = true,
        .fifo_in     = &rxq,
        .deemph_tau  = 50e-6f,      // or 75e-6f
        .fs_rf       = 4000000.0f,
        .fs_audio    = 48000.0f,
        .deemph_y    = 0.0f,
        .last_i      = 0,
        .last_q      = 0,
        .pcm         = NULL,
    };

    // before starting demod_th
    aud10_fifo_t afifo;
    aud10_fifo_init(&afifo, 64);

    // Use your playback opener (change device as needed: "default", "plughw:USB,0", etc.)
    unsigned rate=0, ch=0;
    if (alsa_open_playback(&dm.pcm, "plughw:3,0", &dm.pcm_rate, &dm.pcm_channels) != 0) {
        fprintf(stderr, "[selftest] alsa_open_playback failed\n");
        rf10_fifo_destroy(&rxq);
        return;
    }
    
    alsa_tune_sw(dm.pcm);

    // ping: quick ping of 2.525 kHz quindar tone to verify audio path is working
    int16_t ping[12000]; // 0.25s @ 48k
    for (int i = 0; i < 12000; i++) {
        float x = sinf(2.f * M_PI * 2525.f * (float)i / 48000.f);
        ping[i] = (int16_t)lrintf(0.6f * 32767.f * x);
    }
    write_exact_alsa_16(dm.pcm, ping, 12000, /*channels*/dm.pcm_channels);  // same as speaker-test


    audio_writer_ctrl_t aw = {
        .active   = true,
        .pcm      = dm.pcm,
        .channels = dm.pcm_channels,
        .fifo     = &afifo,
    };
    pthread_t aw_th;
    pthread_create(&aw_th, NULL, audio_writer_thread, &aw);

    dm.afifo_out = &afifo;
    dm.pcm_gain = 12000.0f;
    dm.pcm_channels = ch ? ch : 1;
    
    pthread_t demod_th;
    pthread_create(&demod_th, NULL, nbfm_demod_thread, &dm);

    // 2) Build the NBFM modulator you already use in TX
    nbfm4m_cfg_t cfg = {
        .audio_fs      = 48000.0,
        .rf_fs         = 4000000.0,
        .f_dev_hz      = 2500.0,
        .preemph_tau_s = 0.0,
        .out_scale     = 4000.0f,
        .linear_interp = 1,
    };
    nbfm4m_mod_t* fm = nbfm4m_create(&cfg);
    float*  a48k   = (float*)calloc(480,    sizeof(float));  // 10 ms audio
    iq16_t* iq4m   = (iq16_t*)calloc(40000, sizeof(iq16_t)); // 10 ms RF

    if (!fm || !a48k || !iq4m) {
        fprintf(stderr, "[selftest] alloc/mod create failed\n");
        if (fm) nbfm4m_destroy(fm);
        free(a48k); free(iq4m);
        dm.active = false;
        rf10_fifo_stop(&rxq);
        pthread_join(demod_th, NULL);
        if (dm.pcm) snd_pcm_close(dm.pcm);
        rf10_fifo_destroy(&rxq);
        return;
    }

    // 3) Run for N seconds: generate 600 Hz tone audio -> mod -> pull 40k IQ -> push to demod FIFO
    const double seconds = 15.0;
    const size_t loops   = (size_t)(seconds * 100.0); // 100 * 10ms per second
    float tone_phase = 0.0f, tone_hz = 600.0f, tone_amp = 0.6f;
    const float dphi = 2.0f * (float)M_PI * (tone_hz / 48000.0f);

    for (size_t k = 0; k < loops; k++) {
        // Fill 10 ms of 48k audio using your existing tone gen helper
        // (If you want to use the local code: replicate fill_tone_48k logic here.)
        for (size_t i = 0; i < 480; i++) {
            tone_phase += dphi;
            if (tone_phase >= 2.0f * (float)M_PI) tone_phase -= 2.0f * (float)M_PI;
            a48k[i] = tone_amp * sinf(tone_phase);
        }

        nbfm4m_push_audio(fm, a48k, 480);

        size_t pulled = 0;
        while (pulled < 40000) {
            pulled += nbfm4m_pull_iq(fm, iq4m + pulled, 40000 - pulled);
        }

        // diagnostics: peak amplitude of the 10 ms IQ frame
        int16_t peak = 0;
        for (size_t i = 0; i < 40000; i++) {
            int16_t ai = (int16_t)abs(iq4m[i].i);
            int16_t aq = (int16_t)abs(iq4m[i].q);
            if (ai > peak) peak = ai;
            if (aq > peak) peak = aq;
        }
        static int frames_gen = 0;
        if ((frames_gen++ % 50) == 0) {
            fprintf(stderr, "MOD: iq_peak=%d (out_scale=%g)\n", peak, cfg.out_scale);
        }

        rf10_frame_t frm;
        for (size_t i = 0; i < 40000; i++) {
            frm.data[i].i = iq4m[i].i;
            frm.data[i].q = iq4m[i].q;
        }

        // Block until demod thread consumes (no drops in self-test)
        if (!rf10_fifo_put(&rxq, &frm, -1)) break;
    }

    // 4) Teardown
    dm.active = false;
    aw.active = false;
    rf10_fifo_stop(&rxq);
    aud10_fifo_stop(&afifo);
    pthread_join(demod_th, NULL);
    pthread_cancel(aw_th);
    pthread_join(aw_th, NULL);

    // ping: quick ping of 2.475 kHz tone to signal audio path is closing
    // int16_t ping[12000]; // 0.25s @ 48k
    for (int i = 0; i < 12000; i++) {
        float x = sinf(2.f * M_PI * 2475.f * (float)i / 48000.f);
        ping[i] = (int16_t)lrintf(0.6f * 32767.f * x);
    }
    write_exact_alsa_16(dm.pcm, ping, 12000, /*channels*/dm.pcm_channels);  // same as speaker-test
    usleep(250 * 1000); // wait a little so the tone doesn't get cut off

    if (dm.pcm) snd_pcm_close(dm.pcm);
    aud10_fifo_destroy(&afifo);
    rf10_fifo_destroy(&rxq);
    nbfm4m_destroy(fm);
    
    free(a48k);
    free(iq4m);
    
    fprintf(stderr, "[selftest] done — you should have heard a 600 Hz tone.\n");
}

void monitor_modem_status(sys_st *sys)
{
	//mlockall(MCL_CURRENT | MCL_FUTURE);
    
    // --- NEW: pipelines ---
    tx_pipeline_t txp = {0};
    rx_pipeline_t rxp = {0};

    tx_params_t txpar = {
        .freq_hz      = 430100000.0,
        .tx_power_dbm = -3,
        .tone_mode    = true,
        .tone_hz      = 600.0f,
        .tone_amp     = 0.4f,
        .mic_dev      = NULL,
        .out_scale    = 4000.0f,
        .f_dev_hz     = 2500.0f,
    };

    rx_params_t rxpar = {
        .freq_hz       = 430100000.0,
        .pcm_dev       = "plughw:3,0",
        .deemph_tau_s  = 50e-6f,
        .pcm_gain      = 8000.0f,
        .fs_rf         = 4000000.0f,
        .fs_audio      = 48000.0f,
    };

    // init once (threads idle until start)
    tx_pipeline_init(&txp, sys, &sys->radio_low, &txpar);
    rx_pipeline_init(&rxp, sys, &sys->radio_low, &rxpar);

	nbfm_tx_active = false;
    nbfm_rx_active = false;

	initscr(); // Initialize ncurses mode
	cbreak();
	noecho();
	timeout(200);
        
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
		//printw("    TX Frequency: %.0f Hz", round(frequency/1000)*1000);
		//printw("    TX Power: %d dBm", tx_power);
        printw("    TX Frequency: %.0f Hz", round(txpar.freq_hz/1000)*1000);
        printw("    TX Power: %d dBm", txpar.tx_power_dbm);
        move(1, screen_max_x - 12);
		printw("%12.5f",elapsed_time);
		move(2,0);
		printw("Modem Status Registers:");
		move(3,0);
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
        printw("    RF09-IQRS  :0x%02X  RF24-IRQS  :0x%02X\n", rf09_irqs,  rf24_irqs);	
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
            if (dt > 0.0) {
                tx_rate_puts = (double)(stx.puts - tx_last_s.puts) / dt;
                tx_rate_gets = (double)(stx.gets - tx_last_s.gets) / dt;
            }
        }
        tx_last_ts = now; tx_last_s = stx;
        
        printw("Linux TX FIFO:\n");
        printw("    depth: %zu/%zu (%.0f%%), min:%zu max:%zu\n",
            stx.count, stx.cap, tx_fill_pct, stx.min_depth, stx.max_depth);
        printw("    puts:%zu gets:%zu drops:%zu tO_put:%zu tO_get:%zu\n",
            stx.puts, stx.gets, stx.drops, stx.timeouts_put, stx.timeouts_get);
        printw("    rate: puts %.1f/s, gets %.1f/s  (expect ~100 fps @ 10ms)\n",
            tx_rate_puts, tx_rate_gets);

        // If you want to highlight trouble:
        if (stx.min_depth == 0)          printw("    NOTE: Under-runs observed (producer late)\n");
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
            if (dt > 0.0) {
                rx_rate_puts = (double)(srx.puts - rx_last_s.puts) / dt;
                rx_rate_gets = (double)(srx.gets - rx_last_s.gets) / dt;
            }
        }
        rx_last_ts = now; rx_last_s = srx;
        
        printw("Linux RX FIFO:\n");
        printw("    depth: %zu/%zu (%.0f%%), min:%zu max:%zu\n",
            srx.count, srx.cap, rx_fill_pct, srx.min_depth, srx.max_depth);
        printw("    puts:%zu gets:%zu drops:%zu tO_put:%zu tO_get:%zu\n",
            srx.puts, srx.gets, srx.drops, srx.timeouts_put, srx.timeouts_get);
        printw("    rate: puts %.1f/s, gets %.1f/s  (expect ~100 fps @ 10ms)\n",
            rx_rate_puts, rx_rate_gets);
        
        // Same “trouble” hints, adapted to RX roles
        if (srx.min_depth == 0)          printw("    NOTE: Under-runs observed (reader late)\n");
        if (srx.drops > 0)               printw("    NOTE: Overwrites occurred (demod slower than reader)\n");
        if (srx.timeouts_put > 0)        printw("    NOTE: Reader timed out waiting to enqueue\n");
        if (srx.timeouts_get > 0)        printw("    NOTE: Demod timed out waiting for frames\n");

		char key = 0;
		key = getch();
		
		if(key == 'q') // Press 'q' to exit
		{
			break;
		}

		if (key == 'x' || key == 'X') {      // reset FIFO diagnostics
			rf10_fifo_reset_stats(&txp.txq);
            rf10_fifo_reset_stats(&rxp.rxq);
		}

        // --- T: toggle TX ---
        if (key == 't') {
            if (!tx_pipeline_running(&txp)) {
                // ensure RX is off (pipeline start already does this defensively)
                rx_pipeline_stop(&rxp);
                tx_pipeline_start(&txp);
            } else {
                tx_pipeline_stop(&txp);
            }
        }

        // --- R: toggle RX ---
        if (key == 'r') {
            if (!rx_pipeline_running(&rxp)) {
                tx_pipeline_stop(&txp);
                rx_pipeline_start(&rxp);
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
