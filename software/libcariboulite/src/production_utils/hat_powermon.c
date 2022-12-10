#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "HAT_POWERMON"

#include <stdio.h>
#include <string.h>
#include "io_utils/io_utils_fs.h"
#include "hat_powermon.h"
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>
 
//=======================================================================
static void* hat_powermon_reader_thread(void* arg)
{
	hat_power_monitor_st *dev = (hat_power_monitor_st*)arg;
	bool fault = false;
	float i, v, p;

	ZF_LOGI("HAT Power-Monitor reader thread started");

	while (dev->thread_running)
	{
		io_utils_usleep(20000);

		if (hat_powermon_read_fault(dev, &fault) != 0)
		{
			//ZF_LOGI("HAT Power-Monitor reader thread finished");
		}

		if (hat_powermon_read_data(dev, &i, &v, &p) != 0)
		{

		}

		if (dev->cb)
		{
			dev->cb(dev->context, dev->state);
		}
	}

	ZF_LOGI("HAT Power-Monitor reader thread finished");
	return NULL;
}

//=======================================================================
int hat_powermon_init(hat_power_monitor_st* dev, uint8_t i2c_addr, hat_powermon_callback cb, void* context)
{
	memset (dev, 0, sizeof(hat_power_monitor_st))

	dev->cb = cb;
	dev->context = context;

	int bus = io_utils_i2cbus_exists();
	if (bus >= 0)
	{
		ZF_LOGI("i2c-%d has been found successfully", bus);
	}

	// neither bus 0,9 were found in the dev dir -> we need to probe bus9
	if (bus == -1)
	{
		if (io_utils_probe_gpio_i2c() == -1)
		{
			ZF_LOGE("Failed to probe i2c-9");
			return -1;
		}
		else
		{
			bus = 9;
			ZF_LOGI("i2c-9 has been probed successfully");
		}
	}

	if (io_utils_i2c_open(&dev->i2c_dev, bus, i2c_addr) != 0)
	{
		ZF_LOGE("Failed to open i2c-%d", bus);
		return -1;
	}

	// read the software version from the hardwrae
	if (hat_powermon_read_versions(dev, NULL, NULL) != 0)
	{
		return -1;
	}

	dev->thread_running = true;
	if (pthread_create(&dev->reader_thread, NULL, &hat_powermon_reader_thread, dev) != 0)
    {
        ZF_LOGE("HAT Power-Monitor reader thread creation failed");
        hat_powermon_release(dev);
        return NULL;
    }
}

//=======================================================================
int hat_powermon_release(hat_power_monitor_st* dev)
{
	dev->thread_running = false;
	pthread_join(dev->reader_thread, NULL);

	// close the i2c port
	io_utils_i2c_close(dev->i2c_dev);

	return 0;
}

//=======================================================================
int hat_powermon_set_power_state(hat_power_monitor_st* dev, bool on)
{
	uint8_t data[] = {HAT_POWERMON_REG_LOAD_SW_STATE, on}
	if (io_utils_i2c_write(dev->i2c_dev, data, 2) != 0)
	{
		ZF_LOGE("HAT Power-Monitor load switch setting failed");
		return -1;
	}
	dev->state.load_switch_state = on;
	return 0;
}

//=======================================================================
int hat_powermon_get_power_state(hat_power_monitor_st* dev, bool* on)
{
	uint8_t data = 0;
	if (io_utils_i2c_read_reg(dev->i2c_dev, HAT_POWERMON_REG_LOAD_SW_STATE, &data, 1) != 0)
	{
		ZF_LOGE("HAT Power-Monitor load switch getting failed");
		return -1;
	}
	dev->state.load_switch_state = data;
	if (on) *on = data;
	return 0;
}

//=======================================================================
int hat_powermon_set_leds_state(hat_power_monitor_st* dev, bool led1, bool led2)
{
	uint8_t data[3]

	data[0] = HAT_POWERMON_REG_LED1_STATE;
	data[1]  = led1;
	data[2]  = led2;
	if (io_utils_i2c_write(dev->i2c_dev, data, 3) != 0)
	{
		ZF_LOGE("HAT Power-Monitor leds setting failed");
		return -1;
	}

	return 0;

}

//=======================================================================
int hat_powermon_get_leds_state(hat_power_monitor_st* dev, bool *led1, bool *led2)
{
	uint8_t data[2] = {0};

	if (io_utils_i2c_read_reg(dev->i2c_dev, HAT_POWERMON_REG_LED1_STATE, data, 2) != 0)
	{
		ZF_LOGE("HAT Power-Monitor leds reading of setting failed");
		return -1;
	}
	if (led1) *led1 = data[0];
	if (led2) *led2 = data[1];

	return 0;
}

//=======================================================================
int hat_powermon_read_fault(hat_power_monitor_st* dev, bool* fault)
{
	if (!dev->state.monitor_active)
	{
		return -1;
	}

	uint8_t data[1] = {0};
	if (io_utils_i2c_read_reg(dev->i2c_dev, HAT_POWERMON_REG_FAULT_STATE, data, 1) != 0)
	{
		ZF_LOGE("HAT Power-Monitor fault state reading failed");
		return -1;
	}
	dev->state.fault = data[0];
	if (fault) *fault = dev->state.fault;
	return 0;
}

//=======================================================================
int hat_powermon_read_data(hat_power_monitor_st* dev, float *i, float *v, float *p)
{
	if (!dev->state.monitor_active)
	{
		return -1;
	}

	uint8_t data[3] = {0};
	if (io_utils_i2c_read_reg(dev->i2c_dev, HAT_POWERMON_REG_CURRENT, data, 3) != 0)
	{
		ZF_LOGE("HAT Power-Monitor power measures reading failed");
		return -1;
	}
	dev->state.i_ma = (float)(data[0]) * 5.0f;
	dev->state.v_mv = (float)(data[1]) * 25.0f;
	dev->state.p_mw = (float)(data[2]) * 125.0f;

	if (i) *i = dev->state.i_ma;
	if (v) *v = dev->state.v_mv;
	if (p) *p = dev->state.p_mw;
	return 0;
}

//=======================================================================
int hat_powermon_read_versions(hat_power_monitor_st* dev, int *ver, int *subver)
{
	uint8_t data = 0;
	if (io_utils_i2c_read_reg(&dev->i2c_dev, HAT_POWERMON_REG_VERSION, data, 1) != 0)
	{
		ZF_LOGE("HAT Power-Monitor versions read failed");
        return -1;
	}

	dev->version.ver = (data >> 4) & 0xF;
	dev->version.subver = (data & 0xF);
	if (ver) *ver = dev->version.ver;
	if (subver) *subver = dev->version.subver;
	return 0;
}