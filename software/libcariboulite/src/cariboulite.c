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
bool cariboulite_detect_connected_board(cariboulite_version_en *hw_ver, char* name, char *uuid)
{
    hat_board_info_st hat;
    if (hat_detect_board(&hat) == 0)
	{
		return false;
	}
    
    switch (hat.numeric_product_id)
    {
        case system_type_cariboulite_full:
            if (hw_ver) *hw_ver = cariboulite_full;
            if (name) sprintf(name, "CaribouLite 6G");
            break;
            
        case system_type_cariboulite_ism:
            if (hw_ver) *hw_ver = cariboulite_ism;
            if (name) sprintf(name, "CaribouLite ISM");
            break;
            
        case system_type_unknown:
            if (hw_ver) *hw_ver = cariboulite_unknown;
            if (name) sprintf(name, "CaribouLite Unknown");
        default: break;
    }
    
    if (uuid) sprintf(uuid, "%s", hat.product_uuid);
    
    return true;
}

//=============================================================================
int cariboulite_init(bool force_fpga_prog, cariboulite_log_level_en log_lvl)
{
    sys.force_fpga_reprogramming = force_fpga_prog;
    
    cariboulite_set_log_level(log_lvl);
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
    if (!ctx.initialized) return;
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
unsigned int cariboulite_get_sn()
{
    uint32_t sn = 0;
    int count = 0;
    cariboulite_get_serial_number(&sys, &sn, &count);
    return sn;
}

//=============================================================================
cariboulite_radio_state_st* cariboulite_get_radio(cariboulite_channel_en ch)
{
    return cariboulite_get_radio_handle(&sys, ch);
}

//=============================================================================
cariboulite_version_en cariboulite_get_version()
{
    system_type_en type = cariboulite_get_system_type(&sys);
    switch(type)
    {
        case cariboulite_full: return cariboulite_full; break;
        case cariboulite_ism: return cariboulite_ism; break;
        case system_type_unknown:
        default: return cariboulite_unknown; break;
    }
    return cariboulite_unknown;
}

//=============================================================================
bool cariboulite_frequency_available(cariboulite_channel_en ch, float freq_hz)
{
    if (ch == cariboulite_channel_s1g)
    {
        return (freq_hz >= CARIBOULITE_S1G_MIN1 && freq_hz <= CARIBOULITE_S1G_MAX1 ||
                freq_hz >= CARIBOULITE_S1G_MIN2 && freq_hz <= CARIBOULITE_S1G_MAX2);
    }
    else if (ch == cariboulite_channel_hif)
    {
        cariboulite_version_en ver = cariboulite_get_version();
        if (ver == cariboulite_full)
        {
            return (freq_hz >= CARIBOULITE_6G_MIN && freq_hz <= CARIBOULITE_6G_MAX);
        }
        else if (ver == cariboulite_ism)
        {
            return (freq_hz >= CARIBOULITE_2G4_MIN && freq_hz <= CARIBOULITE_2G4_MAX);
        }
    }
    return false;
}

//=============================================================================
int cariboulite_get_num_frequency_ranges(cariboulite_channel_en ch)
{
    if (ch == cariboulite_channel_s1g) return 2;
    else if (ch == cariboulite_channel_hif) return 1;
    return -1;
}

//=============================================================================
int cariboulite_get_frequency_limits(cariboulite_channel_en ch, float *freq_low, float *freq_hi, int* num_ranges)
{
    if (ch == cariboulite_channel_s1g)
    {
        freq_low[0] = CARIBOULITE_S1G_MIN1;
        freq_hi[0] = CARIBOULITE_S1G_MAX1;
        freq_low[1] = CARIBOULITE_S1G_MIN2;
        freq_hi[1] = CARIBOULITE_S1G_MAX2;
        if (num_ranges) *num_ranges = 2;
    }
    else if (ch == cariboulite_channel_hif)
    {
        cariboulite_version_en ver = cariboulite_get_version();
        if (ver == cariboulite_full)
        {
            freq_low[0] = CARIBOULITE_6G_MIN;
            freq_hi[0] = CARIBOULITE_6G_MAX;
        }
        else if (ver == cariboulite_ism)
        {
            freq_low[0] = CARIBOULITE_2G4_MIN;
            freq_hi[0] = CARIBOULITE_2G4_MAX;
        }
        else
        {
            return -1;
        }
    }
    else
    {
        return -1;
    }
    
    return 0;
}

//=============================================================================
 int cariboulite_get_channel_name(cariboulite_channel_en ch, char* name, size_t max_len)
{
    if (ch == cariboulite_channel_s1g)
    {
        snprintf(name, max_len-1, "CaribouLite S1G");
        return 0;
    }
    else if (ch == cariboulite_channel_hif)
    {
        cariboulite_version_en ver = cariboulite_get_version();
        if (ver == cariboulite_full)
        {
            snprintf(name, max_len-1, "CaribouLite 6GHz");
            return 0;
        }
        else if (ver == cariboulite_ism)
        {
            snprintf(name, max_len-1, "CaribouLite 2.4GHz");
            return 0;
        }
    }
    return -1;
}