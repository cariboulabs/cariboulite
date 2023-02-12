#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Test"
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

struct sigaction act;
int program_running = 1;
int signal_shown = 0;
CARIBOULITE_CONFIG_DEFAULT(cariboulite_sys);

int app_menu(sys_st* sys);

//=================================================
int stop_program ()
{
    if (program_running) ZF_LOGD("program termination requested");
    program_running = 0;
    return 0;
}

//=================================================
void sighandler( struct sys_st_t *sys,
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
        case SIGFPE: stop_program(); break;
        default: return; break;
    }
}

//=================================================
int main(int argc, char *argv[])
{
    // init the program
	cariboulite_sys.force_fpga_reprogramming = 0;
    if (cariboulite_init_driver(&cariboulite_sys, NULL)!=0)
    {
        ZF_LOGE("driver init failed, terminating...");
        return -1;
    }

    // setup the signal handler
    cariboulite_setup_signal_handler (&cariboulite_sys, sighandler, signal_handler_op_last, &cariboulite_sys);

    sleep(1);
	while (program_running)
	{
		int ret = app_menu(&cariboulite_sys);

		if (ret < 0)
		{
			ZF_LOGE("Error occurred, terminating...");
			break;
		}
		else if (ret == 0)
		{
			ZF_LOGI("Quit command => terminating...");
			break;
		}
    }

    // close the driver and release resources
    cariboulite_release_driver(&cariboulite_sys);
    return 0;
}
