#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif
#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "PRODUCTION_TESTING"
#include "zf_log/zf_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "production_testing.h"

//===================================================================
void production_testing_lcd_key_callback(void* context, int key1, int key2)
{
	printf("%d, %d\n", key1, key2);
}

//===================================================================
void production_testing_powermon_callback(void* context, hat_powermon_state_st* state)
{
}

//===================================================================
int production_init(production_sequence_st* prod, production_test_st* tests, int num_tests, void* context)
{
	memset(prod, 0, sizeof(production_sequence_st));
	
	if (lcd_init(&prod->lcd, production_testing_lcd_key_callback, prod) != 0)
	{
		ZF_LOGE("LCD init failed");
		return -1;
	}
	
	io_utils_get_rpi_info(&prod->tester.rpi_info);
	
	prod->context = context;
	prod->tests = tests;
	prod->number_of_tests = num_tests;
	prod->current_test_number = 0;
	return 0;
}

//===================================================================
int production_close(production_sequence_st* prod)
{
	lcd_close(&prod->lcd);
	return 0;
}


#define PROD_GET_TIME(T)  {time_t time_now; time(&time_now); memcpy(&(T),gmtime(&time_now),sizeof(struct tm));}

//===================================================================
int production_start_tests(production_sequence_st* prod)
{
	prod->current_tests_pass = true;
	
	for (	prod->current_test_number = 0; 
			prod->current_test_number < prod->number_of_tests;
			prod->current_test_number ++ )
	{
		production_test_st* current_test = &prod->tests[prod->current_test_number];
		ZF_LOGI("Starting test No. %d ['%s']", prod->current_test_number, current_test->test_name);
		
		current_test->test_number = prod->current_test_number;

		PROD_GET_TIME(current_test->start_time_of_test);
		current_test->started = true;

		if (current_test->func != NULL)
		{
			current_test->test_pass = current_test->func(prod->context, prod);
			
			if (!current_test->test_pass)
			{
				prod->current_tests_pass = false;
			}
		}
		
		current_test->finished = true;
		PROD_GET_TIME(current_test->end_time_of_test);
		ZF_LOGI("Finished test No. %d ['%s'] => %s", prod->current_test_number, current_test->test_name, 
											current_test->test_pass ? "PASS" : "FAIL");
	}
	
	return 0;
}

//===================================================================
int production_generate_report(production_sequence_st* prod, char* path, uint64_t serial_number)
{
	uint32_t i;
	
	char filename[256] = {0};
	char date1[128] = {0};
	char date[256] = {0};
	strftime(date1, 128, "%Y_%m_%d__%H_%M_%S", &prod->tests[0].start_time_of_test);
	strftime(date, 256, "%d/%m/%Y %H:%M:%S", &prod->tests[0].start_time_of_test);
	sprintf(filename, "%s/%llu__%s.yml", path, serial_number, date1);
	
	FILE* fid = fopen(filename, "w");
	if (fid == NULL)
	{
		ZF_LOGE("File opening failed for results generation");
		return -1;
	}
	
	fprintf(fid, "version: 1\n");
	fprintf(fid, "file_type: cariboulite_test_results\n");
	fprintf(fid, "date: %s\n", date);
	fprintf(fid, "product_name: %s\n", "cariboulite_full_r2.8");					// <====
	fprintf(fid, "rpi_serial_number: %s\n", prod->tester.rpi_info.serial_number);
	fprintf(fid, "summary_test_results: %s\n", prod->current_tests_pass?"PASS":"FAIL");
	fprintf(fid, "test_results:\n");
	
	for (i = 0; i < prod->number_of_tests; i++)
	{
		fprintf(fid, "\t%s: %s\n", prod->tests[i].test_name, prod->tests[i].test_result_textual);
	}
	
	fclose(fid);
	return 0;
}
