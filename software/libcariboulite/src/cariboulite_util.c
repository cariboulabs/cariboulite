#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Main"
#include "zf_log/zf_log.h"

#include <sys/types.h>
#include <unistd.h>

#include "cariboulite_setup.h"
#include "cariboulite_events.h"
#include "cariboulite.h"
#include "hat/hat.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

//=======================================================================
// INTERNAL VARIABLES AND DEFINITIONS

static struct sigaction act;
static int signal_shown = 0;
CARIBOULITE_CONFIG_STATIC_DEFAULT(cariboulite_sys);

// Program state structure
typedef struct
{
    // Arguments
    char *filename;
    int rx_channel;
    double frequency;
    float gain;
    float ppm_error;
    size_t samples_to_read;
    int force_fpga_prog;
    int write_metadata;
    
    // State
    int sample_infinite;
    int program_running;
    int sys_type;
    size_t native_read_len;
    cariboulite_sample_complex_int16* buffer;
    cariboulite_sample_meta* metadata;
    cariboulite_radio_state_st *radio;
    FILE *file;
} prog_state_st;

static prog_state_st state = {0};

//=================================================
static int stop_program (void)
{
    if (state.program_running) ZF_LOGD("program termination requested");
    state.program_running = 0;
    return 0;
}

//=================================================
static void sighandler(void* context,
                       int signal_number,
                       siginfo_t *si)
{
    struct sys_st_t *s = (struct sys_st_t *)context;
    
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
        case SIGFPE: stop_program(); break;
        default: return; break;
    }
}

//=================================================
static void init_program_state(void)
{
    state.filename = NULL;
    state.rx_channel = 0;       // low freq channel
    state.frequency = 915e6;
    state.gain = 0;
    state.ppm_error = 0;
    state.samples_to_read = 1024*1024/8;
    state.force_fpga_prog = 0;
    state.write_metadata = 0;
    
    // state
    state.sample_infinite = 0;
    state.program_running = 1;
    state.sys_type = system_type_cariboulite_ism;
    state.native_read_len = 1024 * 1024 / 8;
    state.buffer = NULL;
    state.metadata = NULL;
    state.radio = NULL;
    state.file = NULL;
}

//=======================================================================
static void usage(void)
{
	fprintf(stderr,
		"CaribouLite I/Q recorder\n\n"
		"Usage:\t-c the RX channel to use (0: low, 1: high)\n"
        "\t-f frequency [Hz]\n"
		"\t[-g gain (default: -1 for agc)]\n"
		"\t[-p ppm_error (default: 0)]\n"
		"\t[-n number of samples to read (default: 0, infinite)]\n"
		"\t[-S force sync output (default: async)]\n"
        "\t[-F force fpga reprogramming (default: '0')]\n"
        "\t[-M write metadata (default: '0')]\n"
		"\tfilename ('-' dumps samples to stdout)\n\n"
        "Example:\n"
        "\t1. Sample S1G channel at 905MHz into filename capture.bin\n"
        "\t\tcariboulite_util -c 0 -f 905000000 capture.bin\n"
        "\t2. Sample S1G channel at 905MHz into filename capture.bin, only 30000 samples\n"
        "\t\tcariboulite_util -c 0 -f 905000000 -n 30000 capture.bin\n\n");
	exit(1);
}

//=======================================================================
static int check_inputs(void)
{
    state.sys_type = cariboulite_sys.board_info.numeric_product_id;
    
    if (state.rx_channel != 0 && state.rx_channel != 1) 
    {
        ZF_LOGE("Radio selection incompatible [%d] (should be either '0' or '1')", state.rx_channel);
        return -1;
    }
    
    if (state.rx_channel == 0 && 
        (state.frequency < CARIBOULITE_S1G_MIN1 || state.frequency > CARIBOULITE_S1G_MAX2 ||
         (state.frequency > CARIBOULITE_S1G_MAX1 && state.frequency < CARIBOULITE_S1G_MIN2)) )
    {
        ZF_LOGE("S1G radio frequency (%.2f) is out of the [%.0f .. %.0f, %.0f .. %.0f] MHz range", state.frequency,
            CARIBOULITE_S1G_MIN1/1e6, CARIBOULITE_S1G_MAX1/1e6, CARIBOULITE_S1G_MIN2/1e6, CARIBOULITE_S1G_MAX2/1e6);
        return -1;
    }
    
    if (state.rx_channel == 1 && state.sys_type == system_type_cariboulite_full &&
        (state.frequency < CARIBOULITE_6G_MIN && state.frequency > CARIBOULITE_6G_MAX))
    {
        ZF_LOGE("HiF (full) radio frequency (%.2f) is out of the [%.0f .. %.0f] MHz range", state.frequency,
            CARIBOULITE_6G_MIN/1e6, CARIBOULITE_6G_MAX/1e6);
        return -1;
    }
    
    if (state.rx_channel == 1 && state.sys_type == system_type_cariboulite_ism &&
        (state.frequency < CARIBOULITE_2G4_MIN && state.frequency > CARIBOULITE_2G4_MAX))
    {
        ZF_LOGE("HiF (ISM) radio frequency (%.2f) is out of the [%.0f .. %.0f] MHz range", state.frequency,
            CARIBOULITE_2G4_MIN/1e6, CARIBOULITE_2G4_MAX/1e6);
        return -1;
    }
    
    if ((state.gain < 0 || state.gain > 23.0*3.0) && state.gain != -1)
    {
        ZF_LOGE("Rx channel gain %.0f is incompatible (legal range: [%.0f .. %.0f] dB", state.gain,
            0.0, 23.0*3.0);
        return -1;
    }
    
    return 0;
}

