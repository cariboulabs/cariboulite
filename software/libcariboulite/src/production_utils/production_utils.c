#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/wait.h>
#include "io_utils/io_utils_fs.h"
#include "production_utils.h"


//=======================================================================================
int production_utils_rpi_leds_init(int state)
{
	FILE* trigger_green = fopen("/sys/class/leds/led0/trigger", "w");
	if (trigger_green == NULL)
	{
		printf("Production Utils 'cariboulite_production_utils_rpi_leds_init' ERROR: couldn't open internal LED0\n");
		return -1;
	}

	FILE* trigger_red = fopen("/sys/class/leds/led1/trigger", "w");
	if (trigger_red == NULL)
	{
		printf("Production Utils 'cariboulite_production_utils_rpi_leds_init' ERROR: couldn't open internal LED1\n");
		fclose(trigger_green);
		return -1;
	}

	if (state == 1)		// operation mode for gpio
	{
		fprintf(trigger_green, "gpio");
		fprintf(trigger_red, "gpio");
	}
	else
	{
		fprintf(trigger_green, "mmc0");
		fprintf(trigger_red, "actpwr");
	}

	fclose(trigger_red);
	fclose(trigger_green);

	return 0;
}

//=======================================================================================
int production_utils_rpi_leds_set_state(int led_green, int led_red)
{
	if (led_green != -1)
	{
		FILE* fid = fopen("/sys/class/leds/led0/brightness", "w");
		if (fid == NULL)
		{
			printf("Production Utils 'cariboulite_production_utils_rpi_leds_set_state' error ctrling LED green\n");
			return -1;
		}

		if (led_green > 0) led_green = 1;
		fprintf(fid, "%d", led_green);
		fclose(fid);
	}

	if (led_red != -1)
	{
		FILE* fid = fopen("/sys/class/leds/led1/brightness", "w");
		if (fid == NULL)
		{
			printf("Production Utils 'cariboulite_production_utils_rpi_leds_set_state' error ctrling LED red\n");
			return -1;
		}

		if (led_red > 0) led_red = 1;
		fprintf(fid, "%d", led_red);
		fclose(fid);
	}

	return 0;
}

//=======================================================================================
void production_utils_rpi_leds_blink_start_tests(void)
{
	int val = 0;
	int N_cycles = 20;

	production_utils_rpi_leds_set_state(0, 0);
	usleep(1000000);

	production_utils_rpi_leds_set_state(1, 1);
	usleep(2000000);

	for (int i = 0; i < N_cycles; i++)
	{
		production_utils_rpi_leds_set_state(val, -1);
		usleep(50000);

		production_utils_rpi_leds_set_state(-1, val);
		usleep(50000);

		production_utils_rpi_leds_set_state(-1, -1);
		usleep(50000);

		val = !val;
	}
}

//=======================================================================================
void production_utils_rpi_leds_blink_fatal_error(void)
{
	int val = 1;
	int N_cycles = 30;

	for (int i = 0; i < N_cycles; i++)
	{
		production_utils_rpi_leds_set_state(val, val);
		usleep(150000);
		val = !val;
	}
}

//=======================================================================================
int production_check_wifi_state(production_wifi_status_st* wifi_stat)
{
	char res[100] = {0};
	io_utils_execute_command_read("/usr/sbin/iwgetid", res, 99);

	if (wifi_stat == NULL) return 0;
	wifi_stat->internet_access = false;

	if (strlen(res)<1)
	{
		wifi_stat->available = false;
		wifi_stat->wlan_id = -1;
		strcpy(wifi_stat->essid, "");
		return -1;
	}
	else
	{
		wifi_stat->available = true;
		char* wlan_id = strstr(res, "wlan");
		char* essid = strstr(res, "ESSID:");

		wifi_stat->wlan_id = atoi(wlan_id + strlen("wlan"));
		strcpy(wifi_stat->essid, essid + strlen("ESSID:") + 1);
		wifi_stat->essid[strlen(wifi_stat->essid)-2] = '\0';
	}

	io_utils_execute_command_read("/usr/bin/curl -s -I https://linuxhint.com/", res, 99);
	char* inter = strstr(res, "HTTP/2 200");
	if (inter != NULL) wifi_stat->internet_access = true;

	return 0;
}
