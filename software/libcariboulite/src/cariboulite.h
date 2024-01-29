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
 * @brief System Type (version)
 */
typedef enum
{
    cariboulite_unknown = 0,
    cariboulite_full = 1,
    cariboulite_ism = 2,
} cariboulite_version_en;

/**
 * @brief custom signal handler
 */
typedef void (*cariboulite_signal_handler)( void* context,      // custom context - can be a higher level app class
                                            int signal_number,  // the signal number
                                            siginfo_t *si);

/**
 * @brief check if board connected (without initing it)
 *
 * This function attempts to read the HAT information and checks if a
 * CaribouLite is registerred in the OS. If so gets its details.
 * this fucntion is good to check whether the board is mounted without the need
 * to initialize it. Preferably this is a simple check before we start working
 * - kind of a sanity check.
 *
 * @param hw_ver the type of HW (full / ism) as ENUM (nullable is not needed)
 * @param name the type of HW in textual manner - preallocate the string (at least 32 bytes)
 *             (nullable is not needed)
 * @uuid uuid if this is needed, the user needs to provide a preallocated string 64 byte typical
 *            (nullable if not needed)
 * @return true - when board was detected
 *         false - board was not detected and then other parameters will not be valid
 */
bool cariboulite_detect_connected_board(cariboulite_version_en *hw_ver, char* name, char *guid);

/**
 * @brief initialize the system
 *
 * This function performs fully entry initialization of the system and
 * a short self-test sequence to communication and check all the components
 * respond.
 *
 * @param force_fpga_prog force the FPGA to program (even if it is already programmed
 *                        this will have a penalty of around 3 seconds on init. If false
 *                        and the FPGA is already programmed (typically when this is not
 *                        the first time a program has run since last boot session), the program
 *                        will not rerun the FPGA programming process.
 * @param log_lvl the logging level according to 'cariboulite_log_level_en'
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
 * @return board serial number (32 bit)
 */
unsigned int cariboulite_get_sn();

/**
 * @brief Getting the used radio handle
 *
 * After initializing the drivers, a radio device is created and stored in
 * the device driver main struct. To manipulate the radio features, this radio
 * handle (pointer) needs to be obtained by the user. This handle is normally
 * passed to the radio manipulation functions in "cariboulite_radio.h"
 *
 * @param type the radio channel (6G/2.4G or ISM)
 * @return the handle of the channel, or NULL if invalid channel id
 */
cariboulite_radio_state_st* cariboulite_get_radio(cariboulite_channel_en ch);

/**
 * @brief Getting CaribouLite version
 *
 * Returns the version of the hardware - ISM / 6G / Unknown
 * ISM has two channels - 900MHz and 2.4 GHz
 * 6G has two channels - 900MHz and 6000MHz
 *
 * @return according to cariboulite_version_en
 */
cariboulite_version_en cariboulite_get_version(void);

/**
 * @brief Getting frequency availability
 *
 * Given a certain frequency (in Hz) checking if that frequency is available in the
 * connected hardware
 *
 * @return true / false
 */
bool cariboulite_frequency_available(cariboulite_channel_en ch, float freq_hz);

/**
 * @brief Getting the number frequency ranges
 *
 * Each channel has its frequency capabilities. in the ISM channel there are 2
 * ranges, while in the HiF there is a single range. This function returns the number
 * of available ranges per channel. To get the actual limits use 'cariboulite_get_frequency_limits'
 *
 * @return number of ranges or -1 if the channel doesn't exist
 */
int cariboulite_get_num_frequency_ranges(cariboulite_channel_en ch);

/**
 * @brief Getting frequency ranges limits
 *
 * freq_low and freq_hi need to be pre-allocated according to 'cariboulite_get_num_frequency_ranges'
 * then all the minimum values will be in the freq_low list and the corresponding max values will be in
 * freq_hi. All frequencies in Hz
 * num_ranges returns the number of written ranges
 *
 * @return 0 for success or -1 if channel is wrong
 */
int cariboulite_get_frequency_limits(cariboulite_channel_en ch, float *freq_low, float *freq_hi, int* num_ranges);

/**
 * @brief Get channel name
 *
 * Returns the string name of a channel
 *
 * @param ch the chosen channel
 * @param name a pre-allocated char array
 * @param max_len the size of the pre-allocated char array
 * @return 0 (success) or -1 (failed - when channel is incorrect)
 */
 int cariboulite_get_channel_name(cariboulite_channel_en ch, char* name, size_t max_len);

/**
 * @brief Flush driver FIFO
 *
 * Removing all items in the pipeline (drivers kfifo)
 *
 * @return 0 (success) or -1 (failed - when channel is incorrect)
 */
 int cariboulite_flush_pipeline(void);

/**
 * @brief IO Control
 *
 * FPGA level IO control API
 *
 * @return 0 (success) or -1 (failed)
 */
int cariboulite_set_leds_state (int led0, int led1);
int cariboulite_get_leds_state (int *led0, int *led1);
int cariboulite_get_button_state (int *btn);
int cariboulite_set_pmod_val (uint8_t val);
int cariboulite_get_pmod_val (uint8_t *val);

#ifdef __cplusplus
}
#endif


#endif // __CARIBOULITE_H__
