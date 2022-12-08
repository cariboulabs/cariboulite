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
	ZF_LOGI("HAT Power-Monitor reader thread started");
	
	while (dev->thread_running)
	{
		io_utils_usleep(20000);
	}
	
	ZF_LOGI("HAT Power-Monitor reader thread finished");
	return NULL;
{

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
	
	
	io_utils_i2c_close(dev->i2c_dev);
}

//=======================================================================
int hat_powermon_set_power_state(hat_power_monitor_st* dev, bool on)
{
	
}

//=======================================================================
int hat_powermon_read_fault(hat_power_monitor_st* dev, bool* fault)
{
	if (!dev->state.monitor_active)
	{
		return -1;
	}
	
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
	
	if (i) *i = dev->state.i_ma;
	if (v) *v = dev->state.v_mv;
	if (p) *p = dev->state.p_mw;
	return 0;
}

//=======================================================================
int hat_powermon_read_versions(hat_power_monitor_st* dev, int *ver, int *subver)
{
	
}