/**
 * @file cariboulite.h
 * @author David Michaeli
 * @date September 2023
 * @brief Main Init/Close API
 *
 * A high level management API for CaribouLite
 */
 
#ifndef __CARIBOULITE_H__
#define __CARIBOULITE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <signal.h>
#include "cariboulite_radio.h"

/**
 * @brief library version
 */
typedef struct
{
    int major_version;
    int minor_version;
    int revision;
} cariboulite_lib_version_st;

/**
 * @brief Log Level
 */
typedef enum
{
    cariboulite_log_level_verbose,   /**< Full */
    cariboulite_log_level_info,      /**< partial - no debug */
    cariboulite_log_level_none,      /**< none - errors only*/
} cariboulite_log_level_en;

/**
 * @brief custom signal handler
 */
typedef void (*cariboulite_signal_handler)( void* context,      // custom context - can be a higher level app class
                                            int signal_number,  // the signal number
                                            siginfo_t *si);

/**
 * @brief initialize the system
 *
 * This function performs fully entry initialization of the system and
 * a short self-test sequence to communication and check all the components
 * respond.
 *
 * @param sys a pre-allocated device handle structure
 * @param info the initialization performs internally the board detection sequence.
 *             which is stored into the "sys" struct. If the user wants to receive the
 *             information explicitely, he can pass here a pre-allocated info structure and
 *             it will be filled with the board information. If this is not needed
 *             user can pass "NULL"
 * @return success / fail codes according to "cariboulite_errors_en"
 */
int cariboulite_init(bool force_fpga_prog, cariboulite_log_level_en log_lvl);

/**
 * @brief Release resources
 *
 * Releasing resources taken during init and program execution. Should be
 * called at the end of the session.
 *
 * @param sys a pre-allocated device handle structure
 */
void cariboulite_close(void);

/**
 * @brief Returns init status
 *
 * checks whether the driver is initialized
 *
 * @return boolean result (true = initialized)
 */
bool cariboulite_is_initialized(void);


/**
 * @brief Register an explicit linux signal handler in the application level
 *
 * If a linux signal occures it is caught by the system. The user may want to be
 * notified on the signal in the app level. This is done by registering a handler
 * function with this fucntion.
 *
 * @param sys a pre-allocated device handle structure
 * @param handler a handler function with the following prototype:
 *           void (*signal_handler)( void* context,      // custom context - can be a higher level app class
 *								     int signal_number,  // the signal number
 *								     siginfo_t *si);
 * @param context this void* pointer may contain an app specific handle to be passed to the explicit signal handler.
 * @return always 0 (success)
 */
void cariboulite_register_signal_handler ( cariboulite_signal_handler handler,
                                           void *context);
                                        


/**
 * @brief Get lib version
 *
 * @param v a struct containing the version information
 */
void cariboulite_get_lib_version(cariboulite_lib_version_st* v);

/**
 * @brief Get board serial number
 *
 * Note - this 32bit serial number is a digest value of the UUID 128 bit that
 * is automatically generated for each board. It is basically the result of
 * "xor" operations on the UUID's parts.
 *
 * @param serial_number a 32bit storage to store the serial number (can be NULL - if not needed)
 * @param count the number of 32bit variables returned - always returns "1".
 * @return always 0
 */
int cariboulite_get_sn(uint32_t* serial_number, int *count);

/**
 * @brief Getting the used radio handle
 *
 * After initializing the drivers, a radio device is created and stored in
 * the device driver main struct. To manipulate the radio features, this radio
 * handle (pointer) needs to be obtained by the user. This handle is normally
 * passed to the radio manipulation functions in "cariboulite_radio.h"
 *
 * @param type the radio channel (6G/2.4G or ISM)
 * @return 0 (sucess), -1 (fail)
 */
cariboulite_radio_state_st* cariboulite_get_radio(cariboulite_channel_en type);


#ifdef __cplusplus
}
#endif


#endif // __CARIBOULITE_H__
