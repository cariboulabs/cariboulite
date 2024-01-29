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
} production_wifi_status_st;

int production_utils_rpi_leds_init(int state);
int production_utils_rpi_leds_set_state(int led_green, int led_red);
void production_utils_rpi_leds_blink_fatal_error(void);
void production_utils_rpi_leds_blink_start_tests(void);
int production_check_wifi_state(production_wifi_status_st* wifi_stat);

#ifdef __cplusplus
}
#endif

#endif //__PRODUCTION_UTILS_H__