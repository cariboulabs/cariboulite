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
#include "io_utils/io_utils_fs.h"
#include "io_utils/io_utils.h"
#include "production_testing.h"

//===================================================================
/*void production_testing_lcd_key_callback(void* context, int key1, int key2)
{
	//printf("%d, %d\n", key1, key2);
}*/

//===================================================================
/*void production_testing_powermon_callback(void* context, hat_powermon_state_st* state)
{
}*/

//===================================================================
int production_init(production_sequence_st* prod, production_test_st* tests, int num_tests, void* context)
{
	memset(prod, 0, sizeof(production_sequence_st));
	
	// init the power-monitor
	if (hat_powermon_init(&prod->powermon, PROD_TESTING_PWR_MON_ADDR, /*production_testing_powermon_callback*/NULL, prod) != 0)
	{
		ZF_LOGE("Failed to init power monitor");
		return -1;
	}
	
	hat_powermon_set_power_state(&prod->powermon, false);
	
	if (lcd_init(&prod->lcd, /*production_testing_lcd_key_callback*/NULL, prod) != 0)
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
int production_set_git_repo(production_sequence_st* prod, char* pat, char* repo, char* dir)
{
	strcpy(prod->git_pat_path, pat);
	strcpy(prod->git_repo, repo);
	strcpy(prod->git_res_directory, dir);
	
	lcd_writeln(&prod->lcd, "CaribouLite Tst", "Git repo setup", true);
	
	// read the PAT file
	FILE* fid = NULL;
	fid = fopen(prod->git_pat_path, "r");
	if (fid == NULL)
	{
		ZF_LOGE("opening file '%s' for reading failed", prod->git_pat_path);
		return -1;
	}
	
	char* st = fgets(prod->git_pat_user, 255, fid);
	if (st == NULL)
	{
		ZF_LOGE("PAT file doesn't contain username");
		return -1;
	}
	int len = strlen(prod->git_pat_user);
	if (prod->git_pat_user[len-1] == '\n' || prod->git_pat_user[len-1] == '\r')
	{
		prod->git_pat_user[len-1] = 0;
	}
	
	st = fgets(prod->git_pat_pass, 255, fid);
	if (st == NULL)
	{
		ZF_LOGE("PAT file doesn't contain pass");
		return -1;
	}
	
	len = strlen(prod->git_pat_pass);
	if (prod->git_pat_pass[len-1] == '\n' || prod->git_pat_pass[len-1] == '\r')
	{
		prod->git_pat_pass[len-1] = 0;
	}
	
	fclose(fid);
	
	// create commands
	sprintf(prod->git_base_command, "git --git-dir=%s/.git/ --work-tree=%s", 
												prod->git_res_directory,
												prod->git_res_directory);
												
	sprintf(prod->git_add_command, "%s add --all", prod->git_base_command);
	sprintf(prod->git_commit_command, "%s commit -m", prod->git_base_command);
	
	// git push https://user:pass@yourrepo.com/path HEAD
	sprintf(prod->git_push_command, "%s push https://%s:%s@%s", prod->git_base_command,
													prod->git_pat_user,
													prod->git_pat_pass,
													prod->git_repo);
								
	sprintf(prod->git_pull_command, "%s pull https://%s:%s@%s", prod->git_base_command,
													prod->git_pat_user,
													prod->git_pat_pass,
													prod->git_repo);


	ZF_LOGI("GIT REPO PARAMETERS");
	ZF_LOGI("BASE COMMAND: %s", prod->git_base_command);
	ZF_LOGI("ADD COMMAND: %s", prod->git_add_command);
	ZF_LOGI("COMMIT COMMAND: %s", prod->git_commit_command);
	//ZF_LOGI("PUSH COMMAND: %s", prod->git_push_command);
	//ZF_LOGI("PULL COMMAND: %s", prod->git_pull_command);
	return 0;
}

//===================================================================
void production_git_sync_sequence(production_sequence_st* prod, char* commit_string)
{
	char pull_command[1500] = {0};
	char add_command[1500] = {0};
	char commit_command[1700] = {0};
	char push_command[1500] = {0};
	char *argv_pull[64] = {0};
	char *argv_add[64] = {0};
	char *argv_commit[64] = {0};
	char *argv_push[64] = {0};
	
	char commit_str[256] = {0};
	
	lcd_writeln(&prod->lcd, "CaribouLite Tst", "Git repo sync", true);

	for (unsigned int i = 0; i<strlen(commit_string); i++) 
	{
		if (commit_string[i] == ' ') commit_str[i] = '_';
		else commit_str[i] = commit_string[i];
	}

	sprintf(pull_command, "%s", prod->git_pull_command);
	sprintf(add_command, "%s", prod->git_add_command);
	sprintf(commit_command, "%s '%s'", prod->git_commit_command, commit_str);
	sprintf(push_command, "%s", prod->git_push_command);
	
	lcd_writeln(&prod->lcd, "CaribouLite Tst", "Git repo pull", true);
	ZF_LOGI("GIT PULL");
	io_utils_parse_command(pull_command, argv_pull);
	io_utils_execute_command(argv_pull);
	
	ZF_LOGI("GIT ADD");
	io_utils_parse_command(add_command, argv_add);
	io_utils_execute_command(argv_add);
	
	lcd_writeln(&prod->lcd, "CaribouLite Tst", "Git repo commit", true);
	ZF_LOGI("GIT COMMIT");
	io_utils_parse_command(commit_command, argv_commit);
	io_utils_execute_command(argv_commit);
	
	lcd_writeln(&prod->lcd, "CaribouLite Tst", "Git repo push", true);
	ZF_LOGI("GIT PUSH");
	io_utils_parse_command(push_command, argv_push);
	io_utils_execute_command(argv_push);
}

//===================================================================
int production_close(production_sequence_st* prod)
{
	ZF_LOGI("CLOSING LCD");
	lcd_writeln(&prod->lcd, "CL TESTER", "GOODBYE!", true);
	lcd_close(&prod->lcd);
	
	ZF_LOGI("CLOSING HAT MON");
	hat_powermon_set_power_state(&prod->powermon, false);
	hat_powermon_release(&prod->powermon);
	return 0;
}

//===================================================================
int production_rewind(production_sequence_st* prod)
{
	prod->current_test_number = 0;
	prod->current_tests_pass = true;
	
	lcd_writeln(&prod->lcd, "TESTER", "RESTARTS...", true);
	
	for (	prod->current_test_number = 0; 
			prod->current_test_number < prod->number_of_tests;
			prod->current_test_number ++ )
	{
		production_test_st* current_test = &prod->tests[prod->current_test_number];
		current_test->started = false;
		current_test->finished = false;
		
		memset (&current_test->start_time_of_test, 0, sizeof(struct tm));
		memset (&current_test->end_time_of_test, 0, sizeof(struct tm));

		current_test->test_result_float = 0.0;
		memset(current_test->test_result_textual, 0, sizeof(current_test->test_result_textual));
		current_test->test_pass = false;
	}
	
	return 0;
}


#define PROD_GET_TIME(T)  {time_t time_now; time(&time_now); memcpy(&(T),gmtime(&time_now),sizeof(struct tm));}

//===================================================================
int production_start_tests(production_sequence_st* prod)
{
	char line1[128], line2[128];
	prod->current_tests_pass = true;
	
	for (	prod->current_test_number = 0; 
			prod->current_test_number < prod->number_of_tests;
			prod->current_test_number ++ )
	{
		production_test_st* current_test = &prod->tests[prod->current_test_number];
		ZF_LOGI("Starting test No. %d ['%s']", prod->current_test_number, current_test->test_name);
		
		sprintf(line1, "Testing [%d]", prod->current_test_number);
		sprintf(line2, "%s", current_test->name_short);
		lcd_writeln(&prod->lcd, line1, line2, true);
		
		current_test->test_number = prod->current_test_number;

		PROD_GET_TIME(current_test->start_time_of_test);
		current_test->started = true;

		if (current_test->func != NULL)
		{
			current_test->test_pass = current_test->func(prod->context, prod, prod->current_test_number);
			
			sprintf(line2, "%d %s", prod->current_test_number, current_test->name_short);
			lcd_writeln(&prod->lcd, line2, current_test->test_pass?"Pass":"Fail", true);
			
			if (!current_test->test_pass)
			{
				ZF_LOGD("Test '%s' Failed: '%s'", current_test->test_name, current_test->test_result_textual);
				prod->current_tests_pass = false;
			}
		}
		
		current_test->finished = true;
		PROD_GET_TIME(current_test->end_time_of_test);
		ZF_LOGI("Finished test No. %d ['%s'] => %s", prod->current_test_number, current_test->test_name, 
											current_test->test_pass ? "PASS" : "FAIL");
											
		if (!prod->current_tests_pass) break;
	}
	
	hat_powermon_set_power_state(&prod->powermon, false);
	
	return prod->current_tests_pass;
}

//===================================================================
int production_generate_event_file(production_sequence_st* prod, char* path, char* event, char* tester)
{
	char filename[256] = {0};
	char date1[128] = {0};
	
	struct tm event_time;
	PROD_GET_TIME(event_time);
	
	strftime(date1, 128, "%Y_%m_%d__%H_%M_%S", &event_time);
	sprintf(filename, "%s/events/%s__%s.yml", path, date1, tester);
	
	FILE* fid = fopen(filename, "w");
	if (fid == NULL)
	{
		ZF_LOGE("File opening failed for results generation");
		return -1;
	}
	
	fprintf(fid, event);
	
	fflush(fid);
	fclose(fid);
	return 0;
}

//===================================================================
int production_generate_report(production_sequence_st* prod, char* path, uint32_t serial_number)
{
	uint32_t i;
	
	char filename[256] = {0};
	char date1[128] = {0};
	char date[256] = {0};
	
	lcd_writeln(&prod->lcd, "Generating", "Report", true);
	strftime(date1, 128, "%Y_%m_%d__%H_%M_%S", &prod->tests[0].start_time_of_test);
	strftime(date, 256, "%d/%m/%Y %H:%M:%S", &prod->tests[0].start_time_of_test);
	sprintf(filename, "%s/boards/%s__%08x.yml", path, date1, serial_number);
	
	FILE* fid = fopen(filename, "w");
	if (fid == NULL)
	{
		ZF_LOGE("File opening failed for results generation");
		return -1;
	}
	
	fprintf(fid, "version: 1\n");
	fprintf(fid, "file_type: cariboulite_test_results\n");
	fprintf(fid, "date: %s\n", date);
	fprintf(fid, "product_name: %s\n", prod->product_name);
	fprintf(fid, "rpi_serial_number: %s\n", prod->tester.rpi_info.serial_number);
	fprintf(fid, "summary_test_results: %s\n", prod->current_tests_pass?"PASS":"FAIL");
	fprintf(fid, "test_results:\n");
	
	for (i = 0; i < prod->number_of_tests; i++)
	{
		fprintf(fid, "\t%s: \"%s\"\n", prod->tests[i].test_name, prod->tests[i].test_result_textual);
		fflush(fid);
	}
	
	fclose(fid);
	return 0;
}

//===================================================================
int production_wait_for_button(production_sequence_st* prod, lcd_button_en but, char* top_line, char* bottom_line)
{
	int key1, key2;
	lcd_writeln(&prod->lcd, top_line, bottom_line, true);
	
	while (1)
	{
		io_utils_usleep(100000);
		lcd_get_keys(&prod->lcd, &key1, &key2);
		
		if (but == lcd_button_bottom && key1)
		{
			return 1;
		}
		else if (but == lcd_button_top && key2)
		{
			return 1;
		}
	}
	return 0;
}

//===================================================================
int production_monitor_power_fault(production_sequence_st* prod, bool* fault, float *i, float* v, float* p)
{
	char line2[32];
	if (hat_powermon_read_fault(&prod->powermon, fault) != 0)
	{
		//ZF_LOGI("HAT Power-Monitor reader thread finished");
		return -1;
	}

	if (hat_powermon_read_data(&prod->powermon, i, v, p) != 0)
	{
		return -1;
	}
	
	sprintf(line2, "%s %.1f mA", (*fault)?"FLT":"Okay", *i);
	lcd_writeln(&prod->lcd, "Power on...", line2, true);
	return 0;
}