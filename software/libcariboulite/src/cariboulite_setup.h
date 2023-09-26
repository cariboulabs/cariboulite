/**
 * @file cariboulite_setup.h
 * @author David Michaeli
 * @date March 2023
 * @brief System level setup functionality
 *
 * High level fucntionality - initialization, de-initialization,
 * drivers loading, FPGA programming etc.
 */

#ifndef __CARIBOULITE_SETUP_H__
#define __CARIBOULITE_SETUP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "cariboulite.h"
#include "cariboulite_internal.h"
#include "cariboulite_radio.h"
#include "caribou_fpga/caribou_fpga.h"
#include "at86rf215/at86rf215.h"
#include "rffc507x/rffc507x.h"
#include "caribou_smi/caribou_smi.h"
#include "io_utils/io_utils.h"
#include "io_utils/io_utils_spi.h"
#include "io_utils/io_utils_sys_info.h"
#include "ustimer/ustimer.h"
#include "cariboulite_config_default.h"

#define CARIBOULITE_MAJOR_VERSION 1
#define CARIBOULITE_MINOR_VERSION 2
#define CARIBOULITE_REVISION 0

typedef struct
{
    int fpga_fail;
    int modem_fail;
    int mixer_fail;
    int smi_fail;
} cariboulite_self_test_result_st;

/**
 * @brief FPGA programming source definition
 *
 * While programming the FPGA, using "cariboulite_configure_fpga", it
 * expects to receive the FGPA binary file to program either as a
 * binary file full path or by a memory allocated buffer blob.
 */
typedef enum
{
    cariboulite_firmware_source_file = 0,
    cariboulite_firmware_source_blob = 1,
} cariboulite_firmware_source_en;

typedef enum
{
    cariboulite_ok                              = 0,
    cariboulite_board_detection_failed          = 1,
    cariboulite_io_setup_failed                 = 2,
    cariboulite_fpga_configuration_failed       = 3,
    cariboulite_submodules_init_failed          = 4,
    cariboulite_self_test_failed                = 5,
    cariboulite_board_dependent_config_failed   = 6,
    cariboulite_signal_registration_failed      = 7,
} cariboulite_errors_en;

/**
 * @brief Detects the connected board and returns the detection result
 *
 * When a fully configures board is mounted, it is being pre-detected by the
 * Linux HAT configuration during startup. Then this information is written as
 * a "device-tree"-hat sysfs configuration. This configuration can be found in
 * '/proc/device-tree/hat/...' files. They populate the system type, version,
 * type and other attributes. This function tries to read these attributes to determine
 * the type of system that is connected. If it doesn't find the system, it tries to
 * further query it directrly through its i2c interface (EEPROM device on board).
 * If all these doesn't work - it fails.
 *
 * @param sys a pre-allocated device handle structure
 * @return success (1) or failure (0)
 */
int cariboulite_detect_board(sys_st *sys);

/**
 * @brief Printing detected board information
 *
 * Once the detection succeeded, this function can print the device
 * information within the HAT interface.
 *
 * @param sys a pre-allocated device handle structure
 * @param log if true - print out the information on as log entries. Otherwise print
 *            it as a stdout (printf) output.
 */
void cariboulite_print_board_info(sys_st *sys, bool log);


/**
 * @brief Setting global logging level
 *
 * options: cariboulite_log_level_verbose, cariboulite_log_level_info, cariboulite_log_level_none
 */
void cariboulite_set_log_level(cariboulite_log_level_en lvl);

/**
 * @brief Fully initialize the system
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
int cariboulite_init_driver(sys_st *sys, hat_board_info_st *info);

/**
 * @brief Partially initialize the system
 *
 * Partial initialization of the system (low level drivers only) without the
 * SDR functionality. This is mainly used for production where we want only to program the
 * FPGA and test the system at its low level.
 *
 * @param sys a pre-allocated device handle structure
 * @param info the initialization performs internally the board detection sequence.
 *             which is stored into the "sys" struct. If the user wants to receive the
 *             information explicitely, he can pass here a pre-allocated info structure and
 *             it will be filled with the board information. If this is not needed
 *             user can pass "NULL"
 * @return success / fail codes according to "cariboulite_errors_en"
 */
int cariboulite_init_driver_minimal(sys_st *sys, hat_board_info_st *info, bool production);
int cariboulite_init_system_production(sys_st *sys);
int cariboulite_deinit_system_production(sys_st *sys);