//=================================================
int analyze_arguments(int argc, char *argv[])
{
    int opt;
    while ((opt = getopt(argc, argv, "c:f:g:n:S:F")) != -1) {
		switch (opt) {
		case 'c':
			state.rx_channel = (int)atoi(optarg);
            printf("DBG: RX Channel = %d\n", state.rx_channel);
			break;
		case 'f':
			state.frequency = atof(optarg);
            printf("DBG: Frequency = %.1f Hz\n", state.frequency);
			break;
		case 'g':
			state.gain = (int)(atof(optarg));
            printf("DBG: Rx Gain = %.1f dB\n", state.gain);
			break;
		case 'p':
			state.ppm_error = atof(optarg);
            printf("DBG: PPM Error = %.2f\n", state.ppm_error);
			break;
		case 'n':
			state.samples_to_read = atoi(optarg);
            state.sample_infinite = state.samples_to_read > 0 ? 0 : 1;
            
            if (state.sample_infinite) printf("DBG: Infinite Read\n");
            else printf("DBG: Number of samples: %lu\n", state.samples_to_read);
			break;
        case 'F':
			state.force_fpga_prog = 1;
            printf("DBG: Force FPGA programming = %d\n", state.force_fpga_prog);
			break;
        case 'M':
			state.write_metadata = 1;
            printf("DBG: Write metadata = %d\n", state.write_metadata);
			break;
		default:
			usage();
            return -1;
			break;
		}
	}
    
    if (argc <= optind) 
    {
        usage();
        return -1;
    }
    else state.filename = argv[optind];
    return 0;
}

//=================================================
void release_system(void)
{
    cariboulite_radio_activate_channel(state.radio, cariboulite_channel_dir_rx, false);
    if (state.buffer) free (state.buffer);
    if (state.metadata) free (state.metadata);
    if (strcmp(state.filename, "-") != 0) 
    {
        if (state.file) fclose(state.file);
    }
    cariboulite_close();
}

//=================================================
int main(int argc, char *argv[])
{   
    // pre-init the program state
    //-------------------------------------
    init_program_state();
    
    // Analyze program opts
    //-------------------------------------
    if (analyze_arguments(argc, argv) != 0)
    {
        return 0;
    }

    // Init the program
    //-------------------------------------
    if (cariboulite_init(state.force_fpga_prog, cariboulite_log_level_none) != 0)
    {
        ZF_LOGE("driver init failed, terminating...");
        return -1;
    }

    // setup the signal handler
    cariboulite_register_signal_handler ( sighandler, &cariboulite_sys);

    // check the input arguments (done after init to identify system type)
    if (check_inputs() != 0)
    {
        cariboulite_close();
        return -1;
    }
    
    // get the correct radio from the possible two
    state.radio = cariboulite_get_radio((cariboulite_channel_en)(state.rx_channel));
    
    // Allocate rx buffer and metadata
    state.native_read_len = cariboulite_radio_get_native_mtu_size_samples(state.radio);
    state.buffer = malloc(sizeof(cariboulite_sample_complex_int16)*state.native_read_len);
    if (state.buffer == NULL)
    {
        ZF_LOGE("RX Buffer allocation failed");
        release_system();
        return -1;
    }

    state.metadata = malloc(sizeof(cariboulite_sample_meta)*state.native_read_len);
    if (state.metadata == NULL)
    {
        ZF_LOGE("Metadata allocation failed");
        release_system();
        return -1;
    }
    
    // Align the length (only if it is >0)
    if (!state.sample_infinite)
    {
        state.samples_to_read = ((state.samples_to_read % state.native_read_len) == 0) ? 
                                 (state.samples_to_read) : 
                                 (state.samples_to_read / state.native_read_len + 1) * state.native_read_len;
    }
    
    // Init the radio
    //-------------------------------------    
    // Set radio parameters
    cariboulite_radio_set_frequency(state.radio, true, &state.frequency);
    cariboulite_radio_set_rx_gain_control(state.radio, state.gain == -1.0, state.gain);
    cariboulite_radio_sync_information(state.radio);
    cariboulite_radio_activate_channel(state.radio, cariboulite_channel_dir_rx, true);
    
    // Open the file for writing
    if(strcmp(state.filename, "-") == 0) 
    {
		state.file = stdout;
	}
    else
    {
		state.file = fopen(state.filename, "wb");
		if (!state.file) 
        {
            ZF_LOGE("Failed to open %s", state.filename);
            release_system();
            return -1;
        }
	}
    
    usleep(100000);
	while (state.program_running)
	{
		int ret = cariboulite_radio_read_samples(state.radio, state.buffer, state.metadata, state.native_read_len);
        if (ret < 0)
        {
            ZF_LOGE("Samples read operation failed. Quiting...");
            continue;
        }
        
        // TODO: how should the metadata be expressed in the file?
        int wret = fwrite(state.buffer, 1, ret*4, state.file);
        if (wret != (ret*4))
        {
            ZF_LOGE("Writing into file failed, exiting!\n");
            break;
        }
        
        if (!state.sample_infinite) 
        {
            state.samples_to_read -= ret;
            if (state.samples_to_read <= 0)
                break;
        }
    }

    // close the driver and release resources
    cariboulite_close();
    return 0;
}
