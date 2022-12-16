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

typedef int (*test_function)(void* context, void* tests, int test_num);
#define PROD_TESTING_PWR_MON_ADDR	(0x25)

typedef struct
{
    io_utils_sys_info_st rpi_info;
} production_tester_st;

typedef struct
{
	// inputs
	char name_short[20];
    char test_name[128];
    uint32_t test_number;
	test_function func;
	uint32_t group;
	bool started;
	bool finished;
	
	// timing
	struct tm start_time_of_test;
    struct tm end_time_of_test;

	// outputs
	void* test_result_context;
	float test_result_float;
    char test_result_textual[512];
    bool test_pass;
} production_test_st;

typedef struct
{
	lcd_st lcd;
	hat_power_monitor_st powermon;
    production_tester_st tester;

    production_test_st *tests;
	uint32_t number_of_tests;
    void* context;
	
	// git
	char git_pat_path[256];
	char git_repo[256];
	char git_res_directory[256];
	
	char git_pat_pass[256];
	char git_pat_user[256];
	
	char git_base_command[600];
	char git_add_command[1400];
	char git_commit_command[1400];
	char git_push_command[1400];
	char git_pull_command[1400];
	
	// state
	uint32_t current_test_number;
	bool current_tests_pass;
} production_sequence_st;

int production_init(production_sequence_st* prod, production_test_st* tests, int num_tests, void* context);
int production_set_git_repo(production_sequence_st* prod, char* pat, char* repo, char* dir);
int production_rewind(production_sequence_st* prod);
int production_close(production_sequence_st* prod);
int production_start_tests(production_sequence_st* prod);
int production_generate_report(production_sequence_st* prod, char* path, uint64_t serial_number);
int production_wait_for_button(production_sequence_st* prod, lcd_button_en but, char* top_line, char* bottom_line);
void production_git_sync_sequence(production_sequence_st* prod, char* commit_string);
int production_monitor_power_fault(production_sequence_st* prod, bool* fault, float *i, float* v, float* p);


#ifdef __cplusplus
}
#endif

#endif // ___PRODUCTION_TESTS_H__
