#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/wait.h>
#include "production_utils.h"

int cariboulite_production_utils_rpi_leds_init(int state)
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

int cariboulite_production_utils_rpi_leds_set_state(int led_green, int led_red)
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

void cariboulite_production_utils_rpi_leds_blink_start_tests(void)
{
	int val = 0;
	int N_cycles = 20;

	cariboulite_production_utils_rpi_leds_set_state(0, 0);
	usleep(1000000);

	cariboulite_production_utils_rpi_leds_set_state(1, 1);
	usleep(2000000);

	for (int i = 0; i < N_cycles; i++)
	{
		cariboulite_production_utils_rpi_leds_set_state(val, -1);
		usleep(50000);

		cariboulite_production_utils_rpi_leds_set_state(-1, val);
		usleep(50000);

		cariboulite_production_utils_rpi_leds_set_state(-1, -1);
		usleep(50000);

		val = !val;
	}
}

void cariboulite_production_utils_rpi_leds_blink_fatal_error(void)
{
	int val = 1;
	int N_cycles = 30;

	for (int i = 0; i < N_cycles; i++)
	{
		cariboulite_production_utils_rpi_leds_set_state(val, val);
		usleep(150000);
		val = !val;
	}
}

static int cariboulite_production_execute_command(char **argv)
{
     pid_t  pid;
     int    status;

     if ((pid = fork()) < 0) {     // fork a child process
          printf("*** ERROR: forking child process failed\n");
          exit(1);
     }
     else if (pid == 0) {          // for the child process:
          if (execvp(*argv, argv) < 0) {     // execute the command
               printf("*** ERROR: exec failed\n");
               exit(1);
          }
     }
     else {                                  /* for the parent:      */
          while (wait(&status) != pid)       /* wait for completion  */
               ;
     }
	 return status;
}


static int cariboulite_production_execute_command_read(char *cmd, char* res, int res_size)
{
    int i = 0;
    FILE *p = popen(cmd,"r");
    if (p != NULL )
    {
        while (!feof(p) && (i < res_size) )
        {
            fread(&res[i++],1,1,p);
        }
        res[i] = 0;
        //printf("%s",res);
        pclose(p);
        return 0;
    }
    else
    {
        return -1;
    }
}

int cariboulite_production_check_wifi_state(cariboulite_production_wifi_status_st* wifi_stat)
{
	char res[100] = {0};
	cariboulite_production_execute_command_read("/usr/sbin/iwgetid", res, 99);

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

	cariboulite_production_execute_command_read("/usr/bin/curl -s -I https://linuxhint.com/", res, 99);
	char* inter = strstr(res, "HTTP/2 200");
	if (inter != NULL) wifi_stat->internet_access = true;

	return 0;
}

int cariboulite_production_get_rpi_info(cariboulite_rpi_info_st* rpi)
{
	char res[1024] = {0};
	cariboulite_production_execute_command_read("/usr/bin/uname -a", rpi->uname, 255);
	cariboulite_production_execute_command_read("/usr/bin/cat /proc/cpuinfo", res, 1023);

	strtok(rpi->uname, "\n");

	char* Serial = strstr(res, "Serial");
	char* Model = strstr(res, "Model");
	char* CPU = strstr(res, "Hardware");
	char* Revision = strstr(res, "Revision");

	Serial = strstr(Serial,": ") + 2; strtok(Serial, " \n");
	Model = strstr(Model,": ") + 2; strtok(Model, "\n");
	CPU = strstr(CPU,": ") + 2; strtok(CPU, " \n");
	Revision = strstr(Revision,": ") + 2; strtok(Revision, " \n");

	sprintf(rpi->cpu_revision, "%s", Revision);
	sprintf(rpi->cpu_name, "%s", CPU);
	sprintf(rpi->model, "%s", Model);
	sprintf(rpi->cpu_serial_number, "%s", Serial);
	return 0;
}
