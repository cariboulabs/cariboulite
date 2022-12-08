#ifndef __DRV_POWER_MON_H__
#define __DRV_POWER_MON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "io_utils/io_utils_i2c.h"


typedef enum
{
	HAT_POWERMON_REG_VERSION = 0,			// read only,		bits[3:0] = Version, bits[7:0] = Sub-Version
	HAT_POWERMON_REG_LOAD_SW_STATE = 1,		// read / write,	0x00 = Load switch is off, 0x01 = Load switch is on
	HAT_POWERMON_REG_LED1_STATE = 2,		// read / write,	0x00 = off, 0x01 = on
	HAT_POWERMON_REG_LED2_STATE = 3,		// read / write,	0x00 = off, 0x01 = on
	HAT_POWERMON_REG_FAULT_STATE = 4,		// read only,		0x00 = OK, 0x01 = Fault
	HAT_POWERMON_REG_CURRENT = 5,			// read only,		Val = real_current_mA / 5.0, possible currents [0.0 mA .. 1,275 mA] truncated if above 1.275 A, 5 mA resolution
	HAT_POWERMON_REG_VOLTAGE = 6,			// read only		Val = real_voltage_mV / 25.0, possible voltages [0.0 mV .. 6375 mV] truncated if above 6.375 V, 25 mV resolution
	HAT_POWERMON_REG_POWER = 7,				// read only		Val = real_power_mW / 125.0, possible powers [0.0 mW .. 8,128.125 mW] truncated if above 8.128125 Watt, 125 mW resolution
	HAT_POWERMON_REGS_MAX = 8,
} HAT_POWERMON_REG_en;

typedef struct
{
	bool monitor_active;
	bool load_switch_state;
	bool fault;
	float i_ma;
	float v_mv;
	float p_mw;
} hat_powermon_state_st;

typedef void (*hat_powermon_callback)(void* context, hat_powermon_state_st* state);

typedef struct
{
	io_utils_i2c_st i2c_dev;
	hat_powermon_callback cb;
	void *context;
	
	hat_powermon_state_st state;
	
	pthread_t reader_thread;
	bool thread_running;
} hat_power_monitor_st;

int hat_powermon_init(hat_power_monitor_st* dev, uint8_t i2c_addr, hat_powermon_callback cb, void* context);
int hat_powermon_release(hat_power_monitor_st* dev);

int hat_powermon_set_power_state(hat_power_monitor_st* dev, bool on);
int hat_powermon_read_fault(hat_power_monitor_st* dev, bool* fault);
int hat_powermon_read_data(hat_power_monitor_st* dev, float *i, float *v, float *p);
int hat_powermon_read_versions(hat_power_monitor_st* dev, int *ver, int *subver);


#ifdef __cplusplus
}
#endif

#endif // __DRV_POWER_MON_H__