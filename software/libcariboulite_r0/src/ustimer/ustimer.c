#include "ustimer.h"
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static ustimer_t timer_info;

//================================================
static void ustimer_handle_quant ( int sig )
{
	int i;
	struct timeval new_time = {0};

	if (sig != SIGUSR1)
	{
		return;
	}

	gettimeofday(&new_time, NULL);
	timer_info.timer_count ++;

	if (!timer_info.global_running)
	{
		return;
	}

	for (i = 0; i<USTIMER_NUM_SLOTS; i++)
	{
		if (timer_info.time_enabled[i] &&
			timer_info.handlers[i] &&
			!timer_info.timer_func_busy[i] &&
			timer_info.timer_count%timer_info.timer_quant[i]==0)
		{
			timer_info.timer_func_busy[i] = 1;
			timer_info.handlers[i](i, timer_info.timetags[i]);
			timer_info.timer_func_busy[i] = 0;
		}

		// record the last time this event occured
		timer_info.timetags[i].tv_sec = new_time.tv_sec;
		timer_info.timetags[i].tv_usec = new_time.tv_usec;
	}
}


//================================================
static int ustimer_make_timer( timer_t *timerID, int expireMS, int intervalMS )
{
	struct sigevent te;
	struct itimerspec its;
	struct sigaction sa;
	int sigNo = SIGUSR1;

	// Set up signal handler
	sa.sa_flags = SA_RESTART;
	sa.sa_handler = ustimer_handle_quant;
	sigemptyset(&sa.sa_mask);
	if (sigaction(sigNo, &sa, NULL) == -1)
	{
		return -1;
	}

	// Set and enable alarm
	te.sigev_notify = SIGEV_SIGNAL;
	te.sigev_signo = sigNo;
	te.sigev_value.sival_ptr = timerID;
	timer_create(CLOCK_REALTIME, &te, timerID);	// monotonic clock to prevent clock change glitches

	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = intervalMS * 1000000;
	its.it_value.tv_sec = 0;
	its.it_value.tv_nsec = expireMS * 1000000;

	if (timer_settime(*timerID, 0, &its, NULL) < 0)
	{
		return -2;
	}

	return 0;
}

//================================================
int ustimer_init ( void )
{
	int err = 0;
	struct timespec res;
	int i;

	memset (&timer_info, 0, sizeof(ustimer_t));

	// all clocks are disabled
	for (i = 0; i<USTIMER_NUM_SLOTS; i++)
	{
		timer_info.time_enabled[i] = 0;
		timer_info.handlers[i] = NULL;
		timer_info.timer_func_busy[i] = 0;
		timer_info.timer_quant[i] = 100000000;
	}

	// init the periodic counter
	timer_info.timer_count = 0;
	timer_info.global_running = 0;

	// check RT ability capability
	clock_getres(CLOCK_REALTIME, &res);

	if (res.tv_sec==0 && res.tv_nsec < 100)
	{
		timer_info.initialized = 1;
	}

	// create the global timer with QUANTIZED interval
	err = ustimer_make_timer(&timer_info.timer_id, USTIMER_MIN_QUANT, USTIMER_MIN_QUANT);
	if (err == 0)
	{
		return 0;
	}

	return -1;
}

//================================================
int ustimer_close ( void )
{
	int i;

	// check if module inited
	if (timer_info.initialized == 0)
	{
		return 0;
	}

	// disable all active clocks
	for (i = 0; i < USTIMER_NUM_SLOTS; i++)
	{
		if (timer_info.time_enabled[i])
		{
			ustimer_unregister( i );
		}
	}

	timer_delete(timer_info.timer_id);

	return 0;
}

//================================================
int ustimer_register_function  ( ustimer_handler func,
                                  unsigned int interval,
                                  int *id )
{
	int cur_id = -1;
	int found_slot = 0;

	// check if initialized
	if (timer_info.initialized == 0)
	{
		return -1;
	}

	// check that id is not NULL
	if (id == NULL)
	{
		return -1;
	}

	// find an empty slot
	for (cur_id = 0; cur_id < USTIMER_NUM_SLOTS; cur_id ++)
	{
		// if the current slot is free
		if (timer_info.time_enabled[cur_id] == 0)
		{
			found_slot = 1;
			break;
		}
	}

	if (found_slot == 0)
	{
		return -2;
	}


	timer_info.timer_quant[cur_id] = interval / USTIMER_MIN_QUANT;
	if (timer_info.timer_quant[cur_id] == 0) timer_info.timer_quant[cur_id] = 1; // for smaller then QUANT millisec
	timer_info.handlers[cur_id] = func;
	gettimeofday ( &(timer_info.timetags[cur_id]), NULL);
	timer_info.time_enabled[cur_id] = 1;


	*id = cur_id;
	return 0;
}

//================================================
int ustimer_unregister         ( int id )
{
	// check if the module is initialized
	if (timer_info.initialized == 0)
	{
		return -1;
	}

	// check that the id is a valid value
	if ( id < 0 || id >= USTIMER_NUM_SLOTS )
	{
		return -2;
	}

	// check if the specific timer id is enabled
	if (timer_info.time_enabled[id] == 0)
	{
		return -3;
	}
	timer_info.time_enabled[id] = 0;
	timer_info.handlers[id] = NULL;
	timer_info.timer_quant[id] = 100000000;
	return 0;
}

//================================================
int ustimer_start 	( unsigned int run )
{
	timer_info.global_running = run;
	return 0;
}
