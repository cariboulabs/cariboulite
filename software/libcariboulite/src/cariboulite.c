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
int init_program()
{
    ZF_LOGI("program initializing");
    if (cariboulite_setup_io (sighandler) != 0)
    {
        return -1;
    }

    if (cariboulite_configure_fpga ("top.bin") != 0)
    {
        cariboulite_release_io ();
        return -2;
    }

    if (cariboulite_init_submodules () != 0)
    {
        cariboulite_release_io ();
        return -3;
    }

    if (cariboulite_self_test() != 0)
    {

    }

    return 0;
}

//=================================================
int close_program()
{
    ZF_LOGI("program closing");

    cariboulite_release_submodules();
    cariboulite_release_io ();
}

//=================================================
int main(int argc, char *argv[])
{
    // init the program
    if (init_program()!=0)
    {
        ZF_LOGE("program init failed, terminating...");
        return -1;
    }

    // dummy loop
    while (program_running)
    {
        sleep(1);
    }

    // close the program
    close_program();
    return 0;
}