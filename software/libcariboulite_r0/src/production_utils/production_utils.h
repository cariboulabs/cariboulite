#ifndef __PRODUCTION_UTILS_H__
#define __PRODUCTION_UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif



typedef struct
{
	bool available;
	int wlan_id;
	char essid[512];
	bool internet_access;
} cariboulite_production_wifi_status_st;

typedef struct
{
    char uname[256];                // uname -a
    char cpu_name[32];              // cat /proc/cpuinfo
	char cpu_revision[32];
	char model[64];
    char cpu_serial_number[32];
} cariboulite_rpi_info_st;

int cariboulite_production_utils_rpi_leds_init(int state);
int cariboulite_production_utils_rpi_leds_set_state(int led_green, int led_red);
void cariboulite_production_utils_rpi_leds_blink_fatal_error(void);
void cariboulite_production_utils_rpi_leds_blink_start_tests(void);
int cariboulite_production_check_wifi_state(cariboulite_production_wifi_status_st* wifi_stat);
int cariboulite_production_get_rpi_info(cariboulite_rpi_info_st* rpi);

#ifdef __cplusplus
}
#endif

#endif //__PRODUCTION_UTILS_H__