/**
 * @brief Register an explicit linux signal handler in the application level
 *
 * If a linux signal occures it is caught by the system. The user may want to be
 * notified on the signal in the app level. This is done by registering a handler
 * function with this fucntion.
 *
 * @param sys a pre-allocated device handle structure
 * @param handler a handler function with the following prototype:
 *           void (*signal_handler)( struct sys_st_t *sys,           // the current cariboulite low-level management struct
 *								void* context,                      // custom context - can be a higher level app class
 *								int signal_number,                  // the signal number
 *								siginfo_t *si);
 * @param op determines when the handler is shot:
 *                  signal_handler_op_last      => The custom sighandler operates (if present) after the default sig handler
 *                  signal_handler_op_first     => The custom sighandler operates (if present) before the default sig handler
 *                  signal_handler_op_override  => The custom sighandler operates (if present) instead of the default sig handler
 * @param context this void* pointer may contain an app specific handle to be passed to the explicit signal handler.
 * @return always 0 (success)
 */
int cariboulite_setup_signal_handler (sys_st *sys,
                                        signal_handler handler,
                                        signal_handler_operation_en op,
                                        void *context);


/**
 * @brief Program and configure the FPGA
 *
 * Programming the FPGA using either an external binary file (.bin) or
 * an image of the file within the local memory (a buffer).
 *
 * @param sys a pre-allocated device handle structure
 * @param src the source of the binary file. In case of the "blob" choise,
 *            the binary file is stored inside an array named "cariboulite_fpga_firmware"
 *            which is automatically generated after FPGA binary is created. This
 *            buffer is located inside a file named "cariboulite_fpga_firmware.h"
 * @param fpga_bin_path the path of the binary file - only applicable if
 *                      "cariboulite_firmware_source_file" was chosen as the "src". otherwise
 *                      NULL should be passed here.
 * @return 0 (success), -1 (fail)
 */
int cariboulite_configure_fpga (sys_st* sys, cariboulite_firmware_source_en src, char* fpga_bin_path);

/**
 * @brief Release resources
 *
 * Releasing resources taken during init and program execution. Should be
 * called at the end of the session.
 *
 * @param sys a pre-allocated device handle structure
 */
void cariboulite_release_driver(sys_st *sys);

/**
 * @brief Get current API lib version
 *
 * @param v a struct containing the version information
 */
void cariboulite_lib_version(cariboulite_lib_version_st* v);

/**
 * @brief Get board serial number
 *
 * Note - this 32bit serial number is a digest value of the UUID 128 bit that
 * is automatically generated for each board. It is basically the result of
 * "xor" operations on the UUID's parts.
 *
 * @param sys a pre-allocated device handle structure
 * @param serial_number a 32bit storage to store the serial number (can be NULL - if not needed)
 * @param count the number of 32bit variables returned - always returns as "1".
 * @return always 0
 */
int cariboulite_get_serial_number(sys_st *sys, uint32_t* serial_number, int *count);

/**
 * @brief Initilize IOs
 *
 * This is a sub-part of the initialization sequence and it
 * initializes only the IOs (GPIOs, interfaces) of the RPI
 *
 * @param sys a pre-allocated device handle structure
 * @return 0 (sucess), -1 (fail)
 */
int cariboulite_setup_io (sys_st* sys);

/**
 * @brief Release IOs
 *
 * Giving back the IOs and returning them to their default state
 *
 * @param sys a pre-allocated device handle structure
 * @return 0 (sucess), -1 (fail)
 */
int cariboulite_release_io (sys_st* sys);
int cariboulite_init_submodules (sys_st* sys);
int cariboulite_release_submodules(sys_st* sys);

/**
 * @brief Component communication test
 *
 * Self testing communication with components, their versions, etc.
 *
 * @param sys a pre-allocated device handle structure
 * @param res test-result
 * @return 0 (sucess), -1 (fail)
 */
int cariboulite_self_test(sys_st* sys, cariboulite_self_test_result_st* res);
                                    
/**
 * @brief Getting the used radio handle
 *
 * After initializing the drivers, a radio device is created and stored in
 * the device driver main struct. To manipulate the radio features, this radio
 * handle (pointer) needs to be obtained by the user. This handle is normally
 * passed to the radio manipulation functions in "cariboulite_radio.h"
 *
 * @param sys a pre-allocated device handle structure
 * @param res test-result
 * @return 0 (sucess), -1 (fail)
 */
cariboulite_radio_state_st* cariboulite_get_radio_handle(sys_st* sys, cariboulite_channel_en type);
#ifdef __cplusplus
}
#endif


#endif // __CARIBOULITE_SETUP_H__