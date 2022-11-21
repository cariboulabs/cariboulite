#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Main"
#include "zf_log/zf_log.h"

#include "cariboulite_setup.h"
#include "cariboulite_events.h"
#include "cariboulite.h"
#include "cariboulite_eeprom/cariboulite_eeprom.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

struct sigaction act;
int program_running = 1;
int signal_shown = 0;
CARIBOULITE_CONFIG_DEFAULT(cariboulite_sys);

//=================================================
int stop_program ()
{
    if (program_running) ZF_LOGD("program termination requested");
    program_running = 0;
    return 0;
}

//=================================================
void sighandler( struct cariboulite_st_t *sys,
                 void* context,
                 int signal_number,
                 siginfo_t *si)
{
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
        case SIGFPE: stop_program (); break;
        default: return; break;
    }
}

cariboulite_eeprom_st ee = { .i2c_address = 0x50, .eeprom_type = eeprom_type_24c32,};

//=================================================
int main(int argc, char *argv[])
{
    //strcpy(cariboulite_sys.firmware_path_operational, "top.bin");
    //strcpy(cariboulite_sys.firmware_path_testing, "top.bin");

    // init the program
    if (cariboulite_init_driver(&cariboulite_sys, NULL)!=0)
    {
        ZF_LOGE("driver init failed, terminating...");
        cariboulite_eeprom_init(&ee);
        return -1;
    }

    // setup the signal handler
    cariboulite_setup_signal_handler (&cariboulite_sys, sighandler, cariboulite_signal_handler_op_last, &cariboulite_sys);

    // dummy loop
    double freq = 1089e6;
    double step = 0.1e6;
    rffc507x_calibrate(&cariboulite_sys.mixer);
    sleep(1);
    while (program_running)
    {
        //double set_freq = freq;
        /*cariboulite_setup_frequency(&cariboulite_sys, 
                                    cariboulite_channel_6g, 
                                    cariboulite_channel_dir_tx,
                                    &set_freq);
        
        */
        /*caribou_fpga_set_io_ctrl_mode (&cariboulite_sys.fpga, 0, caribou_fpga_io_ctrl_rfm_tx_lowpass);
        
        rffc507x_set_frequency(&cariboulite_sys.mixer, set_freq);
        
        rffc507x_device_status_st stat = {0};
        rffc507x_readback_status(&cariboulite_sys.mixer, NULL, &stat);
        rffc507x_print_stat(&stat);
        */
        //sleep(1);
        freq += step;
        //if (freq > 45e6) freq = 30e6;
        //io_utils_usleep(200000);
        getchar();
    }

    // close the driver and release resources
    cariboulite_release_driver(&cariboulite_sys);
    return 0;
}