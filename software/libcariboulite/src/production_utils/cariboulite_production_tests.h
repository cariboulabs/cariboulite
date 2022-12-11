#ifndef ___PRODUCTION_TESTS_H__
#define ___PRODUCTION_TESTS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "lcd.h"
#include "hat_powermon.h"
#include "io_utils/io_utils_sys_info.h"

typedef int (*test_function)(void* context, void* tests);

typedef struct
{
    io_utils_sys_info_st rpi_info;
} production_tester_st;

typedef struct
{
	// inputs
    char test_name[128];
    uint32_t test_number;
	test_function func;
	uint32_t group;
	bool started;
	bool finished;
	
	// timing
	struct tm start_time_of_test;
    struct tm end_time_of_test

	// outputs
	void* test_result_context;
	float test_result_float;
    char test_result_textual[512];
    bool test_pass;
} production_test_st;

typedef struct
{
	lcd_st lcd;
    production_tester_st tester;
    hat_board_info_st board_info;
    hat_power_monitor_st powermon;

    production_test_st *tests;
	uint32_t number_of_tests;
    void* context;
} production_sequence_st;


int cariboulite_production_init(production_sequence_st* prod, production_test_st* tests, int num_tests);
int cariboulite_production_close(production_sequence_st* prod);
int cariboulite_production_start_tests(production_sequence_st* prod);
int cariboulite_production_generate_report(production_sequence_st* prod, char* path);


#ifdef __cplusplus
}
#endif

#endif // ___PRODUCTION_TESTS_H__
