#ifndef __CARIBOULITE_PRODUCTION_H__
#define __CARIBOULITE_PRODUCTION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "cariboulite_setup.h"
#include "hat_powermon.h"
#include "io_utils/io_utils_sys_info.h"

typedef enum
{
    cariboulite_test_en_rpi_id_eeprom = 0,
    cariboulite_test_en_rpi_driver_caribou,
    cariboulite_test_en_fpga_programming,
    cariboulite_test_en_fpga_reset,
    cariboulite_test_en_fpga_pmod,
    cariboulite_test_en_fpga_switch,
    cariboulite_test_en_fpga_leds,
    cariboulite_test_en_fpga_versions,
    cariboulite_test_en_fpga_communication,
    cariboulite_test_en_fpga_smi,
    cariboulite_test_en_mixer_reset,
    cariboulite_test_en_mixer_communication,
    cariboulite_test_en_mixer_versions_id,
    cariboulite_test_en_modem_reset,
    cariboulite_test_en_modem_leds,
    cariboulite_test_en_modem_configuration,
    cariboulite_test_en_modem_versions_id,
    cariboulite_test_en_modem_communication,
    cariboulite_test_en_modem_interrupt,
    cariboulite_test_en_current_measurements,
    cariboulite_test_en_max,
} cariboulite_test_en;

typedef struct
{
    char company[128];
    char name[128];
    char email[128];
} cariboulite_production_facility_st;

typedef struct
{
    struct tm start_time_of_test;
    struct tm end_time_of_test;
    cariboulite_test_en test_type;
    int test_serial_number;

    char test_result_textual[512];
    uint32_t test_pass;
} cariboulite_production_test_st;

typedef struct
{
    cariboulite_production_facility_st tester;
    hat_board_info_st board_info;
    io_utils_sys_info_st rpi_info;
    hat_power_monitor_st powermon;

    cariboulite_production_test_st tests[cariboulite_test_en_max];
    void* context;
} cariboulite_production_sequence_st;

int cariboulite_production_init(cariboulite_production_sequence_st* prod);
int cariboulite_production_close(cariboulite_production_sequence_st* prod);
int cariboulite_production_start_tests(cariboulite_production_sequence_st* prod);
int cariboulite_production_generate_report(cariboulite_production_sequence_st* prod, char* path);

int cariboulite_test_rpi_id_eeprom(void* context);
int cariboulite_test_rpi_driver_caribou(void* context);
int cariboulite_test_fpga_programming(void* context);
int cariboulite_test_fpga_reset(void* context);
int cariboulite_test_fpga_pmod(void* context);
int cariboulite_test_fpga_switch(void* context);
int cariboulite_test_fpga_leds(void* context);
int cariboulite_test_fpga_versions(void* context);
int cariboulite_test_fpga_communication(void* context);
int cariboulite_test_fpga_programming(void* context);
int cariboulite_test_fpga_smi(void* context);
int cariboulite_test_mixer_reset(void* context);
int cariboulite_test_mixer_communication(void* context);
int cariboulite_test_mixer_versions_id(void* context);
int cariboulite_test_modem_reset(void* context);
int cariboulite_test_modem_leds(void* context);
int cariboulite_test_modem_configuration(void* context);
int cariboulite_test_modem_versions_id(void* context);
int cariboulite_test_modem_communication(void* context);
int cariboulite_test_modem_interrupt(void* context);
int cariboulite_test_current_measurements(void* context);

#ifdef __cplusplus
}
#endif

#endif // __CARIBOULITE_PRODUCTION_H__
