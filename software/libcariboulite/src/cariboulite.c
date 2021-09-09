#define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "CARIBOULITE Main"
#include "zf_log/zf_log.h"

#include "cariboulite_setup.h"
#include "cariboulite_events.h"
#include "cariboulite.h"

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

struct sigaction act;
int program_running = 1;
CARIBOULITE_CONFIG_DEFAULT(cariboulite_sys);

//=================================================
int stop_program ()
{
    ZF_LOGD("program termination requested");
    program_running = 0;
}

//=================================================
int sighandler(int signum)
{
    ZF_LOGI("Received signal %d", signum);

    switch (signum)
    {
        case SIGINT:
        case SIGTERM:
        case SIGABRT:
        case SIGILL:
        case SIGSEGV:
        case SIGFPE: stop_program (); break;
        default: return -1; break;
    }
    return 0;
}

//=================================================
int main(int argc, char *argv[])
{
    strcpy(cariboulite_sys.firmware_path_operational, "top.bin");
    strcpy(cariboulite_sys.firmware_path_testing, "top.bin");

    // init the program
    if (cariboulite_init_driver(&cariboulite_sys, sighandler)!=0)
    {
        ZF_LOGE("driver init failed, terminating...");
        return -1;
    }

    // dummy loop
    while (program_running)
    {
        sleep(1);
    }

    // close the driver and release resources
    cariboulite_release_driver(&cariboulite_sys);
    return 0;
}