#ifndef ZF_LOG_LEVEL
    #define ZF_LOG_LEVEL ZF_LOG_VERBOSE
#endif

#define ZF_LOG_DEF_SRCLOC ZF_LOG_SRCLOC_LONG
#define ZF_LOG_TAG "LCD"
#include "zf_log/zf_log.h"

#include <zmq.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include "io_utils/io_utils_fs.h"
#include "io_utils/io_utils.h"
#include "lcd.h"

//=======================================================================
static void* comm_thread(void* arg)
{
	lcd_st *dev = (lcd_st*)arg;
	//char buffer [10];
	ZF_LOGI("LCD enterring monitoring thread");

	while (dev->thread_running)
	{
		io_utils_usleep(500000);
		/*zmq_send (dev->requester, "3", 1, 0);
		int ret = zmq_recv (dev->requester, buffer, 5, 0);
        if (ret < 0)
		{
			ZF_LOGE("error reception");
			continue;
		}
		
		if (ret >= 2)
		{
			dev->key1 = buffer[0] == '1';
			dev->key2 = buffer[1] == '1';
			
			if (dev->cb != NULL)
			{
				dev->cb(dev->cb_context, dev->key1, dev->key2);
			}
		}*/
	}

	ZF_LOGI("LCD exitting monitoring thread");
	return NULL;
}

//========================================================
int lcd_init(lcd_st* dev, lcd_key_callback cb, void* cb_context)
{
	char *argv[64];
	dev->cb = cb;
	dev->cb_context = cb_context;
	

	char command[] = "/usr/bin/python3 /home/pi/projects/cariboulite/software/libcariboulite/src/production_utils/python/lcd_task.py";
	io_utils_parse_command(command, argv);
	dev->py_pid = io_utils_execute_command_parallel(argv);
	if (dev->py_pid < 0)
	{
		ZF_LOGE("LCD failed executing LCD script (python)");
		zmq_close (dev->requester);
		zmq_ctx_destroy (dev->context);
		return -1;
	}
	sleep(2);
	//io_utils_usleep(500000);

	dev->context = zmq_ctx_new ();
    dev->requester = zmq_socket (dev->context, ZMQ_REQ);
	zmq_connect (dev->requester, "tcp://localhost:55550");
	
	dev->thread_running = true;
	if (pthread_create(&dev->comm_thread, NULL, &comm_thread, dev) != 0)
    {
        ZF_LOGE("LCD thread creation failed");
		dev->thread_running = false;
		lcd_close(dev);
        return -1;
    }	
	return 0;
}

//========================================================
int lcd_close(lcd_st* dev)
{
	char buffer [10];
	if (dev->thread_running)
	{
		dev->thread_running = false;
		pthread_join(dev->comm_thread, NULL);
	}
	
	ZF_LOGI("Sending close message to script");
	zmq_send (dev->requester, "9", 1, 0);
	io_utils_usleep(10000);
	zmq_recv (dev->requester, buffer, 2, 0);
		
	ZF_LOGI("ZMQ destroy");
	zmq_close (dev->requester);
    zmq_ctx_destroy (dev->context);
	
	io_utils_wait_command_parallel(dev->py_pid);
	return 0;
}

//========================================================
int lcd_clear_screan(lcd_st* dev)
{
	char buffer [10];
	//ZF_LOGI("LCD clearing");
	zmq_send (dev->requester, "0", 1, 0);
	io_utils_usleep(10000);
	zmq_recv (dev->requester, buffer, 2, 0);
	return 0;
}

//========================================================
int lcd_write(lcd_st* dev, int row, int col, char* text)
{
	char buffer [10];
	char msg[128];
	sprintf(msg, "1,%d,%d,%s", row, col, text);
	zmq_send (dev->requester, msg, strlen(msg), 0);
	//io_utils_usleep(10000);
	zmq_recv (dev->requester, buffer, 2, 0);
	return 0;
}

//========================================================
int lcd_writeln(lcd_st* dev, char* line1, char* line2, int clear)
{
	char buffer [10];
	char msg[128];
	if (clear)
	{
		lcd_clear_screan(dev);
	}
	
	sprintf(msg, "1,0,0,%s", line1);
	zmq_send (dev->requester, msg, strlen(msg), 0);
	io_utils_usleep(10000);
	zmq_recv (dev->requester, buffer, 2, 0);
	
	//io_utils_usleep(10000);
	sprintf(msg, "1,1,0,%s", line2);
	zmq_send (dev->requester, msg, strlen(msg), 0);
	io_utils_usleep(10000);
	zmq_recv (dev->requester, buffer, 2, 0);
	return 0;
}

//========================================================
int lcd_get_keys(lcd_st* dev, int* key1, int* key2)
{
	char buffer [10];
	
	zmq_send (dev->requester, "3", 1, 0);
	int ret = zmq_recv (dev->requester, buffer, 5, 0);
	if (ret < 0)
	{
		ZF_LOGE("error reception");
		return -1;
	}
	
	if (ret >= 2)
	{
		dev->key1 = buffer[0] == '1';
		dev->key2 = buffer[1] == '1';
	}
	
	if (key1)
		*key1 = dev->key1;
	if (key2)
		*key2 = dev->key2;
	
	return 0;
}

//========================================================
int lcd_set_params(lcd_st* dev, int brightness, int contrast)
{
	char buffer [10];
	char msg[128];
	sprintf(msg, "2,%d,%d", brightness, contrast);
	zmq_send (dev->requester, msg, strlen(msg), 0);
	io_utils_usleep(10000);
	zmq_recv (dev->requester, buffer, 2, 0);
	return 0;
}

