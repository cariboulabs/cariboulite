#include "cariboulite.h"
#include "cariboulite_setup.h"
#include "cariboulite_radio.h"

// ----------------------
// INTERNAL DATA TYPES
// ----------------------
typedef struct
{
    cariboulite_lib_version_st lib_version;
    cariboulite_signal_handler  sighandler;
    void* sig_context;
    bool initialized;
    cariboulite_log_level_en log_level;
    int signal_shown;
    
} cariboulite_api_context_st;

// ----------------------
// STATIC VARIABLES
// ----------------------
CARIBOULITE_CONFIG_STATIC_DEFAULT(sys);
static cariboulite_api_context_st ctx = {
    .lib_version = {0},
    .sighandler = NULL,
    .sig_context = NULL,
    .initialized = false,
    .log_level = cariboulite_log_level_verbose,
    .signal_shown = 0,
};

//=============================================================================
static void internal_sighandler( struct sys_st_t *sys,
                                 void* context,
                                 int signal_number,
                                 siginfo_t *si)
{
    if (ctx.signal_shown != signal_number)
    {
        fprintf(stderr, "Received signal %d", signal_number);
        ctx.signal_shown = signal_number;
    }

    if (ctx.sighandler) ctx.sighandler(ctx.sig_context, signal_number, si);
}

//=============================================================================
int cariboulite_init(bool force_fpga_prog, cariboulite_log_level_en log_lvl)
{
    sys.force_fpga_reprogramming = force_fpga_prog;
    
    if (cariboulite_init_driver(&sys, NULL)!=0)
    {
        fprintf(stderr, "CaribouLite driver init failed, terminating...");
        return -1;
    }
    
    cariboulite_setup_signal_handler (&sys, internal_sighandler, signal_handler_op_last, &sys);
    
    ctx.initialized = true;
    return 0;
}

//=============================================================================
void cariboulite_close(void)
{
    cariboulite_release_driver(&sys);
}

//=============================================================================
bool cariboulite_is_initialized(void)
{
    return ctx.initialized;
}

//=============================================================================
void cariboulite_register_signal_handler ( cariboulite_signal_handler handler,
                                           void *context)
{
    ctx.sighandler = handler;
    ctx.sig_context = context;
}

//=============================================================================
void cariboulite_get_lib_version(cariboulite_lib_version_st* v)
{
    cariboulite_lib_version(v);
}

//=============================================================================
int cariboulite_get_sn(uint32_t* serial_number, int *count)
{
    return cariboulite_get_serial_number(&sys, serial_number, count);
}

//=============================================================================
cariboulite_radio_state_st* cariboulite_get_radio(cariboulite_channel_en type)
{
    return cariboulite_get_radio_handle(&sys, type);
}